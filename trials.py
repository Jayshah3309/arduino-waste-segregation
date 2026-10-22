"""Validate sorting-trial CSV, evaluate the actual compiled classifier and suggest calibration."""
import argparse
import bisect
import csv
from datetime import date, datetime, timezone
import hashlib
import html
import io
import json
import math
from pathlib import Path
import re
import statistics
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parent
CLASSES=('METAL','WET','DRY')
PREDICTED=CLASSES+('UNKNOWN',)
BINS=CLASSES+('REJECT','NONE')
FIELDS=('date','trial_id','evidence_type','split','firmware_commit','operator','sample_id','true_class','material',
        'contact_condition','normalization','moisture_raw','moisture_normalized','metal_votes','commanded_class','actual_bin','cycle_ms','jam','notes')
ID=re.compile(r'[A-Za-z0-9_.-]{1,80}\Z')


def read_trials(path):
    raw=Path(path).read_bytes()
    if len(raw)>5*1024*1024:raise ValueError('CSV exceeds 5 MiB')
    reader=csv.DictReader(io.StringIO(raw.decode('utf-8-sig'),newline=''))
    if reader.fieldnames!=list(FIELDS):raise ValueError('CSV header must match docs/measurements.csv exactly')
    rows=[];ids=set();evidence=set();samples={}
    for line,row in enumerate(reader,2):
        if len(rows)>=5000:raise ValueError('At most 5000 trial rows per report')
        if None in row or any(v is None for v in row.values()):raise ValueError(f'line {line}: malformed CSV row')
        if any(len(v)>500 or any(ord(c)<32 for c in v) for v in row.values()):raise ValueError(f'line {line}: invalid text')
        row={k:v.strip() for k,v in row.items()}
        if not re.fullmatch(r'\d{4}-\d{2}-\d{2}',row['date']):raise ValueError(f'line {line}: use YYYY-MM-DD date')
        try:date.fromisoformat(row['date'])
        except ValueError as exc:raise ValueError(f'line {line}: invalid date') from exc
        for key in ('trial_id','sample_id','firmware_commit'):
            if not ID.fullmatch(row[key]):raise ValueError(f'line {line}: invalid {key}')
        if row['trial_id'] in ids:raise ValueError('Duplicate trial_id')
        ids.add(row['trial_id'])
        if row['evidence_type'] not in ('synthetic','physical'):raise ValueError('evidence_type must be synthetic or physical')
        evidence.add(row['evidence_type'])
        if row['split'] not in ('calibration','evaluation'):raise ValueError('split must be calibration or evaluation')
        if row['sample_id'] in samples and samples[row['sample_id']]!=row['split']:raise ValueError('Sample leakage across calibration/evaluation splits')
        samples[row['sample_id']]=row['split']
        if not row['operator'] or not row['material'] or not row['contact_condition']:raise ValueError('Operator, material and contact condition are required')
        if row['true_class'] not in CLASSES or row['commanded_class'] not in PREDICTED or row['actual_bin'] not in BINS:raise ValueError('Unsupported class or bin')
        if row['normalization'] not in ('wet_low','wet_high'):raise ValueError('Invalid normalization polarity')
        for key,low,high in [('moisture_raw',0,1023),('moisture_normalized',0,1023),('metal_votes',0,10),('jam',0,1)]:
            if not re.fullmatch(r'\d+',row[key]):raise ValueError(f'Invalid integer {key}')
            row[key]=int(row[key])
            if not low<=row[key]<=high:raise ValueError(f'Out of range {key}')
        expected=row['moisture_raw'] if row['normalization']=='wet_low' else 1023-row['moisture_raw']
        allowed=(expected,) if row['normalization']=='wet_low' else (expected,expected-1)
        if row['moisture_normalized'] not in allowed:raise ValueError('Raw/normalized moisture does not match polarity/window rounding')
        if row['cycle_ms']:
            if not re.fullmatch(r'\d+',row['cycle_ms']) or not 1<=int(row['cycle_ms'])<=3_600_000:raise ValueError('cycle_ms must be blank or 1..3600000')
            row['cycle_ms']=int(row['cycle_ms'])
        else:row['cycle_ms']=None
        rows.append(row)
    if not rows:raise ValueError('No completed trial rows; fill the blank worksheet first')
    if len(evidence)!=1:raise ValueError('Do not mix physical and synthetic evidence in one file')
    # Results must be attributable to one recorded firmware revision and normalization policy.
    if len({r['firmware_commit'] for r in rows})!=1 or len({r['normalization'] for r in rows})!=1:
        raise ValueError('Use one firmware revision and normalization policy per input file')
    return rows,hashlib.sha256(raw).hexdigest()


def native_predictions(rows,wet=None,dry=None,cxx='g++'):
    if (wet is None)!=(dry is None):raise ValueError('Supply both threshold overrides or neither')
    if wet is not None and not 0<=wet<dry<=1023:raise ValueError('Thresholds require 0 <= wet_max < dry_min <= 1023')
    with tempfile.TemporaryDirectory() as tmp:
        binary=Path(tmp)/'classifier'
        subprocess.run([cxx,'-std=c++11','-Wall','-Wextra','-Werror',str(ROOT/'host/classifier.cpp'),'-o',str(binary)],check=True,capture_output=True,text=True,timeout=60)
        command=[str(binary)]+([str(wet),str(dry)] if wet is not None else [])
        payload=''.join(f"{r['moisture_normalized']} {r['metal_votes']}\n" for r in rows)
        run=subprocess.run(command,input=payload,capture_output=True,text=True,check=True,timeout=10)
        lines=run.stdout.splitlines()
    config=json.loads(lines[0]);predictions=lines[1:]
    if len(predictions)!=len(rows) or any(p not in PREDICTED for p in predictions):raise ValueError('Invalid native classifier output')
    return predictions,config


def matrix(rows,labels,values):
    result={truth:{p:0 for p in labels} for truth in CLASSES}
    for row,value in zip(rows,values):result[row['true_class']][value]+=1
    return result


def summarize(rows,predictions):
    n=len(rows);confusion=matrix(rows,PREDICTED,predictions)
    correct=sum(row['true_class']==p for row,p in zip(rows,predictions))
    unknown=predictions.count('UNKNOWN');metrics={}
    for category in CLASSES:
        support=sum(confusion[category].values());predicted=sum(confusion[t][category] for t in CLASSES);tp=confusion[category][category]
        metrics[category]=dict(support=support,precision=tp/predicted if predicted else None,recall=tp/support if support else None)
    cycles=[r['cycle_ms'] for r in rows if r['cycle_ms'] is not None]
    return dict(trials=n,unique_samples=len({r['sample_id'] for r in rows}),classifier_confusion=confusion,
        recorded_command_confusion=matrix(rows,PREDICTED,[r['commanded_class'] for r in rows]),
        actual_bin_confusion=matrix(rows,BINS,[r['actual_bin'] for r in rows]),
        classifier_accuracy=correct/n,unknown_rate=unknown/n,classified_coverage=(n-unknown)/n,
        accuracy_among_classified=correct/(n-unknown) if n!=unknown else None,class_metrics=metrics,
        recorded_command_mismatches=sum(r['commanded_class']!=p for r,p in zip(rows,predictions)),
        strict_sort_success=sum(r['actual_bin']==r['true_class'] and not r['jam'] for r in rows)/n,
        jams=sum(r['jam'] for r in rows),cycle_timing=dict(count=len(cycles),mean_ms=statistics.mean(cycles) if cycles else None,
        median_ms=statistics.median(cycles) if cycles else None,p95_ms=sorted(cycles)[math.ceil(.95*len(cycles))-1] if cycles else None))


def suggest_thresholds(rows,min_gap):
    calibration=[r for r in rows if r['split']=='calibration']
    wet=[r for r in calibration if r['true_class']=='WET'];dry=[r for r in calibration if r['true_class']=='DRY']
    if min_gap<1 or min_gap>1023:raise ValueError('min_gap must be 1..1023')
    if len({r['sample_id'] for r in wet})<2 or len({r['sample_id'] for r in dry})<2:raise ValueError('Need at least two distinct WET and two distinct DRY calibration samples')
    eligible_wet=sorted(r['moisture_normalized'] for r in wet if r['metal_votes']<=3)
    eligible_dry=sorted(r['moisture_normalized'] for r in dry if r['metal_votes']<=3)
    wet_correct=[bisect.bisect_right(eligible_wet,v) for v in range(1024)]
    dry_correct=[len(eligible_dry)-bisect.bisect_left(eligible_dry,v) for v in range(1024)]
    best=None
    for w in range(1024-min_gap):
        for d in range(w+min_gap,1024):
            # Integer ranking avoids floating-point tie artifacts. Maximize balanced WET/DRY recall.
            score=wet_correct[w]*len(dry)+dry_correct[d]*len(wet)
            rank=(score,d-w,-w)
            if best is None or rank>best[0]:best=(rank,w,d)
    return dict(wet_max=best[1],dry_min=best[2],balanced_wet_dry_recall=best[0][0]/(2*len(wet)*len(dry)),
        calibration_trials=len(calibration),evaluation_trials_used=0,minimum_gap=min_gap,
        applied_to_firmware=False,method='maximize calibration WET/DRY balanced recall; ties prefer widest reject band')


def write_report(out,report):
    out=Path(out);out.mkdir(parents=True,exist_ok=True)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    def table(title,data,labels):
        markup='<h2>'+html.escape(title)+'</h2><div class="scroll"><table><tr><th>True / observed</th>'+''.join('<th>'+p+'</th>' for p in labels)+'</tr>'
        for t in CLASSES:markup+='<tr><th>'+t+'</th>'+''.join('<td>'+str(data[t][p])+'</td>' for p in labels)+'</tr>'
        return markup+'</table></div>'
    m=report['metrics'];evidence=report['evidence_type'].upper()
    page='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Waste sorting trial report</title><style>body{font-family:system-ui;background:#f3f6f5;color:#18382d;max-width:1000px;margin:auto;padding:30px}header,section{background:white;border:1px solid #ccded4;padding:24px;border-radius:12px;margin:18px 0}strong{color:#156c53}table{width:100%;border-collapse:collapse;background:white}th,td{text-align:left;padding:12px;border-bottom:1px solid #ccded4}.scroll{overflow:auto}code{overflow-wrap:anywhere}small,p{color:#526f62}.tag{background:#ffe9bc;color:#644c13;padding:8px;border-radius:5px;display:inline-block}</style><header><small>JAY SHAH / EMBEDDED SYSTEMS</small><h1>Waste sorting trial report</h1>'''
    page+='<span class="tag">'+evidence+' EVIDENCE · '+html.escape(report['split'].upper())+'</span><p>'+('Synthetic fixture results demonstrate the software only. No real sorting performance is established.' if evidence=='SYNTHETIC' else 'Operator-declared physical trials; provenance and observations are not independently verified.')+'</p></header><section>'
    for label,key in [('Classifier accuracy (UNKNOWN included)','classifier_accuracy'),('UNKNOWN rate','unknown_rate'),('Strict observed sorting success','strict_sort_success')]:page+='<p>'+label+': <strong>'+f'{m[key]:.1%}'+'</strong></p>'
    page+=f"<p>{m['trials']} trials · {m['unique_samples']} unique samples · {m['jams']} observed jam flags · {m['recorded_command_mismatches']} command/model mismatches</p>"
    page+='<p>Strict sorting success requires the correct observed bin and no jam. Rejected/no-bin trials remain in the denominator. Repeated samples are not independent evidence. Cycle timing excludes any unrecorded handling time and is not line throughput.</p></section>'
    page+=table('Compiled classifier predictions',m['classifier_confusion'],PREDICTED)
    page+=table('Recorded firmware commands',m['recorded_command_confusion'],PREDICTED)
    page+=table('Observed destination bins',m['actual_bin_confusion'],BINS)
    page+='<section><h2>Run evidence</h2><p>UTC: '+html.escape(report['generated_at'])+'</p><p>CSV SHA-256: <code>'+report['csv_sha256']+'</code></p><p>Controller SHA-256: <code>'+report['controller_sha256']+'</code></p><p>Thresholds: '+html.escape(json.dumps(report['classifier_config']))+'</p><p>See report.json for all class metrics, timing summaries, revision identifiers and input provenance.</p></section></html>'
    (out/'report.html').write_text(page)


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    for name in ('analyze','calibrate'):
        item=sub.add_parser(name);item.add_argument('--input',type=Path,required=True);item.add_argument('--out',type=Path,default=ROOT/'reports');item.add_argument('--cxx',default='g++')
        if name=='analyze':
            item.add_argument('--split',choices=['calibration','evaluation'],default='evaluation');item.add_argument('--wet-max',type=int);item.add_argument('--dry-min',type=int)
        else:item.add_argument('--min-gap',type=int,default=50)
    args=p.parse_args(argv)
    try:
        rows,csv_hash=read_trials(args.input)
        if args.command=='calibrate':
            proposal=suggest_thresholds(rows,args.min_gap)
            selected=[r for r in rows if r['split']=='calibration']
            predictions,config=native_predictions(selected,proposal['wet_max'],proposal['dry_min'],args.cxx)
            args.out.mkdir(parents=True,exist_ok=True)
            evidence=dict(evidence_type=rows[0]['evidence_type'],csv_sha256=csv_hash,controller_sha256=hashlib.sha256((ROOT/'firmware/waste_sorter/control.h').read_bytes()).hexdigest(),generated_at=datetime.now(timezone.utc).isoformat(),proposal=proposal,calibration_metrics=summarize(selected,predictions),classifier_config=config)
            (args.out/'calibration.json').write_text(json.dumps(evidence,indent=2)+'\n')
            print(json.dumps(evidence,indent=2));return 0
        selected=[r for r in rows if r['split']==args.split]
        if not selected:raise ValueError('No rows in requested split')
        predictions,config=native_predictions(selected,args.wet_max,args.dry_min,args.cxx)
        version=subprocess.run([args.cxx,'--version'],check=True,capture_output=True,text=True,timeout=5).stdout.splitlines()[0]
        report=dict(schema_version=1,evidence_type=rows[0]['evidence_type'],split=args.split,generated_at=datetime.now(timezone.utc).isoformat(),
            csv_sha256=csv_hash,controller_sha256=hashlib.sha256((ROOT/'firmware/waste_sorter/control.h').read_bytes()).hexdigest(),
            recorded_firmware_commit=rows[0]['firmware_commit'],normalization=rows[0]['normalization'],compiler=version,classifier_config=config,
            threshold_override=args.wet_max is not None,metrics=summarize(selected,predictions))
        write_report(args.out,report)
        print(f"{report['evidence_type'].upper()} {args.split}: {len(selected)} trials; classifier accuracy {report['metrics']['classifier_accuracy']:.3f}; not independent hardware validation")
        print('Report: '+str((args.out/'report.html').resolve()));return 0
    except (ValueError,OSError,UnicodeError,csv.Error,subprocess.SubprocessError) as exc:
        print('Error: '+str(exc),file=sys.stderr)
        if isinstance(exc,subprocess.CalledProcessError):print(exc.stderr,file=sys.stderr)
        return 2


if __name__=='__main__':sys.exit(main())
