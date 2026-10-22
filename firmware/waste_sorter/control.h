#pragma once
#include <stdint.h>
namespace waste {
enum Category : uint8_t { UNKNOWN, METAL, WET, DRY };
enum Stage : uint8_t { STOPPED, IDLE, SETTLE, SAMPLE, ALIGN, RELEASE,
                       CLOSE, HOME, WAIT_CLEAR, FAULT };
enum FaultReason : uint8_t { NO_FAULT, STOP_LOOP, LOST_ITEM, INVALID_ADC,
                            AMBIGUOUS_SAMPLE, CLEAR_TIMEOUT, GATE_FEEDBACK };
inline bool elapsed(uint32_t now, uint32_t then, uint32_t duration) {
  return uint32_t(now - then) >= duration;
}
constexpr uint16_t WET_MAX = 350;
constexpr uint16_t DRY_MIN = 650;
inline Category classify(uint16_t moisture, uint8_t metalVotes,
                         uint16_t wetMax = WET_MAX, uint16_t dryMin = DRY_MIN) {
  if (moisture > 1023 || metalVotes > 10 || wetMax >= dryMin || dryMin > 1023) return UNKNOWN;
  if (metalVotes >= 7) return METAL;
  if (metalVotes >= 4) return UNKNOWN; // Unstable metal reading
  if (moisture <= wetMax) return WET; // Example low-wet thresholds; calibrate physically
  if (moisture >= dryMin) return DRY;
  return UNKNOWN; // Ambiguous moisture band
}
inline uint8_t binAngle(Category c) {
  return c == METAL ? 30 : c == WET ? 90 : c == DRY ? 150 : 90;
}
class Cycle {
  Stage stage_ = STOPPED;
  Category category_ = UNKNOWN;
  uint32_t entered_ = 0, lastSample_ = 0, presenceAt_ = 0, clearAt_ = 0;
  uint32_t sum_ = 0;
  uint8_t samples_ = 0, metalVotes_ = 0, angle_ = 90;
  bool trackingPresence_ = false, trackingClear_ = false;
  uint32_t routed_ = 0;
  FaultReason fault_ = NO_FAULT;
  void enter(Stage s, uint32_t now) {
    stage_ = s; entered_ = now;
    if (s == HOME) angle_ = 90;
    if (s == IDLE) trackingPresence_ = false;
    if (s == WAIT_CLEAR) trackingClear_ = false;
  }
public:
  Stage stage() const { return stage_; }
  Category category() const { return category_; }
  uint8_t angle() const { return angle_; }
  bool gateOpen() const { return stage_ == RELEASE; }
  uint32_t routedCommands() const { return routed_; }
  FaultReason faultReason() const { return fault_; }
  uint8_t sampleCount() const { return samples_; }
  uint8_t metalVotes() const { return metalVotes_; }
  uint16_t meanMoisture() const { return samples_ ? uint16_t(sum_ / samples_) : 0; }
  uint16_t rawMean(bool wetIsLow) const {
    return samples_ ? uint16_t((wetIsLow ? sum_ : 1023UL*samples_-sum_) / samples_) : 0;
  }
  void trip(FaultReason reason, uint32_t now) {
    if (stage_ != FAULT) fault_ = reason; // Preserve first observed cause.
    enter(FAULT, now);
  }
  void command(char c, uint32_t now, bool present, bool stopClosed) {
    if (c == 'X') { if (stage_ != FAULT) enter(STOPPED, now); return; }
    if (c == 'R' && stage_ == FAULT && !present && stopClosed) {
      category_ = UNKNOWN; fault_ = NO_FAULT; enter(STOPPED, now); return;
    }
    if (c == 'A' && stage_ == STOPPED && !present && stopClosed) {
      category_ = UNKNOWN; fault_ = NO_FAULT; enter(HOME, now);
    }
  }
  void tick(uint32_t now, bool present, bool metal, uint16_t moisture,
            bool stopClosed) {
    if (!stopClosed && stage_ != STOPPED) { trip(STOP_LOOP, now); return; }
    switch (stage_) {
      case IDLE:
        if (!present) trackingPresence_ = false;
        else if (!trackingPresence_) { trackingPresence_ = true; presenceAt_ = now; }
        else if (elapsed(now, presenceAt_, 150)) enter(SETTLE, now);
        break;
      case SETTLE:
        if (!present) trip(LOST_ITEM, now);
        else if (elapsed(now, entered_, 200)) {
          samples_ = metalVotes_ = 0; sum_ = 0; lastSample_ = now;
          enter(SAMPLE, now);
        }
        break;
      case SAMPLE:
        if (!present) { trip(LOST_ITEM, now); break; }
        if (moisture > 1023) { trip(INVALID_ADC, now); break; }
        if (elapsed(now, lastSample_, 20)) {
          lastSample_ = now; sum_ += moisture; metalVotes_ += metal ? 1 : 0;
          if (++samples_ == 10) {
            category_ = classify(uint16_t(sum_ / 10), metalVotes_);
            if (category_ == UNKNOWN) trip(AMBIGUOUS_SAMPLE, now);
            else { angle_ = binAngle(category_); enter(ALIGN, now); }
          }
        }
        break;
      case ALIGN: if (elapsed(now, entered_, 700)) enter(RELEASE, now); break;
      case RELEASE: if (elapsed(now, entered_, 600)) enter(CLOSE, now); break;
      case CLOSE:
        if (elapsed(now, entered_, 400)) { ++routed_; enter(HOME, now); }
        break;
      case HOME: if (elapsed(now, entered_, 700)) enter(WAIT_CLEAR, now); break;
      case WAIT_CLEAR:
        if (elapsed(now, entered_, 5000)) { trip(CLEAR_TIMEOUT, now); break; }
        if (present) trackingClear_ = false;
        else if (!trackingClear_) { trackingClear_ = true; clearAt_ = now; }
        else if (elapsed(now, clearAt_, 300)) enter(IDLE, now);
        break;
      default: break;
    }
  }
};

// Optional endpoint feedback. A command must reach its endpoint in 350 ms.
// This is not motor-current sensing, item-exit confirmation or a hardware cutoff.
class GateWatchdog {
  bool started_ = false, requestedOpen_ = false, fault_ = false;
  uint32_t commandedAt_ = 0;
public:
  static constexpr uint32_t TIMEOUT_MS = 350;
  bool faulted() const { return fault_; }
  bool reset(bool closedSwitch, bool openSwitch) {
    if (!closedSwitch || openSwitch) return false;
    fault_=false; started_=false; return true;
  }
  bool update(uint32_t now, bool requestedOpen, bool closedSwitch, bool openSwitch) {
    if (fault_) return false;
    if (closedSwitch && openSwitch) { fault_=true; return false; }
    if (!started_ || requestedOpen != requestedOpen_) {
      started_=true; requestedOpen_=requestedOpen; commandedAt_=now;
    }
    const bool reached=requestedOpen ? openSwitch && !closedSwitch : closedSwitch && !openSwitch;
    if (!reached && elapsed(now,commandedAt_,TIMEOUT_MS)) fault_=true;
    return !fault_;
  }
};
} // namespace waste
