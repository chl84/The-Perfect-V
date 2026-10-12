#!/usr/bin/env python3
"""Regression against observed DSJ replay playback for the four original hills permitted by the supplied demo."""
import csv,json,subprocess,sys,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from compare_original_runs import compare
probe=Path(sys.argv[1]).resolve();manifest_path=Path(sys.argv[2]).resolve();manifest=json.loads(manifest_path.read_text());directory=manifest_path.parent
covered=manifest['covered_hills'];require_count=len(covered)
if covered != [0,1,2,3]:
    raise SystemExit('The supplied-demo regression must retain all four measured original hills')
if len(manifest['scenarios'])!=require_count or sorted(s['hill']for s in manifest['scenarios'])!=covered:
    raise SystemExit('Original runtime manifest does not match its declared measured hill coverage')
ticks=0;failed=[]
with tempfile.TemporaryDirectory(prefix='sjp-original-rpl-runtime-')as temporary:
    temp=Path(temporary)
    for scenario in manifest['scenarios']:
        if 'error'in scenario:failed.append(scenario);continue
        out=directory/scenario['directory'];native=temp/(scenario['directory']+'.csv');report=temp/(scenario['directory']+'.json')
        with native.open('w')as file:subprocess.run([str(probe),str(out/'input.RPL')],stdout=file,check=True)
        status=compare(out/'original.csv',native,report,0)
        data=json.loads(report.read_text());ticks+=data['original_frames']
        if data['original_frames'] != scenario['ticks'] or data['compared_fields'] != 121:
            raise SystemExit('Original replay comparison lost states or measured fields: '+scenario['directory'])
        if status:failed.append({'hill':scenario['hill'],'failures':data['failures'],'first':data['first_differences'][:5]})
if failed:raise SystemExit(json.dumps({'failed':failed},indent=2))
print(json.dumps({'original_hills':require_count,'provided_edition':manifest['provided_edition'],'remaining_hills_require':manifest.get('remaining_hills_require'),'actual_DOS_ticks':ticks,'absolute_tolerance':0,'status':'all original replay playback fields match'}))
