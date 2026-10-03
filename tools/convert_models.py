#!/usr/bin/env python3
"""Convert glTF/GLB models into the game's compact .gmd format.

Textures are baked into per-vertex colours (Kenney models use a small colour
palette texture, so this is lossless for them). The mesh is scaled so that its
largest dimension equals the requested size in metres and is centred on its
bounding box.

.gmd layout (little endian):
  char[4] "GMD1"; u32 vertex_count; u32 index_count;
  vertex_count * { f32 pos[3]; f32 normal[3]; u8 rgba[4] }
  index_count  * u32

Usage: convert_models.py manifest.txt SRC_ROOT OUT_DIR
manifest lines: <out_name> <path/relative/to/SRC_ROOT.glb> <size_m>
"""
import struct
import sys
from pathlib import Path

import numpy as np
import trimesh


def convert(src: Path, dst: Path, size: float) -> None:
    scene = trimesh.load(src, force="scene", process=False)
    parts = []
    for geom in scene.dump():
        if not isinstance(geom, trimesh.Trimesh) or len(geom.faces) == 0:
            continue
        try:
            colors = geom.visual.to_color().vertex_colors
        except Exception:
            colors = np.full((len(geom.vertices), 4), 255, np.uint8)
        parts.append((np.asarray(geom.vertices, np.float32), np.asarray(geom.vertex_normals, np.float32),
                      np.asarray(colors, np.uint8)[:, :4], np.asarray(geom.faces, np.uint32)))
    verts = np.concatenate([p[0] for p in parts])
    lo, hi = verts.min(axis=0), verts.max(axis=0)
    centre = (lo + hi) / 2
    scale = size / float((hi - lo).max())
    out = bytearray(b"GMD1")
    nv = sum(len(p[0]) for p in parts)
    ni = sum(p[3].size for p in parts)
    out += struct.pack("<II", nv, ni)
    for v, n, c, _ in parts:
        v = (v - centre) * scale
        rec = np.zeros(len(v), dtype=[("p", "<f4", 3), ("n", "<f4", 3), ("c", "u1", 4)])
        rec["p"], rec["n"], rec["c"] = v, n, c
        out += rec.tobytes()
    base = 0
    for v, _, _, f in parts:
        out += (f + base).astype("<u4").tobytes()
        base += len(v)
    dst.write_bytes(bytes(out))
    print(f"{dst.name:28s} {nv:6d} verts  {(hi - lo) * scale}")


def main() -> None:
    manifest, src_root, out_dir = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    out_dir.mkdir(parents=True, exist_ok=True)
    for line in manifest.read_text().splitlines():
        line = line.split("#")[0].strip()
        if not line:
            continue
        name, rel, size = line.rsplit(None, 2)
        convert(src_root / rel.replace("%20", " "), out_dir / f"{name}.gmd", float(size))


if __name__ == "__main__":
    main()
