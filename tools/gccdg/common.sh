# Shared paths for the gcc.dg runtime-test triage tools (sourced).
#   EDG  = the EDG compiler checkout (sparse: only tests/tests/imported/gnu/c)
#   R    = the gcc.dg C test tree inside it
#   W    = work dir (untracked, under build/)
#   OUT  = per-model outputs
TOOLS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
Q="$(cd "$TOOLS/../.." && pwd)"
W="${GCCDG_W:-$Q/build/gccdg}"
EDG="$W/edg"
R="$EDG/tests/tests/imported/gnu/c"
MODEL="${MODEL:-small}"
OUT="$W/out-$MODEL"
WRAPPED="$W/wrapped"
# Build-dir / exe prefix so the per-test build/examples/<name> dirs are
# recognisable (and never collide with a gate probe of the same name).
PFX="gd_"
