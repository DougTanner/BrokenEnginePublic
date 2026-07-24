"""Dump a RenderDoc capture's action tree — the default triage tool.

  RDC_ARGS='<capture.rdc> [--out FILE]' qrenderdoc --python rdc_summary.py

Recurses GetRootActions() and prints one line per action (eventId, kind, name, and
draw/dispatch params) plus a per-kind totals footer. See references/renderdoc.md.
"""

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else os.getcwd()
if _HERE not in sys.path:
	sys.path.insert(0, _HERE)

import argparse
import rdc_common
from rdc_common import rd, open_capture, emit


def _kind(flags):
	flag = rd.ActionFlags
	if flags & flag.Dispatch:
		return "Dispatch"
	if flags & flag.Drawcall:
		return "Draw"
	if flags & flag.Clear:
		return "Clear"
	if flags & (flag.Copy | flag.Resolve):
		return "Copy"
	if flags & (flag.PushMarker | flag.SetMarker | flag.PopMarker):
		return "Marker"
	return "Action"


def _name(action, structured_file):
	get_name = getattr(action, "GetName", None)
	if callable(get_name):
		try:
			return get_name(structured_file)
		except TypeError:
			pass
	return getattr(action, "customName", None) or getattr(action, "name", "")


def _walk(actions, structured_file, lines, counts, depth):
	flag = rd.ActionFlags
	for action in actions:
		kind = _kind(action.flags)
		counts[kind] = counts.get(kind, 0) + 1
		detail = ""
		if action.flags & flag.Dispatch:
			groups = action.dispatchDimension
			detail = " groups=%dx%dx%d" % (groups[0], groups[1], groups[2])
		elif action.flags & flag.Drawcall:
			detail = " indices=%d instances=%d" % (action.numIndices, action.numInstances)
		lines.append("%s[%d] %s %s%s" % (
			"  " * depth, action.eventId, kind, _name(action, structured_file), detail))
		_walk(action.children, structured_file, lines, counts, depth + 1)


def main():
	# Explicit prog: the embedded qrenderdoc interpreter has no sys.argv for argparse to read.
	parser = argparse.ArgumentParser(prog="rdc_summary", description="Dump a RenderDoc capture action tree.")
	parser.add_argument("capture", help="path to a .rdc capture file")
	parser.add_argument("--out", help="results file (default <capture>.summary.txt)")
	args = rdc_common.parse_args(parser)

	out_path = args.out or (args.capture + ".summary.txt")
	lines = []
	counts = {}
	with open_capture(args.capture) as controller:
		structured_file = controller.GetStructuredFile()
		_walk(controller.GetRootActions(), structured_file, lines, counts, 0)

	if counts:
		footer = "totals: " + ", ".join("%s=%d" % (k, counts[k]) for k in sorted(counts))
	else:
		footer = "totals: (no actions)"
	emit("\n".join(lines + ["", footer]), out_path)


if __name__ == "__main__":
	rdc_common.run(main, "rdc_summary")
