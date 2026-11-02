#!/usr/bin/env bash
# Run a test program through chiron in a scratch "game folder".
set -euo pipefail
chiron=$1 exe=$2 mode=$3
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
name=$(basename "$exe")
cp "$exe" "$dir/${name^^}"
printf '[Alpha Centauri]\r\nDifficulty=3\r\n' > "$dir/test.ini"
[[ $mode == relocated ]] && export CHIRON_FORCE_RELOC=1
out=$("$chiron" --game "$dir" --exe "$name" --allow-unknown-exe 2>&1) || { echo "$out"; exit 1; }
echo "$out"
grep -q '^PASS$' <<<"$out"
