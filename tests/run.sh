#!/bin/sh
set -eu
export UBSAN_OPTIONS=halt_on_error=1
cd "$(dirname "$0")/.."
python3 scripts/embed_web.py
mkdir -p .local
if [ -z "${CMAKE:-}" ] && [ -x .local/cmake/bin/cmake ]; then CMAKE="$PWD/.local/cmake/bin/cmake"; fi
"${CMAKE:-cmake}" -S . -B .local/host
"${CMAKE:-cmake}" --build .local/host -j 4
case_dir=$(mktemp -d "$PWD/.local/qa.XXXXXX")
trap 'rm -rf "$case_dir"' EXIT
python3 scripts/card_package.py "$case_dir/card.zip"
python3 -m zipfile -e "$case_dir/card.zip" "$case_dir/card"
python3 tests/fixtures.py "$case_dir"
.local/host/jingdu_test "$case_dir" "$PWD/.local/screens"
python3 tests/lan_test.py .local/host/jingdu_lan_test "$case_dir"
python3 tests/screen_test.py .local/host/jingdu_screen_test
.local/host/jingdu_screen_ui_test "$case_dir"
.local/host/jingdu_card_test "$case_dir/card"
