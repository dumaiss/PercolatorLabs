#!/bin/sh
# vDrip9958 project static checks.
#
# Verifies, without building:
#   - no stale 9928 / TMS9918 names in active source/build/test identifiers
#     (only classified historical / license / attribution references allowed),
#   - no stale copied/build artifacts in the source tree,
#   - no excluded active dependencies (host / network / serial / RFB / Python /
#     vDrip9928),
#   - required documentation, license, and manual files are present.
#
# Exits non-zero with concise path-oriented diagnostics on any failure.

set -u

# Resolve the project root (parent of this script's tools/ directory).
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root" || exit 2

fail=0
note() { printf '  - %s\n' "$1"; fail=1; }

echo "vDrip9958 project checks (root: $root)"

# --- 1. Stale legacy names in active code ---------------------------------
# Allowed historical references live only in the attribution comment block of
# src/vDrip9958.h. Any other match is a defect.
echo "[1] legacy-name sweep"
legacy=$(grep -rniE '9928|tms9918|vremutms9918|vr_emu_tms9918|vr_tms9918' \
           src tests CMakeLists.txt 2>/dev/null \
         | grep -viE 'Derived in part|permanent fork|used under the MIT')
if [ -n "$legacy" ]; then
  echo "$legacy" | while IFS= read -r line; do note "unexpected legacy name: $line"; done
fi

# Stale filenames anywhere in the tree.
stale_files=$(find . -type f \( -name '*9928*' -o -name '*tms9918*' \
                -o -name '*vrEmu*' -o -name 'vdrip_vdp.h' \) 2>/dev/null)
[ -n "$stale_files" ] && echo "$stale_files" | while IFS= read -r f; do \
  note "stale filename: $f"; done

# --- 2. Copied / build artifacts ------------------------------------------
echo "[2] copied-artifact check"
for p in pybindings res; do
  [ -e "$p" ] && note "stale copied directory present: $p/"
done
arts=$(find . -type f \( -name '*.o' -o -name '*.so' -o -name '*.a' \
         -o -name 'image.bin' -o -name '*.gif' \) 2>/dev/null)
[ -n "$arts" ] && echo "$arts" | while IFS= read -r f; do \
  note "build/demo artifact in source tree: $f"; done

# --- 3. Excluded active dependencies --------------------------------------
echo "[3] excluded-dependency check"
deps=$(grep -rniE '#include[[:space:]]*[<"].*(python|pybind|vnc|rfb|socket|serial|vDrip9928)' \
         src 2>/dev/null)
[ -n "$deps" ] && echo "$deps" | while IFS= read -r d; do \
  note "excluded dependency: $d"; done

# --- 4. Required files -----------------------------------------------------
echo "[4] required-file check"
required="LICENSE README.md
docs/yamaha_v9938.pdf docs/yamaha_v9958_ocr.pdf
docs/README.md docs/api-reference.md docs/architecture.md
docs/register-support.md docs/display-mode-support.md docs/command-support.md
docs/building.md docs/testing.md docs/deviations.md docs/verification.md
src/vDrip9958.h src/vDrip9958_internal.h src/vDrip9958.c
src/vDrip9958_render.c src/vDrip9958_commands.c"
for f in $required; do
  [ -f "$f" ] || note "missing required file: $f"
done

# --- result ----------------------------------------------------------------
if [ "$fail" -eq 0 ]; then
  echo "PASS: all project checks passed"
  exit 0
fi
echo "FAIL: project checks reported issues above"
exit 1
