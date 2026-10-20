# Setup and calibration

## Upload

Install Arduino AVR Boards 1.8.6 and Servo library 1.2.2. Select Arduino Uno and the actual serial port. Open `firmware/waste_sorter/waste_sorter.ino` with `control.h` beside it. Use Serial Monitor at 115200 baud. Record firmware commit, actual parts, supply and tool versions with results.

## Bring up sensors first

Disconnect actuator supply. Check sensor voltage/output compatibility. Read serial columns:

`ms,present,metal,moisture_raw,moisture_normalized,stage,category,routed_commands`

Present/metal are logical detections (1 means detected), not raw pin levels. Test D2 and D3 using representative clean samples at fixed placement. Confirm active polarity. Do not assume a steel target verifies detection of aluminium, foil or every metal object.

For moisture, record distributions for chosen dry and damp demonstration samples with repeatable contact pressure, placement and settling. WET_IS_LOW=true means normalized value equals raw. If your sensor reads higher when wet, set false; normalized then equals 1023−raw. Thresholds <=350 wet and >=650 dry in `classify()` are examples. Replace using observed distributions, keeping a meaningful ambiguous band. Wetness is not a universal proxy for waste type.

Metal is sampled ten times: >=7 detections gives metal priority, 4–6 gives UNKNOWN, <=3 uses moisture. Validate this policy and document false positives/negatives. Unknown classification holds the item behind a commanded-closed gate; manually remove it with actuator power isolated before resetting.

## Calibrate actuator travel

1. With horns/linkages unloaded, confirm servo supply, common GND and current budget.
2. Verify commanded positions individually before mounting mechanics. Set safe travel limits using actual servo/linkage specifications.
3. Align each selector angle to its compartment and verify the gate holds a sample while the selector moves.
4. Validate 700 ms alignment/home, 600 ms release and 400 ms closure against actual motion. Increase/rework only based on observation.
5. Do not feed items until one-item retention/release is demonstrated. No mechanism CAD or load rating is supplied.

## Operate a controlled cycle

With chamber empty and D4 loop closed, send A. Wait through HOME and clear qualification to IDLE. Insert exactly one small, clean, nonhazardous mock sample. Keep it stationary during sensing. Observe the selected category, selector movement and gate release. Do not add another item until IDLE.

Send X to stop. The gate is commanded closed and selector position retained; actuators are not electrically isolated. Opening D4 faults an active cycle. For FAULT, isolate actuator power, remove the sample/obstruction, restore safe setup, then send R with chamber clear and stop loop closed. This returns STOPPED; A is required to restart. Closing the loop alone does not resume a faulted cycle.

Mixed/composite samples, liquids, batteries, sharps or hazardous waste are outside the reference demonstration. Counters indicate commanded cycles only and reset at boot.

## Troubleshooting

| Symptom | Check |
|---|---|
| Won't arm | Chamber must be clear, stop loop closed, stage STOPPED |
| Always metal | Interface polarity, stray metal, sensing range and leakage |
| Always wet/dry | Normalize polarity, sample contact, threshold calibration |
| UNKNOWN / FAULT | Ambiguous band, unstable metal votes, sample removed early or chamber stuck |
| Servo jitter / UNO reset | Separate supply, peak current, grounding and decoupling |
| Wrong compartment | Angle mapping and actual chute orientation |
| Repeats on same item | Check presence sensor and one-item mechanism; IDLE requires clear qualification |

Servo uses Timer1 on UNO, affecting analogWrite PWM on pins 9/10. Those pins here are servo signals, not DC-motor PWM outputs. Review timer use before adding motors or other timing libraries.
