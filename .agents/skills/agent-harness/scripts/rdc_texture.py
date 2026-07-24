"""Save one bound render target as PNG from a RenderDoc capture event.

  RDC_ARGS='<capture.rdc> --event N [--target K | --depth] [--out PNG]' qrenderdoc --python rdc_texture.py

Sets the frame event, picks a color target (--target K, default 0) or the depth
target (--depth), and writes it via controller.SaveTexture. The PNG is the completion
contract; its absolute path is printed to stdout. Default out is
<capture>_evN_targetK.png (or _evN_depth.png). See references/renderdoc.md.
"""

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else os.getcwd()
if _HERE not in sys.path:
	sys.path.insert(0, _HERE)

import argparse
import rdc_common
from rdc_common import rd, open_capture


def _resolve_target(state, args):
	if args.depth:
		bound = state.GetDepthTarget()
		if bound.resource == rd.ResourceId.Null():
			raise RuntimeError("event %d has no depth target" % args.event)
		return bound.resource
	color = state.GetOutputTargets()
	if args.target < 0 or args.target >= len(color):
		raise RuntimeError("target %d out of range (%d color target(s))" % (args.target, len(color)))
	resource_id = color[args.target].resource
	if resource_id == rd.ResourceId.Null():
		raise RuntimeError("color target %d is unbound at event %d" % (args.target, args.event))
	return resource_id


def main():
	# Explicit prog: the embedded qrenderdoc interpreter has no sys.argv for argparse to read.
	parser = argparse.ArgumentParser(prog="rdc_texture", description="Save a capture render target as PNG.")
	parser.add_argument("capture", help="path to a .rdc capture file")
	parser.add_argument("--event", type=int, required=True, help="eventId (from rdc_summary.py)")
	group = parser.add_mutually_exclusive_group()
	group.add_argument("--target", type=int, default=0, help="color target index (default 0)")
	group.add_argument("--depth", action="store_true", help="save the depth target instead")
	parser.add_argument("--out", help="output PNG path (default <capture>_evN_targetK.png)")
	args = rdc_common.parse_args(parser)

	if args.depth:
		out_path = args.out or (args.capture + "_ev%d_depth.png" % args.event)
	else:
		out_path = args.out or (args.capture + "_ev%d_target%d.png" % (args.event, args.target))

	with open_capture(args.capture) as controller:
		controller.SetFrameEvent(args.event, True)
		resource_id = _resolve_target(controller.GetPipelineState(), args)
		save = rd.TextureSave()
		save.resourceId = resource_id
		save.mip = 0
		save.slice.sliceIndex = 0
		save.destType = rd.FileType.PNG
		if not controller.SaveTexture(save, out_path):
			raise RuntimeError("SaveTexture failed for %s -> '%s'" % (resource_id, out_path))

	absolute = os.path.abspath(out_path)
	sys.stdout.write(absolute + "\n")
	sys.stdout.flush()


if __name__ == "__main__":
	rdc_common.run(main, "rdc_texture")
