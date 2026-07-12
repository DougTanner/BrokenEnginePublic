#!/usr/bin/env python3
"""inspect_samples.py - Query shipping Gaea 2 sample .terrain files.

The shipping examples in `C:\\Program Files\\QuadSpinner\\Gaea 2\\Examples\\`
are the ground truth for what Gaea actually accepts: enum values, port
catalogues, required fields, default property shapes. Whenever Gaea rejects a
hand-built node, the fastest path to a fix is to grep these for the same node
type and copy a known-good shape.

Subcommands:
  --enum TYPE.PROP        Distinct values for a property across all samples
  --type TYPE             Property samples for a node type (first 5 instances)
  --ports TYPE            Port catalogue for a node type
  --list-types            Every node $type seen across samples (with counts)
  --field-grep KEY        Every JSON path where this key appears
  --fingerprint           Write a structural fingerprint to the shared PC-local cache
  --fingerprint --diff    Write fingerprint AND diff against the cached one

check_gaea_version.py runs `--fingerprint --diff` automatically when the Gaea
version moves; if QuadSpinner adds/changes example files without a version bump,
the cached fingerprint silently goes stale until this is rerun by hand.

Examples:
  inspect_samples.py --enum SatMap.Library
  inspect_samples.py --type Erosion2
  inspect_samples.py --ports Combine
"""

import argparse
import glob
import json
import os
import re
import sys
from collections import Counter, OrderedDict

from gaea_cache import GaeaCacheError, atomic_write_text, cache_file

SAMPLES_DIR_DEFAULT = r'C:\Program Files\QuadSpinner\Gaea 2\Examples'


def load_terrain(path):
    """Tolerant loader: Gaea sometimes emits BOMs or non-strict JSON."""
    with open(path, 'rb') as f:
        raw = f.read()
    if raw.startswith(b'\xef\xbb\xbf'):
        raw = raw[3:]
    text = raw.decode('utf-8')
    # Newtonsoft sometimes emits trailing commas before } or ]; strict JSON rejects those
    # (same fix as load_terrain.py — without it, files like Glacier - Complex Setup.terrain are skipped).
    text = re.sub(r",(\s*[}\]])", r"\1", text)
    return json.loads(text)


def short_type(fqn):
    if not isinstance(fqn, str):
        return None
    head = fqn.split(',', 1)[0].strip()
    # Filter Modifiers — they share short names with Nodes but live in a different namespace.
    if '.Modifiers.' in head:
        return None
    return head.rsplit('.', 1)[-1]


def walk_nodes(obj, on_node):
    """Call on_node(obj) for every dict that looks like a Gaea node (has $type + Id)."""
    if isinstance(obj, dict):
        if '$type' in obj and 'Id' in obj:
            on_node(obj)
        for v in obj.values():
            walk_nodes(v, on_node)
    elif isinstance(obj, list):
        for v in obj:
            walk_nodes(v, on_node)


def iter_samples(samples_dir):
    for path in sorted(glob.glob(os.path.join(samples_dir, '*.terrain'))):
        try:
            yield path, load_terrain(path)
        except Exception as e:
            print(f'# skip {os.path.basename(path)}: {e}', file=sys.stderr)


# --------------------------------------------------------------------- enum

def cmd_enum(args):
    if '.' not in args.enum:
        sys.exit('--enum expects TYPE.PROPERTY (e.g. SatMap.Library)')
    type_name, prop = args.enum.split('.', 1)
    values = Counter()
    for _, data in iter_samples(args.samples_dir):
        def on_node(n):
            if short_type(n.get('$type')) == type_name:
                values[n.get(prop, '<absent>')] += 1
        walk_nodes(data, on_node)
    if not values:
        sys.exit(f'No {type_name} nodes found.')
    print(f'Distinct values for {type_name}.{prop}:')
    for v, c in values.most_common():
        print(f'  {c:4d}x  {v!r}')


# --------------------------------------------------------------------- type

def cmd_type(args):
    samples = []
    for path, data in iter_samples(args.samples_dir):
        def on_node(n, _src=path):
            if short_type(n.get('$type')) == args.type and len(samples) < args.limit:
                flat = {k: v for k, v in n.items()
                        if not isinstance(v, (dict, list)) and k != '$id'}
                samples.append((os.path.basename(_src), flat))
        walk_nodes(data, on_node)
        if len(samples) >= args.limit:
            break
    if not samples:
        sys.exit(f'No {args.type} nodes found.')
    print(f'{args.type} samples (showing {len(samples)}):')
    for src, props in samples:
        print(f'  -- {src}')
        for k, v in props.items():
            print(f'     {k}: {v!r}')


# --------------------------------------------------------------------- ports

def cmd_ports(args):
    for _, data in iter_samples(args.samples_dir):
        found = [None]
        def on_node(n):
            if found[0] is None and short_type(n.get('$type')) == args.ports:
                ports = (n.get('Ports') or {}).get('$values') or []
                if ports:
                    found[0] = ports
        walk_nodes(data, on_node)
        if found[0]:
            print(f'{args.ports} ports:')
            for p in found[0]:
                print(f"  Name={p.get('Name')!r:24s} Type={p.get('Type')!r}")
            return
    sys.exit(f'No {args.ports} node with a Ports catalogue found.')


# --------------------------------------------------------------------- list-types

def cmd_list_types(args):
    counts = Counter()
    for _, data in iter_samples(args.samples_dir):
        def on_node(n):
            t = short_type(n.get('$type'))
            if t:
                counts[t] += 1
        walk_nodes(data, on_node)
    print(f'{len(counts)} distinct node types across samples:')
    for t, c in counts.most_common():
        print(f'  {c:4d}x  {t}')


# --------------------------------------------------------------------- field-grep

def cmd_field_grep(args):
    hits = Counter()
    def visit(obj, path):
        if isinstance(obj, dict):
            for k, v in obj.items():
                if k == args.field_grep:
                    short = short_type(obj.get('$type')) or '<no-$type>'
                    val = v if not isinstance(v, (dict, list)) else f'<{type(v).__name__}>'
                    hits[(short, str(val))] += 1
                visit(v, f'{path}.{k}')
        elif isinstance(obj, list):
            for i, v in enumerate(obj):
                visit(v, f'{path}[{i}]')
    for _, data in iter_samples(args.samples_dir):
        visit(data, '$')
    if not hits:
        sys.exit(f'Key {args.field_grep!r} not found in any sample.')
    print(f'{args.field_grep!r} occurrences across samples:')
    for (ntype, val), c in hits.most_common():
        print(f'  {c:4d}x  {ntype}.{args.field_grep} = {val}')


# --------------------------------------------------------------------- fingerprint

def build_fingerprint(samples_dir):
    """Build a compact structural fingerprint of the samples directory."""
    types = {}  # short -> {'count': int, 'props': set}
    enums = {}  # 'Short.Prop' -> set of values

    def on_node(n):
        t = short_type(n.get('$type'))
        if not t:
            return
        entry = types.setdefault(t, {'count': 0, 'props': set()})
        entry['count'] += 1
        for k, v in n.items():
            if k in ('$id', '$type', 'Ports', 'Modifiers', 'Position', 'Id', 'Name'):
                continue
            entry['props'].add(k)
            # Treat short, string-valued, non-path-looking properties as enum candidates.
            if isinstance(v, str) and 0 < len(v) <= 32 and '\\' not in v and '/' not in v:
                enums.setdefault(f'{t}.{k}', set()).add(v)

    files = []
    for path, data in iter_samples(samples_dir):
        files.append(os.path.basename(path))
        walk_nodes(data, on_node)

    return OrderedDict([
        ('samples_dir', samples_dir),
        ('sample_files', sorted(files)),
        ('node_types', OrderedDict(
            (t, {'count': v['count'], 'properties': sorted(v['props'])})
            for t, v in sorted(types.items()))),
        ('enums', OrderedDict(
            (k, sorted(vals)) for k, vals in sorted(enums.items()))),
    ])


def diff_fingerprints(old, new):
    """Yield human-readable lines describing what changed."""
    old_types = set(old.get('node_types', {}))
    new_types = set(new.get('node_types', {}))
    for t in sorted(new_types - old_types):
        yield f'+ Node type added: {t} ({new["node_types"][t]["count"]} instances in samples)'
    for t in sorted(old_types - new_types):
        yield f'- Node type removed: {t}'

    for t in sorted(old_types & new_types):
        old_props = set(old['node_types'][t]['properties'])
        new_props = set(new['node_types'][t]['properties'])
        for p in sorted(new_props - old_props):
            yield f'+ {t}.{p}: new property'
        for p in sorted(old_props - new_props):
            yield f'- {t}.{p}: property no longer seen in samples'

    old_enums = set(old.get('enums', {}))
    new_enums = set(new.get('enums', {}))
    for e in sorted(old_enums | new_enums):
        old_vals = set(old.get('enums', {}).get(e, []))
        new_vals = set(new.get('enums', {}).get(e, []))
        for v in sorted(new_vals - old_vals):
            yield f'+ {e}: new value {v!r}'
        for v in sorted(old_vals - new_vals):
            yield f'- {e}: dropped value {v!r}'


def cmd_fingerprint(args):
    try:
        fingerprint_path = cache_file('sample-fingerprint.json')
    except GaeaCacheError as error:
        sys.exit(f'Could not access shared Gaea cache: {error}')
    fp = build_fingerprint(args.samples_dir)

    cached = None
    if os.path.isfile(fingerprint_path):
        try:
            with open(fingerprint_path, 'r', encoding='utf-8') as f:
                cached = json.load(f)
        except (json.JSONDecodeError, UnicodeDecodeError):
            cached = None
        except OSError as error:
            sys.exit(f'Could not access shared Gaea cache: {error}')

    try:
        atomic_write_text(fingerprint_path, json.dumps(fp, indent=2))
    except GaeaCacheError as error:
        sys.exit(f'Could not access shared Gaea cache: {error}')
    print(f'Wrote {fingerprint_path}', file=sys.stderr)
    print(f'  samples: {len(fp["sample_files"])}  types: {len(fp["node_types"])}  enums: {len(fp["enums"])}',
          file=sys.stderr)

    if args.diff:
        if cached is None:
            print('# No cached fingerprint to diff against; current fingerprint stored.')
            return
        changes = list(diff_fingerprints(cached, fp))
        if not changes:
            print('# No structural changes vs cached fingerprint.')
        else:
            print(f'# {len(changes)} changes vs cached fingerprint:')
            for line in changes:
                print(line)


# --------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--samples-dir', default=SAMPLES_DIR_DEFAULT,
                    help='Override samples directory')
    ap.add_argument('--limit', type=int, default=5, help='Max samples to show for --type')
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument('--enum', metavar='TYPE.PROP')
    g.add_argument('--type', metavar='TYPE')
    g.add_argument('--ports', metavar='TYPE')
    g.add_argument('--list-types', action='store_true')
    g.add_argument('--field-grep', metavar='KEY')
    g.add_argument('--fingerprint', action='store_true')
    ap.add_argument('--diff', action='store_true', help='Diff fingerprint against cached version')
    args = ap.parse_args()

    if args.enum:
        cmd_enum(args)
    elif args.type:
        cmd_type(args)
    elif args.ports:
        cmd_ports(args)
    elif args.list_types:
        cmd_list_types(args)
    elif args.field_grep:
        cmd_field_grep(args)
    elif args.fingerprint:
        cmd_fingerprint(args)


if __name__ == '__main__':
    main()
