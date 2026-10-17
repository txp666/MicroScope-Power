#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/microscope-firmware-tests.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
cc_bin=${CC:-cc}
"$cc_bin" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -pthread \
  -I"$repo_dir/tests/firmware/stubs" -I"$repo_dir/firmware/src" \
  "$repo_dir/tests/firmware/test_sampler.c" "$repo_dir/firmware/src/protocol.c" \
  -o "$build_dir/test_sampler"
"$cc_bin" -std=c11 -Wall -Wextra -Werror \
  -I"$repo_dir/tests/firmware/stubs" -I"$repo_dir/firmware/src" \
  "$repo_dir/tests/firmware/test_ads8866.c" -o "$build_dir/test_ads8866"
"$cc_bin" -std=c11 -Wall -Wextra -Werror \
  -I"$repo_dir/tests/firmware/stubs" -I"$repo_dir/firmware/src" \
  "$repo_dir/tests/firmware/test_app_settings.c" -o "$build_dir/test_app_settings"
"$cc_bin" -std=c11 -Wall -Wextra -Werror \
  -I"$repo_dir/tests/firmware/stubs" -I"$repo_dir/firmware/src" \
  "$repo_dir/tests/firmware/test_buzzer.c" -o "$build_dir/test_buzzer"
"$cc_bin" -std=c11 -Wall -Wextra -Werror \
  -I"$repo_dir/tests/firmware/stubs" -I"$repo_dir/firmware/src" \
  "$repo_dir/tests/firmware/test_voltage_telemetry.c" -o "$build_dir/test_voltage_telemetry"
"$cc_bin" -std=c11 -Wall -Wextra -Werror \
  -I"$repo_dir/tests/firmware/stubs" -I"$repo_dir/firmware/src" \
  "$repo_dir/tests/firmware/test_adc_voltage.c" -o "$build_dir/test_adc_voltage"
"$cc_bin" -std=c11 -Wall -Wextra -Werror \
  -I"$repo_dir/tests/firmware/stubs" -I"$repo_dir/firmware/src" \
  "$repo_dir/tests/firmware/test_local_ui.c" -o "$build_dir/test_local_ui"
"$cc_bin" -std=c11 -Wall -Wextra -Werror \
  -I"$repo_dir/tests/firmware/stubs" -I"$repo_dir/firmware/src" \
  "$repo_dir/tests/firmware/test_encoder.c" -o "$build_dir/test_encoder"
"$build_dir/test_sampler"
"$build_dir/test_ads8866"
"$build_dir/test_app_settings"
"$build_dir/test_buzzer"
"$build_dir/test_voltage_telemetry"
"$build_dir/test_adc_voltage"
"$build_dir/test_local_ui"
"$build_dir/test_encoder"
