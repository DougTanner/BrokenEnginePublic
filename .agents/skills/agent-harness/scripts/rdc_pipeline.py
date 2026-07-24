"""Report bound pipeline state at one event in a RenderDoc capture.

  RDC_ARGS='<capture.rdc> --event N [--out FILE]' qrenderdoc --python rdc_pipeline.py

Sets the frame event, then reports bound shaders, viewport/scissor, depth/blend,
color/depth targets (format + dimensions via GetTextures()), and vertex/index
bindings. Use an eventId from rdc_summary.py. See references/renderdoc.md.
"""

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else os.getcwd()
if _HERE not in sys.path:
	sys.path.insert(0, _HERE)

import argparse
import rdc_common
from rdc_common import rd, open_capture, emit


def _stages():
	stage = rd.ShaderStage
	return [
		(stage.Vertex, "vertex"), (stage.Hull, "hull"), (stage.Domain, "domain"),
		(stage.Geometry, "geometry"), (stage.Pixel, "pixel"), (stage.Compute, "compute")]


def _shaders(state, lines):
	lines.append("shaders:")
	null_id = rd.ResourceId.Null()
	found = False
	for stage, label in _stages():
		if state.GetShader(stage) == null_id:
			continue
		found = True
		lines.append("  %s: %s (%s)" % (label, state.GetShaderEntryPoint(stage), state.GetShader(stage)))
	if not found:
		lines.append("  (none bound)")


def _rasterizer(state, controller, lines):
	viewport = state.GetViewport(0)
	scissor = state.GetScissor(0)
	lines.append("viewport0: x=%g y=%g w=%g h=%g minDepth=%g maxDepth=%g" % (
		viewport.x, viewport.y, viewport.width, viewport.height, viewport.minDepth, viewport.maxDepth))
	lines.append("scissor0: x=%d y=%d w=%d h=%d" % (scissor.x, scissor.y, scissor.width, scissor.height))
	# Depth/blend detail lives on the Vulkan-specific state; field names drift across
	# RenderDoc versions, so read them defensively rather than asserting a layout.
	vulkan = controller.GetVulkanPipelineState()
	depth_stencil = getattr(vulkan, "depthStencil", None)
	if depth_stencil is not None:
		lines.append("depth: test=%s write=%s func=%s" % (
			getattr(depth_stencil, "depthTestEnable", "?"),
			getattr(depth_stencil, "depthWriteEnable", "?"),
			getattr(depth_stencil, "depthFunction", "?")))
	color_blend = getattr(vulkan, "colorBlend", None)
	blends = getattr(color_blend, "blends", None) if color_blend is not None else None
	if blends:
		lines.append("blend0: enabled=%s" % getattr(blends[0], "enabled", "?"))


def _target_line(bound, textures, prefix):
	resource_id = bound.resource
	if resource_id == rd.ResourceId.Null():
		return prefix + "(none)"
	texture = textures.get(resource_id)
	if texture is None:
		return prefix + str(resource_id)
	return "%s%s %s %dx%d" % (prefix, resource_id, texture.format.Name(), texture.width, texture.height)


def _targets(state, textures, lines):
	lines.append("color targets:")
	color = state.GetOutputTargets()
	for index in range(len(color)):
		lines.append(_target_line(color[index], textures, "  [%d] " % index))
	if len(color) == 0:
		lines.append("  (none)")
	lines.append("depth target:")
	lines.append(_target_line(state.GetDepthTarget(), textures, "  "))


def _geometry(state, lines):
	lines.append("vertex buffers:")
	vertex_buffers = state.GetVBuffers()
	bound_any = False
	for index in range(len(vertex_buffers)):
		vbuffer = vertex_buffers[index]
		if vbuffer.resourceId == rd.ResourceId.Null():
			continue
		bound_any = True
		lines.append("  [%d] %s offset=%d stride=%d" % (
			index, vbuffer.resourceId, vbuffer.byteOffset, vbuffer.byteStride))
	if not bound_any:
		lines.append("  (none)")
	index_buffer = state.GetIBuffer()
	if index_buffer.resourceId != rd.ResourceId.Null():
		lines.append("index buffer: %s offset=%d" % (index_buffer.resourceId, index_buffer.byteOffset))
	else:
		lines.append("index buffer: (none)")


def main():
	# Explicit prog: the embedded qrenderdoc interpreter has no sys.argv for argparse to read.
	parser = argparse.ArgumentParser(prog="rdc_pipeline", description="Report pipeline state at one capture event.")
	parser.add_argument("capture", help="path to a .rdc capture file")
	parser.add_argument("--event", type=int, required=True, help="eventId (from rdc_summary.py)")
	parser.add_argument("--out", help="results file (default <capture>.pipeline_evN.txt)")
	args = rdc_common.parse_args(parser)

	out_path = args.out or (args.capture + ".pipeline_ev%d.txt" % args.event)
	lines = ["event %d" % args.event]
	with open_capture(args.capture) as controller:
		controller.SetFrameEvent(args.event, True)
		state = controller.GetPipelineState()
		textures = dict((texture.resourceId, texture) for texture in controller.GetTextures())
		_shaders(state, lines)
		_rasterizer(state, controller, lines)
		_targets(state, textures, lines)
		_geometry(state, lines)
	emit("\n".join(lines), out_path)


if __name__ == "__main__":
	rdc_common.run(main, "rdc_pipeline")
