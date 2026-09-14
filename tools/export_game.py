#!/usr/bin/env python3
"""Package a ProjectUnity scene into a first standalone Windows build."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


SCRIPT_MODULE_NAMES = {
    "win32": "ProjectUnityGameScripts.dll",
    "cygwin": "ProjectUnityGameScripts.dll",
    "darwin": "libProjectUnityGameScripts.dylib",
}


def script_module_name() -> str:
    return SCRIPT_MODULE_NAMES.get(sys.platform, "libProjectUnityGameScripts.so")


def run(command: list[str], cwd: Path) -> None:
    print("+ " + " ".join(command))
    subprocess.run(command, cwd=cwd, check=True)


def find_cmake(explicit: Path | None) -> str:
    if explicit is not None:
        return str(explicit)
    from_path = shutil.which("cmake")
    if from_path is not None:
        return from_path
    candidates = [
        Path(r"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"),
        Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"),
        Path(r"C:\Program Files\CMake\bin\cmake.exe"),
    ]
    for candidate in candidates:
        if candidate.exists():
            return str(candidate)
    raise FileNotFoundError("cmake was not found. Pass --cmake <path-to-cmake> or add cmake to PATH.")


def clean_dir(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path)
    os.makedirs(path, exist_ok=True)


def copy_tree(source: Path, destination: Path) -> None:
    if not source.exists():
        raise FileNotFoundError(f"Required directory does not exist: {source}")
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(source, destination)


def collect_asset_ids(value: object, output: set[int], allow_plain_id: bool = False) -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            if key in {"assetId", "modelAssetId", "generatedModelAssetId"} and isinstance(child, int) and child > 0:
                output.add(child)
            if allow_plain_id and key == "id" and isinstance(child, int) and child > 0:
                output.add(child)
            collect_asset_ids(child, output, allow_plain_id)
        return
    if isinstance(value, list):
        for child in value:
            collect_asset_ids(child, output, allow_plain_id)


def copy_scene_assets(scene_path: Path, source_cache: Path, destination_cache: Path, copy_all_assets: bool) -> None:
    if copy_all_assets:
        copy_tree(source_cache, destination_cache)
        return

    scene = json.loads(scene_path.read_text(encoding="utf-8"))
    asset_ids: set[int] = set()
    collect_asset_ids(scene.get("assetDependencies", []), asset_ids, allow_plain_id=True)
    collect_asset_ids(scene.get("entities", []), asset_ids)

    destination_cache.mkdir(parents=True, exist_ok=True)
    missing: list[str] = []
    for asset_id in sorted(asset_ids):
        copied_any = False
        for suffix in (".ffult", ".asset.json"):
            source = source_cache / f"{asset_id}{suffix}"
            if source.exists():
                shutil.copy2(source, destination_cache / source.name)
                copied_any = True
        if not copied_any:
            missing.append(str(asset_id))
    if missing:
        raise FileNotFoundError("Missing cooked assets in cache: " + ", ".join(missing))


def find_target_file(build_dir: Path, file_name: str, config: str | None = None) -> Path:
    matches = sorted(build_dir.rglob(file_name))
    if not matches:
        raise FileNotFoundError(f"Could not find {file_name} under {build_dir}")
    if config is not None:
        normalized = config.lower()
        for match in matches:
            if any(part.lower() == normalized for part in match.parts):
                return match
    return matches[0]


def executable_file_name(game_name: str) -> str:
    safe = "".join(ch if ch.isalnum() or ch in {"-", "_"} else "_" for ch in game_name).strip("_")
    if not safe:
        safe = "ProjectUnityGame"
    return f"{safe}.exe" if os.name == "nt" else safe


def copy_player_runtime(player_exe: Path, output_dir: Path, game_name: str) -> None:
    source_dir = player_exe.parent
    game_exe_name = executable_file_name(game_name)
    for item in source_dir.iterdir():
        if item.suffix.lower() == ".pdb":
            continue
        destination_name = game_exe_name if item.resolve() == player_exe.resolve() else item.name
        destination = output_dir / destination_name
        if item.is_dir():
            copy_tree(item, destination)
        else:
            shutil.copy2(item, destination)


def copy_msvc_runtime(output_dir: Path) -> None:
    if os.name != "nt":
        return
    roots: list[Path] = []
    vc_install = os.environ.get("VCINSTALLDIR")
    if vc_install:
        roots.append(Path(vc_install).parent / "Redist" / "MSVC")
    roots.extend(
        [
            Path(r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC"),
            Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC"),
            Path(r"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC"),
        ]
    )
    runtime_dirs: list[Path] = []
    for root in roots:
        if root.exists():
            runtime_dirs.extend(root.glob("*/x64/Microsoft.VC*.CRT"))
            runtime_dirs.extend(root.glob("v*/x64/Microsoft.VC*.CRT"))
    runtime_dirs = sorted({path.resolve() for path in runtime_dirs if path.is_dir()})
    if not runtime_dirs:
        print("warning: MSVC redistributable runtime directory was not found; exported build may need VC runtime installed.")
        return
    runtime_dir = runtime_dirs[-1]
    for name in ("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "concrt140.dll"):
        source = runtime_dir / name
        if source.exists():
            shutil.copy2(source, output_dir / name)


def write_manifest(output_dir: Path, game_name: str, width: int, height: int) -> None:
    manifest = {
        "version": 1,
        "name": game_name,
        "scene": "Content/Scenes/main.scene.json",
        "assetCache": "Content/Assets",
        "scripts": f"Scripts/{script_module_name()}",
        "requireScripts": True,
        "windowWidth": width,
        "windowHeight": height,
    }
    (output_dir / "game.projectunity.json").write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
    )


def zip_build(output_dir: Path) -> Path:
    archive_base = output_dir.with_suffix("")
    archive_path = shutil.make_archive(str(archive_base), "zip", output_dir)
    return Path(archive_path)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Export a ProjectUnity game folder.")
    parser.add_argument("--source-dir", type=Path, default=Path.cwd(), help="ProjectUnity source directory.")
    parser.add_argument("--build-dir", type=Path, default=Path("build/dev-editor"), help="Configured CMake build directory.")
    parser.add_argument("--config", default="Release", help="CMake configuration to build.")
    parser.add_argument("--scene", type=Path, required=True, help="Scene JSON to package.")
    parser.add_argument("--asset-cache", type=Path, default=Path("Cache/Assets"), help="Cooked asset cache directory.")
    parser.add_argument("--output", type=Path, required=True, help="Output game directory.")
    parser.add_argument("--name", default="ProjectUnityGame", help="Game/window name.")
    parser.add_argument("--width", type=int, default=1280, help="Player window width.")
    parser.add_argument("--height", type=int, default=720, help="Player window height.")
    parser.add_argument("--cmake", type=Path, help="Path to cmake executable.")
    parser.add_argument("--all-assets", action="store_true", help="Copy the whole asset cache instead of scene dependencies.")
    parser.add_argument("--skip-build", action="store_true", help="Do not run CMake build first.")
    parser.add_argument("--zip", action="store_true", help="Create a .zip next to the output folder.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source_dir = args.source_dir.resolve()
    build_dir = (source_dir / args.build_dir).resolve() if not args.build_dir.is_absolute() else args.build_dir.resolve()
    scene_path = (source_dir / args.scene).resolve() if not args.scene.is_absolute() else args.scene.resolve()
    asset_cache = (source_dir / args.asset_cache).resolve() if not args.asset_cache.is_absolute() else args.asset_cache.resolve()
    output_dir = (source_dir / args.output).resolve() if not args.output.is_absolute() else args.output.resolve()

    if not scene_path.exists():
        raise FileNotFoundError(f"Scene does not exist: {scene_path}")
    if not asset_cache.exists():
        raise FileNotFoundError(f"Asset cache does not exist: {asset_cache}")

    if not args.skip_build:
        cmake = find_cmake(args.cmake)
        run([cmake, "--build", str(build_dir), "--config", args.config, "--target", "projectunity_player"], source_dir)
        run([cmake, "--build", str(build_dir), "--config", args.config, "--target", "ProjectUnityGameScripts"], source_dir)

    player_exe_name = "projectunity_player.exe" if os.name == "nt" else "projectunity_player"
    player_exe = find_target_file(build_dir, player_exe_name, args.config)
    scripts_module = source_dir / "Project" / "Binaries" / "Scripts" / args.config / script_module_name()
    if not scripts_module.exists():
        scripts_module = find_target_file(source_dir / "Project" / "Binaries" / "Scripts", script_module_name(), args.config)

    clean_dir(output_dir)
    copy_player_runtime(player_exe, output_dir, args.name)
    copy_msvc_runtime(output_dir)

    scenes_dir = output_dir / "Content" / "Scenes"
    scenes_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(scene_path, scenes_dir / "main.scene.json")

    copy_scene_assets(scene_path, asset_cache, output_dir / "Content" / "Assets", args.all_assets)

    scripts_dir = output_dir / "Scripts"
    scripts_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(scripts_module, scripts_dir / script_module_name())

    write_manifest(output_dir, args.name, args.width, args.height)

    archive = zip_build(output_dir) if args.zip else None
    print(f"Exported game: {output_dir}")
    if archive is not None:
        print(f"Exported zip: {archive}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
