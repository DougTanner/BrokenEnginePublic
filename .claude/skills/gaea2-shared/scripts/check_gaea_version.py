#!/usr/bin/env python3
"""check_gaea_version.py - Probe installed Gaea 2 version vs cached value.

Exit codes:
  0  Version unchanged since last cached run.
  1  Version changed. The script prints an auto-fingerprint-diff to stderr
     (new node types, new enum values, dropped fields) so /gaea2-load can
     surface it to the user.
  2  First run on this machine — no cached version exists. Cache is initialised
     silently and a baseline fingerprint is written.
  3  Could not determine current Gaea version (Gaea 2 not installed, never
     opened, version probe failed). Caller should treat as a soft warning.

Source for current version (in priority order):
  1. Latest .txt log in %APPDATA%\\QuadSpinner\\Gaea\\2.0\\Logs (every log starts
     with `INF Version X.Y.Z.W`). Free, no subprocess.
  2. Gaea.exe ProductVersion via PowerShell. Slower but works even if the user
     has never opened Gaea 2.

We never invoke the Gaea binary itself.
"""

import glob
import json
import os
import re
import subprocess
import sys

CACHE_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                          '..', 'cache'))
VERSION_FILE = os.path.join(CACHE_DIR, 'gaea-version.txt')
LOG_DIR = os.path.expandvars(r'%APPDATA%\QuadSpinner\Gaea\2.0\Logs')
GAEA_EXE = r'C:\Program Files\QuadSpinner\Gaea 2\Gaea.exe'


def version_from_latest_log():
    if not os.path.isdir(LOG_DIR):
        return None
    logs = glob.glob(os.path.join(LOG_DIR, '*.txt'))
    if not logs:
        return None
    latest = max(logs, key=os.path.getmtime)
    try:
        with open(latest, 'r', encoding='utf-8', errors='replace') as f:
            for line in f:
                # Both general logs and SWARM logs emit this line near the top.
                m = re.search(r'\bINF Version\s+(\S+)', line)
                if m:
                    return m.group(1)
                if line.count('\n') > 50:  # stop scanning if it's not in the header
                    break
    except OSError:
        return None
    return None


def version_from_exe():
    if not os.path.isfile(GAEA_EXE):
        return None
    try:
        result = subprocess.run(
            ['powershell', '-NoProfile', '-Command',
             f"(Get-Item '{GAEA_EXE}').VersionInfo.ProductVersion"],
            capture_output=True, text=True, timeout=10)
    except (subprocess.SubprocessError, OSError):
        return None
    out = (result.stdout or '').strip()
    return out or None


def detect_version():
    return version_from_latest_log() or version_from_exe()


def fingerprint_diff_if_changed(prev_version, new_version):
    """Run inspect_samples.py --fingerprint --diff and capture its stdout for relay."""
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'inspect_samples.py')
    if not os.path.isfile(script):
        return None
    try:
        result = subprocess.run(
            [sys.executable, script, '--fingerprint', '--diff'],
            capture_output=True, text=True, timeout=60)
    except (subprocess.SubprocessError, OSError) as e:
        return f'(fingerprint diff failed: {e})'
    return result.stdout.strip() or '(no structural changes — only the version string moved)'


def main():
    current = detect_version()
    if not current:
        print('# Could not determine Gaea 2 version (not installed, or no log present).', file=sys.stderr)
        sys.exit(3)

    os.makedirs(CACHE_DIR, exist_ok=True)
    cached = None
    if os.path.isfile(VERSION_FILE):
        try:
            with open(VERSION_FILE, 'r', encoding='utf-8') as f:
                cached = f.read().strip() or None
        except OSError:
            cached = None

    if cached is None:
        with open(VERSION_FILE, 'w', encoding='utf-8') as f:
            f.write(current + '\n')
        # First run: silently establish the baseline fingerprint too.
        fingerprint_diff_if_changed(None, current)
        print(f'# First version probe: cached Gaea {current}.', file=sys.stderr)
        sys.exit(2)

    if cached == current:
        print(f'# Gaea version unchanged: {current}', file=sys.stderr)
        sys.exit(0)

    # Version moved — refresh the fingerprint and report what changed.
    diff_text = fingerprint_diff_if_changed(cached, current)
    with open(VERSION_FILE, 'w', encoding='utf-8') as f:
        f.write(current + '\n')

    print(f'# Gaea version changed: {cached} -> {current}', file=sys.stderr)
    if diff_text:
        print('# Sample-fingerprint diff:', file=sys.stderr)
        for line in diff_text.splitlines():
            print(f'  {line}', file=sys.stderr)
    sys.exit(1)


if __name__ == '__main__':
    main()
