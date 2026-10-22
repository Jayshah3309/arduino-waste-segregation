#include "../firmware/waste_sorter/control.h"
#include <assert.h>
#include <iostream>
using namespace waste;
void ready(Cycle& c, uint32_t start = 0) {
  c.command('A', start, false, true);
  c.tick(start + 700, false, false, 800, true);
  c.tick(start + 701, false, false, 800, true);
  c.tick(start + 1001, false, false, 800, true);
  assert(c.stage() == IDLE);
}
void sample(Cycle& c, bool metal, uint16_t adc) {
  c.tick(1100, true, metal, adc, true);
  c.tick(1250, true, metal, adc, true);
  assert(c.stage() == SETTLE);
  c.tick(1450, true, metal, adc, true);
  assert(c.stage() == SAMPLE);
  for (uint32_t n = 1; n <= 10; ++n) c.tick(1450 + 20*n, true, metal, adc, true);
}
int main() {
  assert(classify(350, 0) == WET);
  assert(classify(351, 0) == UNKNOWN);
  assert(classify(649, 0) == UNKNOWN);
  assert(classify(650, 0) == DRY);
  assert(classify(100, 7) == METAL);
  assert(classify(800, 6) == UNKNOWN);
  assert(classify(800, 3) == DRY);
  assert(classify(1024, 10) == UNKNOWN);
  assert(classify(100, 11) == UNKNOWN);
  assert(classify(400, 0, 450, 700) == WET);
  assert(classify(800, 0, 700, 700) == UNKNOWN);
  Cycle c;
  assert(c.stage() == STOPPED && !c.gateOpen());
  c.command('A', 0, true, true); assert(c.stage() == STOPPED);
  c.command('A', 0, false, false); assert(c.stage() == STOPPED);
  ready(c); sample(c, false, 800);
  assert(c.stage() == ALIGN && c.category() == DRY && c.angle() == 150);
  c.tick(2349, true, false, 800, true); assert(!c.gateOpen());
  c.tick(2350, true, false, 800, true); assert(c.gateOpen());
  c.tick(2950, false, false, 800, true); assert(c.stage() == CLOSE && !c.gateOpen());
  c.tick(3350, false, false, 800, true);
  assert(c.stage() == HOME && c.angle() == 90 && c.routedCommands() == 1);
  c.tick(4050, true, false, 800, true); assert(c.stage() == WAIT_CLEAR);
  c.tick(9049, true, false, 800, true); assert(c.stage() == WAIT_CLEAR);
  c.tick(9050, true, false, 800, true); assert(c.stage() == FAULT);
  assert(c.faultReason() == CLEAR_TIMEOUT);
  c.command('R', 9060, true, true); assert(c.stage() == FAULT);
  c.command('R', 9070, false, true); assert(c.stage() == STOPPED);
  Cycle unknown; ready(unknown); sample(unknown, false, 500);
  assert(unknown.stage() == FAULT && !unknown.gateOpen() && unknown.routedCommands() == 0);
  assert(unknown.faultReason() == AMBIGUOUS_SAMPLE && unknown.sampleCount() == 10);
  assert(unknown.meanMoisture() == 500 && unknown.metalVotes() == 0);
  assert(unknown.rawMean(true) == 500 && unknown.rawMean(false) == 523);
  Cycle lost; ready(lost);
  lost.tick(1100, true, false, 800, true); lost.tick(1250, true, false, 800, true);
  lost.tick(1251, false, false, 800, true); assert(lost.stage() == FAULT);
  assert(lost.faultReason() == LOST_ITEM);
  Cycle stopped; ready(stopped); sample(stopped, true, 100);
  assert(stopped.category() == METAL);
  stopped.tick(1700, true, true, 100, false); assert(stopped.stage() == FAULT && !stopped.gateOpen());
  assert(stopped.faultReason() == STOP_LOOP);
  stopped.command('X', 1800, false, true); assert(stopped.stage() == FAULT);
  stopped.command('A', 1801, false, true); assert(stopped.stage() == FAULT);
  stopped.command('R', 1802, false, true); assert(stopped.stage() == STOPPED && stopped.faultReason() == NO_FAULT);
  GateWatchdog gate;
  assert(gate.update(0,false,true,false));
  assert(gate.update(100,true,false,false));
  assert(gate.update(449,true,false,false));
  assert(!gate.update(450,true,false,false) && gate.faulted());
  assert(!gate.reset(false,true));
  assert(gate.reset(true,false));
  assert(!gate.update(500,false,true,true));
  GateWatchdog arrived;
  assert(arrived.update(UINT32_MAX-100,true,false,false));
  assert(arrived.update(248,true,false,true)); // 349 ms across rollover
  assert(arrived.update(249,true,false,true)); // Endpoint reached by deadline
  assert(!arrived.update(250,true,false,false)); // Feedback lost after deadline
  GateWatchdog closing;
  assert(closing.update(0,false,false,true));
  assert(closing.update(349,false,false,false));
  assert(!closing.update(350,false,false,false));
  Cycle rollover; ready(rollover, UINT32_MAX - 500);
  assert(elapsed(10, UINT32_MAX - 99, 100));
  std::cout << "Waste sorter classification and cycle tests passed\n";
}
