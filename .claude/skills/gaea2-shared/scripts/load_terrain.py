#!/usr/bin/env python3
"""load_terrain.py - Convert a Gaea 2 .terrain JSON to editable Markdown + sidecar.

Usage: python load_terrain.py <input.terrain> [--output-dir Temp]

Outputs (under Temp/ by default):
  <basename>.md                 - editable: frontmatter + Mermaid topology + node sections
  <basename>.passthrough.json   - structural state needed for round-trip (do not edit by hand)
"""

import argparse
import json
import os
import re
import sys
from collections import OrderedDict

NODE_STRUCTURAL = {"$id", "$type", "Id", "Name", "Position", "Ports", "Modifiers"}


def parse_terrain(path):
    with open(path, "r", encoding="utf-8-sig") as f:
        text = f.read()
    # Newtonsoft sometimes emits trailing commas before } or ]; strict JSON rejects those.
    text = re.sub(r",(\s*[}\]])", r"\1", text)
    try:
        return json.loads(text, object_pairs_hook=OrderedDict)
    except json.JSONDecodeError as e:
        print(f"ERROR: {path} is not valid JSON (.terrain files are JSON; check the file).", file=sys.stderr)
        print(f"  {e}", file=sys.stderr)
        sys.exit(2)


def short_type(fqn):
    head = fqn.split(",")[0].strip()
    prefix = "QuadSpinner.Gaea."
    if head.startswith(prefix):
        head = head[len(prefix):]
    if head.startswith("Nodes."):
        head = head[6:]
    return head


def _is_numeric_literal(s):
    """True iff s would be re-parsed as int/float by save_terrain's parse_scalar."""
    # Mirror parse_scalar's underscore rejection so fmt_scalar agrees with parse_scalar.
    if "_" in s:
        return False
    try:
        if "." in s or "e" in s or "E" in s:
            float(s)
        else:
            int(s)
        return True
    except ValueError:
        return False


def fmt_scalar(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if v is None:
        return "null"
    if isinstance(v, float):
        # Non-finite floats survive only as quoted strings; parse_scalar restores them.
        if v != v or v in (float("inf"), float("-inf")):
            return json.dumps(repr(v))
        return repr(v)
    if isinstance(v, int):
        return str(v)
    if isinstance(v, str):
        # Quote strings that would otherwise round-trip as a different type.
        if (v == "" or any(c in v for c in ":#\n\"") or v.strip() != v
                or v in ("true", "false", "null", "nan", "inf", "-inf")
                or v.startswith(("[", "{"))
                or _is_numeric_literal(v)):
            return json.dumps(v)
        return v
    return json.dumps(v)


def fmt_value(v):
    if isinstance(v, (list, dict, OrderedDict)):
        return json.dumps(v, separators=(",", ":"))
    return fmt_scalar(v)


def fmt_kv_block(d):
    return "\n".join(f"{k}: {fmt_value(v)}" for k, v in d.items())


def collect_edges(nodes):
    edges = []
    for nid, node in nodes.items():
        for port in node.get("Ports", {}).get("$values", []):
            rec = port.get("Record")
            if rec:
                edges.append({
                    "from_id": rec.get("From"),
                    "from_port": rec.get("FromPort"),
                    "to_id": rec.get("To"),
                    "to_port": rec.get("ToPort"),
                })
    return edges


def build_mermaid(nodes_md, edges):
    lines = ["```mermaid", "flowchart TD"]
    for nid, md in nodes_md.items():
        label = md["type"].replace('"', '\\"')
        lines.append(f'  n{nid}["{label}"]')
    for e in edges:
        if e["from_port"] == "Out" and e["to_port"] == "In":
            lines.append(f'  n{e["from_id"]} --> n{e["to_id"]}')
        else:
            label = f'{e["from_port"]} → {e["to_port"]}'
            lines.append(f'  n{e["from_id"]} -- "{label}" --> n{e["to_id"]}')
    lines.append("```")
    return "\n".join(lines)


def extract_node(node):
    nid = node["Id"]
    type_fqn = node.get("$type", "")
    short = short_type(type_fqn)
    name = node.get("Name", short)
    pos = node.get("Position", {}) or {}

    md = OrderedDict()
    md["type"] = short
    if name and name != short:
        md["name"] = name
    if pos:
        md["position"] = [pos.get("X", 0.0), pos.get("Y", 0.0)]
    for k, v in node.items():
        if k in NODE_STRUCTURAL:
            continue
        md[k] = v

    pt = OrderedDict()
    pt["type_fqn"] = type_fqn
    pt["ports"] = []
    for port in node.get("Ports", {}).get("$values", []):
        port_record = OrderedDict()
        # Preserve everything except $id (regenerated), Record (becomes edge in
        # Mermaid), and Parent (regenerated as $ref backref by fixup_port_parents).
        for pk, pv in port.items():
            if pk in ("$id", "Record", "Parent"):
                continue
            port_record[pk] = pv
        pt["ports"].append(port_record)
    modifiers = node.get("Modifiers", {}).get("$values", [])
    pt["modifiers"] = modifiers
    return nid, md, pt


def fmt_position(pos):
    if isinstance(pos, list) and len(pos) == 2:
        return f"{repr(float(pos[0]))}, {repr(float(pos[1]))}"
    return fmt_value(pos)


def emit_node_section(nid, md):
    lines = [f"### n{nid}: {md['type']}"]
    for k, v in md.items():
        if k == "type":
            continue
        if k == "position":
            lines.append(f"position: {fmt_position(v)}")
        else:
            lines.append(f"{k}: {fmt_value(v)}")
    return "\n".join(lines)


def emit_notes(notes_obj):
    out = []
    for nid, n in notes_obj.items():
        if nid.startswith("$"):
            continue
        pos = n.get("Position", {}) or {}
        size = n.get("Size", {}) or {}
        out.append(f"### note{n.get('Id', nid)}")
        out.append(f"name: {fmt_value(n.get('Name', 'Note'))}")
        out.append(f"color: {fmt_value(n.get('Color', 'Gray'))}")
        out.append(f"position: {repr(float(pos.get('X', 0.0)))}, {repr(float(pos.get('Y', 0.0)))}")
        out.append(f"size: {repr(float(size.get('X', 200.0)))}, {repr(float(size.get('Y', 100.0)))}")
        out.append(f"type: {fmt_value(n.get('Type', 'OnlyText'))}")
        markdown = n.get("Markdown", "")
        out.append("markdown: |")
        for ml in markdown.replace("\r\n", "\n").split("\n"):
            out.append(f"  {ml}")
        out.append("")
    return "\n".join(out).rstrip()


def main():
    ap = argparse.ArgumentParser(description="Convert .terrain to editable Markdown + sidecar.")
    ap.add_argument("input", help="Path to a Gaea .terrain file")
    ap.add_argument("--output-dir", default="Temp", help="Where to write the .md and .passthrough.json")
    args = ap.parse_args()

    if not os.path.isfile(args.input):
        print(f"ERROR: input not found: {args.input}", file=sys.stderr)
        sys.exit(2)

    terrain = parse_terrain(args.input)
    assets = terrain.get("Assets", {}).get("$values", [])
    if not assets:
        print("ERROR: no Assets in input", file=sys.stderr)
        sys.exit(2)
    if len(assets) > 1:
        print(f"WARNING: {len(assets)} assets found; only the first will be editable", file=sys.stderr)

    asset = assets[0]
    t = asset["Terrain"]
    nodes_obj = t.get("Nodes", {}) or {}
    node_entries = OrderedDict((k, v) for k, v in nodes_obj.items() if not k.startswith("$"))

    md_nodes = OrderedDict()
    pt_nodes = OrderedDict()
    for nid_str, node in node_entries.items():
        nid, md, pt = extract_node(node)
        md_nodes[nid] = md
        pt_nodes[str(nid)] = pt

    edges = collect_edges(node_entries)

    metadata = t.get("Metadata", {}) or {}
    fm = OrderedDict()
    fm["gaea_version"] = metadata.get("Version", "")
    fm["modified_version"] = metadata.get("ModifiedVersion", "")
    fm["terrain_id"] = t.get("Id", "")
    fm["file_id"] = terrain.get("Id", "")
    fm["branch"] = terrain.get("Branch", 1)
    fm["name"] = metadata.get("Name", "")
    fm["description"] = metadata.get("Description", "")
    fm["date_created"] = metadata.get("DateCreated", "")
    fm["date_last_built"] = metadata.get("DateLastBuilt", "")
    fm["date_last_saved"] = metadata.get("DateLastSaved", "")
    fm["width"] = t.get("Width", 5000.0)
    fm["height"] = t.get("Height", 2500.0)
    fm["ratio"] = t.get("Ratio", 0.5)

    bd = asset.get("BuildDefinition", {}) or {}
    for k, v in bd.items():
        if k.startswith("$"):
            continue
        fm[f"build_{k}"] = v

    notes_obj = t.get("Notes", {}) or {}
    notes_md = emit_notes(notes_obj)

    automation = asset.get("Automation", {}) or {}
    variables_md_lines = []
    var_obj = automation.get("Variables") or {}
    for k, v in var_obj.items():
        if k.startswith("$"):
            continue
        variables_md_lines.append(f"{k}: {fmt_value(v)}")
    variables_md = "\n".join(variables_md_lines) if variables_md_lines else "(none)"
    bindings = (automation.get("Bindings") or {}).get("$values", [])
    # Preserve auxiliary automation keys (VariablesEx in older files, future fields)
    # so save can emit them in the correct slot.
    automation_extras = OrderedDict()
    for k, v in automation.items():
        if k.startswith("$"):
            continue
        if k in ("Bindings", "Expressions", "Variables"):
            continue
        automation_extras[k] = v

    out = []
    out.append("---")
    out.append(fmt_kv_block(fm))
    out.append("---")
    out.append("")
    out.append(f"# {os.path.splitext(os.path.basename(args.input))[0]}")
    out.append("")
    out.append("## Topology")
    out.append("")
    out.append(build_mermaid(md_nodes, edges))
    out.append("")
    out.append("## Nodes")
    out.append("")
    for nid, md in md_nodes.items():
        out.append(emit_node_section(nid, md))
        out.append("")
    out.append("## Notes")
    out.append("")
    out.append(notes_md if notes_md else "(none)")
    out.append("")
    out.append("## Variables")
    out.append("")
    out.append(variables_md)
    out.append("")

    # Capture full metadata blocks so save can replay key order/presence verbatim.
    # Older Gaea files lack ModifiedVersion / Owner; emitting those unconditionally
    # would gain spurious empty fields on every save.
    metadata_template_terrain = OrderedDict(
        (k, v) for k, v in metadata.items() if not k.startswith("$"))
    root_metadata_obj = terrain.get("Metadata", {}) or {}
    metadata_template_root = OrderedDict(
        (k, v) for k, v in root_metadata_obj.items() if not k.startswith("$"))

    # Capture root-level key order so save can reproduce it exactly. Shipping
    # examples emit `Assets, Id, Branch, Metadata`; files saved from the Gaea 2
    # UI may also include `Macros` (and possibly future fields). Either pattern
    # must round-trip — Gaea rejects files with the wrong root-key shape with
    # "File is corrupt or missing additional data".
    ROOT_HANDLED = {"$id", "Assets", "Id", "Branch", "Metadata"}
    root_key_order = [k for k in terrain.keys() if not k.startswith("$")]
    root_extras = OrderedDict(
        (k, terrain[k]) for k in terrain
        if not k.startswith("$") and k not in ROOT_HANDLED
    )

    pt = OrderedDict()
    pt["__schema"] = "gaea2-passthrough-v1"
    pt["__warning"] = "Do not hand-edit. Regenerated by gaea2-load."
    pt["original_file"] = os.path.abspath(args.input)
    pt["metadata_template_terrain"] = metadata_template_terrain
    pt["metadata_template_root"] = metadata_template_root
    pt["root_key_order"] = root_key_order
    pt["root_extras"] = root_extras
    pt["nodes"] = pt_nodes
    pt["graph"] = OrderedDict([
        ("groups", t.get("Groups", {})),
        ("graphTabs", (t.get("GraphTabs", {}) or {}).get("$values", [])),
        ("regions", (t.get("Regions", {}) or {}).get("$values", []) if "Regions" in t else None),
    ])
    pt["asset"] = OrderedDict([
        ("automation_bindings", bindings),
        ("automation_expressions", automation.get("Expressions", {})),
        ("automation_extras", automation_extras),
        ("state", asset.get("State", {})),
        ("buildProfiles", asset.get("BuildProfiles", {})),
    ])

    os.makedirs(args.output_dir, exist_ok=True)
    base = os.path.splitext(os.path.basename(args.input))[0]
    md_path = os.path.join(args.output_dir, f"{base}.md")
    pt_path = os.path.join(args.output_dir, f"{base}.passthrough.json")

    with open(md_path, "w", encoding="utf-8") as f:
        f.write("\n".join(out))
    with open(pt_path, "w", encoding="utf-8") as f:
        json.dump(pt, f, indent=2)

    print(f"Wrote {md_path}")
    print(f"Wrote {pt_path}")
    print(f"Nodes: {len(md_nodes)}  Edges: {len(edges)}  Notes: {sum(1 for k in notes_obj if not k.startswith('$'))}")


if __name__ == "__main__":
    main()
