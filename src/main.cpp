#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

// Helper headers
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// Define your secrets here or include your secrets.h
#include <secrets.h>

#define PIN_BTN_BELL    21
#define PIN_BTN_TOGGLE  19
#define PIN_LIMIT_SW    18

// --- RGB LED PINS (Choose pins not used by other peripherals) ---
#define PIN_RGB_RED     25
#define PIN_RGB_GREEN   33
#define PIN_RGB_BLUE    32

#define PIN_SERVO       26
#define PIN_BUZZER      23

#define LOCKED_ANGLE    0
#define UNLOCKED_ANGLE  90
#define WDT_TIMEOUT     10
#define BUZZER_CHANNEL  0

// ================= OBJECTS & VARIABLES =================
FirebaseData fbDO;
FirebaseAuth auth;
FirebaseConfig config;
Servo lockServo;
Preferences preferences;

// State Variables
bool isLocked = false;
bool doorClosed = false;
bool alarmTriggered = false;

// Timers
unsigned long lastDebounceTime = 0;
unsigned long lastFirebaseUpdate = 0;
unsigned long alarmTimer = 0;
unsigned long bellTimer = 0;
bool bellActive = false;
int alarmToneState = 0; // 0 or 1, used for buzzer tone and LED blinking

// Paths
String parentPath = "/device_001";
String pathControl = parentPath + "/control";
String pathStatus = parentPath + "/status";

// Function Declarations
void updateHardware();

void setRGB(bool r, bool g, bool b); // Helper function
void checkInputs();

void handleBuzzer();

void syncFirebase();

void setupWiFi();

void setup() {
    Serial.begin(115200);

    // 1. Init Watchdog
    esp_task_wdt_init(WDT_TIMEOUT, true);
    esp_task_wdt_add(NULL);

    // 2. Pin Modes
    pinMode(PIN_BTN_BELL, INPUT_PULLUP);
    pinMode(PIN_BTN_TOGGLE, INPUT_PULLUP);
    pinMode(PIN_LIMIT_SW, INPUT_PULLUP);

    // RGB Pin Modes
    pinMode(PIN_RGB_RED, OUTPUT);
    pinMode(PIN_RGB_GREEN, OUTPUT);
    pinMode(PIN_RGB_BLUE, OUTPUT);

    // Turn RGB OFF initially (HIGH = OFF for Common Anode)
    setRGB(false, false, false);

    // 3. Setup Buzzer
    ledcSetup(BUZZER_CHANNEL, 2000, 8);
    ledcAttachPin(PIN_BUZZER, BUZZER_CHANNEL);

    // 4. Setup Servo
    lockServo.attach(PIN_SERVO);

    // 5. Load Preferences
    preferences.begin("door_lock", false);
    isLocked = preferences.getBool("locked", false);

    // 6. Setup WiFi
    setupWiFi(); // LED will show Blue here

    // 7. Setup Firebase
    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    config.token_status_callback = tokenStatusCallback;

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    // Initial hardware update after setup
    updateHardware();
}

void loop() {
    esp_task_wdt_reset();
    checkInputs();

    // Alarm Logic
    if (isLocked && !doorClosed) {
        alarmTriggered = true;
    } else {
        alarmTriggered = false;
    }

    updateHardware();
    handleBuzzer();
    syncFirebase();
}

// Helper to handle Common Anode Logic
// true = ON (Pin LOW), false = OFF (Pin HIGH)
void setRGB(bool r, bool g, bool b) {
    digitalWrite(PIN_RGB_RED, r ? LOW : HIGH);
    digitalWrite(PIN_RGB_GREEN, g ? LOW : HIGH);
    digitalWrite(PIN_RGB_BLUE, b ? LOW : HIGH);
}

void checkInputs() {
    bool currentDoorState = (digitalRead(PIN_LIMIT_SW) == LOW);
    doorClosed = currentDoorState;

    // Toggle Button
    if (digitalRead(PIN_BTN_TOGGLE) == LOW) {
        if (millis() - lastDebounceTime > 250) {
            isLocked = !isLocked;
            preferences.putBool("locked", isLocked);

            // Sync manual press immediately (Optimistic UI)
            if (Firebase.ready()) {
                Firebase.RTDB.setBoolAsync(&fbDO, pathControl + "/set_lock", isLocked);
            }
            lastDebounceTime = millis();
        }
    }

    // Doorbell
    if (digitalRead(PIN_BTN_BELL) == LOW) {
        if (!bellActive) {
            bellActive = true;
            bellTimer = millis();
        }
    }
}

void updateHardware() {
    // 1. Handle Servo
    static bool lastServoState = !isLocked; // Force initial update
    if (lastServoState != isLocked) {
        lockServo.write(isLocked ? LOCKED_ANGLE : UNLOCKED_ANGLE);
        lastServoState = isLocked;
    }

    // 2. Handle RGB LED
    if (alarmTriggered) {
        // Blink Red when alarm is triggered (synced with buzzer state)
        if (alarmToneState) {
            setRGB(true, false, false); // RED ON
        } else {
            setRGB(false, false, false); // OFF
        }
    } else if (isLocked) {
        setRGB(true, false, false); // Solid RED
    } else {
        setRGB(false, true, false); // Solid GREEN
    }
}

void handleBuzzer() {
    unsigned long currentMillis = millis();

    if (alarmTriggered) {
        if (currentMillis - alarmTimer > 300) {
            alarmTimer = currentMillis;
            alarmToneState = !alarmToneState; // Toggle state

            // Note: The LED blinking is handled in updateHardware() using this variable
            ledcWriteTone(BUZZER_CHANNEL, alarmToneState ? 2000 : 1000);
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
        }
        return;
    }
    ledcWriteTone(BUZZER_CHANNEL, 0);
}

void syncFirebase() {
    if (Firebase.ready()) {
        // We update every 3 seconds to keep the heartbeat alive
        if (millis() - lastFirebaseUpdate > 3000 || lastFirebaseUpdate == 0) {
            Serial.println("Syncing firebase");
            lastFirebaseUpdate = millis();

            // 1. READ CONTROL
            if (Firebase.RTDB.getBool(&fbDO, pathControl + "/set_lock")) {
                bool cloudLockState = fbDO.boolData();
                if (cloudLockState != isLocked) {
                    isLocked = cloudLockState;
                    preferences.putBool("locked", isLocked);
                }
            }

            // 2. WRITE STATUS
            Firebase.RTDB.setBoolAsync(&fbDO, pathStatus + "/is_locked", isLocked);
            Firebase.RTDB.setBoolAsync(&fbDO, pathStatus + "/door_closed", doorClosed);
            Firebase.RTDB.setBoolAsync(&fbDO, pathStatus + "/alarm_triggered", alarmTriggered);

            // 3. HEARTBEAT
            Firebase.RTDB.setTimestampAsync(&fbDO, parentPath + "/timestamp");
        }
    }
}

void setupWiFi() {
    // Show Blue LED while connecting
    setRGB(false, false, true);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 20) {
        delay(500);
        retry++;
        esp_task_wdt_reset();
    }

    // Turn off Blue (will be overridden by updateHardware in main loop)
    setRGB(false, false, false);
}
