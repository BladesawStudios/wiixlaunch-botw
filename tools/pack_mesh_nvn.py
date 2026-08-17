#!/usr/bin/env python3
"""Converts a triangulated Wavefront .obj mesh into a flat, non-indexed
position+normal vertex array C++ header, for use with a from-scratch NVN
draw path (no texture, no material - see shaders/normals.vert/.frag and
WiiXLaunch::BotW::NVN::CreateMesh/DrawMesh).

Deliberately minimal: no UV/material handling (the normals-visualization
shader doesn't sample a texture), no indexed draw (every other demo in this
project already draws expanded, non-indexed triangle lists - see
DrawBnshQuadDirect/DrawTextureQuadDirect in nvn_overlay.hpp - so this keeps
the same shape rather than introducing index-buffer binding as a new,
unproven code path for a POC). Faces must already be triangles (this
project's Fish.obj already is, per Blender's default triangulated export) -
this tool intentionally does not fan-triangulate n-gons, to avoid silently
producing wrong-looking geometry from an assumption that hasn't been
checked.

Vertex layout emitted: position (x,y,z,1.0) + normal (nx,ny,nz,0.0), both
plain vec4 - reuses the ONE vertex attribute format (0x2e, NVN_FORMAT_RGBA32F)
already independently confirmed elsewhere in this project (see
docs/switch-nvn-findings.md), rather than guessing at a 3-component format
code that's never been verified against this driver.

Usage:
    python pack_mesh.py <mesh.obj> <output_name> [--out include/]

Produces include/<output_name>_mesh.hpp with g_<Name>MeshVertices (flat
float array) / k<Name>MeshVertexCount / k<Name>MeshCenter / k<Name>MeshRadius
(the mesh's bounding-sphere center/radius in model space, so a caller can
easily recenter + normalize it to a fixed on-screen size).
"""
import argparse
import pathlib


def parse_obj(path: pathlib.Path):
    positions = []
    normals = []
    tris = []  # list of (pos_idx, nrm_idx) triples, one per triangle corner

    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        tag = parts[0]
        if tag == "v":
            positions.append(tuple(float(c) for c in parts[1:4]))
        elif tag == "vn":
            normals.append(tuple(float(c) for c in parts[1:4]))
        elif tag == "f":
            corners = parts[1:]
            if len(corners) != 3:
                raise SystemExit(
                    f"face with {len(corners)} vertices found (expected triangles only): {line!r} - "
                    "this tool does not triangulate n-gons, re-export as triangles first."
                )
            face = []
            for corner in corners:
                fields = corner.split("/")
                v_idx = int(fields[0])
                n_idx = int(fields[2]) if len(fields) >= 3 and fields[2] else None
                face.append((v_idx, n_idx))
            tris.append(face)

    return positions, normals, tris


def resolve_index(idx: int, count: int) -> int:
    # OBJ indices are 1-based; negative indices are relative to the end.
    return (idx - 1) if idx > 0 else (count + idx)


def build_vertex_array(positions, normals, tris):
    # Emits each triangle TWICE, once per winding order. NVN::DrawMesh (see
    # nvn.hpp) now binds and clears BotW's own live depth buffer and does
    # real hardware depth testing with a strict LESS compare, so this is
    # safe again (an earlier version of this tool did the same thing before
    # real depth testing existed, and it caused visible artifacts back then -
    # see git history): the caller never binds its own polygon/cull-face
    # state, so whatever BotW last left active still applies; drawing both
    # windings means the mesh renders correctly regardless of which winding
    # that cull state happens to accept. The duplicate (whichever winding
    # ALSO survives, e.g. if culling happens to be off) lands at the exact
    # same depth as the first copy and fails the strict LESS test against
    # it, so it's discarded automatically - no double-draw, no z-fighting.
    def corner_vertex(v_idx, n_idx):
        px, py, pz = positions[resolve_index(v_idx, len(positions))]
        if n_idx is not None and normals:
            nx, ny, nz = normals[resolve_index(n_idx, len(normals))]
        else:
            nx, ny, nz = 0.0, 0.0, 0.0
        return (px, py, pz, nx, ny, nz)

    verts = []
    for face in tris:
        corners = [corner_vertex(v_idx, n_idx) for v_idx, n_idx in face]
        verts.extend(corners)                              # original winding
        verts.extend([corners[0], corners[2], corners[1]])  # reversed winding
    return verts


def bounding_sphere(positions):
    xs = [p[0] for p in positions]
    ys = [p[1] for p in positions]
    zs = [p[2] for p in positions]
    cx = (min(xs) + max(xs)) / 2.0
    cy = (min(ys) + max(ys)) / 2.0
    cz = (min(zs) + max(zs)) / 2.0
    radius = max(
        ((p[0] - cx) ** 2 + (p[1] - cy) ** 2 + (p[2] - cz) ** 2) ** 0.5
        for p in positions
    )
    return (cx, cy, cz), radius


def emit_header(verts, center, radius, name: str, out_path: pathlib.Path) -> None:
    array_name = f"g_{name}MeshVertices"
    count_name = f"k{name}MeshVertexCount"
    center_name = f"k{name}MeshCenter"
    radius_name = f"k{name}MeshRadius"

    lines = [
        "#pragma once",
        "#include <cstddef>",
        "",
        "// Generated by scripts/pack_mesh.py - do not hand-edit.",
        "// Flat, non-indexed triangle list: each vertex is (x,y,z, nx,ny,nz), model space.",
        f"constexpr size_t {count_name} = {len(verts)};",
        f"constexpr float {center_name}[3] = {{{center[0]:.6f}f, {center[1]:.6f}f, {center[2]:.6f}f}};",
        f"constexpr float {radius_name} = {radius:.6f}f;",
        f"alignas(4096) inline const float {array_name}[{count_name} * 6] = {{",
    ]
    flat = []
    for v in verts:
        flat.extend(v)
    for i in range(0, len(flat), 6):
        chunk = flat[i:i + 6]
        lines.append("    " + " ".join(f"{c:.6f}f," for c in chunk))
    lines.append("};")
    lines.append("")
    out_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {out_path} ({len(verts)} vertices, {len(verts) * 6 * 4} bytes)")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("obj_file", type=pathlib.Path)
    parser.add_argument("output_name", help="PascalCase name, e.g. Fish -> include/fish_mesh.hpp")
    parser.add_argument("--out", type=pathlib.Path, default=pathlib.Path("include"))
    args = parser.parse_args()

    positions, normals, tris = parse_obj(args.obj_file)
    print(f"parsed {len(positions)} positions, {len(normals)} normals, {len(tris)} triangles")

    verts = build_vertex_array(positions, normals, tris)
    center, radius = bounding_sphere(positions)
    print(f"bounding sphere: center={center}, radius={radius:.6f}")

    out_file = args.out / f"{args.output_name.lower()}_mesh.hpp"
    emit_header(verts, center, radius, args.output_name, out_file)


if __name__ == "__main__":
    main()
