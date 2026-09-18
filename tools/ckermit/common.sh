# Shared paths for the C-Kermit triage tools (sourced).
#   CK   = the C-Kermit Victor port tree (read-only; never modified here)
#   W    = work dir for inputs/outputs (untracked, under build/)
#   TOOLS= this directory
TOOLS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
Q="$(cd "$TOOLS/../.." && pwd)"
CK="${CK:-$HOME/projects/ckermit}"
W="${CKW:-$Q/build/ckermit-triage}"
MODEL="${MODEL:-large}"
OUT="$W/out-$MODEL${OUTSUFFIX:-}"
MODULES="ckcmai ckclib ckcfns ckcfn2 ckcfn3 ckcpro ckucmd ckuusr ckuus2 ckuus3
 ckuus4 ckuus5 ckuus6 ckuus7 ckuusx ckuusy ckutio ckufio ckusig ckuxla ckcuni
 ckcnet ckctel ckvictor"
