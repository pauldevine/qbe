#!/bin/bash
# pp.sh [module...] : regenerate $W/pp/<module>.i by preprocessing the C-Kermit
# sources with Open Watcom `wcc -p` and the exact victorow.mak flags (large
# model, -fi=ckvictor.h).  Runs inside the ia16-ubuntu-2 container, which sees
# ~/projects at /mnt/projects.  Verified byte-identical to the §9f inputs.
set -eu
. "$(dirname "$0")/common.sh"
CT=ia16-ubuntu-2
if ! container list 2>/dev/null | grep -q "^$CT .*running"; then
  container system start >/dev/null 2>&1 || true
  container start "$CT" >/dev/null
fi
rel() { case "$1" in "$HOME/projects/"*) echo "/mnt/projects/${1#$HOME/projects/}";; *) echo "pp.sh: $1 not under ~/projects" >&2; exit 1;; esac; }
CKC=$(rel "$CK"); PPC=$(rel "$W/pp")
mkdir -p "$W/pp"
for m in ${*:-$MODULES}; do
  container exec "$CT" sh -c "cd $CKC && WATCOM=/opt/open-watcom-v2/rel && PATH=\$WATCOM/arml64:\$PATH && \
    wcc -p -ml -0 -os -zq -zc -bt=dos -fr=/dev/null -i=victorow -i=victor -i=\$WATCOM/h \
        -fi=ckvictor.h -fo=$PPC/$m.i $m.c" || { echo "$m: wcc -p failed" >&2; exit 1; }
  echo "$m.i"
done
