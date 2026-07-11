#!/usr/bin/env python3
"""load_tor.py - Convert a Gaea 1 .tor file to a read-only Markdown view.

Gaea 1 .tor wire format:
  1. ASCII text, single line: base64-encoded payload
  2. After base64 decode: [4-byte prefix] + gzip stream
     The 4-byte prefix is little-endian uint32 == decompressed payload size.
     (The trailing FA 4A 05 00 you may notice is NOT a separate suffix —
     it's the gzip footer's ISIZE field, which equals the decompressed size
     and happens to match the prefix.)
  3. After gunzip: UTF-8 XML with root <Terrain>.

XML shape:
  <Terrain ...metadata attrs...>
    <Definition>     <Parameter Name="Height" Value="..."/> ...
    <Resolution Working="..." Final="..."/>
    <Layers>         <Node xsi:type="Mountain" Id="..." ...>...</Node> ...
    <GraphData>
      <LayerIDs>     <string>NODE-ID</string> ...
      <Shapes>       <string>NODE-ID|PosX|PosY|0</string> ...
      <Connections>  <string>FROM-ID|TO-ID|FROM-PORT|TO-PORT||0</string> ...
      <Notes>...</Notes>
      <Containers>...</Containers>

Usage: python load_tor.py <input.tor> [--output-dir Temp]

Output (read-only — no round-trip sidecar):
  <output-dir>/<basename>.md  - frontmatter + Mermaid topology + per-node properties
"""

import argparse
import base64
import gzip
import os
import sys
import xml.etree.ElementTree as ET
from collections import OrderedDict

XSI_TYPE = "{http://www.w3.org/2001/XMLSchema-instance}type"


def decode_tor(path):
    with open(path, "rb") as f:
        raw = f.read()
    # Files are pure base64 with no whitespace, but tolerate stray newlines.
    decoded = base64.b64decode(raw, validate=False)
    if len(decoded) < 8:
        sys.exit(f"ERROR: {path} too short to be a .tor wrapper ({len(decoded)} bytes)")
    body = decoded[4:]
    if body[:2] != b"\x1f\x8b":
        sys.exit("ERROR: payload is not gzip (expected 0x1F 0x8B after 4-byte prefix)")
    return gzip.decompress(body)


def parse_terrain_xml(xml_bytes):
    return ET.fromstring(xml_bytes)


def fmt_kv_block(d):
    return "\n".join(f"{k}: {v}" for k, v in d.items() if v not in (None, ""))


def sanitize_label(s):
    return s.replace('"', "'").replace("\n", " ").strip() or "?"


def parse_shape_entry(text):
    # "NODE-ID|PosX|PosY|0"
    if not text:
        return None, None, None
    parts = text.split("|")
    if len(parts) < 3:
        return parts[0], None, None
    try:
        return parts[0], float(parts[1]), float(parts[2])
    except ValueError:
        return parts[0], None, None


def parse_connection_entry(text):
    # "FROM-ID|TO-ID|FROM-PORT|TO-PORT||0"
    if not text:
        return None
    parts = text.split("|")
    if len(parts) < 4:
        return None
    return {
        "from_id": parts[0],
        "to_id": parts[1],
        "from_port": parts[2],
        "to_port": parts[3],
    }


def extract_parameter(p):
    """Map a <Parameter xsi:type=PFoo Name=... Value=...> to (name, value)."""
    name = p.attrib.get("Name", "")
    if not name:
        return None
    ptype = p.attrib.get(XSI_TYPE, "")
    if ptype == "PTitle":
        return None  # PTitle entries are just section headers in Gaea's UI.
    value = p.attrib.get("Value")
    if value is None:
        # Some parameters store values in child elements; fall back to text.
        value = (p.text or "").strip() or None
    return (name, ptype, value)


def extract_node(node_elem, positions):
    nid = node_elem.attrib.get("Id", "")
    node_type = node_elem.attrib.get(XSI_TYPE, "Node")
    display = node_elem.attrib.get("DisplayName", node_type)
    node_category = node_elem.attrib.get("NodeType", "")
    is_layer = node_elem.attrib.get("IsLayer", "false") == "true"
    is_bypassed = node_elem.attrib.get("IsBypassed", "false") == "true"

    params_elem = node_elem.find("Parameters")
    params = []
    if params_elem is not None:
        for p in params_elem.findall("Parameter"):
            entry = extract_parameter(p)
            if entry:
                params.append(entry)

    mask_params = []
    mask_elem = node_elem.find("MaskParams")
    if mask_elem is not None:
        for p in mask_elem.findall("Parameter"):
            entry = extract_parameter(p)
            if entry is None:
                continue
            # Skip false bools / zero numerics to cut noise — they're defaults.
            name, ptype, value = entry
            if ptype == "PBoolean" and value == "false":
                continue
            if ptype in ("PDouble", "PInt") and value in ("0", "0.0"):
                continue
            mask_params.append(entry)

    ports = []
    ports_elem = node_elem.find("Ports")
    if ports_elem is not None:
        for port in ports_elem.findall("Port"):
            ports.append(port.attrib.get("Name", "?"))

    pos = positions.get(nid, (None, None))
    return OrderedDict([
        ("id", nid),
        ("type", node_type),
        ("display", display),
        ("category", node_category),
        ("is_layer", is_layer),
        ("is_bypassed", is_bypassed),
        ("position", pos),
        ("ports", ports),
        ("parameters", params),
        ("mask_parameters", mask_params),
    ])


def build_mermaid(nodes, edges):
    lines = ["```mermaid", "flowchart TD"]
    short_id = {n["id"]: f"n{i}" for i, n in enumerate(nodes)}
    for n in nodes:
        label_main = sanitize_label(n["display"] or n["type"])
        # Show "DisplayName (Type)" only when they differ (Mountain vs Transform-Placer etc.)
        if n["display"] and n["display"] != n["type"]:
            label = f"{label_main}<br/><i>{sanitize_label(n['type'])}</i>"
        else:
            label = label_main
        marker = ":::bypassed" if n["is_bypassed"] else ""
        lines.append(f'  {short_id[n["id"]]}["{label}"]{marker}')
    for e in edges:
        f = short_id.get(e["from_id"])
        t = short_id.get(e["to_id"])
        if not f or not t:
            continue  # Connection references an unknown node; skip silently.
        if e["from_port"] == "Output" and e["to_port"] == "Input":
            lines.append(f"  {f} --> {t}")
        else:
            label = f'{e["from_port"]} → {e["to_port"]}'
            lines.append(f'  {f} -- "{label}" --> {t}')
    lines.append("classDef bypassed stroke-dasharray: 5 5,opacity:0.6;")
    lines.append("```")
    return "\n".join(lines)


def emit_node_section(n):
    lines = [f"### {sanitize_label(n['display'] or n['type'])}  `{n['id']}`"]
    lines.append(f"type: {n['type']}")
    if n["category"]:
        lines.append(f"category: {n['category']}")
    if n["is_layer"]:
        lines.append("is_layer: true")
    if n["is_bypassed"]:
        lines.append("is_bypassed: true")
    px, py = n["position"]
    if px is not None and py is not None:
        lines.append(f"position: {px:.3f}, {py:.3f}")
    if n["ports"]:
        lines.append(f"ports: {', '.join(n['ports'])}")
    if n["parameters"]:
        lines.append("")
        lines.append("**Parameters:**")
        for name, ptype, value in n["parameters"]:
            lines.append(f"- {name} ({ptype}): {value}")
    if n["mask_parameters"]:
        lines.append("")
        lines.append("**Mask / Post-process (non-default only):**")
        for name, ptype, value in n["mask_parameters"]:
            lines.append(f"- {name} ({ptype}): {value}")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description="Convert a Gaea 1 .tor to a Markdown view.")
    ap.add_argument("input", help="Path to a Gaea 1 .tor file")
    ap.add_argument("--output-dir", default="Temp", help="Where to write the .md (default Temp/)")
    args = ap.parse_args()

    if not os.path.isfile(args.input):
        sys.exit(f"ERROR: input not found: {args.input}")

    xml_bytes = decode_tor(args.input)
    root = parse_terrain_xml(xml_bytes)
    if root.tag != "Terrain":
        sys.exit(f"ERROR: expected <Terrain> root, got <{root.tag}>")

    # Frontmatter from <Terrain> attributes and <Definition>/<Resolution>.
    fm = OrderedDict()
    for k in ("Id", "Name", "Workflow"):
        if k in root.attrib:
            fm[k.lower()] = root.attrib[k]
    resolution = root.find("Resolution")
    if resolution is not None:
        for k, v in resolution.attrib.items():
            fm[f"resolution_{k.lower()}"] = v
    definition = root.find("Definition")
    if definition is not None:
        for p in definition.findall("Parameter"):
            entry = extract_parameter(p)
            if entry:
                name, _, value = entry
                fm[f"def_{name.lower()}"] = value

    # Positions: GraphData/Shapes "NODE-ID|X|Y|0"
    gd = root.find("GraphData")
    positions = {}
    edges = []
    if gd is not None:
        shapes = gd.find("Shapes")
        if shapes is not None:
            for s in shapes.findall("string"):
                nid, x, y = parse_shape_entry(s.text)
                if nid:
                    positions[nid] = (x, y)
        conns = gd.find("Connections")
        if conns is not None:
            for c in conns.findall("string"):
                edge = parse_connection_entry(c.text)
                if edge:
                    edges.append(edge)

    layers = root.find("Layers")
    nodes = []
    if layers is not None:
        for n in layers.findall("Node"):
            nodes.append(extract_node(n, positions))

    base = os.path.splitext(os.path.basename(args.input))[0]
    out = []
    out.append("---")
    out.append(fmt_kv_block(fm))
    out.append("---")
    out.append("")
    out.append(f"# {base}")
    out.append("")
    out.append(f"_{len(nodes)} nodes · {len(edges)} edges_")
    out.append("")
    out.append("## Topology")
    out.append("")
    out.append(build_mermaid(nodes, edges))
    out.append("")
    out.append("## Nodes")
    out.append("")
    for n in nodes:
        out.append(emit_node_section(n))
        out.append("")

    os.makedirs(args.output_dir, exist_ok=True)
    md_path = os.path.join(args.output_dir, f"{base}.md")
    with open(md_path, "w", encoding="utf-8") as f:
        f.write("\n".join(out))

    print(f"Wrote {md_path}")
    print(f"Nodes: {len(nodes)}  Edges: {len(edges)}")


if __name__ == "__main__":
    main()
