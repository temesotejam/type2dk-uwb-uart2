#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Iinclude tests/test_protocol.c -o "$out/protocol"
ASAN_OPTIONS=detect_leaks=0 "$out/protocol"
gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Iinclude tests/test_log.c -o "$out/log"
ASAN_OPTIONS=detect_leaks=0 "$out/log"
python tests/test_log_check.py
gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Itests tests/test_usb_driver.c -o "$out/usb"
ASAN_OPTIONS=detect_leaks=0 "$out/usb"
python tests/test_profile.py
for node in A B; do
  python scripts/generate_profile.py config/example_3bp.json --node "$node" --out "$out/tag_profile.h"
  gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=address,undefined -g -Iinclude -Itests -I"$out" tests/test_tag.c -o "$out/tag"
  ASAN_OPTIONS=detect_leaks=0 "$out/tag"
done
python scripts/generate_profile.py config/dual_3bp.json --anchor 1 --out "$out/anchor_profile.h"
gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -fsanitize=address,undefined -g -Itests -I"$out" tests/test_anchor.c -o "$out/anchor"
ASAN_OPTIONS=detect_leaks=0 "$out/anchor"
gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Iinclude tests/test_range_view.c -o "$out/view"
ASAN_OPTIONS=detect_leaks=0 "$out/view"
