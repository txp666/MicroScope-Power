#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
usb_test_dir="$(mktemp -d)"
trap 'rm -rf "$usb_test_dir"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
  -I "$repo_root/tests/firmware/usb_stubs" \
  -I "$repo_root/tests/firmware/stubs" \
  -I "$repo_root/firmware/src" \
  "$repo_root/tests/firmware/test_usb_webusb.c" -o "$usb_test_dir/test_usb_webusb"
"$usb_test_dir/test_usb_webusb"
