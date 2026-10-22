# Sorting trial and calibration protocol

This workflow measures recorded outcomes separately from software decisions. It does not create missing measurements, validate sensor hardware, or infer the physical bin from a servo command.

## Start with synthetic data

Python 3.10+ and a C++11 g++ compiler are required. No Python packages are needed.

```sh
python trials.py analyze --input examples/synthetic-trials.csv --out reports/synthetic
python trials.py calibrate --input examples/synthetic-trials.csv --out reports/calibration
```

Open `reports/synthetic/report.html`. JSON output contains additional class precision/recall and cycle timing summaries. The calibration command creates `calibration.json`, not flashed firmware. Reports explicitly label synthetic evidence. Generated files are overwritten within the selected output directory, so use separate `--out` paths to preserve experiments.

`examples/synthetic-trials.csv` is deliberately imperfect: it contains a reject, missed metal and a fabricated misplaced/jammed result. It exists to exercise the report code, not to claim hardware accuracy. The committed physical worksheet `docs/measurements.csv` has a header only and refuses analysis until real rows are filled.

## Plan physical trials before collecting results

1. Confirm sensor models, interface voltages, stop behavior, mechanics, servo power and travel limits. Use only small clean nonhazardous mock samples. No liquids, batteries, sharps or hazardous waste.
2. Define the operational categories in advance. This demo's labels are METAL, WET and DRY under its stated sensing policy; moisture does not establish recyclability, organic content or safe disposal.
3. Assign stable sample IDs. Put distinct physical objects into calibration and evaluation sets before tuning. The tool rejects the same sample ID across splits within one CSV. Across separate files this must be enforced by the operator. Repeating one object is useful for repeatability but is not independent evidence.
4. Record the actual firmware commit, WET_IS_LOW polarity, threshold settings, sensor mounting/contact conditions, supply, angles, tool versions and environmental conditions in a separate experiment note. Use one firmware revision and one normalization policy per CSV.
5. Copy the blank worksheet to ignored `data/physical-trials.csv`. Keep actual logs/photos and personal information out of public synthetic fixtures.
6. For each single-item attempt, record ground truth before seeing the outcome. Capture the final ten-sample classification window, recorded command, actual destination, timing and any jam. Do not drop failed, rejected or no-bin attempts. No manual redirection should be counted as an autonomous successful sort; record intervention in notes and the relevant reject/no-bin/jam outcome.
7. Tune only on calibration data, then freeze firmware/configuration and run the held-out evaluation. New firmware revision/configuration requires a separate attributable file. Report actual sample counts, repeated-object counts, rejects and failures.
8. Record a real demonstration only after the mechanism works. Show an entire cycle, failed/rejected cases and stop/reset behavior. No physical video/photo has been produced by this software workflow.

## CSV schema

Header names and order must match the worksheet exactly. Each text field is one line, at most 500 characters; maximum 5000 trials / 5 MiB per file. Trial IDs are unique. A file cannot mix physical and synthetic rows.

| Field | Meaning |
|---|---|
| date | YYYY-MM-DD trial date |
| trial_id | Unique row ID, 1–80 letters/digits/underscore/dot/hyphen |
| evidence_type | `physical` or `synthetic`; operator declaration, not an authenticity proof |
| split | `calibration` or `evaluation` |
| firmware_commit | Actual source revision for physical trials; `synthetic-fixture` only for fabricated examples |
| operator | Nonempty operator identifier; use a nonpersonal code in shared data |
| sample_id | Stable physical-object ID; never reuse across splits |
| true_class | METAL / WET / DRY, assigned using the experiment's predefined category policy |
| material | Descriptive mock material |
| contact_condition | Controlled placement/contact/environment description |
| normalization | `wet_low` (raw unchanged) or `wet_high` (1023−raw per sample) |
| moisture_raw | Integer mean of the same ten raw classification samples, 0..1023 |
| moisture_normalized | Integer mean of those normalized samples, 0..1023 |
| metal_votes | Active detections in that ten-sample window, 0..10 |
| commanded_class | Recorded category METAL / WET / DRY / UNKNOWN, not an inferred observed bin |
| actual_bin | Independently observed METAL / WET / DRY / REJECT / NONE |
| cycle_ms | Observed defined cycle duration, integer 1..3600000, or blank if not completed/measured |
| jam | 1 if an observed jam occurred, otherwise 0; independent of whether gate feedback was enabled |
| notes | Failures, intervention and other observations; do not silently omit these |

Use `sample_count=10`, `mean_moisture`, `mean_moisture_raw` and `metal_votes` from the completed classification-window telemetry. Earlier raw/normalized columns are instantaneous values, not window means. Wet-high integer mean rounding can produce normalized mean `1023−raw_mean` or one lower; the validator accepts exactly this one-count rounding case. At other polarities, inconsistent values are rejected.

REJECT means the item was deliberately retained/rejected under the protocol; NONE means no completed destination was observed. An UNKNOWN command by itself does not prove physical retention. Record the observed outcome and any manual removal.

## Analysis definitions

The tool compiles `host/classifier.cpp`, which includes the firmware's actual `control.h`, then evaluates every selected row. It does not maintain a second Python classification implementation. Default thresholds come from the compiled header; `--wet-max` and `--dry-min` together evaluate a hypothetical override and are marked in the report. Recorded firmware revision is operator-provided, while controller SHA-256 identifies the source being recomputed now; verify they correspond before interpreting disagreement.

```sh
python trials.py analyze --input data/physical-trials.csv --split evaluation --out reports/physical-evaluation
```

| Output | Definition |
|---|---|
| Classifier confusion | Rows true category; columns compiled METAL/WET/DRY/UNKNOWN |
| Recorded-command confusion | Same ground truth versus actual logged category |
| Actual-bin confusion | Ground truth versus observed METAL/WET/DRY/REJECT/NONE |
| Classifier accuracy | Correct compiled predictions / all selected trials, including UNKNOWN failures |
| UNKNOWN rate | UNKNOWN predictions / all selected trials |
| Classified coverage | Non-UNKNOWN predictions / all selected trials |
| Accuracy among classified | Correct / non-UNKNOWN predictions; conditional metric, never replace overall accuracy with it |
| Class precision | True positives / predictions for a class; null if none predicted |
| Class recall | True positives / support of that class; null when support is absent |
| Strict observed sorting success | Correct observed bin AND no observed jam / all selected trials; rejects/no-bin remain in denominator |
| Command/model mismatch | Logged command differs from the recomputed category; investigate configuration/window/revision mismatch |
| Timing summaries | Mean, median and nearest-rank p95 of nonblank cycle_ms, with count; no unrecorded handling/feed time |

Confusion matrices and placement success do not prove automatic disposal, independent observations or category validity. Repeated objects violate simple independent-trial assumptions; the report includes unique sample count but does not compute confidence intervals. Timing summaries are not line throughput, duty cycle, power, capacity or durability measurements.

## Calibration suggestions

```sh
python trials.py calibrate --input data/physical-trials.csv --min-gap 50 --out reports/calibration
```

At least two distinct WET and two distinct DRY calibration sample IDs are required. Only `split=calibration` rows influence threshold selection. The search maximizes the mean of WET/DRY recall over the 0..1023 integer domain, accounting for metal votes that prevent a wet/dry decision. Ties prefer a wider ambiguous band, then lower wet threshold. Trials are weighted equally within each class, so repeated samples can bias the proposal. Metal vote thresholds are not tuned.

The result is a proposal with `applied_to_firmware=false` and `evaluation_trials_used=0`. It never edits `WET_MAX`, `DRY_MIN`, angles or timing. Review it physically, change source deliberately, record a new commit and validate held-out samples before adopting. A perfect calibration fit can still reject or misclassify evaluation samples; the included fixture tests this failure mode.

For a hypothetical software comparison only, supply both overrides to analyze, for example `--wet-max 200 --dry-min 800` on the **synthetic** fixture. These example values are not recommendations for your sensors.

Exit 0 means the report command completed, not that hardware met an accuracy target. Invalid data, tool/compiler failure or timeout exits 2. Failed commands can leave older reports, so inspect timestamps and hashes. No score-based pass/fail release criterion or hardware certification is implied.
