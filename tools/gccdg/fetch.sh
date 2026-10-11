#!/bin/bash
# fetch.sh : sparse, shallow checkout of EDG's imported gcc.dg C tests
# (GPLv3 -- stays OUT of this repo, under build/) pinned to EDG_REV.
# Re-running with the same rev is a no-op.
set -eu
. "$(dirname "$0")/common.sh"
EDG_URL="${EDG_URL:-https://github.com/edgcpp/compiler}"
EDG_REV="${EDG_REV:-43e10b90fc83ed2a436fb79b241be8dbf3dc6846}"   # 2026-10-08
if [ -d "$EDG/.git" ] && [ "$(git -C "$EDG" rev-parse HEAD 2>/dev/null)" = "$EDG_REV" ] && [ -d "$R" ]; then
	echo "fetch: $EDG already at $EDG_REV"; exit 0
fi
rm -rf "$EDG"; mkdir -p "$EDG"; cd "$EDG"
git init -q
git remote add origin "$EDG_URL"
git sparse-checkout init --cone
git sparse-checkout set tests/tests/imported/gnu/c
git fetch -q --depth 1 --filter=blob:none origin "$EDG_REV"
git checkout -q FETCH_HEAD
echo "fetch: $EDG @ $EDG_REV ($(find "$R" -name '*.c' | wc -l | tr -d ' ') .c files)"
