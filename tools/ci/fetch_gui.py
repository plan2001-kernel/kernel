#!/usr/bin/env python3
"""Fetch the immutable Nuklear source declared by the toolchain policy."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[2]


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--dest',type=Path,required=True)
    p.add_argument('--reports',type=Path,required=True)
    a=p.parse_args()
    policy=json.loads((ROOT/'config/gui-sources.json').read_text())['nuklear']
    if a.dest.exists() or a.dest.is_symlink(): raise SystemExit('refusing existing checkout')
    a.dest.mkdir(parents=True)
    def git(*args):return subprocess.check_output(['git',*map(str,args)],text=True).strip()
    git('init','-q',a.dest)
    git('-C',a.dest,'remote','add','origin',policy['url'])
    git('-C',a.dest,'fetch','--depth=1','origin',policy['commit'])
    git('-C',a.dest,'checkout','--detach','FETCH_HEAD')
    if git('-C',a.dest,'rev-parse','HEAD')!=policy['commit']:raise SystemExit('Nuklear SHA mismatch')
    a.reports.mkdir(parents=True,exist_ok=True)
    (a.reports/'gui-sources.json').write_text(json.dumps({'nuklear':policy},indent=2)+'\n')


if __name__=='__main__':main()
