"""Install this repo's git hooks. Run once per clone:

    python tools/git_hooks/install.py

Hooks live inside .git, which isn't cloned, so they can't just be checked in. What gets
installed is a one-line shim per hook that runs the tracked script, so later edits to the
hook take effect without reinstalling. A hook file that isn't one of our shims is left
alone - Git LFS keeps its hooks in the same directory.
"""

import os
import stat
import subprocess
import sys

HOOKS = {"pre-commit": "tools/git_hooks/pre_commit.py"}

MARKER = "# blk: runs the tracked hook script"
SHIM = '#!/bin/sh\n{marker}\nexec python "$(git rev-parse --show-toplevel)/{script}" "$@"\n'


def git(*args):
	return subprocess.run(["git", *args], capture_output=True, text=True, encoding="utf-8", check=True).stdout.strip()


def main():
	repo = git("rev-parse", "--show-toplevel")
	hooks_dir = git("rev-parse", "--git-path", "hooks")  # honours core.hooksPath
	if not os.path.isabs(hooks_dir):
		hooks_dir = os.path.join(repo, hooks_dir)
	os.makedirs(hooks_dir, exist_ok=True)

	for name, script in HOOKS.items():
		path = os.path.join(hooks_dir, name)
		if os.path.exists(path):
			with open(path, encoding="utf-8", errors="replace") as fh:
				if MARKER not in fh.read():
					print(f"{name}: a hook is already installed and isn't ours - leaving it alone", file=sys.stderr)
					continue
		with open(path, "w", encoding="utf-8", newline="\n") as fh:
			fh.write(SHIM.format(marker=MARKER, script=script))
		os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
		print(f"installed {name} -> {script}")
	return 0


if __name__ == "__main__":
	sys.exit(main())
