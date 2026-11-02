#!/usr/bin/env bash
# Run hello.exe through chiron in a scratch "game folder".
set -euo pipefail
chiron=$1 exe=$2 mode=$3
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
cp "$exe" "$dir/HELLO.EXE"
printf '[Alpha Centauri]\r\nDifficulty=3\r\n' > "$dir/test.ini"
[[ $mode == relocated ]] && export CHIRON_FORCE_RELOC=1
out=$("$chiron" --game "$dir" --exe hello.exe --allow-unknown-exe 2>&1) || { echo "$out"; exit 1; }
echo "$out"
grep -q '^PASS$' <<<"$out"
