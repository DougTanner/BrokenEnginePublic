#!/usr/bin/env python3
"""validate_markdown.py - Check a loaded Gaea 2 Markdown view for graph defects.

Usage: python validate_markdown.py <Temp/name.md>

Reads <name>.md and its sibling <name>.passthrough.json and prints one compact
JSON envelope on stdout. Reports only: this script never creates or modifies a
file. Diagnostics go to stderr.

Exit codes: 0 pass (with or without reported entries), 1 error, 2 blocked.
"""

import json
import os
import re
import sys

SCHEMA_VERSION = "broken-engine-gaea2-markdown/v1"

# Output caps, modelled on .agents/skills/update-vcxproj/scripts/Test-VcxprojPair.ps1.
MAXIMUM_ENTRIES = 64
MAXIMUM_MESSAGE_LENGTH = 256
# Invoke-Gaea2Python.ps1 cuts a child stream at 8192 characters, which would leave
# this envelope unparseable; stay below that even when every entry is at its cap.
# json.dumps escapes non-ASCII, so the serialized length is also its byte length.
MAXIMUM_ENVELOPE_CHARACTERS = 7168

# The three type lists below are owned by
# .agents/skills/gaea2-shared/references/node-conventions.md; update them
# there first, then mirror the change here.
VERSION_2_TYPES = ("Mixer", "TextureBase", "Trees", "Cellular3D", "ThermalShaper")
DYNAMIC_PORT_TYPES = ("Combine", "Mixer")
REQUIRED_INPUT_TYPES = (
    "Slope", "Height", "SatMap", "TextureBase", "HSL", "Tint", "Curve", "Clamp",
    "Autolevel", "Adjust", "Combine", "Mixer", "Transform", "Erosion2", "Thermal2",
    "Sea", "Snowfield", "Snow", "Warp", "LightX", "Sandstone",
)

# Gaea's lowest legal node Id; below this the node loads deactivated.
MINIMUM_NODE_ID = 100


class Blocked(Exception):
    """Structured refusal: the input could not be read or parsed at all."""

    def __init__(self, code, message):
        super().__init__(message)
        self.code = code
        self.message = message


def entry(node_id, rule, message):
    return {"nodeId": node_id, "rule": rule, "message": message}


def read_text(path, code):
    try:
        with open(path, "r", encoding="utf-8-sig") as f:
            return f.read()
    except OSError as e:
        raise Blocked(code, f"{path}: {e.strerror}")


def strip_frontmatter(md_text):
    if not md_text.startswith("---"):
        return md_text
    end = md_text.find("\n---", 3)
    if end < 0:
        return md_text
    return md_text[end + 4:].lstrip("\n")


def parse_mermaid(body):
    """Return (declared node ids in order, id -> type, edges).

    Mirrors the emitter in load_terrain.py (build_mermaid) and the edge parser in
    save_terrain.py, including the U+2192 port separator.
    """
    m = re.search(r"```mermaid\n(.*?)\n```", body, re.DOTALL)
    if not m:
        raise Blocked("markdown.structure-unparsed", "No ```mermaid topology block found.")
    ids = []
    types = {}
    edges = []
    for line in m.group(1).splitlines():
        line = line.strip()
        decl = re.match(r'^n(\d+)\["(.*)"\]$', line)
        if decl:
            nid = int(decl.group(1))
            ids.append(nid)
            types.setdefault(nid, decl.group(2))
            continue
        labelled = re.match(r'^n(\d+)\s+--\s+"([^"]+)"\s+-->\s+n(\d+)', line)
        if labelled:
            parts = labelled.group(2).split("→")
            edges.append({
                "from_id": int(labelled.group(1)),
                "from_port": parts[0].strip() if len(parts) >= 1 else "Out",
                "to_port": parts[1].strip() if len(parts) >= 2 else "In",
                "to_id": int(labelled.group(3)),
            })
            continue
        plain = re.match(r"^n(\d+)\s+-->\s+n(\d+)", line)
        if plain:
            edges.append({
                "from_id": int(plain.group(1)),
                "from_port": "Out",
                "to_port": "In",
                "to_id": int(plain.group(2)),
            })
    return ids, types, edges


def parse_sections(body):
    """Return (section ids in order, id -> {type, properties}).

    Mirrors save_terrain.py's parse_node_sections, but keeps the declaration
    order so duplicate ids stay visible instead of overwriting each other.
    """
    m = re.search(r"##\s*Nodes\s*\n(.*?)(?=\n##\s|\Z)", body, re.DOTALL)
    if not m:
        raise Blocked("markdown.structure-unparsed", "No '## Nodes' section found.")
    chunks = re.split(r"(?m)^###\s+n(\d+):\s*([^\n]+?)\s*$", m.group(1))
    ids = []
    sections = {}
    for i in range(1, len(chunks), 3):
        nid = int(chunks[i])
        ids.append(nid)
        if nid in sections:
            continue
        properties = {}
        for line in (chunks[i + 2] if i + 2 < len(chunks) else "").splitlines():
            prop = re.match(r"^([^:]+):\s*(.*)$", line.rstrip())
            if prop:
                properties[prop.group(1).strip()] = prop.group(2).strip()
        sections[nid] = {"type": chunks[i + 1].strip(), "properties": properties}
    return ids, sections


def load_passthrough(path):
    text = read_text(path, "passthrough.unreadable")
    try:
        data = json.loads(text)
    except json.JSONDecodeError as e:
        raise Blocked("passthrough.unreadable", f"{path} is not valid JSON: {e}")
    nodes = data.get("nodes")
    if not isinstance(nodes, dict):
        raise Blocked("passthrough.unreadable", f"{path} has no 'nodes' object.")
    return nodes


def check_mermaid_section_agreement(graph):
    entries = []
    for nid in sorted(set(graph["mermaid_ids"]) - set(graph["section_ids"])):
        entries.append(entry(nid, "mermaid-node-without-section",
                             f"Mermaid declares n{nid} but no '### n{nid}:' section exists."))
    for nid in sorted(set(graph["section_ids"]) - set(graph["mermaid_ids"])):
        entries.append(entry(nid, "section-without-mermaid-node",
                             f"Section '### n{nid}:' has no n{nid} node in the Mermaid block."))
    return entries


def check_duplicate_ids(graph):
    entries = []
    for name, ids in (("section", graph["section_ids"]), ("Mermaid", graph["mermaid_ids"])):
        seen = set()
        for nid in ids:
            if nid in seen:
                continue
            seen.add(nid)
            count = ids.count(nid)
            if count > 1:
                entries.append(entry(nid, "duplicate-node-id",
                                     f"Node id {nid} is declared {count} times in the {name} list."))
    return entries


def check_new_node_id_range(graph):
    entries = []
    for nid in sorted(graph["new_node_ids"]):
        if nid < MINIMUM_NODE_ID:
            entries.append(entry(nid, "new-node-id-below-100",
                                 f"Added node n{nid} has an id below {MINIMUM_NODE_ID}; Gaea loads it deactivated."))
    return entries


def check_edge_endpoints(graph):
    entries = []
    known = set(graph["section_ids"]) | set(graph["mermaid_ids"])
    for e in graph["edges"]:
        for role, nid in (("source", e["from_id"]), ("target", e["to_id"])):
            if nid not in known:
                entries.append(entry(nid, "edge-endpoint-missing",
                                     f"Edge n{e['from_id']} --> n{e['to_id']} names {role} node n{nid}, which does not exist."))
    return entries


def check_required_version(graph):
    entries = []
    for nid in graph["ordered_section_ids"]:
        section = graph["sections"][nid]
        if section["type"] not in VERSION_2_TYPES:
            continue
        if section["properties"].get("Version") != "2":
            entries.append(entry(nid, "missing-version-2",
                                 f"{section['type']} node n{nid} needs 'Version: 2'; without it Gaea loads it deactivated."))
    return entries


def check_port_count(graph):
    entries = []
    for nid in graph["ordered_section_ids"]:
        section = graph["sections"][nid]
        if section["type"] not in DYNAMIC_PORT_TYPES:
            continue
        if "PortCount" not in section["properties"]:
            entries.append(entry(nid, "missing-port-count",
                                 f"{section['type']} node n{nid} needs 'PortCount: N'; extra wired inputs silently drop without it."))
    return entries


def required_input_ports(graph, nid):
    """Required input port names: sidecar port catalogue, or the type list for new nodes."""
    passthrough = graph["passthrough_nodes"].get(str(nid))
    if passthrough is not None:
        return [p.get("Name") for p in passthrough.get("ports", [])
                if isinstance(p, dict) and "Required" in str(p.get("Type", ""))]
    return ["In"] if graph["sections"][nid]["type"] in REQUIRED_INPUT_TYPES else []


def check_required_inputs_wired(graph):
    entries = []
    wired = {(e["to_id"], e["to_port"]) for e in graph["edges"]}
    for nid in graph["ordered_section_ids"]:
        for port in required_input_ports(graph, nid):
            if (nid, port) not in wired:
                entries.append(entry(nid, "required-input-unwired",
                                     f"{graph['sections'][nid]['type']} node n{nid} has required input port '{port}' unwired; it loads deactivated."))
    return entries


def check_multi_producer_inputs(graph):
    entries = []
    producers = {}
    for e in graph["edges"]:
        producers.setdefault((e["to_id"], e["to_port"]), []).append(e["from_id"])
    for (nid, port), sources in sorted(producers.items()):
        if len(sources) > 1:
            listed = ", ".join(f"n{s}" for s in sources)
            entries.append(entry(nid, "input-port-multiple-producers",
                                 f"Input port '{port}' on n{nid} is fed by {len(sources)} producers ({listed}); Gaea allows one."))
    return entries


def check_reaches_output(graph):
    """Flag nodes with no downstream Export / SaveDefinition anchor.

    Shipping example files define no anchor at all (no Export node, no
    SaveDefinition); flagging every node there would be noise, so the rule only
    runs once the graph has at least one anchor to reach.
    """
    anchors = set()
    for nid in graph["ordered_section_ids"]:
        section = graph["sections"][nid]
        if section["type"] == "Export" or "SaveDefinition" in section["properties"]:
            anchors.add(nid)
    if not anchors:
        return []

    downstream = {}
    for e in graph["edges"]:
        downstream.setdefault(e["from_id"], []).append(e["to_id"])

    entries = []
    for nid in graph["ordered_section_ids"]:
        if graph["sections"][nid]["properties"].get("Marked") == "true":
            continue
        reached = False
        seen = {nid}
        stack = [nid]
        while stack and not reached:
            current = stack.pop()
            if current in anchors:
                reached = True
                break
            for nxt in downstream.get(current, []):
                if nxt not in seen:
                    seen.add(nxt)
                    stack.append(nxt)
        if not reached:
            entries.append(entry(nid, "no-path-to-output",
                                 f"Node n{nid} has no downstream path to an Export or SaveDefinition node and is not Marked; it will not bake."))
    return entries


CHECKS = (
    check_mermaid_section_agreement,
    check_duplicate_ids,
    check_new_node_id_range,
    check_edge_endpoints,
    check_required_version,
    check_port_count,
    check_required_inputs_wired,
    check_multi_producer_inputs,
    check_reaches_output,
)


def build_graph(markdown_path, passthrough_path):
    body = strip_frontmatter(read_text(markdown_path, "markdown.unreadable"))
    mermaid_ids, mermaid_types, edges = parse_mermaid(body)
    section_ids, sections = parse_sections(body)
    passthrough_nodes = load_passthrough(passthrough_path)
    ordered_section_ids = []
    for nid in section_ids:
        if nid not in ordered_section_ids:
            ordered_section_ids.append(nid)
    return {
        "mermaid_ids": mermaid_ids,
        "mermaid_types": mermaid_types,
        "edges": edges,
        "section_ids": section_ids,
        "ordered_section_ids": ordered_section_ids,
        "sections": sections,
        "passthrough_nodes": passthrough_nodes,
        "new_node_ids": {nid for nid in ordered_section_ids if str(nid) not in passthrough_nodes},
    }


def main(argv):
    result = {
        "schemaVersion": SCHEMA_VERSION,
        "status": "error",
        "code": "internal.error",
        "message": "Markdown validation did not run.",
        "payload": {
            "markdownPath": None,
            "passthroughPath": None,
            "nodeCount": 0,
            "edgeCount": 0,
            "entryCount": 0,
            "entriesTruncated": False,
            "entries": [],
        },
    }

    def complete(exit_code, status, code, message):
        result["status"] = status
        result["code"] = code
        result["message"] = message
        # entryCount keeps the true total; only the reported list gives way.
        entries = result["payload"]["entries"]
        text = json.dumps(result, separators=(",", ":"))
        while len(text) > MAXIMUM_ENVELOPE_CHARACTERS and entries:
            entries.pop()
            result["payload"]["entriesTruncated"] = True
            text = json.dumps(result, separators=(",", ":"))
        sys.stdout.write(text)
        return exit_code

    try:
        if len(argv) != 1:
            return complete(2, "blocked", "input.invalid",
                            "Expected exactly one argument: the path to a loaded Gaea Markdown file.")
        markdown_path = os.path.abspath(argv[0])
        if not markdown_path.lower().endswith(".md"):
            return complete(2, "blocked", "input.invalid", f"Not a Markdown file: {markdown_path}")
        passthrough_path = markdown_path[:-3] + ".passthrough.json"
        result["payload"]["markdownPath"] = markdown_path
        result["payload"]["passthroughPath"] = passthrough_path
        if not os.path.isfile(markdown_path):
            return complete(2, "blocked", "markdown.missing", f"Markdown file not found: {markdown_path}")
        if not os.path.isfile(passthrough_path):
            return complete(2, "blocked", "passthrough.missing", f"Sidecar not found: {passthrough_path}")

        graph = build_graph(markdown_path, passthrough_path)
        entries = []
        for check in CHECKS:
            entries.extend(check(graph))

        result["payload"]["nodeCount"] = len(graph["ordered_section_ids"])
        result["payload"]["edgeCount"] = len(graph["edges"])
        result["payload"]["entryCount"] = len(entries)
        result["payload"]["entriesTruncated"] = len(entries) > MAXIMUM_ENTRIES
        for item in entries[:MAXIMUM_ENTRIES]:
            message = item["message"]
            item["messageTruncated"] = len(message) > MAXIMUM_MESSAGE_LENGTH
            item["message"] = message[:MAXIMUM_MESSAGE_LENGTH]
            result["payload"]["entries"].append(item)

        if entries:
            print(f"{len(entries)} validation entries for {markdown_path}", file=sys.stderr)
            return complete(0, "pass", "markdown.entries-reported",
                            f"Reported {len(entries)} validation entries.")
        return complete(0, "pass", "ok", "No validation entries.")
    except Blocked as e:
        print(e.message, file=sys.stderr)
        return complete(2, "blocked", e.code, e.message)
    except Exception as e:  # noqa: BLE001 - envelope contract: never exit without one
        print(repr(e), file=sys.stderr)
        return complete(1, "error", "internal.error", f"{type(e).__name__}: {e}")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
