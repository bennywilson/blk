"""Stages the web viewer's assets into build_wasm/fs, split into a core tree
(--preload-file, baked into viewer.data at compile time) and one tree per
level (packaged separately by build_wasm.ps1 via file_packager, fetched at
runtime for whichever level the page's ?level= asks for).

The engine stores every asset path the way ResourceManager::resource() leaves it:
backslashes, and lowercased end to end. Windows doesn't care about either. The
Emscripten filesystem cares about both, so this writes the tree with every path
lowercased; blk::os_path() takes care of the separators at the point of opening.

Layout mirrors the repo under /blk, so the relative paths levels already use
(`.\\assets\\...` from blaise/, `..\\blk_engine\\assets\\...`) resolve unchanged
regardless of which package a given file ends up written to:

    fs/core/blk/blaise/src/                start directory (initialize_engine() chdir("../"))
    fs/core/blk/blaise/assets/levels/...   every level file, wholesale (a few hundred KB)
    fs/core/blk/blk_engine/assets/...      engine assets, minus gaussian_splats/
    fs/levels/<name>/blk/...               only what level <name> itself references
                                            (blaise/ textures & models, or a
                                            blk_engine/assets/gaussian_splats/*.ply)

blaise/assets/ alone runs ~60 MB, and a level's own reference set is usually a
fraction of that - a different demo's gaussian splat, an unused terrain, raw
test photos never appear unless some level actually names them. Splitting by
level (rather than one shared reference set for all of them, which is what
this looked like before per-level packaging existed) means a page load now
only ever fetches the one level it's about to show, not every level this
build happens to know about.

    python tools/wasm/stage_assets.py
"""

import os
import re
import shutil
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FS = os.path.join(REPO, "build_wasm", "fs")
CORE_OUT = os.path.join(FS, "core", "blk")
LEVELS_OUT = os.path.join(FS, "levels")

LEVEL_DIR = os.path.join("blaise", "assets", "levels")
SPLAT_DIR = os.path.join("blk_engine", "assets", "gaussian_splats")

# Everything a level can reference that the web build is actually able to load.
# .fbx models don't load off Windows (Model::LoadFBX()) and .tif textures
# aren't DDS, so either is dead weight even when a level references one.
# .kbshader isn't traced either - shaders resolve by name, not by the literal
# (often stale, pre-rename) path a level stores, and the whole shaders/ dir is
# small enough to stage wholesale in the core package instead of parsing that
# resolution logic here.
ASSET_EXTENSIONS = (".dds", ".ms3d", ".ply")


def stage_file(src_rel, out_root, staged):
	"""Copies repo-relative `src_rel` to its lowercased path under `out_root`."""
	dst_rel = src_rel.replace("\\", "/").lower()
	if dst_rel in staged and staged[dst_rel] != src_rel:
		# Windows is case-insensitive, so a level's own reference (whatever case
		# it happens to be typed in) and stage_tree()'s on-disk casing routinely
		# name the same file two different ways - not a real collision.
		if os.path.samefile(os.path.join(REPO, src_rel), os.path.join(REPO, staged[dst_rel])):
			return False
		sys.exit(f"stage_assets: {src_rel} and {staged[dst_rel]} collide once lowercased")
	staged[dst_rel] = src_rel

	src = os.path.join(REPO, src_rel)
	dst = os.path.join(out_root, dst_rel)
	if os.path.exists(dst) and os.path.getsize(dst) == os.path.getsize(src) and os.path.getmtime(dst) >= os.path.getmtime(src):
		return False

	os.makedirs(os.path.dirname(dst), exist_ok=True)
	shutil.copy2(src, dst)
	return True


def stage_tree(root_rel, out_root, staged, exclude=()):
	copied = 0
	for dirpath, dirnames, filenames in os.walk(os.path.join(REPO, root_rel)):
		rel_dir = os.path.relpath(dirpath, REPO)
		dirnames[:] = [d for d in dirnames if os.path.join(rel_dir, d) not in exclude]
		for f in filenames:
			copied += stage_file(os.path.join(rel_dir, f), out_root, staged)
	return copied


def prune_stale(out_root, staged):
	"""Removes anything left over in `out_root` from a previous run that
	`staged` didn't touch this time - stale files of its own, or (for a level
	tree) everything from a level that no longer references them."""
	if not os.path.isdir(out_root):
		return 0
	removed = 0
	for dirpath, _, filenames in os.walk(out_root):
		for f in filenames:
			full = os.path.join(dirpath, f)
			rel = os.path.relpath(full, out_root).replace("\\", "/")
			if rel not in staged:
				os.remove(full)
				removed += 1
	for dirpath, dirnames, filenames in os.walk(out_root, topdown=False):
		if not dirnames and not filenames and dirpath != out_root:
			os.rmdir(dirpath)
	return removed


def level_files():
	"""(level_name, absolute .blklevel path) for every level, sorted. level_name
	is what the page's ?level= names it by - the filename minus extension."""
	out = []
	levels = os.path.join(REPO, LEVEL_DIR)
	for dirpath, _, filenames in os.walk(levels):
		for f in filenames:
			if f.endswith(".blklevel"):
				out.append((f[: -len(".blklevel")], os.path.join(dirpath, f)))
	return sorted(out)


def referenced_assets(text, extensions):
	"""Every asset with one of `extensions` this level's raw text names, as a
	repo-relative path, EXCEPT anything under blk_engine/assets/ other than a
	gaussian splat. Levels resolve relative paths from blaise/, so a
	`..\\blk_engine\\...` reference resolves outside blaise/ same as any other -
	but every one of those is core-package content already (main() stages all
	of blk_engine/assets except SPLAT_DIR into CORE_OUT). Staging it again into
	a level package too would have both packages call FS_createDataFile for the
	same path - file_packager's generated loader doesn't guard against that."""
	found = set()
	for ext in extensions:
		for ref in re.findall(rf"=\s*(\S+\{ext})\b", text, re.IGNORECASE):
			resolved = os.path.normpath(os.path.join(REPO, "blaise", ref.replace("\\", "/")))
			rel = os.path.relpath(resolved, REPO)
			engine_assets = os.path.join("blk_engine", "assets")
			if rel.startswith(engine_assets + os.sep) and not rel.startswith(SPLAT_DIR + os.sep):
				continue
			found.add(rel)
	return sorted(found)


def main():
	core_staged = {}
	copied = stage_tree(LEVEL_DIR, CORE_OUT, core_staged)
	copied += stage_tree("blk_engine/assets", CORE_OUT, core_staged, exclude={SPLAT_DIR})

	# The start directory has to exist for chdir(); --preload-file drops empty
	# ones, so this isn't tracked in core_staged - prune_stale would remove it.
	start = os.path.join(CORE_OUT, "blaise", "src")
	os.makedirs(start, exist_ok=True)
	open(os.path.join(start, ".keep"), "w").close()
	core_staged["blaise/src/.keep"] = None

	removed = prune_stale(CORE_OUT, core_staged)
	total = sum(os.path.getsize(os.path.join(CORE_OUT, p)) for p in core_staged if p != "blaise/src/.keep")
	print(f"stage_assets: core: {len(core_staged) - 1} files ({total / 1048576:.1f} MB), {copied} copied, {removed} pruned, into {os.path.relpath(CORE_OUT, REPO)}")

	level_names = []
	for name, path in level_files():
		level_names.append(name)
		with open(path, encoding="utf-8", errors="replace") as fh:
			text = fh.read()

		level_out = os.path.join(LEVELS_OUT, name, "blk")
		level_staged = {}
		level_copied = 0
		for asset in referenced_assets(text, ASSET_EXTENSIONS):
			if os.path.exists(os.path.join(REPO, asset)):
				level_copied += stage_file(asset, level_out, level_staged)
			else:
				print(f"stage_assets: warning: {name} references {asset}, which does not exist")

		level_removed = prune_stale(level_out, level_staged)
		size = sum(os.path.getsize(os.path.join(level_out, p)) for p in level_staged)
		print(f"stage_assets: {name}: {len(level_staged)} files ({size / 1048576:.1f} MB), {level_copied} copied, {level_removed} pruned, into {os.path.relpath(level_out, REPO)}")

	# A level whose .blklevel is gone shouldn't leave its old package directory
	# (and stale .data/.js next to viewer.html) behind for build_wasm.ps1 to
	# keep re-packaging.
	if os.path.isdir(LEVELS_OUT):
		for existing in os.listdir(LEVELS_OUT):
			if existing not in level_names:
				shutil.rmtree(os.path.join(LEVELS_OUT, existing))

	manifest = os.path.join(REPO, "build_wasm", "levels.txt")
	os.makedirs(os.path.dirname(manifest), exist_ok=True)
	with open(manifest, "w", encoding="utf-8") as fh:
		fh.write("\n".join(level_names) + "\n")


if __name__ == "__main__":
	main()
