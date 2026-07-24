"""Shared RenderDoc replay loader for the agent-harness rdc_* analysis scripts.

Two run modes (design decision 4 of the RenderDoc capture plan):

  * Primary: ``qrenderdoc --python rdc_<x>.py <capture> ...``. The GUI process embeds
    Python 3.6; the ``renderdoc`` module is preloaded and replay is already
    initialised, so the scripts only open a capture file and drive the pure replay
    controller. qrenderdoc is a GUI process whose stdout is not reliably capturable,
    so results are also written to an ``--out`` file, which is the completion contract.
  * Fallback: a standalone interpreter whose version matches RenderDoc's bundled
    ``python3X.dll`` with the install's ``pymodules`` on ``sys.path``. The v1.44
    installer ships no ``pymodules``/``renderdoc.pyd``, so on this machine the
    standalone path exits 2 and names ``qrenderdoc --python`` as the required call.

Python 3.6 syntax only (qrenderdoc embeds 3.6): no walrus, no f-string '=' specifier,
no stdlib dataclasses. Plain classes and functions only.
"""

import contextlib
import glob
import os
import re
import shlex
import sys

# True when a RenderDoc host (qrenderdoc) already loaded the module and initialised
# replay; False when we are a standalone interpreter that must own initialise/shutdown.
HOST_EMBED = False


def _exit_needs_qrenderdoc(reason):
	sys.stderr.write(
		"rdc_common: %s.\n"
		"Standalone RenderDoc replay is unavailable here; run analysis through the GUI\n"
		"interpreter instead:\n"
		"  qrenderdoc --python <script> <capture> [args]\n" % reason)
	sys.exit(2)


def _find_install():
	# RENDERDOC_PATH overrides discovery; otherwise read the install dir from the Vulkan
	# loader's implicit-layer registration, matching InstanceManager::TryLoadRenderDocDll.
	override = os.environ.get("RENDERDOC_PATH")
	if override:
		return override
	try:
		import winreg
	except ImportError:
		return None
	try:
		key = winreg.OpenKey(
			winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Khronos\Vulkan\ImplicitLayers", 0, winreg.KEY_READ)
	except OSError:
		return None
	try:
		index = 0
		while True:
			try:
				name, _value, _type = winreg.EnumValue(key, index)
			except OSError:
				break
			index += 1
			if "renderdoc.json" in name.lower():
				return os.path.dirname(name)
	finally:
		winreg.CloseKey(key)
	return None


def _bundled_python_version(install):
	# The bundled interpreter (e.g. python36.dll -> (3, 6)) fixes the required standalone
	# version; python3.dll (the stable-ABI forwarder) carries no minor and is ignored.
	for path in glob.glob(os.path.join(install, "python3*.dll")):
		match = re.search(r"python(\d)(\d+)\.dll$", os.path.basename(path).lower())
		if match:
			return (int(match.group(1)), int(match.group(2)))
	return None


def _load_standalone():
	install = _find_install()
	if install is None:
		_exit_needs_qrenderdoc(
			"RenderDoc install directory not found (set RENDERDOC_PATH or register the "
			"Vulkan implicit layer)")
	pymodules = os.path.join(install, "pymodules")
	if not os.path.isfile(os.path.join(pymodules, "renderdoc.pyd")):
		_exit_needs_qrenderdoc("no pymodules/renderdoc.pyd under '%s'" % install)
	bundled = _bundled_python_version(install)
	if bundled is not None and bundled != sys.version_info[:2]:
		_exit_needs_qrenderdoc(
			"interpreter %d.%d does not match RenderDoc's bundled python%d%d" % (
				sys.version_info[0], sys.version_info[1], bundled[0], bundled[1]))
	if pymodules not in sys.path:
		sys.path.insert(0, pymodules)
	import renderdoc as module
	return module


def _load_renderdoc():
	global HOST_EMBED
	main_module = sys.modules.get("__main__", None)
	HOST_EMBED = ("renderdoc" in sys.modules) or hasattr(main_module, "pyrenderdoc")
	if HOST_EMBED:
		import renderdoc as module
		return module
	return _load_standalone()


# Imported once; a failed standalone load raises SystemExit(2) out of the importing script.
rd = _load_renderdoc()


def _result_ok(result):
	# RenderDoc's result type varies by version: newer returns a ResultDetails with OK();
	# older returns a ResultCode/ReplayStatus enum compared against Succeeded.
	ok_method = getattr(result, "OK", None)
	if callable(ok_method):
		return ok_method()
	for enum_name in ("ResultCode", "ReplayStatus"):
		succeeded = getattr(getattr(rd, enum_name, None), "Succeeded", None)
		if succeeded is not None:
			return result == succeeded
	return bool(result)


@contextlib.contextmanager
def open_capture(path):
	"""Open a .rdc file for local replay and yield the replay controller."""
	if not os.path.isfile(path):
		raise RuntimeError("capture not found: '%s'" % path)
	owns_replay = not HOST_EMBED
	if owns_replay:
		rd.InitialiseReplay(rd.GlobalEnvironment(), [])
	cap = None
	controller = None
	try:
		cap = rd.OpenCaptureFile()
		open_result = cap.OpenFile(path, "", None)
		if not _result_ok(open_result):
			raise RuntimeError("could not open '%s': %s" % (path, open_result))
		if cap.LocalReplaySupport() != rd.ReplaySupport.Supported:
			raise RuntimeError("capture cannot be replayed locally: '%s'" % path)
		open_result, controller = cap.OpenCapture(rd.ReplayOptions(), None)
		if not _result_ok(open_result):
			raise RuntimeError("could not initialise replay for '%s': %s" % (path, open_result))
		yield controller
	finally:
		if controller is not None:
			controller.Shutdown()
		if cap is not None:
			cap.Shutdown()
		if owns_replay:
			rd.ShutdownReplay()


def emit(text, out_path):
	"""Write results to out_path (the completion contract) and mirror to stdout."""
	body = text if text.endswith("\n") else text + "\n"
	with open(out_path, "w", encoding="utf-8") as handle:
		handle.write(body)
	sys.stdout.write(body)
	sys.stdout.flush()


def parse_args(parser):
	"""Parse CLI args, sourcing them from RDC_ARGS when the host embed strips sys.argv.

	qrenderdoc's embedded Python 3.6 exposes no sys.argv, so trailing '--python' arguments
	never reach the script; the PowerShell launcher forwards them through the RDC_ARGS
	environment variable (shlex-split). Standalone mode keeps the normal sys.argv path.
	"""
	argv = getattr(sys, "argv", None)
	if argv:
		return parser.parse_args(argv[1:])
	return parser.parse_args(shlex.split(os.environ.get("RDC_ARGS", "")))


def run(main_fn, prog):
	"""Invoke a script entry point, collapsing failures to a one-line reason + exit 1."""
	try:
		main_fn()
	except (SystemExit, KeyboardInterrupt):
		raise
	except Exception as exc:
		sys.stderr.write("%s: %s\n" % (prog, exc))
		sys.exit(1)
