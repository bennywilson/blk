"""Stages the assets the web viewer reads into build_wasm/fs, for --preload-file.

The engine stores every asset path the way ResourceManager::resource() leaves it:
backslashes, and lowercased end to end. Windows doesn't care about either. The
Emscripten filesystem cares about both, so this writes the tree with every path
lowercased; blk::os_path() takes care of the separators at the point of opening.

Layout mirrors the repo under /blk, so the relative paths levels already use
(`.\\assets\\...` from blaise/, `..\\blk_engine\\assets\\...`) resolve unchanged:

    /blk/blaise/src/            start directory (initialize_engine() does chdir("../"))
    /blk/blaise/assets/...      all of blaise/assets
    /blk/blk_engine/assets/...  engine assets, minus gaussian_splats/
                                plus only the .ply files a level references

gaussian_splats/ is ~10 GB, so splats are staged by reference rather than wholesale.

    python tools/wasm/stage_assets.py
"""

import os
import re
import shutil
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO, "build_wasm", "fs", "blk")

SPLAT_DIR = os.path.join("blk_engine", "assets", "gaussian_splats")


def stage_file(src_rel, staged):
	"""Copies repo-relative `src_rel` to its lowercased path under OUT."""
	dst_rel = src_rel.replace("\\", "/").lower()
	if dst_rel in staged and staged[dst_rel] != src_rel:
		sys.exit(f"stage_assets: {src_rel} and {staged[dst_rel]} collide once lowercased")
	staged[dst_rel] = src_rel

	src = os.path.join(REPO, src_rel)
	dst = os.path.join(OUT, dst_rel)
	if os.path.exists(dst) and os.path.getsize(dst) == os.path.getsize(src) and os.path.getmtime(dst) >= os.path.getmtime(src):
		return False

	os.makedirs(os.path.dirname(dst), exist_ok=True)
	shutil.copy2(src, dst)
	return True


def stage_tree(root_rel, staged, exclude=()):
	copied = 0
	for dirpath, dirnames, filenames in os.walk(os.path.join(REPO, root_rel)):
		rel_dir = os.path.relpath(dirpath, REPO)
		dirnames[:] = [d for d in dirnames if os.path.join(rel_dir, d) not in exclude]
		for f in filenames:
			copied += stage_file(os.path.join(rel_dir, f), staged)
	return copied


def referenced_splats():
	"""Every .ply a level names, as a repo-relative path. Levels resolve from blaise/."""
	plys = set()
	levels = os.path.join(REPO, "blaise", "assets", "levels")
	for dirpath, _, filenames in os.walk(levels):
		for f in filenames:
			if not f.endswith(".blklevel"):
				continue
			with open(os.path.join(dirpath, f), encoding="utf-8", errors="replace") as fh:
				for ref in re.findall(r"=\s*(\S+\.ply)\b", fh.read(), re.IGNORECASE):
					resolved = os.path.normpath(os.path.join(REPO, "blaise", ref.replace("\\", "/")))
					plys.add(os.path.relpath(resolved, REPO))
	return sorted(plys)


def main():
	staged = {}
	copied = stage_tree("blaise/assets", staged)
	copied += stage_tree("blk_engine/assets", staged, exclude={SPLAT_DIR})

	for ply in referenced_splats():
		if os.path.exists(os.path.join(REPO, ply)):
			copied += stage_file(ply, staged)
		else:
			print(f"stage_assets: warning: a level references {ply}, which does not exist")

	# The start directory has to exist for chdir(); --preload-file drops empty ones.
	start = os.path.join(OUT, "blaise", "src")
	os.makedirs(start, exist_ok=True)
	open(os.path.join(start, ".keep"), "w").close()

	total = sum(os.path.getsize(os.path.join(OUT, p)) for p in staged)
	print(f"stage_assets: {len(staged)} files ({total / 1048576:.1f} MB), {copied} copied, into {os.path.relpath(OUT, REPO)}")


if __name__ == "__main__":
	main()
