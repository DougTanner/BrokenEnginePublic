#!/usr/bin/env python3
"""save_terrain.py - Reverse of load_terrain.py: Markdown + sidecar back to .terrain JSON.

Usage: python save_terrain.py <input.md> [--output <output.terrain>]

Reads:
  <input>.md                  - editable Markdown
  <input>.passthrough.json    - sidecar with structural state

Writes:
  <output>.terrain            - default: same dir, same basename, .terrain extension
"""

import argparse
import json
import os
import re
import sys
from collections import OrderedDict


def parse_scalar(s):
    if s == "":
        return ""
    if s.startswith('"') and s.endswith('"'):
        try:
            unquoted = json.loads(s)
        except Exception:
            return s[1:-1]
        # Non-finite floats round-trip through quoted "nan"/"inf"/"-inf" tokens.
        if isinstance(unquoted, str) and unquoted in ("nan", "inf", "-inf"):
            return float(unquoted)
        return unquoted
    if s == "true":
        return True
    if s == "false":
        return False
    if s == "null":
        return None
    if s.startswith("[") or s.startswith("{"):
        try:
            return json.loads(s)
        except Exception:
            return s
    # Reject Python's underscore-digit literals (e.g. "1_000") — those are
    # Python source syntax, not values a Gaea file would emit. Treat as string
    # so user-typed "1_000" in markdown survives as a string instead of becoming int 1000.
    if "_" in s:
        return s
    try:
        if "." in s or "e" in s or "E" in s:
            return float(s)
        return int(s)
    except ValueError:
        return s


def parse_frontmatter(md_text):
    if not md_text.startswith("---"):
        return OrderedDict(), md_text
    end = md_text.find("\n---", 3)
    if end < 0:
        return OrderedDict(), md_text
    yaml_block = md_text[3:end].strip()
    body = md_text[end + 4:].lstrip("\n")
    fm = OrderedDict()
    for line in yaml_block.splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        m = re.match(r"^([^:]+):\s*(.*)$", line)
        if m:
            key = m.group(1).strip()
            val = m.group(2).strip()
            fm[key] = parse_scalar(val)
    return fm, body


def parse_mermaid(body):
    edges = []
    m = re.search(r"```mermaid\n(.*?)\n```", body, re.DOTALL)
    if not m:
        return edges
    block = m.group(1)
    for line in block.splitlines():
        line = line.strip()
        m1 = re.match(r'n(\d+)\s+--\s+"([^"]+)"\s+-->\s+n(\d+)', line)
        if m1:
            label = m1.group(2)
            parts = label.split("→")
            from_port = parts[0].strip() if len(parts) >= 1 else "Out"
            to_port = parts[1].strip() if len(parts) >= 2 else "In"
            edges.append({
                "from_id": int(m1.group(1)),
                "from_port": from_port,
                "to_id": int(m1.group(3)),
                "to_port": to_port,
            })
            continue
        m2 = re.match(r"n(\d+)\s+-->\s+n(\d+)", line)
        if m2:
            edges.append({
                "from_id": int(m2.group(1)),
                "from_port": "Out",
                "to_id": int(m2.group(2)),
                "to_port": "In",
            })
    return edges


def parse_node_sections(body):
    m = re.search(r"##\s*Nodes\s*\n(.*?)(?=\n##\s|\Z)", body, re.DOTALL)
    if not m:
        return OrderedDict()
    section = m.group(1)
    nodes = OrderedDict()
    # MULTILINE so empty-body sections (heading immediately followed by another heading)
    # split correctly — the previous trailing-\n form consumed the next heading's leading \n.
    chunks = re.split(r"(?m)^###\s+n(\d+):\s*([^\n]+?)\s*$", section)
    for i in range(1, len(chunks), 3):
        nid = int(chunks[i])
        type_short = chunks[i + 1].strip()
        body_text = chunks[i + 2] if i + 2 < len(chunks) else ""
        node_entry = OrderedDict()
        node_entry["type"] = type_short
        node_entry["properties"] = OrderedDict()
        for line in body_text.splitlines():
            line = line.rstrip()
            if not line.strip():
                continue
            mm = re.match(r"^([^:]+):\s*(.*)$", line)
            if not mm:
                continue
            key = mm.group(1).strip()
            val_text = mm.group(2).strip()
            if key == "position":
                parts = [p.strip() for p in val_text.split(",")]
                node_entry["position"] = [float(parts[0]), float(parts[1])]
            elif key == "name":
                node_entry["name"] = parse_scalar(val_text)
            else:
                node_entry["properties"][key] = parse_scalar(val_text)
        nodes[nid] = node_entry
    return nodes


def parse_notes(body):
    m = re.search(r"##\s*Notes\s*\n(.*?)(?=\n##\s|\Z)", body, re.DOTALL)
    if not m:
        return OrderedDict()
    section = m.group(1).strip()
    if section == "(none)" or not section:
        return OrderedDict()
    notes = OrderedDict()
    chunks = re.split(r"\n###\s+note(\d+)\n", "\n" + section)
    for i in range(1, len(chunks), 2):
        nid = int(chunks[i])
        body_text = chunks[i + 1] if i + 1 < len(chunks) else ""
        note = OrderedDict()
        lines = body_text.splitlines()
        idx = 0
        while idx < len(lines):
            line = lines[idx]
            mm = re.match(r"^([^:]+):\s*(.*)$", line)
            if mm:
                k = mm.group(1).strip()
                v = mm.group(2).strip()
                if v == "|":
                    md_lines = []
                    idx += 1
                    while idx < len(lines):
                        nxt = lines[idx]
                        if nxt.startswith("  "):
                            md_lines.append(nxt[2:])
                            idx += 1
                        elif nxt.strip() == "":
                            md_lines.append("")
                            idx += 1
                        else:
                            break
                    while md_lines and md_lines[-1] == "":
                        md_lines.pop()
                    note[k] = "\r\n".join(md_lines)
                    continue
                if k in ("position", "size"):
                    parts = [p.strip() for p in v.split(",")]
                    note[k] = [float(parts[0]), float(parts[1])]
                else:
                    note[k] = parse_scalar(v)
            idx += 1
        notes[nid] = note
    return notes


def parse_variables(body):
    m = re.search(r"##\s*Variables\s*\n(.*?)(?=\n##\s|\Z)", body, re.DOTALL)
    if not m:
        return OrderedDict()
    section = m.group(1).strip()
    if section == "(none)" or not section:
        return OrderedDict()
    out = OrderedDict()
    for line in section.splitlines():
        line = line.rstrip()
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        mm = re.match(r"^([^:]+):\s*(.*)$", line)
        if mm:
            out[mm.group(1).strip()] = parse_scalar(mm.group(2).strip())
    return out


class IdGenerator:
    def __init__(self):
        self.n = 0

    def next(self):
        self.n += 1
        return str(self.n)


def reassign_ids(obj, ig, remap):
    """Walk obj. For each dict with $id, allocate fresh $id and record old->new in remap.
       For each dict without $id (and not a pure $ref placeholder), allocate fresh $id."""
    if isinstance(obj, dict) or isinstance(obj, OrderedDict):
        if "$ref" in obj and "$id" not in obj:
            return
        if "$id" in obj:
            old = obj["$id"]
            new_id = ig.next()
            remap[old] = new_id
            obj["$id"] = new_id
        else:
            obj["$id"] = ig.next()
        if next(iter(obj)) != "$id":
            new = OrderedDict()
            new["$id"] = obj["$id"]
            for k, v in obj.items():
                if k != "$id":
                    new[k] = v
            obj.clear()
            obj.update(new)
        for k, v in obj.items():
            if k == "$id":
                continue
            reassign_ids(v, ig, remap)
    elif isinstance(obj, list):
        for item in obj:
            reassign_ids(item, ig, remap)


def fixup_refs(obj, remap):
    if isinstance(obj, dict) or isinstance(obj, OrderedDict):
        if "$ref" in obj:
            old = obj["$ref"]
            if old in remap:
                obj["$ref"] = remap[old]
        for k, v in obj.items():
            if k == "$ref":
                continue
            fixup_refs(v, remap)
    elif isinstance(obj, list):
        for item in obj:
            fixup_refs(item, remap)


def fixup_port_parents(root):
    asset = root["Assets"]["$values"][0]
    nodes = asset["Terrain"]["Nodes"]
    for nid_str, node in nodes.items():
        if nid_str.startswith("$"):
            continue
        node_id = node["$id"]
        for port in node.get("Ports", {}).get("$values", []):
            port["Parent"] = OrderedDict([("$ref", node_id)])


def build_terrain(md_path, passthrough_path, output_path):
    with open(md_path, "r", encoding="utf-8") as f:
        md_text = f.read()
    with open(passthrough_path, "r", encoding="utf-8") as f:
        pt = json.load(f, object_pairs_hook=OrderedDict)

    fm, body = parse_frontmatter(md_text)
    edges = parse_mermaid(body)
    parsed_nodes = parse_node_sections(body)
    notes = parse_notes(body)
    variables = parse_variables(body)

    # ---- Nodes ----
    # Gaea / Newtonsoft serialize each node in this canonical order, derived from
    # inspecting many .terrain samples:
    #   $id, $type, <subclass-specific properties>,
    #   Id, [Version],
    #   Name, [NodeSize],
    #   Position, [SaveDefinition], [RenderIntentOverride],
    #   Ports, Modifiers
    # Optional base-class fields (in []) appear only when the source had them; they
    # must be placed in their canonical slot, NOT in the free-form subclass property block.
    SLOT_AFTER_ID = ("Version",)
    SLOT_AFTER_NAME = ("NodeSize",)
    SLOT_AFTER_POSITION = ("SaveDefinition", "GraphIndex", "StateOverride", "RenderIntentOverride")
    ALL_BASE_SLOTS = SLOT_AFTER_ID + SLOT_AFTER_NAME + SLOT_AFTER_POSITION

    pt_nodes_dict = pt.get("nodes", {}) if isinstance(pt.get("nodes"), dict) else {}
    consumed_edges = set()  # edge indices already wired to a port (prevents two ports stealing one edge)

    nodes_obj = OrderedDict()
    for nid, node_md in parsed_nodes.items():
        pt_node = pt_nodes_dict.get(str(nid))
        if pt_node is None:
            print(f"WARNING: node n{nid} ({node_md['type']}) has no passthrough entry; "
                  f"type FQN and ports will be synthesized — Gaea may reject it",
                  file=sys.stderr)
            pt_node = {}

        node = OrderedDict()
        type_fqn = pt_node.get("type_fqn")
        if not type_fqn:
            type_fqn = f"QuadSpinner.Gaea.Nodes.{node_md['type']}, Gaea.Nodes"
            print(f"WARNING: n{nid} ({node_md['type']}) missing type_fqn; "
                  f"synthesized as {type_fqn} (assumes Nodes namespace; "
                  f"automation/exporter types live elsewhere)",
                  file=sys.stderr)
        node["$type"] = type_fqn

        properties = OrderedDict(node_md.get("properties", {}))
        slot_values = {}
        for slot in ALL_BASE_SLOTS:
            if slot in properties:
                slot_values[slot] = properties.pop(slot)
        for k, v in properties.items():
            node[k] = v
        node["Id"] = nid
        for slot in SLOT_AFTER_ID:
            if slot in slot_values:
                node[slot] = slot_values[slot]
        node["Name"] = node_md.get("name", node_md["type"])
        for slot in SLOT_AFTER_NAME:
            if slot in slot_values:
                node[slot] = slot_values[slot]
        pos = node_md.get("position", [0.0, 0.0])
        node["Position"] = OrderedDict([("X", float(pos[0])), ("Y", float(pos[1]))])
        for slot in SLOT_AFTER_POSITION:
            if slot in slot_values:
                node[slot] = slot_values[slot]

        # Canonical port order: Name, [DisplayName], Type, [Record], [IsExporting],
        # Parent (placeholder filled by fixup_port_parents), [PortalState, ...].
        # Parent must exist in the dict at this position so insertion order matches
        # Gaea's serialization; fixup_port_parents only updates its value.
        PORT_PRE_RECORD = ("Name", "DisplayName", "Type")
        PORT_POST_RECORD = ("IsExporting",)
        PORT_KNOWN = set(PORT_PRE_RECORD + ("Record",) + PORT_POST_RECORD + ("Parent",))

        # Ports iterate in passthrough order, which mirrors Gaea's serialization order.
        # When a node has multiple same-named ports (rare but possible), the first port
        # in this list claims the first matching edge — any disambiguation Gaea relies on
        # for same-named ports MUST be preserved by passthrough order, not by Mermaid label.
        ports_values = []
        for port_pt in pt_node.get("ports", []):
            port = OrderedDict()
            for k in PORT_PRE_RECORD:
                if k in port_pt:
                    port[k] = port_pt[k]
            # Match exactly one edge to this port; mark consumed so a sibling
            # port with the same Name doesn't steal it.
            for ei, e in enumerate(edges):
                if ei in consumed_edges:
                    continue
                if e["to_id"] == nid and e["to_port"] == port_pt.get("Name"):
                    port["Record"] = OrderedDict([
                        ("From", e["from_id"]),
                        ("To", e["to_id"]),
                        ("FromPort", e["from_port"]),
                        ("ToPort", e["to_port"]),
                        ("IsValid", True),
                    ])
                    consumed_edges.add(ei)
                    break
            for k in PORT_POST_RECORD:
                if k in port_pt:
                    port[k] = port_pt[k]
            port["Parent"] = None  # placeholder; fixup_port_parents fills value, position stays
            for k, v in port_pt.items():
                if k not in PORT_KNOWN:
                    port[k] = v
            ports_values.append(port)

        node["Ports"] = OrderedDict([("$values", ports_values)])
        node["Modifiers"] = OrderedDict([("$values", pt_node.get("modifiers") or [])])
        nodes_obj[str(nid)] = node

    # Edges that didn't bind to any input port — likely a typo in the Mermaid
    # to_port name or a node added in /gaea2-modify without a port catalogue.
    for ei, e in enumerate(edges):
        if ei not in consumed_edges:
            print(f"WARNING: edge n{e['from_id']} -> n{e['to_id']} "
                  f"(port '{e['to_port']}') not wired — no matching input port on target",
                  file=sys.stderr)

    # ---- Notes ----
    notes_obj = OrderedDict()
    for nid, n in notes.items():
        note = OrderedDict()
        note["Id"] = nid
        note["Name"] = n.get("name", "Note")
        note["Markdown"] = n.get("markdown", "")
        note["Color"] = n.get("color", "Gray")
        pos = n.get("position", [0.0, 0.0])
        note["Position"] = OrderedDict([("X", float(pos[0])), ("Y", float(pos[1]))])
        size = n.get("size", [200.0, 100.0])
        note["Size"] = OrderedDict([("X", float(size[0])), ("Y", float(size[1]))])
        note["Type"] = n.get("type", "OnlyText")
        notes_obj[str(nid)] = note

    # ---- Terrain metadata ----
    # Template from passthrough preserves field order and presence (older Gaea
    # files lack ModifiedVersion; emitting it unconditionally would diff every save).
    fm_metadata_overlay = {
        "Name": fm.get("name"),
        "Description": fm.get("description"),
        "Version": fm.get("gaea_version"),
        "ModifiedVersion": fm.get("modified_version"),
        "DateCreated": fm.get("date_created"),
        "DateLastBuilt": fm.get("date_last_built"),
        "DateLastSaved": fm.get("date_last_saved"),
    }
    template_terrain = pt.get("metadata_template_terrain") or OrderedDict([
        ("Name", ""), ("Description", ""), ("Version", ""), ("DateCreated", ""),
        ("DateLastBuilt", ""), ("DateLastSaved", ""), ("ModifiedVersion", ""),
    ])
    metadata = OrderedDict()
    for k, v in template_terrain.items():
        overlay = fm_metadata_overlay.get(k)
        metadata[k] = overlay if overlay is not None else v

    terrain_obj = OrderedDict()
    terrain_obj["Id"] = fm.get("terrain_id", "")
    terrain_obj["Metadata"] = metadata
    terrain_obj["Nodes"] = nodes_obj
    terrain_obj["Groups"] = pt.get("graph", {}).get("groups") or OrderedDict()
    terrain_obj["Notes"] = notes_obj
    graph_tabs = pt.get("graph", {}).get("graphTabs") or []
    terrain_obj["GraphTabs"] = OrderedDict([("$values", graph_tabs)])
    terrain_obj["Width"] = fm.get("width", 5000.0)
    terrain_obj["Height"] = fm.get("height", 2500.0)
    terrain_obj["Ratio"] = fm.get("ratio", 0.5)
    if pt.get("graph", {}).get("regions") is not None:
        terrain_obj["Regions"] = OrderedDict([("$values", pt["graph"]["regions"] or [])])

    # ---- Automation ----
    variables_obj = OrderedDict()
    for k, v in variables.items():
        variables_obj[k] = v
    automation = OrderedDict()
    automation["Bindings"] = OrderedDict([
        ("$values", pt.get("asset", {}).get("automation_bindings") or [])
    ])
    automation["Expressions"] = pt.get("asset", {}).get("automation_expressions") or OrderedDict()
    # Auxiliary keys (e.g., VariablesEx in older files) live between Expressions and Variables.
    for k, v in (pt.get("asset", {}).get("automation_extras") or {}).items():
        automation[k] = v
    automation["Variables"] = variables_obj

    # ---- BuildDefinition ----
    build_def = OrderedDict()
    for k, v in fm.items():
        if k.startswith("build_"):
            build_def[k[len("build_"):]] = v

    # ---- Asset ----
    asset = OrderedDict()
    asset["Terrain"] = terrain_obj
    asset["Automation"] = automation
    asset["BuildDefinition"] = build_def
    state = pt.get("asset", {}).get("state")
    if state is not None:
        asset["State"] = state
    bp = pt.get("asset", {}).get("buildProfiles")
    if bp:
        asset["BuildProfiles"] = bp

    # ---- Root metadata ----
    template_root = pt.get("metadata_template_root") or OrderedDict([
        ("Name", ""), ("Description", ""), ("Version", ""), ("Owner", ""),
        ("DateCreated", ""), ("DateLastBuilt", ""), ("DateLastSaved", ""),
        ("ModifiedVersion", ""),
    ])
    root_metadata = OrderedDict()
    for k, v in template_root.items():
        overlay = fm_metadata_overlay.get(k)
        root_metadata[k] = overlay if overlay is not None else v

    # Build root respecting the captured key order from passthrough (shipping
    # examples use Assets, Id, Branch, Metadata; UI-saved files may also include
    # Macros). Gaea rejects mismatched root shape with "File is corrupt or
    # missing additional data", so any key that was in the source must come back.
    handled = OrderedDict([
        ("Assets",   OrderedDict([("$values", [asset])])),
        ("Id",       fm.get("file_id", "")),
        ("Branch",   fm.get("branch", 1)),
        ("Metadata", root_metadata),
    ])
    root_extras = pt.get("root_extras") or OrderedDict()
    root_key_order = pt.get("root_key_order") or ["Assets", "Id", "Branch", "Metadata"]

    root = OrderedDict()
    for k in root_key_order:
        if k in handled:
            root[k] = handled[k]
        elif k in root_extras:
            root[k] = root_extras[k]
    # Any handled key the source somehow omitted still has to be emitted, or
    # Gaea will choke. Append at the end.
    for k, v in handled.items():
        if k not in root:
            root[k] = v

    # ---- Renumber $id throughout, then fix Parent backrefs and any $ref values ----
    # fixup_refs must run BEFORE fixup_port_parents: passthrough-embedded $ids (e.g.
    # Range) seed the remap, and a Port.Parent $ref written to the node's new $id
    # could collide with a remap key and get rewritten to point at the Range Float2.
    ig = IdGenerator()
    remap = {}
    reassign_ids(root, ig, remap)
    fixup_refs(root, remap)
    fixup_port_parents(root)

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(root, f, indent=2, ensure_ascii=False)

    real_nodes = [n for k, n in nodes_obj.items() if not k.startswith("$")]
    edge_count = sum(1 for n in real_nodes
                     for p in n["Ports"]["$values"]
                     if "Record" in p)
    real_notes = sum(1 for k in notes_obj if not k.startswith("$"))
    print(f"Wrote {output_path}")
    print(f"Nodes: {len(real_nodes)}  Edges: {edge_count}  Notes: {real_notes}")


def main():
    ap = argparse.ArgumentParser(description="Convert editable Markdown back to .terrain JSON.")
    ap.add_argument("input", help="Path to a .md file (e.g. Temp/Foo.md)")
    ap.add_argument("--output", help="Where to write the .terrain (default: <input>.terrain in same dir)")
    args = ap.parse_args()

    md_path = args.input
    base, _ = os.path.splitext(md_path)
    pt_path = base + ".passthrough.json"
    output_path = args.output or (base + ".terrain")

    if not os.path.isfile(md_path):
        print(f"ERROR: input not found: {md_path}", file=sys.stderr)
        sys.exit(2)
    if not os.path.isfile(pt_path):
        print(f"ERROR: sidecar not found: {pt_path}", file=sys.stderr)
        sys.exit(2)

    build_terrain(md_path, pt_path, output_path)


if __name__ == "__main__":
    main()
