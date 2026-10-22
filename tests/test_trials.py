import copy
import csv
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import trials


class TrialTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=trials.ROOT/'examples/synthetic-trials.csv'
        cls.rows,cls.digest=trials.read_trials(cls.fixture)

    def raw_rows(self):
        with self.fixture.open(newline='') as f:return list(csv.DictReader(f))

    def write(self,path,rows):
        with path.open('w',newline='') as f:
            w=csv.DictWriter(f,fieldnames=trials.FIELDS);w.writeheader();w.writerows(rows)

    def test_synthetic_fixture_metrics(self):
        rows=[r for r in self.rows if r['split']=='evaluation']
        predictions,config=trials.native_predictions(rows)
        self.assertEqual(config,dict(wet_max=350,dry_min=650))
        m=trials.summarize(rows,predictions)
        self.assertAlmostEqual(m['classifier_accuracy'],4/6)
        self.assertAlmostEqual(m['unknown_rate'],1/6)
        self.assertAlmostEqual(m['accuracy_among_classified'],4/5)
        self.assertAlmostEqual(m['strict_sort_success'],3/6)
        self.assertEqual(m['jams'],1)
        self.assertEqual(m['recorded_command_mismatches'],0)
        self.assertEqual(m['actual_bin_confusion']['WET']['REJECT'],1)

    def test_native_priority_and_ambiguous_boundaries(self):
        rows=[dict(self.rows[0],moisture_normalized=m,metal_votes=v) for m,v in [(350,0),(351,0),(649,0),(650,0),(100,7),(800,6)]]
        values,_=trials.native_predictions(rows)
        self.assertEqual(values,['WET','UNKNOWN','UNKNOWN','DRY','METAL','UNKNOWN'])

    def test_calibration_ignores_evaluation_data(self):
        original=trials.suggest_thresholds(self.rows,50)
        changed=copy.deepcopy(self.rows)
        for row in changed:
            if row['split']=='evaluation':row['moisture_normalized']=0;row['true_class']='DRY'
        self.assertEqual(original,trials.suggest_thresholds(changed,50))
        self.assertEqual(original['evaluation_trials_used'],0)
        self.assertFalse(original['applied_to_firmware'])
        self.assertEqual((original['wet_max'],original['dry_min']),(200,800))

    def test_calibrated_thresholds_require_separate_evaluation(self):
        selected=[r for r in self.rows if r['split']=='evaluation']
        default,_=trials.native_predictions(selected)
        proposed,_=trials.native_predictions(selected,200,800)
        self.assertLess(trials.summarize(selected,proposed)['classifier_accuracy'],trials.summarize(selected,default)['classifier_accuracy'])
        with self.assertRaises(ValueError):trials.native_predictions(selected,500,None)
        with self.assertRaises(ValueError):trials.native_predictions(selected,800,700)

    def test_split_leakage_and_mixed_evidence_rejected(self):
        for key,value in [('sample_id','CAL-W1'),('evidence_type','physical'),('firmware_commit','another-revision'),('normalization','wet_high')]:
            rows=self.raw_rows();rows[-1][key]=value
            with tempfile.TemporaryDirectory() as tmp:
                path=Path(tmp)/'bad.csv';self.write(path,rows)
                with self.subTest(key=key),self.assertRaises(ValueError):trials.read_trials(path)

    def test_invalid_input_fields(self):
        for key,value in [('trial_id','C1'),('date','20261022'),('date','2026-99-22'),('metal_votes','11'),('jam','true'),
                          ('moisture_raw','-1'),('moisture_normalized','999'),('cycle_ms','NaN'),('true_class','HAZARDOUS'),('operator','')]:
            rows=self.raw_rows();rows[-1][key]=value
            with tempfile.TemporaryDirectory() as tmp:
                path=Path(tmp)/'bad.csv';self.write(path,rows)
                with self.subTest(key=key,value=value),self.assertRaises(ValueError):trials.read_trials(path)

    def test_blank_worksheet_and_wrong_header(self):
        with self.assertRaises(ValueError):trials.read_trials(trials.ROOT/'docs/measurements.csv')
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'bad.csv';path.write_text('sample,value\nA,100\n')
            with self.assertRaises(ValueError):trials.read_trials(path)

    def test_all_unknown_and_missing_support(self):
        rows=[dict(self.rows[0],true_class='WET',actual_bin='REJECT',jam=0)]
        m=trials.summarize(rows,['UNKNOWN'])
        self.assertEqual(m['classifier_accuracy'],0)
        self.assertEqual(m['unknown_rate'],1)
        self.assertIsNone(m['accuracy_among_classified'])
        self.assertIsNone(m['class_metrics']['DRY']['recall'])
        self.assertEqual(m['strict_sort_success'],0)

    def test_wet_high_window_rounding(self):
        rows=self.raw_rows()
        for row in rows:
            row['normalization']='wet_high'
            row['moisture_raw']=str(1023-int(row['moisture_normalized']))
        rows[-1]['moisture_raw']='222' # floor(raw_mean)=222, floor(normalized_mean)=800
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'high.csv';self.write(path,rows)
            parsed,_=trials.read_trials(path)
            self.assertEqual(parsed[-1]['moisture_normalized'],800)

    def test_jam_is_not_strict_success_even_in_correct_bin(self):
        row=dict(self.rows[0],true_class='WET',actual_bin='WET',jam=1)
        self.assertEqual(trials.summarize([row],['WET'])['strict_sort_success'],0)

    def test_calibration_needs_unique_samples(self):
        rows=[r for r in self.rows if r['split']=='calibration']
        rows=copy.deepcopy(rows)
        for row in rows:
            if row['true_class']=='WET':row['sample_id']='ONE-WET-SAMPLE'
        with self.assertRaises(ValueError):trials.suggest_thresholds(rows,50)
        with self.assertRaises(ValueError):trials.suggest_thresholds(self.rows,0)

    def test_actual_cli_outputs_and_provenance_labels(self):
        with tempfile.TemporaryDirectory() as tmp:
            out=Path(tmp)
            for action in ['analyze','calibrate']:
                run=subprocess.run([sys.executable,str(trials.ROOT/'trials.py'),action,'--input',str(self.fixture),'--out',str(out)],capture_output=True,text=True,timeout=20)
                self.assertEqual(run.returncode,0,run.stderr)
            report=json.loads((out/'report.json').read_text())
            calibration=json.loads((out/'calibration.json').read_text())
            self.assertEqual(report['evidence_type'],'synthetic')
            self.assertEqual(report['csv_sha256'],self.digest)
            self.assertFalse(calibration['proposal']['applied_to_firmware'])
            self.assertIn('SYNTHETIC EVIDENCE',(out/'report.html').read_text())
            self.assertIn('No real sorting performance',(out/'report.html').read_text())

    def test_missing_compiler_or_split_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(trials.main(['analyze','--input',str(self.fixture),'--out',tmp,'--cxx',str(Path(tmp)/'missing-cxx')]),2)


if __name__=='__main__':unittest.main()
