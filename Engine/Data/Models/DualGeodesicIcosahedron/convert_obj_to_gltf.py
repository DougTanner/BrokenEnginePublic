"""Convert DualGeodesicIcosahedron OBJ to glTF 2.0 (no external dependencies)."""

import json
import struct
import math
import os

OBJ_PATH = "../../Models/DualGeodesicIcosahedron/[FN]DualGeodesicIcosahedron.obj"
GLTF_PATH = "DualGeodesicIcosahedron.gltf"
BIN_PATH = "DualGeodesicIcosahedron.bin"


def parse_obj(path):
    positions = []
    faces = []
    with open(path, "r") as f:
        for line in f:
            parts = line.strip().split()
            if not parts:
                continue
            if parts[0] == "v":
                positions.append((float(parts[1]), float(parts[2]), float(parts[3])))
            elif parts[0] == "f":
                # OBJ is 1-based
                faces.append((int(parts[1]) - 1, int(parts[2]) - 1, int(parts[3]) - 1))
    return positions, faces


def normalize(v):
    length = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    if length < 1e-12:
        return (0.0, 0.0, 1.0)
    return (v[0] / length, v[1] / length, v[2] / length)


def cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def compute_face_normal(p0, p1, p2):
    return normalize(cross(sub(p1, p0), sub(p2, p0)))


def expand_and_deduplicate(positions, faces):
    """Expand to per-face vertices (each with face normal), then deduplicate."""
    vertex_map = {}  # (pos_tuple, normal_tuple) -> index
    vertices = []  # list of (px, py, pz, nx, ny, nz)
    indices = []

    for i0, i1, i2 in faces:
        p0, p1, p2 = positions[i0], positions[i1], positions[i2]
        normal = compute_face_normal(p0, p1, p2)

        face_indices = []
        for pos in (p0, p1, p2):
            key = (pos, normal)
            if key not in vertex_map:
                vertex_map[key] = len(vertices)
                vertices.append((*pos, *normal))
            face_indices.append(vertex_map[key])
        indices.extend(face_indices)

    return vertices, indices


def write_bin(path, vertices, indices):
    """Write binary buffer: [positions][normals][indices (padded to 4-byte alignment)]."""
    vertex_count = len(vertices)
    index_count = len(indices)

    # Positions block
    pos_data = b""
    for v in vertices:
        pos_data += struct.pack("<fff", v[0], v[1], v[2])

    # Normals block
    norm_data = b""
    for v in vertices:
        norm_data += struct.pack("<fff", v[3], v[4], v[5])

    # Indices block
    idx_data = b""
    for i in indices:
        idx_data += struct.pack("<H", i)

    # Pad indices to 4-byte alignment
    if len(idx_data) % 4 != 0:
        idx_data += b"\x00" * (4 - len(idx_data) % 4)

    buf = pos_data + norm_data + idx_data
    with open(path, "wb") as f:
        f.write(buf)

    return len(pos_data), len(norm_data), len(idx_data), len(buf)


def compute_min_max(vertices, component_start, component_count):
    mins = [float("inf")] * component_count
    maxs = [float("-inf")] * component_count
    for v in vertices:
        for c in range(component_count):
            val = v[component_start + c]
            mins[c] = min(mins[c], val)
            maxs[c] = max(maxs[c], val)
    return mins, maxs


def write_gltf(path, bin_filename, vertices, indices, pos_bytes, norm_bytes, idx_bytes, total_bytes):
    vertex_count = len(vertices)
    index_count = len(indices)

    pos_min, pos_max = compute_min_max(vertices, 0, 3)
    norm_min, norm_max = compute_min_max(vertices, 3, 3)

    gltf = {
        "asset": {"version": "2.0", "generator": "OBJ-to-glTF converter"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [
            {
                "primitives": [
                    {
                        "attributes": {"POSITION": 1, "NORMAL": 2},
                        "indices": 0,
                        "material": 0,
                        "mode": 4,
                    }
                ]
            }
        ],
        "materials": [
            {
                "pbrMetallicRoughness": {
                    "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                    "metallicFactor": 0.0,
                    "roughnessFactor": 1.0,
                }
            }
        ],
        "accessors": [
            # 0: indices
            {
                "bufferView": 0,
                "byteOffset": 0,
                "componentType": 5123,  # UNSIGNED_SHORT
                "count": index_count,
                "max": [max(indices)],
                "min": [min(indices)],
                "type": "SCALAR",
            },
            # 1: positions
            {
                "bufferView": 1,
                "byteOffset": 0,
                "componentType": 5126,  # FLOAT
                "count": vertex_count,
                "max": pos_max,
                "min": pos_min,
                "type": "VEC3",
            },
            # 2: normals
            {
                "bufferView": 2,
                "byteOffset": 0,
                "componentType": 5126,  # FLOAT
                "count": vertex_count,
                "max": norm_max,
                "min": norm_min,
                "type": "VEC3",
            },
        ],
        "bufferViews": [
            # 0: indices
            {
                "buffer": 0,
                "byteOffset": pos_bytes + norm_bytes,
                "byteLength": idx_bytes,
                "target": 34963,  # ELEMENT_ARRAY_BUFFER
            },
            # 1: positions
            {
                "buffer": 0,
                "byteOffset": 0,
                "byteLength": pos_bytes,
                "byteStride": 12,
                "target": 34962,  # ARRAY_BUFFER
            },
            # 2: normals
            {
                "buffer": 0,
                "byteOffset": pos_bytes,
                "byteLength": norm_bytes,
                "byteStride": 12,
                "target": 34962,  # ARRAY_BUFFER
            },
        ],
        "buffers": [{"byteLength": total_bytes, "uri": bin_filename}],
    }

    with open(path, "w") as f:
        json.dump(gltf, f, indent=4)


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    obj_path = os.path.normpath(os.path.join(script_dir, OBJ_PATH))
    gltf_path = os.path.join(script_dir, GLTF_PATH)
    bin_path = os.path.join(script_dir, BIN_PATH)

    print(f"Reading OBJ: {obj_path}")
    positions, faces = parse_obj(obj_path)
    print(f"  {len(positions)} positions, {len(faces)} faces")

    print("Computing face normals and deduplicating...")
    vertices, indices = expand_and_deduplicate(positions, faces)
    print(f"  {len(vertices)} unique vertices, {len(indices)} indices")

    print(f"Writing binary: {bin_path}")
    pos_bytes, norm_bytes, idx_bytes, total_bytes = write_bin(bin_path, vertices, indices)
    print(f"  positions: {pos_bytes} bytes, normals: {norm_bytes} bytes, indices: {idx_bytes} bytes")
    print(f"  total: {total_bytes} bytes")

    print(f"Writing glTF: {gltf_path}")
    write_gltf(gltf_path, BIN_PATH, vertices, indices, pos_bytes, norm_bytes, idx_bytes, total_bytes)

    # Verification
    print("\n--- Verification ---")
    print(f"Vertex count: {len(vertices)} (expected ~1140)")
    print(f"Index count: {len(indices)} (expected {len(faces) * 3} = {len(faces)}*3)")
    assert len(indices) == len(faces) * 3, "Index count mismatch"
    assert max(indices) < 65536, "Indices exceed uint16 range"
    assert max(indices) < len(vertices), "Index out of bounds"

    # Verify a few face normals
    print("\nSample face normals (first 3 faces):")
    for fi in range(min(3, len(faces))):
        i0, i1, i2 = faces[fi]
        p0, p1, p2 = positions[i0], positions[i1], positions[i2]
        n = compute_face_normal(p0, p1, p2)
        print(f"  Face {fi}: verts ({i0},{i1},{i2}) -> normal ({n[0]:.6f}, {n[1]:.6f}, {n[2]:.6f})")

    print("\nDone!")


if __name__ == "__main__":
    main()
