from __future__ import annotations

import json
import math
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAP_DIR = ROOT / "Project" / "Assets" / "Maps" / "industrial_arena"
PROPS_DIR = MAP_DIR / "props"
MATERIALS_DIR = MAP_DIR / "materials"

FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
MODEL_SALT = b"projectunity-model-cook-stable-batch-v2"


MATERIALS = [
    ("concrete_floor", [0.43, 0.42, 0.39, 1.0], 0.0, 0.86),
    ("concrete_var_a", [0.36, 0.36, 0.34, 1.0], 0.0, 0.9),
    ("dark_wall", [0.25, 0.26, 0.27, 1.0], 0.0, 0.75),
    ("warehouse_roof", [0.31, 0.32, 0.33, 1.0], 0.0, 0.82),
    ("container_blue", [0.16, 0.28, 0.42, 1.0], 0.0, 0.62),
    ("container_red", [0.48, 0.20, 0.16, 1.0], 0.0, 0.64),
    ("container_green", [0.28, 0.42, 0.22, 1.0], 0.0, 0.68),
    ("wood_crate", [0.47, 0.31, 0.16, 1.0], 0.0, 0.78),
    ("low_barrier", [0.55, 0.55, 0.52, 1.0], 0.0, 0.82),
    ("hazard_yellow", [0.93, 0.68, 0.12, 1.0], 0.0, 0.7),
    ("hazard_black", [0.05, 0.05, 0.045, 1.0], 0.0, 0.75),
    ("spawn_a", [0.68, 0.23, 0.15, 1.0], 0.0, 0.9),
    ("spawn_b", [0.20, 0.36, 0.64, 1.0], 0.0, 0.9),
    ("plant_green", [0.20, 0.38, 0.19, 1.0], 0.0, 0.92),
    ("cable_dark", [0.035, 0.035, 0.033, 1.0], 0.0, 0.88),
]


def fnv_hash(data: bytes, seed: int) -> int:
    value = seed
    for byte in data:
        value ^= byte
        value = (value * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return value or 1


def model_asset_id(path: Path) -> int:
    seed = fnv_hash(MODEL_SALT, FNV_OFFSET)
    seed = fnv_hash(bytes([1]), seed)
    return fnv_hash(path.read_bytes(), seed)


def pack_floats(values: list[float]) -> bytes:
    return struct.pack("<" + "f" * len(values), *values)


def pack_u16(values: list[int]) -> bytes:
    return struct.pack("<" + "H" * len(values), *values)


def align4(data: bytearray, pad: int = 0) -> None:
    while len(data) % 4:
        data.append(pad)


def cube_geometry() -> tuple[list[float], list[float], list[float], list[int]]:
    faces = [
        ((0, 0, 1), [(-0.5, -0.5, 0.5), (0.5, -0.5, 0.5), (0.5, 0.5, 0.5), (-0.5, 0.5, 0.5)]),
        ((0, 0, -1), [(0.5, -0.5, -0.5), (-0.5, -0.5, -0.5), (-0.5, 0.5, -0.5), (0.5, 0.5, -0.5)]),
        ((1, 0, 0), [(0.5, -0.5, 0.5), (0.5, -0.5, -0.5), (0.5, 0.5, -0.5), (0.5, 0.5, 0.5)]),
        ((-1, 0, 0), [(-0.5, -0.5, -0.5), (-0.5, -0.5, 0.5), (-0.5, 0.5, 0.5), (-0.5, 0.5, -0.5)]),
        ((0, 1, 0), [(-0.5, 0.5, 0.5), (0.5, 0.5, 0.5), (0.5, 0.5, -0.5), (-0.5, 0.5, -0.5)]),
        ((0, -1, 0), [(-0.5, -0.5, -0.5), (0.5, -0.5, -0.5), (0.5, -0.5, 0.5), (-0.5, -0.5, 0.5)]),
    ]
    positions: list[float] = []
    normals: list[float] = []
    texcoords: list[float] = []
    indices: list[int] = []
    uv = [(0, 0), (1, 0), (1, 1), (0, 1)]
    for face_index, (normal, points) in enumerate(faces):
        first = face_index * 4
        for point, coord in zip(points, uv):
            positions.extend(point)
            normals.extend(normal)
            texcoords.extend(coord)
        indices.extend([first, first + 1, first + 2, first, first + 2, first + 3])
    return positions, normals, texcoords, indices


def plane_geometry() -> tuple[list[float], list[float], list[float], list[int]]:
    positions = [-0.5, 0, -0.5, -0.5, 0, 0.5, 0.5, 0, 0.5, 0.5, 0, -0.5]
    normals = [0, 1, 0] * 4
    texcoords = [0, 0, 0, 1, 1, 1, 1, 0]
    return positions, normals, texcoords, [0, 1, 2, 0, 2, 3]


def add_accessor(blob: bytearray, views: list[dict], accessors: list[dict], raw: bytes, kind: str, component: int, count: int, target: int, minmax: tuple[list[float], list[float]] | None = None) -> int:
    align4(blob)
    offset = len(blob)
    blob.extend(raw)
    view = {"buffer": 0, "byteOffset": offset, "byteLength": len(raw), "target": target}
    views.append(view)
    accessor = {"bufferView": len(views) - 1, "componentType": component, "count": count, "type": kind}
    if minmax is not None:
        accessor["min"], accessor["max"] = minmax
    accessors.append(accessor)
    return len(accessors) - 1


def make_mesh(blob: bytearray, views: list[dict], accessors: list[dict], shape: str, material: int) -> dict:
    positions, normals, texcoords, indices = plane_geometry() if shape == "plane" else cube_geometry()
    xs, ys, zs = positions[0::3], positions[1::3], positions[2::3]
    pos = add_accessor(blob, views, accessors, pack_floats(positions), "VEC3", 5126, len(xs), 34962, ([min(xs), min(ys), min(zs)], [max(xs), max(ys), max(zs)]))
    nor = add_accessor(blob, views, accessors, pack_floats(normals), "VEC3", 5126, len(xs), 34962)
    uv = add_accessor(blob, views, accessors, pack_floats(texcoords), "VEC2", 5126, len(xs), 34962)
    idx = add_accessor(blob, views, accessors, pack_u16(indices), "SCALAR", 5123, len(indices), 34963)
    return {"primitives": [{"attributes": {"POSITION": pos, "NORMAL": nor, "TEXCOORD_0": uv}, "indices": idx, "material": material}]}


def gltf_translation(x: float, y: float, z: float) -> list[float]:
    return [round(x, 4), round(y, 4), round(-z, 4)]


def yaw_quat(degrees: float) -> list[float]:
    radians = math.radians(-degrees)
    return [0, round(math.sin(radians / 2), 8), 0, round(math.cos(radians / 2), 8)]


def append_node(nodes: list[dict], mesh: int, name: str, x: float, y: float, z: float, scale: tuple[float, float, float], yaw: float = 0) -> None:
    node = {"name": name, "mesh": mesh, "translation": gltf_translation(x, y, z), "scale": [round(v, 4) for v in scale]}
    if abs(yaw) > 0.001:
        node["rotation"] = yaw_quat(yaw)
    nodes.append(node)


def write_glb(path: Path, nodes_spec: list[dict]) -> None:
    blob = bytearray()
    views: list[dict] = []
    accessors: list[dict] = []
    meshes: list[dict] = []
    mesh_lookup: dict[tuple[str, int], int] = {}
    nodes: list[dict] = []
    material_index = {name: idx for idx, (name, *_rest) in enumerate(MATERIALS)}
    for spec in nodes_spec:
        key = (spec["shape"], material_index[spec["material"]])
        if key not in mesh_lookup:
            mesh_lookup[key] = len(meshes)
            meshes.append(make_mesh(blob, views, accessors, key[0], key[1]))
        append_node(nodes, mesh_lookup[key], spec["name"], spec["x"], spec["y"], spec["z"], spec["scale"], spec.get("yaw", 0))
    align4(blob)
    materials = []
    for name, color, metallic, roughness in MATERIALS:
        materials.append({
            "name": name,
            "pbrMetallicRoughness": {"baseColorFactor": color, "metallicFactor": metallic, "roughnessFactor": roughness},
        })
    doc = {
        "asset": {"version": "2.0", "generator": "ProjectUnity industrial arena generator"},
        "materials": materials,
        "buffers": [{"byteLength": len(blob)}],
        "bufferViews": views,
        "accessors": accessors,
        "meshes": meshes,
        "nodes": nodes,
        "scenes": [{"nodes": list(range(len(nodes)))}],
        "scene": 0,
    }
    json_bytes = bytearray(json.dumps(doc, separators=(",", ":")).encode("utf-8"))
    align4(json_bytes, ord(" "))
    glb = bytearray()
    glb.extend(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(json_bytes) + 8 + len(blob)))
    glb.extend(struct.pack("<II", len(json_bytes), 0x4E4F534A))
    glb.extend(json_bytes)
    glb.extend(struct.pack("<II", len(blob), 0x004E4942))
    glb.extend(blob)
    path.write_bytes(glb)


def box(name: str, material: str, x: float, z: float, sx: float, sy: float, sz: float, yaw: float = 0) -> dict:
    return {"name": name, "shape": "cube", "material": material, "x": x, "y": sy / 2, "z": z, "scale": (sx, sy, sz), "yaw": yaw}


def decal(name: str, material: str, x: float, z: float, sx: float, sz: float, y: float = 0.025, yaw: float = 0) -> dict:
    return {"name": name, "shape": "plane", "material": material, "x": x, "y": y, "z": z, "scale": (sx, 1, sz), "yaw": yaw}


def layout_nodes() -> list[dict]:
    nodes = [
        decal("floor_main", "concrete_floor", 0, 0, 60, 34),
        decal("floor_center_wear", "concrete_var_a", 0, 0, 18, 10, 0.027),
        decal("spawn_zone_a_floor", "spawn_a", -26, 0, 6, 24, 0.03),
        decal("spawn_zone_b_floor", "spawn_b", 26, 0, 6, 24, 0.03),
        box("wall_north", "dark_wall", 0, -17.4, 60, 3.2, 1.0),
        box("wall_south", "dark_wall", 0, 17.4, 60, 3.2, 1.0),
        box("wall_west", "dark_wall", -30.4, 0, 1.0, 3.2, 34),
        box("wall_east", "dark_wall", 30.4, 0, 1.0, 3.2, 34),
        box("warehouse_left", "warehouse_roof", -9.2, -2.7, 9.5, 3.6, 8.6),
        box("warehouse_right", "warehouse_roof", 9.4, 2.6, 9.5, 3.6, 8.6),
        box("container_top_blue", "container_blue", 3.5, -10.7, 7.4, 2.45, 2.45),
        box("container_bottom_red", "container_red", -6.7, 12.0, 8.2, 2.35, 2.35),
        box("container_bottom_blue", "container_blue", 11.4, 10.6, 7.4, 2.45, 2.45, -8),
        box("container_green_flank", "container_green", 19.0, -10.6, 2.4, 2.5, 7.2, -12),
    ]
    barriers = [
        (-22, -4, 0.7, 1.2, 6.0, 0), (-20.5, 8.2, 0.7, 1.2, 5.0, 0),
        (-13, 8.5, 5.5, 1.15, 0.7, 0), (-2.0, -5.7, 0.7, 1.15, 6.0, 0),
        (0.0, 5.9, 5.2, 1.15, 0.7, 0), (20.8, -7.8, 0.7, 1.2, 5.0, 0),
        (22.0, 4.0, 0.7, 1.2, 6.0, 0), (14.1, -5.7, 0.7, 1.15, 5.5, 0),
    ]
    for i, (x, z, sx, sy, sz, yaw) in enumerate(barriers, 1):
        nodes.append(box(f"low_barrier_{i:02d}", "low_barrier", x, z, sx, sy, sz, yaw))
    crates = [(-21, -12), (-18.8, -12.2), (-18.7, -9.7), (-7, -11.6), (-5.3, -10.0), (-5.0, 3.0), (14, 5.9), (22, -1.6), (24, -4.0), (24, 11.2), (27, 12.5), (18, 13.2)]
    for i, (x, z) in enumerate(crates, 1):
        nodes.append(box(f"crate_{i:02d}", "wood_crate", x, z, 1.45, 1.45, 1.45, 0))
    for i, x in enumerate([-15, 0, 15], 1):
        nodes.append(decal(f"hazard_center_yellow_{i}", "hazard_yellow", x, -16.15, 3.0, 0.22))
        nodes.append(decal(f"hazard_center_black_{i}", "hazard_black", x + 0.65, -16.15, 1.0, 0.24))
    for i, (x, z) in enumerate([(-28, 15), (28, -15), (-27, -15), (27, 15)], 1):
        nodes.append(box(f"plant_{i}", "plant_green", x, z, 0.9, 0.55, 0.9))
    for i, (x, z, sx, sz, yaw) in enumerate([(-2, 12.8, 9, 0.12, 0), (8, -13.5, 7, 0.12, 0), (-16, 2, 0.12, 8, 0)], 1):
        nodes.append(decal(f"floor_cable_{i}", "cable_dark", x, z, sx, sz, 0.035, yaw))
    return nodes


COLLIDERS = [
    ("wall_north", "walls", 0, -17.4, 60, 3.2, 1.0), ("wall_south", "walls", 0, 17.4, 60, 3.2, 1.0),
    ("wall_west", "walls", -30.4, 0, 1.0, 3.2, 34), ("wall_east", "walls", 30.4, 0, 1.0, 3.2, 34),
    ("warehouse_left", "buildings", -9.2, -2.7, 9.5, 3.6, 8.6), ("warehouse_right", "buildings", 9.4, 2.6, 9.5, 3.6, 8.6),
    ("container_top_blue", "containers", 3.5, -10.7, 7.4, 2.45, 2.45), ("container_bottom_red", "containers", -6.7, 12.0, 8.2, 2.35, 2.35),
    ("container_bottom_blue", "containers", 11.4, 10.6, 7.4, 2.45, 2.45), ("container_green_flank", "containers", 19.0, -10.6, 2.4, 2.5, 7.2),
    ("low_barrier_01", "cover", -22, -4, 0.7, 1.2, 6.0), ("low_barrier_02", "cover", -20.5, 8.2, 0.7, 1.2, 5.0),
    ("low_barrier_03", "cover", -13, 8.5, 5.5, 1.15, 0.7), ("low_barrier_04", "cover", -2.0, -5.7, 0.7, 1.15, 6.0),
    ("low_barrier_05", "cover", 0.0, 5.9, 5.2, 1.15, 0.7), ("low_barrier_06", "cover", 20.8, -7.8, 0.7, 1.2, 5.0),
    ("low_barrier_07", "cover", 22.0, 4.0, 0.7, 1.2, 6.0), ("low_barrier_08", "cover", 14.1, -5.7, 0.7, 1.15, 5.5),
    ("crate_01", "cover", -21, -12, 1.45, 1.45, 1.45), ("crate_02", "cover", -18.8, -12.2, 1.45, 1.45, 1.45),
    ("crate_03", "cover", -18.7, -9.7, 1.45, 1.45, 1.45), ("crate_04", "cover", -7, -11.6, 1.45, 1.45, 1.45),
    ("crate_05", "cover", -5.3, -10.0, 1.45, 1.45, 1.45), ("crate_06", "cover", -5.0, 3.0, 1.45, 1.45, 1.45),
    ("crate_07", "cover", 14, 5.9, 1.45, 1.45, 1.45), ("crate_08", "cover", 22, -1.6, 1.45, 1.45, 1.45),
    ("crate_09", "cover", 24, -4.0, 1.45, 1.45, 1.45), ("crate_10", "cover", 24, 11.2, 1.45, 1.45, 1.45),
    ("crate_11", "cover", 27, 12.5, 1.45, 1.45, 1.45), ("crate_12", "cover", 18, 13.2, 1.45, 1.45, 1.45),
]


def base_entity(entity_id: int, parent: int | None, name: str, position=(0, 0, 0), rotation=(0, 0, 0), scale=(1, 1, 1)) -> dict:
    return {"id": entity_id, "parent": parent, "name": name, "transform": {"position": list(position), "rotationEuler": list(rotation), "scale": list(scale)}}


def write_scene(asset_id: int) -> None:
    entities: list[dict] = [base_entity(1, None, "Industrial Arena")]
    visual = base_entity(2, 1, "Industrial Arena Visual")
    visual["meshRenderer"] = {"modelAssetId": asset_id, "renderable": True, "runtimeCook": {"staticBatchable": True, "mutable": False, "physics": "none", "grabbable": False}}
    entities.append(visual)
    light = base_entity(3, 1, "Arena Directional Light")
    light["light"] = {"type": "directional", "direction": [0.35, -0.82, 0.45], "color": [1.0, 0.96, 0.88], "intensity": 3.2, "range": 0, "innerConeAngle": 0, "outerConeAngle": 0.785398}
    entities.append(light)
    camera = base_entity(4, 1, "Top Down Test Camera", (0, 36, 24))
    camera["camera"] = {"projection": "orthographic", "direction": [0, -0.832, -0.555], "right": [1, 0, 0], "up": [0, 0.555, -0.832], "verticalFovRadians": 1.04719755, "aspectRatio": 1.777778, "xMagnitude": 34, "yMagnitude": 21, "nearPlane": 0.05, "farPlane": 120}
    entities.append(camera)
    next_id = 5
    for team, x in [("A", -26), ("B", 26)]:
        trigger = base_entity(next_id, 1, f"Spawn Zone {team} Trigger", (x, 0.05, 0))
        trigger["collider"] = {"enabled": True, "shape": "box", "size": [6, 0.1, 24], "offset": [0, 0, 0], "radius": 0.5, "height": 2, "trigger": True}
        entities.append(trigger)
        next_id += 1
        for index, z in enumerate([-8, 0, 8], 1):
            entities.append(base_entity(next_id, 1, f"Spawn {team}{index}", (x, 0, z)))
            next_id += 1
    for name, _group, x, z, sx, sy, sz in COLLIDERS:
        entity = base_entity(next_id, 1, f"Collider {name}", (x, sy / 2, z))
        entity["collider"] = {"enabled": True, "shape": "box", "size": [sx, sy, sz], "offset": [0, 0, 0], "radius": 0.5, "height": 2, "trigger": False}
        entities.append(entity)
        next_id += 1
    scene = {"version": 1, "name": "Industrial Arena", "assetDependencies": [{"path": "industrial_arena.glb", "type": "Model", "id": asset_id}], "entities": entities}
    (MAP_DIR / "industrial_arena.scene.json").write_text(json.dumps(scene, indent=2), encoding="utf-8")


def write_metadata(asset_id: int) -> None:
    cover_points = [{"id": f"cover_{i:02d}", "x": x, "y": 0, "z": z, "facing": facing} for i, (x, z, facing) in enumerate([
        (-23, -7, "east"), (-20, 5, "east"), (-14, 10, "north"), (-3, -8, "east"), (1.5, 7, "south"),
        (15, -8, "west"), (21, -4, "west"), (23, 7, "west"), (6, -13, "south"), (10, 13, "north"),
    ], 1)]
    metadata = {
        "id": "industrial_arena_topdown",
        "name": "Industrial Arena",
        "modeSupport": ["1v1", "2v2", "5v5"],
        "size": {"width": 60, "height": 34},
        "teams": {
            "A": {"spawnArea": "left", "spawns": [{"x": -26, "y": 0, "z": -8}, {"x": -26, "y": 0, "z": 0}, {"x": -26, "y": 0, "z": 8}]},
            "B": {"spawnArea": "right", "spawns": [{"x": 26, "y": 0, "z": -8}, {"x": 26, "y": 0, "z": 0}, {"x": 26, "y": 0, "z": 8}]},
        },
        "collisionGroups": ["walls", "cover", "buildings", "containers"],
        "coverPoints": cover_points,
        "blockLineOfSight": True,
        "visualAsset": {"path": "industrial_arena.glb", "assetId": asset_id},
        "obstacles": [{"id": name, "group": group, "position": {"x": x, "y": sy / 2, "z": z}, "size": {"x": sx, "y": sy, "z": sz}, "blocksLineOfSight": group != "cover"} for name, group, x, z, sx, sy, sz in COLLIDERS],
        "navigation": {"playableBounds": {"minX": -29, "maxX": 29, "minZ": -16, "maxZ": 16}, "preferredLaneWidth": [3, 5], "lanes": ["north flank", "central contest", "south flank"]},
    }
    (MAP_DIR / "industrial_arena_metadata.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")


def write_docs(asset_id: int) -> None:
    manifest = {
        "id": "industrial_arena_topdown",
        "name": "Industrial Arena",
        "scene": "industrial_arena.scene.json",
        "metadata": "industrial_arena_metadata.json",
        "visualAsset": "industrial_arena.glb",
        "visualAssetId": asset_id,
        "assetDependencies": [{"path": "industrial_arena.glb", "type": "Model", "id": asset_id}],
    }
    (MAP_DIR / "industrial_arena.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    palette = {name: {"baseColor": color, "metallic": metallic, "roughness": roughness} for name, color, metallic, roughness in MATERIALS}
    (MATERIALS_DIR / "industrial_palette.json").write_text(json.dumps(palette, indent=2), encoding="utf-8")
    readme = f"""# Industrial Arena

Mapa 3D low-poly para ProjectUnity, pensado para shooter top-down 1v1, 2v2 y 5v5.

## Carga

Abre `Project/Assets/Maps/industrial_arena/industrial_arena.scene.json` desde Archivo > Abrir escena. El modelo visual principal es `industrial_arena.glb` con asset id `{asset_id}` y queda cocinado en `Cache/Assets` como `.ffult`.

## Archivos

- `industrial_arena.scene.json`: escena cargable con luz, camara top-down, visual mesh, spawns y colliders.
- `industrial_arena.glb`: geometria visual modular del mapa completo.
- `industrial_arena_metadata.json`: spawns, cover points, obstaculos y grupos de colision para gameplay.
- `props/*.glb`: props modulares reutilizables para editar o rearmar variantes.
- `materials/industrial_palette.json`: colores PBR simples usados por los GLB.

## Edicion

Los spawns se editan en `industrial_arena_metadata.json` y en las entidades `Spawn A*` / `Spawn B*` de la escena. Los obstaculos bloqueantes tienen entidades `Collider *`; cambia `transform.position` y `collider.size` manteniendo pasillos de 3 a 5 unidades.

## Optimizacion

La escena usa un GLB visual unico, materiales simples sin texturas externas, props low-poly reutilizados y colliders de caja. Las decoraciones pequeñas no tienen colision y las coberturas usan geometria/colliders simples para mantener bajo el coste de render y fisica.
"""
    (MAP_DIR / "MAP_INDUSTRIAL_ARENA.md").write_text(readme, encoding="utf-8")


def main() -> None:
    MAP_DIR.mkdir(parents=True, exist_ok=True)
    PROPS_DIR.mkdir(parents=True, exist_ok=True)
    MATERIALS_DIR.mkdir(parents=True, exist_ok=True)
    write_glb(MAP_DIR / "industrial_arena.glb", layout_nodes())
    asset_id = model_asset_id(MAP_DIR / "industrial_arena.glb")
    write_scene(asset_id)
    write_metadata(asset_id)
    write_docs(asset_id)
    prop_specs = {
        "wall_segment.glb": [box("wall_segment", "dark_wall", 0, 0, 6, 3.2, 1)],
        "concrete_barrier.glb": [box("concrete_barrier", "low_barrier", 0, 0, 4, 1.15, 0.7)],
        "container_blue.glb": [box("container_blue", "container_blue", 0, 0, 7.4, 2.45, 2.45)],
        "container_red.glb": [box("container_red", "container_red", 0, 0, 7.4, 2.45, 2.45)],
        "container_green.glb": [box("container_green", "container_green", 0, 0, 7.4, 2.45, 2.45)],
        "crate_small.glb": [box("crate_small", "wood_crate", 0, 0, 1.45, 1.45, 1.45)],
        "crate_stack.glb": [box("crate_a", "wood_crate", -0.55, 0, 1.2, 1.2, 1.2), box("crate_b", "wood_crate", 0.65, 0.15, 1.2, 1.2, 1.2), box("crate_top", "wood_crate", 0.05, 0.05, 1.0, 2.25, 1.0)],
        "warehouse_block.glb": [box("warehouse_block", "warehouse_roof", 0, 0, 9.5, 3.6, 8.6)],
    }
    for filename, specs in prop_specs.items():
        write_glb(PROPS_DIR / filename, specs)
    print(asset_id)


if __name__ == "__main__":
    main()
