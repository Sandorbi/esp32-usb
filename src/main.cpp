#include <Arduino.h>
#include <string.h>

const uint8_t LED_PINS[] = {16, 17, 18}; // A, B, C; PWM channels 0, 1, 2.
const uint32_t PWM_FREQUENCY = 5000;
const uint16_t SAFETY_THRESHOLD = 2500;

bool running = false;
bool pwmReady = true;
uint8_t intensity = 5;
uint16_t sensorValue = 1000;
uint32_t sequenceStartedAt = 0;
uint32_t lastSensorRead = 0;

void stopAll() {
    running = false;
    for (uint8_t i = 0; i < 3; ++i) ledcWrite(i, 0);
}

void updateLEDs() {
    if (!running) return;

    const uint32_t elapsed = millis() - sequenceStartedAt;
    if (elapsed >= 2000) {
        stopAll();
        Serial.println("OK: sequence complete");
        return;
    }

    const uint8_t duty = intensity * 51;
    ledcWrite(0, duty);
    ledcWrite(1, elapsed >= 400 ? duty : 0);
    ledcWrite(2, elapsed >= 900 ? duty : 0);
}

void startSequence(uint8_t level) {
    if (!pwmReady) {
        Serial.println("ERR: PWM setup failed");
        return;
    }
    if (running) {
        Serial.println("ERR: already running");
        return;
    }
    // Check the current input before enabling outputs, without waiting for a sample.
    if (sensorValue >= SAFETY_THRESHOLD) {
        stopAll();
        Serial.println("ERR: unsafe sensor input");
        return;
    }

    intensity = level;
    sequenceStartedAt = millis();
    running = true;
    updateLEDs();
    Serial.printf("OK: START_SEQUENCE %u\n", intensity);
}

void checkSafety() {
    const uint32_t now = millis();
    if (uint32_t(now - lastSensorRead) < 200) return;
    lastSensorRead = now;

    // Sensor commands only change the input; this periodic check performs the cutoff.
    if (sensorValue >= SAFETY_THRESHOLD) {
        const bool wasRunning = running;
        stopAll();
        if (wasRunning) Serial.println("SAFETY CUTOFF");
    }
}

void handleCommand(const char* text) {
    if (strcmp(text, "STOP") == 0) {
        stopAll();
        Serial.println("OK: STOP; all LEDs OFF");
    } else if (strcmp(text, "START_SEQUENCE") == 0) {
        startSequence(5);
    } else if (strlen(text) == 16 && strncmp(text, "START_SEQUENCE ", 15) == 0
               && text[15] >= '1' && text[15] <= '5') {
        // Exact length rejects extra digits, spaces, or trailing junk.
        startSequence(text[15] - '0');
    } else if (strncmp(text, "SET_SENSOR ", 11) == 0) {
        const char* number = text + 11;
        if (strlen(number) == 0 || strlen(number) > 4) {
            Serial.println("ERR: sensor must be 0-4095");
            return;
        }
        uint16_t value = 0;
        for (uint8_t i = 0; number[i] != '\0'; ++i) {
            if (number[i] < '0' || number[i] > '9') {
                Serial.println("ERR: sensor must be 0-4095");
                return;
            }
            value = value * 10 + (number[i] - '0');
        }
        if (value > 4095) {
            Serial.println("ERR: sensor must be 0-4095");
            return;
        }
        sensorValue = value;
        Serial.printf("OK: sensor input=%u\n", sensorValue);
    } else {
        Serial.println("ERR: invalid command or parameter");
    }
}

void readCommands() {
    static char text[32];
    static uint8_t length = 0;
    static bool invalid = false;

    for (uint8_t i = 0; i < 64 && Serial.available(); ++i) {
        const char c = char(Serial.read());
        if (c == '\n' || c == '\r') {
            text[length] = '\0';
            if (invalid) Serial.println("ERR: malformed command");
            else if (length > 0) handleCommand(text);
            length = 0;
            invalid = false;
            return;
        }
        // Discard a malformed line completely rather than interpreting its suffix.
        if (invalid) continue;
        if (c < 32 || c > 126 || length >= sizeof(text) - 1) {
            invalid = true;
            continue;
        }
        text[length++] = (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;
    }
}

void setup() {
    Serial.begin(115200);
    for (uint8_t i = 0; i < 3; ++i) {
        pinMode(LED_PINS[i], OUTPUT);
        digitalWrite(LED_PINS[i], LOW);
        if (ledcSetup(i, PWM_FREQUENCY, 8) == 0) pwmReady = false;
        ledcAttachPin(LED_PINS[i], i);
    }
    stopAll();
    lastSensorRead = millis();
    if (!pwmReady) Serial.println("ERR: PWM setup failed");
}

void loop() {
    checkSafety();
    updateLEDs();
    readCommands();
    checkSafety();
    updateLEDs();
}
