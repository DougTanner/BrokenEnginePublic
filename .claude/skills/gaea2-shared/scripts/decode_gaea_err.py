#!/usr/bin/env python3
"""decode_gaea_err.py - Decode ERR lines in a Gaea 2 session log.

Gaea 2 writes load/build exceptions as base64-encoded payloads on `ERR` lines.
Each payload is `<4-byte little-endian uncompressed length><gzip stream>`. The
gzip footer (CRC32/ISIZE) is occasionally truncated or padded with junk, so we
decompress the deflate body with negative wbits to skip the trailing checksum
check entirely.

The first decoded line is almost always the gold — it carries the exception
type, message, and the JSON path of the offending field, e.g.:

    Newtonsoft.Json.JsonSerializationException: Error converting value "Sandy"
    to type 'QuadSpinner.Gaea.Nodes.CLUTLibrary'.
    Path 'Assets.$values[0].Terrain.Nodes.810.Library', line 1309, position 32.

Usage:
  decode_gaea_err.py --latest                # auto-find newest non-SWARM log
  decode_gaea_err.py --latest-swarm          # auto-find newest SWARM log
  decode_gaea_err.py --log <path>            # decode every ERR in a file
  decode_gaea_err.py --blob <base64>         # decode one literal payload
"""

import argparse
import base64
import glob
import os
import re
import sys
import zlib

LOG_DIR_DEFAULT = os.path.expandvars(r'%APPDATA%\QuadSpinner\Gaea\2.0\Logs')


def decode_blob(b64: str) -> str:
    raw = base64.b64decode(b64)
    if len(raw) < 14:
        raise ValueError(f'blob too short ({len(raw)} bytes, need >= 14)')
    # bytes[0:4]  = uncompressed length, little-endian (unused; informational)
    # bytes[4:14] = gzip header (1f 8b 08 00 + 6 more)
    # bytes[14:]  = raw deflate stream
    deflate = raw[14:]
    decompressor = zlib.decompressobj(-zlib.MAX_WBITS)
    out = decompressor.decompress(deflate)
    return out.decode('utf-8', errors='replace')


def find_latest_log(log_dir: str, swarm: bool) -> str:
    if not os.path.isdir(log_dir):
        raise FileNotFoundError(
            f'Log dir not found: {log_dir}\n'
            f'  Gaea 2 logs live in Roaming\\QuadSpinner\\Gaea\\2.0\\Logs (note: NOT Gaea\\Logs — that is Gaea 1).\n'
            f'  If Gaea 2 has never been opened on this machine the directory will not exist.'
        )
    paths = glob.glob(os.path.join(log_dir, '*.txt'))
    # Filenames seen in the wild: YYYY-MM-DD_..._SWARM.txt, CRASH_SWARM_YYYY-... .txt.
    # Match SWARM anywhere in the basename, case-insensitive, so CRASH variants are
    # classified correctly.
    def is_swarm(p):
        return 'swarm' in os.path.basename(p).lower()
    paths = [p for p in paths if is_swarm(p) == swarm]
    if not paths:
        kind = 'SWARM' if swarm else 'general'
        raise FileNotFoundError(f'No {kind} log files in {log_dir}')
    return max(paths, key=os.path.getmtime)


def iter_err_blobs(log_path: str):
    """Yield (timestamp, blob_b64) for each ERR line in the log."""
    with open(log_path, 'r', encoding='utf-8', errors='replace') as f:
        for raw_line in f:
            # Three log formats appear in the wild:
            #   [HH:MM:SS] ERR <blob>                                  (bracketed; .txt session log)
            #   HH:MM:SS ERR <blob>                                    (CRASH_SWARM_*.txt; no brackets)
            #   { "time": "HH:MM:SS.NNNN", "level": "ERROR", "data": "<blob>" }   (JSONL; older format)
            m = re.match(r'\[([\d:.]+)\]\s+ERR\s+(\S+)', raw_line)
            if m:
                yield m.group(1), m.group(2)
                continue
            m = re.match(r'([\d:.]+)\s+ERR\s+(\S+)', raw_line)
            if m:
                yield m.group(1), m.group(2)
                continue
            m = re.search(r'"time"\s*:\s*"([\d:.]+)".*?"level"\s*:\s*"ERR(?:OR)?".*?"data"\s*:\s*"([^"]+)"', raw_line)
            if m:
                yield m.group(1), m.group(2)


def trim_for_display(text: str, full: bool) -> str:
    if full:
        return text
    # Keep the exception line + the inner ArgumentException/path hint that
    # always follows on " ---> ..." — those two together name the problem.
    lines = text.splitlines()
    out = []
    for line in lines:
        stripped = line.strip()
        if stripped.startswith('at '):
            out.append('   ... (stack trace omitted; pass --full to see it)')
            break
        out.append(line)
    return '\n'.join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument('--latest', action='store_true', help='Decode the newest non-SWARM log in the default dir')
    src.add_argument('--latest-swarm', action='store_true', help='Decode the newest SWARM log')
    src.add_argument('--log', help='Path to a specific Gaea 2 log file')
    src.add_argument('--blob', help='Literal base64 ERR payload to decode')
    ap.add_argument('--log-dir', default=LOG_DIR_DEFAULT, help='Override log directory')
    ap.add_argument('--full', action='store_true', help='Print the full stack trace (default: trim to exception line)')
    args = ap.parse_args()

    if args.blob:
        print(trim_for_display(decode_blob(args.blob), args.full))
        return

    log_path = args.log
    if args.latest:
        log_path = find_latest_log(args.log_dir, swarm=False)
    elif args.latest_swarm:
        log_path = find_latest_log(args.log_dir, swarm=True)
    print(f'# Log: {log_path}', file=sys.stderr)

    n = 0
    for ts, blob in iter_err_blobs(log_path):
        if n > 0:
            print('---')
        print(f'# [{ts}]')
        try:
            decoded = decode_blob(blob)
        except Exception as e:
            print(f'<decode failed: {type(e).__name__}: {e}>')
        else:
            print(trim_for_display(decoded, args.full))
        n += 1

    if n == 0:
        print('# No ERR lines found in this log.', file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
