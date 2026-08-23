"""
Apply official Water + Landmass landscape brush materials.

Replaces broken /MetaLandRoad/Material/Landscape/* fork assets (internal /TestMode/ refs)
with Epic's working brush pipeline materials.

Usage (UE Editor open, map loaded):
  Tools -> Execute Python Script -> this file

Or Output Log:
  py "D:/UE_Project/WorldDev/Plugins/MetaLandRoad/Scripts/ApplyOfficialBrushMaterials.py"

Optional duplicate mode (copy Water/Landmass assets into MetaLandRoad Content):
  py ".../ApplyOfficialBrushMaterials.py" duplicate
"""

import unreal

OFFICIAL_MATERIALS = {
    "BrushAngleFalloffMaterial": "/Water/Materials/Brushes/MeshBrush_Angle.MeshBrush_Angle",
    "BrushWidthFalloffMaterial": "/Water/Materials/Brushes/MeshBrush_Width.MeshBrush_Width",
    "WeightmapMaterial": "/Water/Materials/Brushes/MeshBrush_Weightmap.MeshBrush_Weightmap",
    "DistanceFieldCacheMaterial": "/Landmass/Landscape/BlueprintBrushes/Materials/CacheDistanceField.CacheDistanceField",
    "CompositeRoadBodyTextureMaterial": "/Water/Materials/Brushes/CompositeWaterBodyTexture.CompositeWaterBodyTexture",
    "RenderRoadSplineDepthMaterial": "/Landmass/Landscape/BlueprintBrushes/Materials/RenderSplineDepths.RenderSplineDepths",
    "JumpStepMaterial": "/Landmass/DistanceFields/Materials/JumpFloodStep.JumpFloodStep",
    "BlurEdgesMaterial": "/Landmass/DistanceFields/Materials/BlurEdges.BlurEdges",
    "FindEdgesMaterial": "/Landmass/DistanceFields/Materials/DetectEdges.DetectEdges",
}

OFFICIAL_MPC = "/Landmass/Landscape/BlueprintBrushes/MPC/MPC_Landscape.MPC_Landscape"

# Optional: duplicate official assets over broken MetaLandRoad fork paths.
DUPLICATE_MAP = {
    "/Water/Materials/Brushes/MeshBrush_Angle": "/MetaLandRoad/Material/Landscape/Brush/MeshBrush_Angle",
    "/Water/Materials/Brushes/MeshBrush_Width": "/MetaLandRoad/Material/Landscape/Brush/MeshBrush_Width",
    "/Water/Materials/Brushes/MeshBrush_Weightmap": "/MetaLandRoad/Material/Landscape/Brush/MeshBrush_Weightmap",
    "/Water/Materials/Brushes/CompositeWaterBodyTexture": "/MetaLandRoad/Material/Landscape/Brush/CompositeWaterBodyTexture",
    "/Landmass/Landscape/BlueprintBrushes/Materials/CacheDistanceField": "/MetaLandRoad/Material/Landscape/CacheDistanceField",
    "/Landmass/Landscape/BlueprintBrushes/Materials/RenderSplineDepths": "/MetaLandRoad/Material/Landscape/RenderMeshDepths",
    "/Landmass/Landscape/BlueprintBrushes/MPC/MPC_Landscape": "/MetaLandRoad/Landscape/BlueprintBrushes/MPC/MPC_Landscape",
}


def _load(path: str):
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.log_error(f"[ApplyOfficialBrush] Missing: {path}")
        return None
    return unreal.EditorAssetLibrary.load_asset(path)


def _duplicate_official_assets() -> int:
    count = 0
    for src, dst in DUPLICATE_MAP.items():
        if not unreal.EditorAssetLibrary.does_asset_exist(src):
            unreal.log_warning(f"[ApplyOfficialBrush] Skip duplicate, missing source: {src}")
            continue

        dst_dir = "/".join(dst.split("/")[:-1])
        if not unreal.EditorAssetLibrary.does_directory_exist(dst_dir):
            unreal.EditorAssetLibrary.make_directory(dst_dir)

        if unreal.EditorAssetLibrary.does_asset_exist(dst):
            unreal.EditorAssetLibrary.delete_asset(dst)

        new_asset = unreal.EditorAssetLibrary.duplicate_asset(src, dst)
        if new_asset is None:
            unreal.log_error(f"[ApplyOfficialBrush] Duplicate failed: {src} -> {dst}")
            continue

        unreal.EditorAssetLibrary.save_loaded_asset(new_asset, only_if_is_dirty=False)
        unreal.log(f"[ApplyOfficialBrush] Duplicated: {src} -> {dst}")
        count += 1

    return count


def _apply_to_actor(actor) -> bool:
    if actor.get_class().get_name() != "WorldSceneManagement":
        return False

    actor.modify()
    applied = 0
    for prop_name, asset_path in OFFICIAL_MATERIALS.items():
        material = _load(asset_path)
        if material is None:
            continue
        actor.set_editor_property(prop_name, material)
        applied += 1

    actor.set_editor_property("DrawCanvasMaterial", None)

    jump_flood = actor.get_editor_property("JumpFloodComponent2D")
    if jump_flood is not None:
        jump_flood.modify()
        for prop_name in ("JumpStepMaterial", "BlurEdgesMaterial", "FindEdgesMaterial"):
            material = _load(OFFICIAL_MATERIALS[prop_name])
            if material is not None:
                jump_flood.set_editor_property(prop_name, material)

    unreal.log(f"[ApplyOfficialBrush] Applied {applied} materials on {actor.get_name()}")
    return applied > 0


def _apply_to_all_world_scene_management() -> int:
    editor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if editor_subsystem is None:
        unreal.log_error("[ApplyOfficialBrush] EditorActorSubsystem unavailable.")
        return 0

    actors = editor_subsystem.get_all_level_actors()
    changed = 0
    for actor in actors:
        if _apply_to_actor(actor):
            changed += 1

    if changed > 0:
        unreal.EditorLevelLibrary.save_current_level()
        unreal.log(f"[ApplyOfficialBrush] Saved level with {changed} WorldSceneManagement actor(s).")
    else:
        unreal.log_warning("[ApplyOfficialBrush] No WorldSceneManagement actors found in current level.")

    return changed


def main():
    import sys

    if len(sys.argv) > 1 and sys.argv[1].lower() == "duplicate":
        dup_count = _duplicate_official_assets()
        unreal.log(f"[ApplyOfficialBrush] Duplicate pass finished: {dup_count} asset(s).")

    mpc = _load(OFFICIAL_MPC)
    if mpc is None:
        unreal.log_error("[ApplyOfficialBrush] MPC_Landscape missing — enable Landmass plugin.")

    actor_count = _apply_to_all_world_scene_management()
    unreal.log(
        f"[ApplyOfficialBrush] Done. Actors updated: {actor_count}. "
        "Recompile MetaLandRoad C++ if not done yet, then drag spline to test terrain."
    )


if __name__ == "__main__":
    main()
