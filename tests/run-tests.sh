#!/usr/bin/env bash
set -euo pipefail

test_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/weekend-bonus-tests.XXXXXX")"
trap 'rm -rf -- "$test_dir"' EXIT

"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -pedantic \
    -I"$test_root/src" "$test_root/tests/engine_test.cpp" \
    -o "$test_dir/engine_test"
"$test_dir/engine_test"
"${LUA:-lua}" "$test_root/tests/addon_test.lua" \
    "$test_root/client/WeekendBonus/WeekendBonus.lua"
