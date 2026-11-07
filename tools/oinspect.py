#!/usr/bin/env python3
"""Compatibility entry point for tools/host/oinspect.py."""
import pathlib, runpy, sys
runpy.run_path(str(pathlib.Path(__file__).resolve().parent / "host" / "oinspect.py"),
               run_name="__main__")
