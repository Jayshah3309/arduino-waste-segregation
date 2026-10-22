/* New UNO reference: three sensor inputs and a two-servo single-item sorter.
 * Proposed pins/mechanics only. Optional endpoint feedback is disabled by default.
 * Calibrate before use. Serial A=arm, X=stop, R=clear fault when chamber clear.
 */
#include <Servo.h>
#include "control.h"
constexpr uint8_t PRESENCE_PIN = 2;
constexpr uint8_t METAL_PIN = 3;
constexpr uint8_t STOP_PIN = 4; // Normally closed contact to GND
constexpr uint8_t GATE_CLOSED_PIN = 5;
constexpr uint8_t GATE_OPEN_PIN = 6;
constexpr bool USE_GATE_FEEDBACK = false; // Enable only after wiring/calibration.
constexpr uint8_t MOISTURE_PIN = A0;
constexpr uint8_t DIVERTER_PIN = 9;
constexpr uint8_t GATE_PIN = 10;
constexpr uint8_t PRESENCE_ACTIVE = LOW;
constexpr uint8_t METAL_ACTIVE = LOW;
constexpr bool WET_IS_LOW = true;
constexpr uint8_t GATE_CLOSED = 20;
constexpr uint8_t GATE_OPEN = 80;
Servo diverter;
Servo gate;
waste::Cycle cycle;
waste::GateWatchdog gateWatchdog;
uint32_t lastLog = 0;
int lastAngle = -1, lastGate = -1;
const char* stageName(waste::Stage s) {
  switch (s) {
    case waste::STOPPED: return "STOPPED";
    case waste::IDLE: return "IDLE";
    case waste::SETTLE: return "SETTLE";
    case waste::SAMPLE: return "SAMPLE";
    case waste::ALIGN: return "ALIGN";
    case waste::RELEASE: return "RELEASE";
    case waste::CLOSE: return "CLOSE";
    case waste::HOME: return "HOME";
    case waste::WAIT_CLEAR: return "WAIT_CLEAR";
    default: return "FAULT";
  }
}
const char* categoryName(waste::Category c) {
  return c == waste::METAL ? "METAL" : c == waste::WET ? "WET" :
         c == waste::DRY ? "DRY" : "UNKNOWN";
}
void setup() {
  pinMode(PRESENCE_PIN, INPUT_PULLUP);
  pinMode(METAL_PIN, INPUT_PULLUP);
  pinMode(STOP_PIN, INPUT_PULLUP);
  if (USE_GATE_FEEDBACK) {
    pinMode(GATE_CLOSED_PIN, INPUT_PULLUP);
    pinMode(GATE_OPEN_PIN, INPUT_PULLUP);
  }
  Serial.begin(115200);
  // Set intended initial positions before enabling servo pulses.
  diverter.write(90); gate.write(GATE_CLOSED);
  diverter.attach(DIVERTER_PIN); gate.attach(GATE_PIN);
  Serial.println(F("UNVERIFIED reference. Power servos separately; calibrate unloaded."));
  Serial.println(F("A=arm X=stop R=clear fault; A/R require chamber clear and stop closed"));
  Serial.println(F("ms,present,metal,moisture_raw,moisture_normalized,stage,category,routed_commands,fault_reason,sample_count,mean_moisture,metal_votes,gate_feedback_enabled,gate_closed,gate_open,mean_moisture_raw"));
}
void loop() {
  const uint32_t now = millis();
  const bool present = digitalRead(PRESENCE_PIN) == PRESENCE_ACTIVE;
  const bool metal = digitalRead(METAL_PIN) == METAL_ACTIVE;
  const bool stopClosed = digitalRead(STOP_PIN) == LOW;
  const bool gateClosed = USE_GATE_FEEDBACK && digitalRead(GATE_CLOSED_PIN) == LOW;
  const bool gateOpen = USE_GATE_FEEDBACK && digitalRead(GATE_OPEN_PIN) == LOW;
  const uint16_t raw = analogRead(MOISTURE_PIN);
  const uint16_t normalized = WET_IS_LOW ? raw : 1023 - raw;
  for (uint8_t count = 0; count < 16 && Serial.available(); ++count) {
    const char command=char(Serial.read());
    if (command=='R' && USE_GATE_FEEDBACK && cycle.stage()==waste::FAULT) {
      if (present || !stopClosed || !gateWatchdog.reset(gateClosed,gateOpen)) continue;
    }
    cycle.command(command, now, present, stopClosed);
  }
  cycle.tick(now, present, metal, normalized, stopClosed);
  if (USE_GATE_FEEDBACK && !gateWatchdog.update(now,cycle.gateOpen(),gateClosed,gateOpen))
    cycle.trip(waste::GATE_FEEDBACK,now);
  const int angle = cycle.angle();
  const int gateAngle = cycle.gateOpen() ? GATE_OPEN : GATE_CLOSED;
  if (angle != lastAngle) { diverter.write(angle); lastAngle = angle; }
  if (gateAngle != lastGate) { gate.write(gateAngle); lastGate = gateAngle; }
  if (waste::elapsed(now, lastLog, 250)) {
    lastLog = now;
    Serial.print(now); Serial.print(','); Serial.print(present);
    Serial.print(','); Serial.print(metal); Serial.print(','); Serial.print(raw);
    Serial.print(','); Serial.print(normalized); Serial.print(','); Serial.print(stageName(cycle.stage()));
    Serial.print(','); Serial.print(categoryName(cycle.category()));
    Serial.print(','); Serial.print(cycle.routedCommands());
    Serial.print(','); Serial.print(uint8_t(cycle.faultReason()));
    Serial.print(','); Serial.print(cycle.sampleCount());
    Serial.print(','); Serial.print(cycle.meanMoisture());
    Serial.print(','); Serial.print(cycle.metalVotes());
    Serial.print(','); Serial.print(USE_GATE_FEEDBACK?1:0);
    Serial.print(','); Serial.print(gateClosed?1:0);
    Serial.print(','); Serial.print(gateOpen?1:0);
    Serial.print(','); Serial.println(cycle.rawMean(WET_IS_LOW));
  }
}
