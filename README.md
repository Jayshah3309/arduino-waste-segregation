# Arduino Waste Segregation Prototype

A single-item Arduino UNO R3 reference sorter using presence, metal and moisture inputs to command a three-position diverter and a separate release gate.

**Status:** newly authored reference code and proposed two-servo mechanics. The original repository was empty. Actual sensors, original DC motor/servo arrangement, wiring, calibration and sorting performance are not verified. This package is not recovered original firmware.

![Proposed reference connections](hardware/circuit.svg)

## Problem and approach

Mixed waste can complicate manual sorting. This prototype explores simple sensor-based routing into metal, wet and dry demonstration compartments. It checks an item in a stationary chamber, samples sensor readings, aligns the diverter, releases the item, then waits for the chamber to clear before accepting another.

This is threshold-based sensing, not AI material recognition. Moisture does not establish biodegradability, recyclability or a complete waste category. Mixed/composite items can be misclassified.

## Reference behaviour

| Sample result | Action |
|---|---|
| Metal active in at least 7 of 10 samples | METAL route, priority over moisture |
| Metal active in 4–6 samples | FAULT / UNKNOWN; gate stays closed |
| Metal active in 0–3 samples; normalized moisture <=350 | WET route |
| Metal active in 0–3 samples; normalized moisture >=650 | DRY route |
| Moisture between 351 and 649 | FAULT / UNKNOWN; gate stays closed |
| Object lost during settling/sampling | FAULT |
| Chamber remains occupied for 5 s in WAIT_CLEAR | FAULT |

Thresholds and angles are examples requiring calibration. Readings 0–3 metal votes are treated as non-metal by policy, not proven absence of metal. Presence/metal/moisture sensor faults/disconnects are not reliably diagnosed. Optional gate endpoint feedback is a separate, disabled-by-default check. Dry/wet routing occurs only after ten samples at 20 ms spacing, following presence qualification and settling.

## Control and sequence

- Starts STOPPED with diverter commanded to 90° and gate closed.
- Serial `A` arms only with chamber clear and stop-input loop closed.
- Home 700 ms → clear for 300 ms → IDLE.
- Presence 150 ms → settle 200 ms → sample → align 700 ms → release 600 ms → close 400 ms → home → wait clear.
- Serial `X` stops the cycle and commands the gate closed; diverter position is retained. An existing FAULT stays latched until qualified `R`.
- Opening the stop-input loop faults an active cycle. This is software monitoring, not a power cutoff.
- Serial `R` clears a fault only when the chamber is clear and stop loop closed; `A` is then required.

Servo commands and timing do not confirm movement or successful disposal. `routed_commands` counts completed command sequences, not measured correctly sorted objects. Counters are RAM-only and reset at boot.

## Proposed components

| Component | Quantity | Requirement |
|---|---:|---|
| Arduino UNO R3 | 1 | 5 V logic |
| IR presence module | 1 | Verified 5 V-compatible digital output |
| Metal sensor + interface | 1 | Verified active-low 0–5 V interface to UNO |
| Moisture/contact module | 1 | Analog output within 0–5 V |
| Positional servo | 2 | Diverter and gate; torque/current matched to mechanism |
| Regulated servo supply | 1 | Voltage and peak current matched to both servos |
| Normally closed stop switch | 1 | D4 to GND loop |
| Chute, holding chamber and three compartments | 1 set | Single-item bench mechanics, not supplied as CAD |
| USB cable, wires and suitable mounting | As needed | Common ground |

A DC motor/conveyor and its driver are not part of this reference. Industrial proximity sensors often require higher supplies and interface conversion; never assume their outputs can connect directly. See [wiring](hardware/wiring.md).

## Trial analysis and optional gate feedback

This version adds a reproducible validation workflow, **not measured physical performance**:

- `trials.py` validates trial CSV, compiles the actual C++ classifier, and generates JSON/HTML confusion matrices.
- Sensor classification, recorded firmware commands and observed destination bins are analyzed separately. UNKNOWN/reject/no-bin trials stay visible.
- Calibration proposes thresholds using only calibration rows, with a configurable ambiguous gap. It never flashes hardware or edits source automatically.
- The committed example dataset is explicitly synthetic; the physical worksheet remains blank. Generated reports and real trial files belong in ignored `reports/` and `data/`.
- Optional D5/D6 gate endpoint feedback detects contradictory contacts or a missing endpoint after 350 ms. It is **disabled by default** and unverified physically.
- Fault causes and ten-sample statistics are included in Serial logs. `X` closes the gate but now preserves an existing FAULT; `R` is still required before re-arming.

Python 3.10+ and g++ are required for the host tools; no Python packages are needed:

```sh
python trials.py analyze --input examples/synthetic-trials.csv --out reports/synthetic
python trials.py calibrate --input examples/synthetic-trials.csv --out reports/calibration
python -m unittest discover -s tests -p 'test_*.py' -v
```

Open `reports/synthetic/report.html`. These example results demonstrate software behavior only. See [trial protocol and metric definitions](docs/trials.md), [optional feedback wiring](hardware/wiring.md), and [verification evidence](docs/testing.md).

## Files

- [Firmware](firmware/waste_sorter/waste_sorter.ino) and tested cycle logic
- [Wiring and power](hardware/wiring.md)
- [Setup and calibration](docs/setup.md)
- [Architecture and state flow](docs/architecture.md)
- [Tests and evidence status](docs/testing.md)
- [Blank measurement worksheet](docs/measurements.csv)
- [Limits and roadmap](docs/roadmap.md)
- [Contributors](docs/contributors.md) and [media instructions](media/README.md)

## Build

Arduino IDE: install Arduino AVR Boards 1.8.6 and Servo 1.2.2, select Arduino Uno, open the sketch with `control.h` beside it, choose the actual port and upload. Serial Monitor: 115200 baud. Start with servo horns unloaded and motor power isolated during wiring.

```sh
arduino-cli core update-index
arduino-cli core install arduino:avr@1.8.6
arduino-cli lib install "Servo@1.2.2"
arduino-cli compile --fqbn arduino:avr:uno firmware/waste_sorter
```

Host tests:

```sh
g++ -std=c++11 -Wall -Wextra -Werror tests/control_test.cpp -o /tmp/waste-sorter-test
/tmp/waste-sorter-test
```

GitHub Actions runs host control tests, Python trial-tool tests and report generation, and builds UNO configurations with feedback disabled and enabled. Tests do not validate actual sensor discrimination, servo torque, physical jams or sorting.

## Evidence and limits

No measured accuracy, throughput, power, durability or bin capacity is claimed. The prototype cannot reliably identify hazardous, composite or contaminated waste. An offline HTML trial report is provided. No network dashboard, bin-full sensing, autonomous jam recovery or persistent on-device statistics is implemented. Gate endpoint fault detection is optional source code, not a verified jam sensor. See [roadmap](docs/roadmap.md).

## Credits, license and references

Owner: [Jayshah3309](https://github.com/Jayshah3309). Reference package prepared with AI assistance; actual project contributions and evidence remain to be confirmed. License selection is pending; [LICENSE](LICENSE) is a status notice.

- [Arduino Servo basics and external supply](https://docs.arduino.cc/learn/electronics/servo-motors/)
- [Arduino Servo library](https://github.com/arduino-libraries/Servo)
- [Arduino analog input](https://docs.arduino.cc/built-in-examples/analog/AnalogInput/)
- [OMRON proximity sensing principles](https://www.ia.omron.com/support/guide/41/further_information.html)
