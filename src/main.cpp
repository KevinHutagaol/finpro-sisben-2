#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

// Helper headers
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#include <secrets.h>

#define PIN_BTN_BELL    21
#define PIN_BTN_TOGGLE  19
#define PIN_LIMIT_SW    18
#define PIN_RGB_RED     25
#define PIN_RGB_GREEN   33
#define PIN_RGB_BLUE    32
#define PIN_SERVO       26
#define PIN_BUZZER      23

#define LOCKED_ANGLE    90
#define UNLOCKED_ANGLE  0
#define BUZZER_CHANNEL  8
#define WDT_TIMEOUT     30

FirebaseData fbDO_stream;
FirebaseData fbDO_write;
FirebaseAuth auth;
FirebaseConfig config;
Servo lockServo;
Preferences preferences;
FirebaseJson json;

TaskHandle_t NetworkTaskHandle = NULL;
QueueHandle_t commandQueue;
SemaphoreHandle_t stateMutex;

struct DeviceState {
    bool isLocked;
    bool doorClosed;
    bool alarmTriggered;
};
DeviceState currentState;

unsigned long lastDebounceTime = 0;
unsigned long alarmTimer = 0;
unsigned long bellTimer = 0;
bool bellActive = false;
int alarmToneState = 0;

String parentPath = "/device_001";
String pathControl = parentPath + "/control";
String pathStatus = parentPath + "/status";

void networkTask(void *pvParameters);
void updateHardwareState();
void moveServo(int angle);
void setRGB(bool r, bool g, bool b);
void handleBuzzer();

void streamCallback(FirebaseStream data) {
    if (data.dataType() == "boolean") {
        bool requestedLockState = data.boolData();
        Serial.printf("[Core 0] Stream received: %s\n", requestedLockState ? "LOCKED" : "UNLOCKED");

        bool cmd = requestedLockState;
        xQueueSend(commandQueue, &cmd, 0);
    }
}

void streamTimeoutCallback(bool timeout) {
    if (timeout) Serial.println("[Core 0] Stream timeout, resuming...");
}

void setup() {
    Serial.begin(115200);

    commandQueue = xQueueCreate(5, sizeof(bool));
    stateMutex = xSemaphoreCreateMutex();

    // 2. Init Pins
    pinMode(PIN_BTN_BELL, INPUT_PULLUP);
    pinMode(PIN_BTN_TOGGLE, INPUT_PULLUP);
    pinMode(PIN_LIMIT_SW, INPUT_PULLUP);

    pinMode(PIN_RGB_RED, OUTPUT);
    pinMode(PIN_RGB_GREEN, OUTPUT);
    pinMode(PIN_RGB_BLUE, OUTPUT);
    setRGB(false, false, false);

    lockServo.setPeriodHertz(50);
    lockServo.attach(PIN_SERVO, 500, 2400);
    // ------------------------------------------------------------

    ledcSetup(BUZZER_CHANNEL, 2000, 8);
    ledcAttachPin(PIN_BUZZER, BUZZER_CHANNEL);

    preferences.begin("door_lock", false);

    if (xSemaphoreTake(stateMutex, portMAX_DELAY)) {
        currentState.isLocked = preferences.getBool("locked", false);
        currentState.doorClosed = (digitalRead(PIN_LIMIT_SW) == LOW);
        currentState.alarmTriggered = false;
        xSemaphoreGive(stateMutex);
    }

    xTaskCreatePinnedToCore(
        networkTask,
        "NetworkTask",
        10000,
        NULL,
        1,
        &NetworkTaskHandle,
        0
    );

    updateHardwareState();
}

void loop() {
    bool incomingCmd;
    if (xQueueReceive(commandQueue, &incomingCmd, 0) == pdTRUE) {
        if (xSemaphoreTake(stateMutex, portMAX_DELAY)) {
            if (currentState.isLocked != incomingCmd) {
                currentState.isLocked = incomingCmd;
                preferences.putBool("locked", currentState.isLocked);
                xSemaphoreGive(stateMutex);

                // Hardware action
                updateHardwareState();
            } else {
                xSemaphoreGive(stateMutex);
            }
        }
    }

    bool localLocked;
    bool localClosed;

    bool currentSwitchState = (digitalRead(PIN_LIMIT_SW) == LOW);

    if (xSemaphoreTake(stateMutex, portMAX_DELAY)) {
        if (currentState.doorClosed != currentSwitchState) {
            currentState.doorClosed = currentSwitchState;
        }

        if (digitalRead(PIN_BTN_TOGGLE) == LOW) {
            if (millis() - lastDebounceTime > 250) {
                currentState.isLocked = !currentState.isLocked;
                preferences.putBool("locked", currentState.isLocked);
                lastDebounceTime = millis();


                xSemaphoreGive(stateMutex);
                updateHardwareState();
            } else {
                xSemaphoreGive(stateMutex);
            }
        } else {
            if (currentState.isLocked && !currentState.doorClosed) {
                currentState.alarmTriggered = true;
            } else {
                currentState.alarmTriggered = false;
            }
            xSemaphoreGive(stateMutex);
        }
    }

    if (digitalRead(PIN_BTN_BELL) == LOW) {
        if (!bellActive) {
            bellActive = true;
            bellTimer = millis();
        }
    }

    handleBuzzer();

    vTaskDelay(10 / portTICK_PERIOD_MS);
}

void networkTask(void *pvParameters) {
    // 1. Connect WiFi
    Serial.print("[Core 0] Connecting to WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    Serial.println("\n[Core 0] WiFi Connected");

    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    config.token_status_callback = tokenStatusCallback;

    fbDO_stream.setBSSLBufferSize(4096, 1024);
    fbDO_write.setBSSLBufferSize(1024, 1024);

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    if (!Firebase.RTDB.beginStream(&fbDO_stream, pathControl + "/set_lock")) {
        Serial.printf("[Core 0] Stream error: %s\n", fbDO_stream.errorReason().c_str());
    }
    Firebase.RTDB.setStreamCallback(&fbDO_stream, streamCallback, streamTimeoutCallback);

    unsigned long lastUpload = 0;

    while(true) {
        if (Firebase.ready()) {

            // Periodic Status Update (Every 3 seconds)
            if (millis() - lastUpload > 3000) {
                lastUpload = millis();

                // Read State safely
                bool txLocked, txClosed, txAlarm;
                if (xSemaphoreTake(stateMutex, portMAX_DELAY)) {
                    txLocked = currentState.isLocked;
                    txClosed = currentState.doorClosed;
                    txAlarm = currentState.alarmTriggered;
                    xSemaphoreGive(stateMutex);
                }

                json.clear();
                json.set("is_locked", txLocked);
                json.set("door_closed", txClosed);
                json.set("alarm_triggered", txAlarm);

                Serial.println("[Core 0] Syncing status...");
                Firebase.RTDB.updateNode(&fbDO_write, pathStatus, &json);
                Firebase.RTDB.setTimestamp(&fbDO_write, parentPath + "/timestamp");
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void moveServo(int angle) {
    lockServo.write(angle);
}
// -----------------------------------------------------------

void updateHardwareState() {
    bool localLocked, localAlarm;

    if (xSemaphoreTake(stateMutex, portMAX_DELAY)) {
        localLocked = currentState.isLocked;
        localAlarm = currentState.alarmTriggered;
        xSemaphoreGive(stateMutex);
    }

    moveServo(localLocked ? LOCKED_ANGLE : UNLOCKED_ANGLE);

    if (localAlarm) {
        setRGB(true, false, false);
    } else if (localLocked) {
        setRGB(true, false, false);
    } else {
        setRGB(false, true, false);
    }
}

void handleBuzzer() {
    unsigned long currentMillis = millis();
    bool localAlarm;

    if (xSemaphoreTake(stateMutex, portMAX_DELAY)) {
        localAlarm = currentState.alarmTriggered;
        xSemaphoreGive(stateMutex);
    }

    if (localAlarm) {
        if (currentMillis - alarmTimer > 300) {
            alarmTimer = currentMillis;
            alarmToneState = !alarmToneState;
            ledcWriteTone(BUZZER_CHANNEL, alarmToneState ? 2000 : 1000);

            if (alarmToneState) setRGB(true, false, false);
            else setRGB(false, false, false);
        }
        return;
    }

    if (bellActive) {
        if (currentMillis - bellTimer < 500) {
            ledcWriteTone(BUZZER_CHANNEL, 600);
        } else if (currentMillis - bellTimer < 1000) {
            ledcWriteTone(BUZZER_CHANNEL, 400);
        } else {
            bellActive = false;
            ledcWriteTone(BUZZER_CHANNEL, 0);

            bool localLocked;
            if (xSemaphoreTake(stateMutex, portMAX_DELAY)) {
                localLocked = currentState.isLocked;
                xSemaphoreGive(stateMutex);
            }
            if (localLocked) setRGB(true, false, false);
            else setRGB(false, true, false);
        }
        return;
    }

    if (!bellActive && !localAlarm) {
        ledcWriteTone(BUZZER_CHANNEL, 0);
    }
}

void setRGB(bool r, bool g, bool b) {
    digitalWrite(PIN_RGB_RED, r ? LOW : HIGH);
    digitalWrite(PIN_RGB_GREEN, g ? LOW : HIGH);
    digitalWrite(PIN_RGB_BLUE, b ? LOW : HIGH);
}