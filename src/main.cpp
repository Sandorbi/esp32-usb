#include <Arduino.h>
#include <stdarg.h>
#include "sequence_controller.h"

const uint8_t LED_PINS[] = {16, 17, 18}; // A, B, C; active-high outputs.
const uint32_t PWM_FREQUENCY = 5000;
SequenceController sequence;
SimulatedSensor simulatedSensor;
CommandBuffer commandBuffer;
uint8_t previousDuty[3] = {};
bool pwmReady = true;

// Queue messages so serial output cannot hold up timing or safety checks.
const uint8_t LOG_CAPACITY = 12;
char logLines[LOG_CAPACITY][128];
uint8_t logHead = 0, logCount = 0;
size_t logOffset = 0;

void report(const char* format, ...) {
    // Drop new messages if full rather than waiting for the host.
    if (logCount == LOG_CAPACITY) return;
    char* message = logLines[(logHead + logCount) % LOG_CAPACITY];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(logLines[0]) - 1, format, args);
    va_end(args);
    strcat(message, "\n");
    ++logCount;
}
void flushLog() {
    if (!logCount) return;
    const int available = Serial.availableForWrite();
    if (available <= 0) return;

    // Send only what fits; resume a partial line on the next loop.
    const char* message = logLines[logHead];
    const size_t length = strlen(message);
    const size_t remaining = length - logOffset;
    const size_t amount = remaining < size_t(available) ? remaining : size_t(available);
    logOffset += Serial.write(reinterpret_cast<const uint8_t*>(message + logOffset), amount);
    if (logOffset == length) {
        logOffset = 0;
        logHead = (logHead + 1) % LOG_CAPACITY;
        --logCount;
    }
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
    const bool tripped = sequence.sample(now, simulatedSensor.read(sequence, now));
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
        else if (sequence.unsafe) report("ERR: unsafe input; reset safety test before starting");
        else if (!sequence.start(now, command.intensity)) report("ERR: sequence already running; use STOP first");
        else {
            applyOutputs(now);
            report("OK: START_SEQUENCE intensity=%u; A=0 B=400 C=900 OFF=2000 ms", command.intensity);
        }
        break;
    case Commands::ArmSafety:
    case Commands::ResetSafety:
        if (sequence.running) { report("ERR: use STOP before changing the safety test"); break; }
        if (command.kind == Commands::ArmSafety) {
            if (simulatedSensor.high) { report("ERR: reset the safety test first"); break; }
            simulatedSensor.armed = true;
            report("OK: safety test ARMED; input rises 1200 ms into the next sequence");
        } else {
            simulatedSensor.reset(); sampleSafety(now);
            report("OK: safety test OFF; simulated input=1000");
        }
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
    flushLog();
}
