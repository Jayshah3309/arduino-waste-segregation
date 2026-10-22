# Validation and evidence status

New reference implementation. Exact sensor models, interface circuits, mechanics, actuator limits and actual sorting results are unverified. Host tests exercise classification thresholds, metal priority, ambiguous cases, startup/arming, complete cycle timing, fault/reset, repeated-item blocking and clock rollover. GitHub Actions compiles for UNO with Servo 1.2.2. Both jobs passed in [verified run #1](https://github.com/Jayshah3309/arduino-waste-segregation/actions/runs/39409247708), commit `e6c2e3c`, on 20 October 2026. The Arduino setup action emitted a runtime deprecation warning; it did not fail the build.

## Bench acceptance checklist

Record commit, AVR/Servo versions, sensor models/polarities, normalization, final thresholds, actuator ratings/angles, power supply, sample identity and trial count.

| ID | Procedure | Expected | Observed |
|---|---|---|---|
| W1 | Boot / arm with occupied chamber | STOPPED, arming refused | Not run |
| W2 | Arm clear, wait home/clear | IDLE with gate closed | Not run |
| W3 | Consistent metal target | METAL route despite wet moisture value | Not run |
| W4 | Calibrated damp/dry non-metal samples | WET/DRY as appropriate | Not run |
| W5 | Mid-band moisture or mixed metal votes | FAULT, no gate-open command | Not run |
| W6 | Remove sample during settling/sampling | FAULT | Not run |
| W7 | Complete one cycle | Align before release, close before home | Not run |
| W8 | Keep chamber occupied after cycle | No second cycle; timeout fault | Not run |
| W9 | Stop command / open stop input | Gate-close command, no automatic fault resume | Not run |
| W10 | Clear chamber, R, then A | Controlled restart through HOME | Not run |
| W11 | Loaded actuator and power tests | No rail sag, reset or exceeded ratings | Not run |
| W12 | Repeated representative samples | Record actual compartment, errors and jams | Not run |

Use `measurements.csv` for physical trials, including true material class, moisture/contact conditions, normalized readings, metal result, chosen command and actual compartment. Report a confusion matrix with UNKNOWN/reject cases and a stated sample count. Do not quote percentage accuracy without actual trials. A commanded angle/cycle counter does not verify correct disposal.

Measure throughput only with a functioning one-item retention mechanism and documented insertion protocol. Test sensors independently from mechanics before combined trials. Simulated or manually injected signals cannot establish sensor discrimination or real jam handling. No physical demonstration, sorting accuracy, capacity or production deployment is claimed.


## Validation tooling upgrade — 22 October 2026

Local C++ classification/cycle tests passed with UndefinedBehaviorSanitizer, including fault causes, X fault-latch preservation, optional gate timeout, contradictory contacts, reset conditions and timer rollover. All **13 Python trial-tool tests passed**. They cover native C++ classification, metric denominators, UNKNOWN/reject/jam behavior, source evidence separation, sample split leakage, malformed inputs, calibration isolation, threshold overrides and real CLI report generation.

The example CSV contains **11 synthetic rows** (5 calibration, 6 evaluation). Expected fixture-only evaluation results are 4/6 classifier matches, one UNKNOWN and 3/6 correctly observed bins without a jam. These numbers are test assertions for fabricated data, not measured sorter accuracy. Calibration proposes 200/800 from the synthetic calibration rows but performs worse on the separate synthetic evaluation rows; it is not applied to source or hardware. This demonstrates why an apparent perfect calibration fit is not deployment evidence.

CI now runs Python 3.10/3.12, native controller tests, synthetic report artifacts and UNO compilation with feedback both disabled and enabled. Actual upgraded CI run evidence will be recorded after inspection. Physical calibration, endpoint wiring, real jam trials, photos/video, power and sorting measurements remain pending.
