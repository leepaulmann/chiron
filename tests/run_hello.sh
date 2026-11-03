#!/usr/bin/env bash
# Run a test program through chiron in a scratch "game folder", with its own
# user folder. In overlay mode the game folder is read-only and must come out
# unchanged.
set -euo pipefail
chiron=$1 exe=$2 mode=$3
dir=$(mktemp -d)
trap 'chmod -R u+w "$dir"; rm -rf "$dir"' EXIT
game=$dir/game user=$dir/user
mkdir "$game"
name=$(basename "$exe")
cp "$exe" "$game/${name^^}"
printf '[Alpha Centauri]\r\nDifficulty=3\r\n' > "$game/TEST.INI"
if [[ $mode == overlay ]]; then
  mkdir "$game/DATA" "$game/SAVES"
  printf 'base' > "$game/DATA/BASE.TXT"
  printf 'old' > "$game/SAVES/OLD.SAV"
  chmod -R a-w "$game"
  before=$(cd "$game" && find . -type f -exec sha256sum {} + | sort)
fi
[[ $mode == relocated ]] && export CHIRON_FORCE_RELOC=1
out=$("$chiron" --game "$game" --user "$user" --exe "$name" --allow-unknown-exe 2>&1) || { echo "$out"; exit 1; }
echo "$out"
grep -q '^PASS$' <<<"$out"
if [[ $mode == overlay ]]; then
  after=$(cd "$game" && find . -type f -exec sha256sum {} + | sort)
  [[ $before == "$after" ]] || { echo "game folder changed"; exit 1; }
  # Copies keep the game folder's spelling.
  for f in TEST.INI DATA/BASE.TXT mine/note.txt; do
    [[ -f $user/$f ]] || { echo "missing $f in the user folder"; ls -R "$user"; exit 1; }
  done
fi
