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

// ================= PINS =================
#define PIN_BTN_BELL    21
#define PIN_BTN_TOGGLE  19
#define PIN_LIMIT_SW    18
#define PIN_RGB_RED     25
#define PIN_RGB_GREEN   33
#define PIN_RGB_BLUE    32
#define PIN_SERVO       26
#define PIN_BUZZER      23

// ================= SETTINGS =================
#define LOCKED_ANGLE    0
#define UNLOCKED_ANGLE  90
#define BUZZER_CHANNEL  0
#define WDT_TIMEOUT     30

// ================= OBJECTS =================
FirebaseData fbDO_stream; // Dedicated for Stream
FirebaseData fbDO_write;  // Dedicated for Writes
FirebaseAuth auth;
FirebaseConfig config;
Servo lockServo;
Preferences preferences;
FirebaseJson json;

// ================= RTOS HANDLES =================
TaskHandle_t NetworkTaskHandle = NULL;
QueueHandle_t commandQueue;     // To send commands from Cloud -> Hardware
SemaphoreHandle_t stateMutex;   // To protect shared variables

// ================= SHARED STATE =================
// These variables are accessed by both cores, so we protect them with a Mutex
struct DeviceState {
    bool isLocked;
    bool doorClosed;
    bool alarmTriggered;
};
DeviceState currentState;

// Internal Hardware Timers (Core 1 only)
unsigned long lastDebounceTime = 0;
unsigned long alarmTimer = 0;
unsigned long bellTimer = 0;
bool bellActive = false;
int alarmToneState = 0;

// Paths
String parentPath = "/device_001";
String pathControl = parentPath + "/control";
String pathStatus = parentPath + "/status";

// ================= FUNCTION PROTOTYPES =================
void networkTask(void *pvParameters);
void updateHardwareState();
void moveServo(int angle);
void setRGB(bool r, bool g, bool b);
void handleBuzzer();

// ================= FIREBASE STREAM CALLBACK =================
// This runs on Core 0 (Network Task)
void streamCallback(FirebaseStream data) {
    if (data.dataType() == "boolean") {
        bool requestedLockState = data.boolData();
        Serial.printf("[Core 0] Stream received: %s\n", requestedLockState ? "LOCKED" : "UNLOCKED");

        // Send this command to Core 1 via Queue
        // We do not write to hardware here directly to avoid conflicts
        bool cmd = requestedLockState;
        xQueueSend(commandQueue, &cmd, 0);
    }
}

void streamTimeoutCallback(bool timeout) {
    if (timeout) Serial.println("[Core 0] Stream timeout, resuming...");
}

// ================= SETUP =================
void setup() {
    Serial.begin(115200);

    // 1. Initialize RTOS Objects
    commandQueue = xQueueCreate(5, sizeof(bool)); // Queue for lock commands
    stateMutex = xSemaphoreCreateMutex();         // Mutex for variable protection

    // 2. Init Pins
    pinMode(PIN_BTN_BELL, INPUT_PULLUP);
    pinMode(PIN_BTN_TOGGLE, INPUT_PULLUP);
    pinMode(PIN_LIMIT_SW, INPUT_PULLUP);

    pinMode(PIN_RGB_RED, OUTPUT);
    pinMode(PIN_RGB_GREEN, OUTPUT);
    pinMode(PIN_RGB_BLUE, OUTPUT);
    setRGB(false, false, false); // Off initially

    ledcSetup(BUZZER_CHANNEL, 2000, 8);
    ledcAttachPin(PIN_BUZZER, BUZZER_CHANNEL);

    // 3. Load Saved State
    preferences.begin("door_lock", false);

    // Initial State Setup
    if (xSemaphoreTake(stateMutex, portMAX_DELAY)) {
        currentState.isLocked = preferences.getBool("locked", false);
        currentState.doorClosed = (digitalRead(PIN_LIMIT_SW) == LOW);
        currentState.alarmTriggered = false;
        xSemaphoreGive(stateMutex);
    }

    // 4. Create Network Task on Core 0
    // Stack size 10000 bytes, Priority 1, pinned to Core 0
    xTaskCreatePinnedToCore(
        networkTask,
        "NetworkTask",
        10000,
        NULL,
        1,
        &NetworkTaskHandle,
        0
    );

    // Apply initial hardware state immediately
    updateHardwareState();
}

// ================= CORE 1: HARDWARE LOOP =================
// This is the default Arduino loop, running on Core 1
void loop() {
    // 1. Process Incoming Commands from Network Task (Core 0)
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

    // 2. Read Inputs (Debounce & Logic)
    bool localLocked;
    bool localClosed;

    // Check Limit Switch
    bool currentSwitchState = (digitalRead(PIN_LIMIT_SW) == LOW);

    // Lock access for state update
    if (xSemaphoreTake(stateMutex, portMAX_DELAY)) {
        if (currentState.doorClosed != currentSwitchState) {
            currentState.doorClosed = currentSwitchState;
            // Release momentarily to update hardware, then re-acquire if needed is complex,
            // but updateHardwareState reads from mutex again or we pass params.
            // Let's just update the local copy for logic.
        }

        // Manual Toggle Button
        if (digitalRead(PIN_BTN_TOGGLE) == LOW) {
            if (millis() - lastDebounceTime > 250) {
                currentState.isLocked = !currentState.isLocked;
                preferences.putBool("locked", currentState.isLocked);
                lastDebounceTime = millis();

                // Note: We don't upload to Firebase here.
                // Core 0 detects the change via periodic check or we could use another queue.
                // For simplicity, Core 0 will sync the new state periodically.

                xSemaphoreGive(stateMutex); // Release before hardware update
                updateHardwareState();      // Update Servo/LED
            } else {
                xSemaphoreGive(stateMutex);
            }
        } else {
            // Logic for Alarm
            if (currentState.isLocked && !currentState.doorClosed) {
                currentState.alarmTriggered = true;
            } else {
                currentState.alarmTriggered = false;
            }
            xSemaphoreGive(stateMutex);
        }
    }

    // 3. Handle Bell
    if (digitalRead(PIN_BTN_BELL) == LOW) {
        if (!bellActive) {
            bellActive = true;
            bellTimer = millis();
        }
    }

    // 4. Update Outputs (Buzzer & LEDs)
    handleBuzzer();

    // 5. Yield to IDLE task
    vTaskDelay(10 / portTICK_PERIOD_MS);
}

// ================= CORE 0: NETWORK TASK =================
void networkTask(void *pvParameters) {
    // 1. Connect WiFi
    Serial.print("[Core 0] Connecting to WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    Serial.println("\n[Core 0] WiFi Connected");

    // 2. Setup Firebase
    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    config.token_status_callback = tokenStatusCallback;

    fbDO_stream.setBSSLBufferSize(4096, 1024);
    fbDO_write.setBSSLBufferSize(1024, 1024);

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    // 3. Start Stream
    if (!Firebase.RTDB.beginStream(&fbDO_stream, pathControl + "/set_lock")) {
        Serial.printf("[Core 0] Stream error: %s\n", fbDO_stream.errorReason().c_str());
    }
    Firebase.RTDB.setStreamCallback(&fbDO_stream, streamCallback, streamTimeoutCallback);

    unsigned long lastUpload = 0;

    // Infinite Loop for Core 0
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

                // Prepare JSON
                json.clear();
                json.set("is_locked", txLocked);
                json.set("door_closed", txClosed);
                json.set("alarm_triggered", txAlarm);

                // Perform Network Writes (Blocking/Slow operations are fine here)
                Serial.println("[Core 0] Syncing status...");
                Firebase.RTDB.updateNode(&fbDO_write, pathStatus, &json);
                Firebase.RTDB.setTimestamp(&fbDO_write, parentPath + "/timestamp");
            }
        }

        // Necessary delay to prevent watchdog triggering on Core 0
        // and allow Stream callback background processing
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// ================= HELPER FUNCTIONS =================

void moveServo(int angle) {
    // This runs on Core 1
    lockServo.attach(PIN_SERVO);
    lockServo.write(angle);
    // Blocking delay is okay here as it only blocks Core 1 logic briefly
    // It will NOT block network on Core 0
    vTaskDelay(300 / portTICK_PERIOD_MS);
    lockServo.detach();
}

void updateHardwareState() {
    bool localLocked, localAlarm;

    // Read state for hardware actuation
    if (xSemaphoreTake(stateMutex, portMAX_DELAY)) {
        localLocked = currentState.isLocked;
        localAlarm = currentState.alarmTriggered;
        xSemaphoreGive(stateMutex);
    }

    // 1. Move Servo
    moveServo(localLocked ? LOCKED_ANGLE : UNLOCKED_ANGLE);

    // 2. Set LED
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

    // Alarm Logic
    if (localAlarm) {
        if (currentMillis - alarmTimer > 300) {
            alarmTimer = currentMillis;
            alarmToneState = !alarmToneState;
            ledcWriteTone(BUZZER_CHANNEL, alarmToneState ? 2000 : 1000);

            // Blink LED
            if (alarmToneState) setRGB(true, false, false);
            else setRGB(false, false, false);
        }
        return;
    }

    // Bell Logic
    if (bellActive) {
        if (currentMillis - bellTimer < 500) {
            ledcWriteTone(BUZZER_CHANNEL, 600);
        } else if (currentMillis - bellTimer < 1000) {
            ledcWriteTone(BUZZER_CHANNEL, 400);
        } else {
            bellActive = false;
            ledcWriteTone(BUZZER_CHANNEL, 0);

            // Restore LED state after bell
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

    // Idle
    if (!bellActive && !localAlarm) {
        ledcWriteTone(BUZZER_CHANNEL, 0);
    }
}

void setRGB(bool r, bool g, bool b) {
    digitalWrite(PIN_RGB_RED, r ? LOW : HIGH);
    digitalWrite(PIN_RGB_GREEN, g ? LOW : HIGH);
    digitalWrite(PIN_RGB_BLUE, b ? LOW : HIGH);
}