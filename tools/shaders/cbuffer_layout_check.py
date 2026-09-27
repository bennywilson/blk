"""Checks whether the web shaders' constant buffers keep D3D12's memory layout.

A WebGPU backend can share the D3D12 backend's CPU-side constant packing only
if every member lands at the same byte offset in both. They can differ: after
a struct member, HLSL packs the next scalar into the struct's unused tail,
while the Vulkan rules round the struct up to 16 bytes first (`{float3} s;
float f;` puts f at 12 in HLSL, 16 in Vulkan). DXC's default Vulkan layout is
the relaxed one, so a plain `float; float3` does NOT diverge.

Compiles each entry point for the web (-D BLK_WEB) twice - DXC's default Vulkan
layout, and -fvk-use-dx-layout (exactly D3D12's packing) - and diffs every
member offset of every constant buffer and StructuredBuffer.

    python tools/shaders/cbuffer_layout_check.py
"""

import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hlsl_to_wgsl as h  # noqa: E402

OUT = os.path.join(h.REPO, "build_wasm", "shader_spike", "layout_check")


def member_offsets(spv):
	dis = subprocess.run([h.SPIRV_DIS, spv], capture_output=True, text=True).stdout
	names = {}
	for m in re.finditer(r'OpMemberName %(\w+) (\d+) "(\w+)"', dis):
		names[(m.group(1), m.group(2))] = m.group(3)
	# Constant buffers (Uniform) and StructuredBuffers (StorageBuffer) - the CPU
	# packs both.
	uniform_types = set()
	for m in re.finditer(r"%(\w+) = OpTypePointer (?:Uniform|StorageBuffer) %(\w+)", dis):
		uniform_types.add(m.group(2))

	# Uniform structs plus every struct nested in them.
	nested = {}
	for m in re.finditer(r"%(\w+) = OpTypeStruct ((?:%\w+ ?)+)", dis):
		nested[m.group(1)] = m.group(2).split()
	pending, structs = list(uniform_types), set()
	while pending:
		t = pending.pop()
		if t in structs:
			continue
		structs.add(t)
		pending += [x.lstrip("%") for x in nested.get(t, []) if x.lstrip("%") in nested]

	offsets = {}
	for m in re.finditer(r"OpMemberDecorate %(\w+) (\d+) Offset (\d+)", dis):
		if m.group(1) in structs:
			offsets[(m.group(1), names.get((m.group(1), m.group(2)), m.group(2)))] = int(m.group(3))
	return offsets


def main():
	os.makedirs(OUT, exist_ok=True)

	mismatches = 0
	checked = 0
	for shader, entry, profile in h.ENTRIES:
		layouts = {}
		for layout, extra in (("vk", []), ("dx", ["-fvk-use-dx-layout"])):
			spv = os.path.join(OUT, f"{shader}.{entry}.{layout}.spv")
			p = subprocess.run([h.DXC, *h.WEB_ARGS, *extra, "-E", entry, "-T", profile, "-I", h.SHADER_DIR,
				os.path.join(h.SHADER_DIR, shader + ".hlsl"), "-Fo", spv], capture_output=True, text=True)
			if p.returncode != 0:
				sys.exit(f"dxc failed for {shader}.{entry} ({layout}):\n{p.stderr}")
			layouts[layout] = member_offsets(spv)

		for key in sorted(set(layouts["vk"]) | set(layouts["dx"])):
			checked += 1
			vk, dx = layouts["vk"].get(key), layouts["dx"].get(key)
			if vk != dx:
				mismatches += 1
				print(f"{shader}.{entry}: {key[0]}.{key[1]}  vulkan layout {vk}  d3d12 layout {dx}")

	print(f"{checked} cbuffer member offsets compared across {len(h.ENTRIES)} entry points: {mismatches} differ")
	return 1 if mismatches else 0


if __name__ == "__main__":
	sys.exit(main())
