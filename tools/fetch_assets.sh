#!/usr/bin/env bash
# Re-creates assets/ from Kenney's CC0 starter kits on GitHub.
# Needs git, python3 and `pip install trimesh numpy scipy`.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${1:-$(mktemp -d)}"
cd "$WORK"
for kit in Starter-Kit-FPS Starter-Kit-City-Builder Starter-Kit-Racing Starter-Kit-3D-Platformer Starter-Kit-Basic-Scene; do
  [ -d "$kit" ] || git clone -q --depth 1 "https://github.com/KenneyNL/$kit.git"
done
python3 "$ROOT/tools/convert_models.py" "$ROOT/tools/models.txt" "$WORK" "$ROOT/assets/models"
mkdir -p "$ROOT/assets/sounds"
while read -r name src; do
  [ -z "$name" ] || [ "${name:0:1}" = "#" ] && continue
  cp "$WORK/$src" "$ROOT/assets/sounds/$name.ogg"
done < "$ROOT/tools/sounds.txt"
echo "Assets written to $ROOT/assets"
