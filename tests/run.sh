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
for profile in config/example_3bp.json config/dual_7bp.json; do
for node in A B; do
  python scripts/generate_profile.py "$profile" --node "$node" --out "$out/tag_profile.h"
  gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=address,undefined -g -Iinclude -Itests -I"$out" tests/test_tag.c -o "$out/tag"
  ASAN_OPTIONS=detect_leaks=0 "$out/tag"
done
done
for anchor in 0x1111 0x7777; do
python scripts/generate_profile.py config/dual_7bp.json --anchor "$anchor" --out "$out/anchor_profile.h"
gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -fsanitize=address,undefined -g -Itests -I"$out" tests/test_anchor.c -o "$out/anchor"
ASAN_OPTIONS=detect_leaks=0 "$out/anchor"
done
gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Iinclude tests/test_range_view.c -o "$out/view"
ASAN_OPTIONS=detect_leaks=0 "$out/view"
