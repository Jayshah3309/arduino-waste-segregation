#pragma once
#include <stdint.h>
namespace waste {
enum Category : uint8_t { UNKNOWN, METAL, WET, DRY };
enum Stage : uint8_t { STOPPED, IDLE, SETTLE, SAMPLE, ALIGN, RELEASE,
                       CLOSE, HOME, WAIT_CLEAR, FAULT };
inline bool elapsed(uint32_t now, uint32_t then, uint32_t duration) {
  return uint32_t(now - then) >= duration;
}
inline Category classify(uint16_t moisture, uint8_t metalVotes) {
  if (moisture > 1023 || metalVotes > 10) return UNKNOWN;
  if (metalVotes >= 7) return METAL;
  if (metalVotes >= 4) return UNKNOWN; // Unstable metal reading
  if (moisture <= 350) return WET; // Example calibrated low-wet thresholds
  if (moisture >= 650) return DRY;
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
  void command(char c, uint32_t now, bool present, bool stopClosed) {
    if (c == 'X') { enter(STOPPED, now); return; }
    if (c == 'R' && stage_ == FAULT && !present && stopClosed) {
      category_ = UNKNOWN; enter(STOPPED, now); return;
    }
    if (c == 'A' && stage_ == STOPPED && !present && stopClosed) {
      category_ = UNKNOWN; enter(HOME, now);
    }
  }
  void tick(uint32_t now, bool present, bool metal, uint16_t moisture,
            bool stopClosed) {
    if (!stopClosed && stage_ != STOPPED) { enter(FAULT, now); return; }
    switch (stage_) {
      case IDLE:
        if (!present) trackingPresence_ = false;
        else if (!trackingPresence_) { trackingPresence_ = true; presenceAt_ = now; }
        else if (elapsed(now, presenceAt_, 150)) enter(SETTLE, now);
        break;
      case SETTLE:
        if (!present) enter(FAULT, now);
        else if (elapsed(now, entered_, 200)) {
          samples_ = metalVotes_ = 0; sum_ = 0; lastSample_ = now;
          enter(SAMPLE, now);
        }
        break;
      case SAMPLE:
        if (!present || moisture > 1023) { enter(FAULT, now); break; }
        if (elapsed(now, lastSample_, 20)) {
          lastSample_ = now; sum_ += moisture; metalVotes_ += metal ? 1 : 0;
          if (++samples_ == 10) {
            category_ = classify(uint16_t(sum_ / 10), metalVotes_);
            if (category_ == UNKNOWN) enter(FAULT, now);
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
        if (elapsed(now, entered_, 5000)) { enter(FAULT, now); break; }
        if (present) trackingClear_ = false;
        else if (!trackingClear_) { trackingClear_ = true; clearAt_ = now; }
        else if (elapsed(now, clearAt_, 300)) enter(IDLE, now);
        break;
      default: break;
    }
  }
};
} // namespace waste
