#pragma once
#include <stdint.h>
#include <string.h>

// Timing and safety state are independent of the hardware I/O.
class SequenceController {
public:
    static constexpr uint32_t DURATION_MS = 2000;
    static constexpr uint32_t SAFETY_PERIOD_MS = 200;
    static constexpr uint16_t SAFETY_THRESHOLD = 2500; // Simulated units, not temperature.
    bool running = false;
    bool unsafe = false;
    uint16_t sensorValue = 1000;
    uint8_t intensity = 5;
    uint32_t startedAt = 0;
    uint32_t sampledAt = 0;

    bool start(uint32_t now, uint8_t level) {
        if (unsafe || running || level < 1 || level > 5) return false;
        intensity = level; startedAt = now; running = true;
        return true;
    }
    void stop() { running = false; }
    // Unsigned subtraction preserves elapsed time across millis() rollover.
    uint32_t elapsed(uint32_t now) const { return uint32_t(now - startedAt); }
    bool safetyDue(uint32_t now) const { return uint32_t(now - sampledAt) >= SAFETY_PERIOD_MS; }
    bool sample(uint32_t now, uint16_t value) {
        sampledAt = now; sensorValue = value;
        const bool wasUnsafe = unsafe;
        unsafe = value >= SAFETY_THRESHOLD;
        if (unsafe) stop();
        return unsafe && !wasUnsafe; // Report a cutoff only on the transition to unsafe.
    }
    bool update(uint32_t now) {
        if (running && elapsed(now) >= DURATION_MS) { stop(); return true; }
        return false;
    }
    uint8_t duty(uint8_t channel, uint32_t now) const {
        const uint32_t onset[] = {0, 400, 900}; // A/B/C offsets from the same start time.
        if (channel >= 3 || !running || unsafe || elapsed(now) >= DURATION_MS || elapsed(now) < onset[channel]) return 0;
        return intensity * 51; // Map levels 1–5 to 20–100% of the 8-bit PWM range.
    }
};

// An armed test raises the input at 1.2 s without another host command.
// Keep the input high after cutoff until reset, preventing an unsafe restart.
class SimulatedSensor {
public:
    bool armed = false;
    bool high = false;
    uint16_t read(const SequenceController& sequence, uint32_t now) {
        if (armed && sequence.running && sequence.elapsed(now) >= 1200) high = true;
        return high ? 3000 : 1000;
    }
    void reset() { armed = false; high = false; }
};

enum class Commands { Start, Stop, ArmSafety, ResetSafety, Invalid };
struct Command {
    Commands kind;
    uint8_t intensity;
    Command(Commands value, uint8_t level = 5) : kind(value), intensity(level) {}
};
inline Command parseCommand(const char* text) {
    if (strcmp(text, "STOP") == 0) return {Commands::Stop};
    if (strcmp(text, "SAFETY_TEST ON") == 0) return {Commands::ArmSafety};
    if (strcmp(text, "SAFETY_TEST OFF") == 0) return {Commands::ResetSafety};
    if (strcmp(text, "START_SEQUENCE") == 0) return {Commands::Start, 5};
    const char prefix[] = "START_SEQUENCE ";
    const size_t length = sizeof(prefix) - 1;
    // Accept exactly one intensity digit; reject trailing characters.
    if (strncmp(text, prefix, length) == 0 && text[length] >= '1' && text[length] <= '5' && text[length + 1] == '\0')
        return {Commands::Start, uint8_t(text[length] - '0')};
    return {Commands::Invalid};
}

class CommandBuffer {
public:
    enum class Result { Pending, Ready, Invalid };
    char text[48] = {};
    Result feed(char c) {
        if (c == '\n' || c == '\r') {
            const bool bad = invalid;
            text[length] = '\0';
            const bool ready = length != 0;
            length = 0; invalid = false;
            return bad ? Result::Invalid : ready ? Result::Ready : Result::Pending;
        }
        // Discard malformed input through the newline, including any command-like suffix.
        if (invalid) return Result::Pending;
        if (c < 32 || c > 126 || length >= sizeof(text) - 1) { invalid = true; return Result::Pending; }
        text[length++] = (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;
        return Result::Pending;
    }
private:
    uint8_t length = 0;
    bool invalid = false;
};
