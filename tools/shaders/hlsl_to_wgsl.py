"""Compiles every engine shader entry point to WGSL for the web build.

The engine's shaders are HLSL written for D3D12: bindless textures through
ResourceDescriptorHeap[], and frame/draw constants in unbounded ConstantBuffer
tables. The browser takes only WGSL and WebGPU has no bindless, so the same
source is compiled with -D BLK_WEB, which switches common_global.hlsli's binding
macros to ordinary bindings (see the note there). Then:

    1. dxc -spirv   HLSL -> SPIR-V, the same entry points and profiles
                    renderer_dx12.cpp compiles
    2. spirv-val    is the SPIR-V valid
    3. naga         SPIR-V -> WGSL, then restore_read_only_storage()

Whether Chrome's own compiler accepts the result, and builds a pipeline from it,
is checked by wgsl_browser_check.mjs from the report this writes.

    python tools/shaders/hlsl_to_wgsl.py [--out DIR]

Writes <out>/<shader>.<entry>.{spv,wgsl,log} and <out>/report.json, which lists
each entry point's bindings as the WGSL declares them.
"""

import argparse
import json
import os
import re
import subprocess

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHADER_DIR = os.path.join(REPO, "blk_engine", "assets", "shaders")

# The DXC GitHub release; the Windows SDK's dxc.exe is built without SPIR-V codegen.
DXC = r"C:\projects\dxc_2025_07_14\bin\x64\dxc.exe"
SPIRV_VAL = r"C:\VulkanSDK\1.4.357.0\Bin\spirv-val.exe"
SPIRV_DIS = r"C:\VulkanSDK\1.4.357.0\Bin\spirv-dis.exe"
NAGA = os.path.join(os.path.expanduser("~"), ".cargo", "bin", "naga.exe")

# Mirrors the load_pipeline() calls in Renderer_Dx12 - file, entry point, profile.
GRAPHICS = ["static_model", "skinned_model", "sprite_particle", "mesh_particle", "directional_light",
	"point_light", "directional_shadow", "terrain", "gaussian_splat_draw"]
SHADOW_DEPTH = {"static_model", "skinned_model"}

ENTRIES = []
for name in GRAPHICS:
	ENTRIES.append((name, "vertex_shader", "vs_6_6"))
	ENTRIES.append((name, "pixel_shader", "ps_6_6"))
	if name in SHADOW_DEPTH:
		ENTRIES.append((name, "shadow_depth_ps", "ps_6_6"))
ENTRIES.append(("gaussian_splat_sort", "main", "cs_6_6"))

# HLSL numbers each register class separately (b1 and t1 can coexist); WebGPU
# has one binding number per group. Offset the classes apart in every space so a
# cbuffer and a texture never land on the same binding.
REGISTER_SHIFTS = ["-fvk-b-shift", "0", "all", "-fvk-t-shift", "16", "all",
	"-fvk-s-shift", "32", "all", "-fvk-u-shift", "48", "all"]

WEB_ARGS = ["-spirv", "-fspv-target-env=vulkan1.3", *REGISTER_SHIFTS, "-D", "BLK_WEB", "-O3"]


def run(cmd):
	p = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
	return p.returncode, (p.stdout + p.stderr).strip()


def first_error(text):
	for line in text.splitlines():
		if "error" in line.lower():
			return line.strip()
	return text.splitlines()[0].strip() if text else ""


def restore_read_only_storage(spv_path, wgsl_path):
	"""Puts back the read-only access naga drops from storage buffers.

	DXC marks a StructuredBuffer read-only with a *member* decoration
	(`OpMemberDecorate %type 0 NonWritable`); naga only honours NonWritable on
	the variable, so it emits `var<storage, read_write>`. WebGPU rejects a
	writable storage buffer in a vertex shader outright, and elsewhere the
	binding loses its read-only guarantee. Returns the variables fixed.
	"""
	code, dis = run([SPIRV_DIS, spv_path])
	if code != 0:
		return []

	members = {}
	non_writable = {}
	for m in re.finditer(r"OpMemberDecorate %(\w+) (\d+) (\w+)", dis):
		members.setdefault(m.group(1), set()).add(m.group(2))
		if m.group(3) == "NonWritable":
			non_writable.setdefault(m.group(1), set()).add(m.group(2))
	read_only_types = {t for t, ms in non_writable.items() if ms == members[t]}

	pointer_to = {m.group(1): m.group(2) for m in re.finditer(r"%(\w+) = OpTypePointer StorageBuffer %(\w+)", dis)}
	read_only_vars = [m.group(1) for m in re.finditer(r"%(\w+) = OpVariable %(\w+) StorageBuffer", dis)
		if pointer_to.get(m.group(2)) in read_only_types]

	text = open(wgsl_path, encoding="utf-8").read()
	fixed = []
	for name in read_only_vars:
		pattern = rf"var<storage, read_write> {name}:"
		if pattern in text:
			text = text.replace(pattern, f"var<storage, read> {name}:")
			fixed.append(name)
	with open(wgsl_path, "w", encoding="utf-8") as fh:
		fh.write(text)
	return fixed


def wgsl_bindings(wgsl_path):
	"""The module's bindings as the WGSL declares them: "group.binding name: kind"."""
	text = open(wgsl_path, encoding="utf-8").read()
	found = re.findall(r"@group\((\d+)\) @binding\((\d+)\)\s*var(<[^>]*>)?\s+(\w+)\s*:\s*([\w<>, ]+);", text)
	return [f"{g}.{b} {name}: {(space or '').strip('<>') or kind}" for g, b, space, name, kind in
		sorted(found, key=lambda f: (int(f[0]), int(f[1])))]


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("--out", default=os.path.join(REPO, "build_wasm", "shader_spike"))
	args = ap.parse_args()
	os.makedirs(args.out, exist_ok=True)

	report = []
	for shader, entry, profile in ENTRIES:
		base = os.path.join(args.out, f"{shader}.{entry}")
		row = {"variant": "web", "shader": shader, "entry": entry, "profile": profile}
		log = []

		code, out = run([DXC, *WEB_ARGS, "-E", entry, "-T", profile, "-I", SHADER_DIR,
			os.path.join(SHADER_DIR, shader + ".hlsl"), "-Fo", base + ".spv"])
		log.append("== dxc -spirv ==\n" + out)
		row["dxc"] = "ok" if code == 0 else first_error(out)

		if code == 0:
			code, out = run([SPIRV_VAL, "--target-env", "vulkan1.3", base + ".spv"])
			log.append("== spirv-val ==\n" + out)
			row["spirv_val"] = "ok" if code == 0 else first_error(out)

			code, out = run([NAGA, base + ".spv", base + ".wgsl"])
			log.append("== naga ==\n" + out)
			row["naga"] = "ok" if code == 0 else first_error(out)
			if code == 0:
				row["wgsl"] = base + ".wgsl"
				row["read_only_restored"] = restore_read_only_storage(base + ".spv", base + ".wgsl")
				row["bindings"] = wgsl_bindings(base + ".wgsl")

		with open(base + ".log", "w", encoding="utf-8") as fh:
			fh.write("\n\n".join(log) + "\n")
		report.append(row)

	with open(os.path.join(args.out, "report.json"), "w", encoding="utf-8") as fh:
		json.dump(report, fh, indent=1)

	cols = ["dxc", "spirv_val", "naga"]
	print(f"{'shader.entry':40} " + " ".join(f"{c:10}" for c in cols) + "bindings")
	for r in report:
		cells = " ".join(f"{'ok' if r.get(c) == 'ok' else ('-' if c not in r else 'FAIL'):10}" for c in cols)
		print(f"{r['shader'] + '.' + r['entry']:40} {cells}{', '.join(r.get('bindings', []))}")
	print("  " + ", ".join(f"{c} {sum(1 for r in report if r.get(c) == 'ok')}/{sum(1 for r in report if c in r)}" for c in cols))


if __name__ == "__main__":
	main()
