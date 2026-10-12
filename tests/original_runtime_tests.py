#!/usr/bin/env python3
"""Regress complete independent trajectories against captured original DOS runs."""
import argparse
import csv
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'tools'))
from compare_original_runs import compare

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('probe',type=Path)
parser.add_argument('manifest',type=Path)
args=parser.parse_args()
manifest=json.loads(args.manifest.read_text())
frames=0
with tempfile.TemporaryDirectory(prefix='sjp-original-runtime-') as scratch:
    for case in manifest['cases']:
        directory=args.manifest.parent/case['name']
        metadata=json.loads((directory/'capture.json').read_text())
        if not metadata.get('physical_stop'):
            raise SystemExit(f"{case['name']}: missing complete original stop boundary")
        command=[str(args.probe),str(case['hill']),str(directory/'initial.ds.bin'),str(directory/'inputs.csv')]
        if case.get('cpu'):
            ai=case['cpu']
            command+=['--ai',str(ai['seed']),str(ai['level']),*[str(trait) for trait in ai['values']]]
        native=Path(scratch)/(case['name']+'.csv')
        with native.open('w') as output:
            subprocess.run(command,stdout=output,check=True)
        with native.open() as stream:
            last=list(csv.DictReader(stream))[-1]
        if int(last['native_phase'])!=4:
            raise SystemExit(f"{case['name']}: native did not finish at the original stop frame")
        report=Path(scratch)/(case['name']+'.json')
        failed=compare(directory/'original.csv',native,report,0,bool(case.get('cpu')))
        result=json.loads(report.read_text())
        if failed or result['original_frames']!=case['frames'] or result['compared_fields']<121:
            print(report.read_text(),file=sys.stderr)
            raise SystemExit(1)
        frames+=case['frames']
print(f"PASS: {len(manifest['cases'])} complete DOS jumps, {frames} original70Hz frames; exact trajectory, pose, wind, RNG and scoring")
