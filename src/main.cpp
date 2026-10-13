#include <Arduino.h>
#include <stdarg.h>
#include "sequence_controller.h"

const uint8_t LED_PINS[] = {16, 17, 18}; // A, B, C; active-high outputs.
const uint32_t PWM_FREQUENCY = 5000;
SequenceController sequence;
uint16_t simulatedSensorValue = 1000;
CommandBuffer commandBuffer;
uint8_t previousDuty[3] = {};
bool pwmReady = true;

// Print only command responses and state changes, never continuously in loop().
void report(const char* format, ...) {
    char message[128];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    Serial.println(message);
}
void applyOutputs(uint32_t now) {
    for (uint8_t i = 0; i < 3; ++i) {
        const uint8_t duty = pwmReady ? sequence.duty(i, now) : 0;
        if (duty == previousDuty[i]) continue;
        ledcWrite(i, duty);
        previousDuty[i] = duty;
    }
}
void sampleSafety(uint32_t now) {
    const bool wasUnsafe = sequence.unsafe;
    const bool tripped = sequence.sample(now, simulatedSensorValue);
    // Apply the cutoff before reporting it to the host.
    applyOutputs(now);
    if (tripped) report("SAFETY CUTOFF: sensor=%u threshold=%u; all LEDs OFF", sequence.sensorValue, SequenceController::SAFETY_THRESHOLD);
    else if (wasUnsafe && !sequence.unsafe) report("SAFETY: input safe; send START_SEQUENCE to start again");
}
void handleCommand(const char* text) {
    const Command command = parseCommand(text);
    const uint32_t now = millis();
    switch (command.kind) {
    case Commands::Stop:
        sequence.stop(); applyOutputs(now);
        report("OK: STOP; all LEDs OFF");
        break;
    case Commands::Start:
        if (sequence.running) { report("ERR: sequence already running; use STOP first"); break; }
        // Never start using a stale safety reading.
        sampleSafety(now);
        if (!pwmReady) report("ERR: PWM setup failed; restart the board");
        else if (sequence.unsafe) report("ERR: unsafe input; set sensor below 2500 before starting");
        else if (!sequence.start(now, command.value)) report("ERR: sequence already running; use STOP first");
        else {
            applyOutputs(now);
            report("OK: START_SEQUENCE intensity=%u; A=0 B=400 C=900 OFF=2000 ms", command.value);
        }
        break;
    case Commands::SetSensor:
        // Change the input only; the periodic safety check decides whether to stop.
        simulatedSensorValue = command.value;
        report("OK: sensor input=%u", simulatedSensorValue);
        break;
    default:
        report("ERR: invalid command or parameter");
    }
}
void readCommands() {
    // Process at most one command or 64 bytes before checking timing and safety again.
    for (uint8_t i = 0; i < 64 && Serial.available(); ++i) {
        const auto result = commandBuffer.feed(char(Serial.read()));
        if (result == CommandBuffer::Result::Ready) {
            handleCommand(commandBuffer.text);
            return;
        }
        if (result == CommandBuffer::Result::Invalid) {
            report("ERR: malformed/overlong command discarded");
            return;
        }
    }
}
void setup() {
    Serial.begin(115200);
    for (uint8_t i = 0; i < 3; ++i) {
        pinMode(LED_PINS[i], OUTPUT); digitalWrite(LED_PINS[i], LOW);
        if (ledcSetup(i, PWM_FREQUENCY, 8) == 0) pwmReady = false;
        ledcAttachPin(LED_PINS[i], i);
        ledcWrite(i, 0);
    }
    sampleSafety(millis());
    report("ESP32 CAPABILITY TEST | A=GPIO16 B=GPIO17 C=GPIO18 | 115200 8N1");
    report("Safety mode: SIMULATED | threshold >=2500 | interval 200 ms");
    report(pwmReady ? "Ready. Send START_SEQUENCE [1-5] or STOP." : "ERR: PWM setup failed; START disabled");
}
void updateController() {
    const uint32_t now = millis();
    if (sequence.safetyDue(now)) sampleSafety(now);
    if (sequence.update(now)) { applyOutputs(now); report("OK: sequence complete; all LEDs OFF at 2000 ms"); }
    applyOutputs(now);
}
void loop() {
    // Recheck deadlines after processing commands; no blocking sequence delays.
    updateController();
    readCommands();
    updateController();
}
