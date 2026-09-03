#!/bin/sh
# Builds the host preview of the splash and the dashboard.
#
#   tools/uisim/build.sh /tmp/uisim && /tmp/uisim <out-dir> [theme]
#
# The UI reads its colours from Kconfig, so the defaults are turned into a
# header and force-included; pass a different Kconfig by setting KCONFIG.
set -e

out=${1:-/tmp/uisim}
root=$(cd "$(dirname "$0")/../.." && pwd)
defs=$(mktemp)   # the -include file needs no particular name

python3 - "${KCONFIG:-$root/Kconfig}" > "$defs" <<'PY'
import re, sys

print("/* generated from Kconfig defaults by tools/uisim/build.sh */")
print("#define CONFIG_ARCADE_USE_COMPLETE_CUSTOM_THEME 1")
name = None
for line in open(sys.argv[1]):
    m = re.match(r"^config (ARCADE_\S+)", line)
    if m:
        name = m.group(1)
        continue
    m = re.match(r"^    default (.+)$", line)
    if m and name:
        value = m.group(1).strip()
        if value not in ("y", "n"):
            print(f"#define CONFIG_{name} {value}")
        name = None
PY

cc -O1 -Wall -include "$defs" \
   -I "$root/tools/uisim/stub" -I "$root/boards/shields/arcade_adapter/widgets" \
   -o "$out" \
   "$root/tools/uisim/uisim.c" \
   "$root/boards/shields/arcade_adapter/widgets/helpers/display.c" \
   "$root/boards/shields/arcade_adapter/widgets/splash.c" \
   "$root/boards/shields/arcade_adapter/widgets/logo.c" \
   "$root/boards/shields/arcade_adapter/widgets/frames.c" \
   "$root/boards/shields/arcade_adapter/widgets/theme.c" \
   "$root/boards/shields/arcade_adapter/widgets/battery_status.c" \
   "$root/boards/shields/arcade_adapter/widgets/output_status.c" \
   "$root/boards/shields/arcade_adapter/widgets/layer_status.c" \
   "$root/boards/shields/arcade_adapter/widgets/modifier.c" \
   "$root/boards/shields/arcade_adapter/widgets/wpm.c" \
   "$root/boards/shields/arcade_adapter/widgets/action_button.c" \
   "$root/boards/shields/arcade_adapter/widgets/cabinet.c" \
   "$root/boards/shields/arcade_adapter/widgets/progress.c" \
   "$root/tools/uisim/stub/settings_stub.c"

rm -f "$defs"
echo "built $out"
