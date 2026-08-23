"""
Copy official Water + Landmass landscape brush assets into MetaLandRoad Content,
then apply them to WorldSceneManagement actors in the current level.

Run once after pulling the plugin (replaces broken /TestMode/ fork uassets).

Usage (UE Editor, map open):
  Tools -> Execute Python Script -> this file

Or Output Log:
  py "D:/UE_Project/WorldDev/Plugins/MetaLandRoad/Scripts/CopyBrushMaterialsToMetaLandRoad.py"
"""

import unreal

# source (official) -> destination (MetaLandRoad Content)
COPY_MAP = {
    "/Water/Materials/Brushes/MeshBrush_Angle": "/MetaLandRoad/Material/Landscape/Brush/MeshBrush_Angle",
    "/Water/Materials/Brushes/MeshBrush_Width": "/MetaLandRoad/Material/Landscape/Brush/MeshBrush_Width",
    "/Water/Materials/Brushes/MeshBrush_Weightmap": "/MetaLandRoad/Material/Landscape/Brush/MeshBrush_Weightmap",
    "/Water/Materials/Brushes/CompositeWaterBodyTexture": "/MetaLandRoad/Material/Landscape/Brush/CompositeMeshBodyTexture",
    "/Landmass/Landscape/BlueprintBrushes/Materials/CacheDistanceField": "/MetaLandRoad/Material/Landscape/CacheDistanceField",
    "/Landmass/Landscape/BlueprintBrushes/Materials/RenderSplineDepths": "/MetaLandRoad/Material/Landscape/RenderMeshDepths",
    "/Landmass/Landscape/BlueprintBrushes/MPC/MPC_Landscape": "/MetaLandRoad/Landscape/BlueprintBrushes/MPC/MPC_Landscape",
    "/Landmass/DistanceFields/Materials/JumpFloodStep": "/MetaLandRoad/Material/Landscape/DistanceFields/JumpFloodStep",
    "/Landmass/DistanceFields/Materials/BlurEdges": "/MetaLandRoad/Material/Landscape/DistanceFields/BlurEdges",
    "/Landmass/DistanceFields/Materials/DetectEdges": "/MetaLandRoad/Material/Landscape/DistanceFields/DetectEdges",
}

LOCAL_MATERIALS = {
    "BrushAngleFalloffMaterial": "/MetaLandRoad/Material/Landscape/Brush/MeshBrush_Angle.MeshBrush_Angle",
    "BrushWidthFalloffMaterial": "/MetaLandRoad/Material/Landscape/Brush/MeshBrush_Width.MeshBrush_Width",
    "WeightmapMaterial": "/MetaLandRoad/Material/Landscape/Brush/MeshBrush_Weightmap.MeshBrush_Weightmap",
    "DistanceFieldCacheMaterial": "/MetaLandRoad/Material/Landscape/CacheDistanceField.CacheDistanceField",
    "CompositeRoadBodyTextureMaterial": "/MetaLandRoad/Material/Landscape/Brush/CompositeMeshBodyTexture.CompositeMeshBodyTexture",
    "RenderRoadSplineDepthMaterial": "/MetaLandRoad/Material/Landscape/RenderMeshDepths.RenderMeshDepths",
    "JumpStepMaterial": "/MetaLandRoad/Material/Landscape/DistanceFields/JumpFloodStep.JumpFloodStep",
    "BlurEdgesMaterial": "/MetaLandRoad/Material/Landscape/DistanceFields/BlurEdges.BlurEdges",
    "FindEdgesMaterial": "/MetaLandRoad/Material/Landscape/DistanceFields/DetectEdges.DetectEdges",
}


def _load(path: str):
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.log_error(f"[CopyBrushMaterials] Missing: {path}")
        return None
    return unreal.EditorAssetLibrary.load_asset(path)


def _duplicate_all() -> int:
    count = 0
    for src, dst in COPY_MAP.items():
        if not unreal.EditorAssetLibrary.does_asset_exist(src):
            unreal.log_warning(f"[CopyBrushMaterials] Skip, source missing: {src}")
            continue

        dst_dir = "/".join(dst.split("/")[:-1])
        if not unreal.EditorAssetLibrary.does_directory_exist(dst_dir):
            unreal.EditorAssetLibrary.make_directory(dst_dir)

        if unreal.EditorAssetLibrary.does_asset_exist(dst):
            unreal.EditorAssetLibrary.delete_asset(dst)

        new_asset = unreal.EditorAssetLibrary.duplicate_asset(src, dst)
        if new_asset is None:
            unreal.log_error(f"[CopyBrushMaterials] Duplicate failed: {src} -> {dst}")
            continue

        unreal.EditorAssetLibrary.save_loaded_asset(new_asset, only_if_is_dirty=False)
        unreal.log(f"[CopyBrushMaterials] Copied: {src} -> {dst}")
        count += 1

    return count


def _apply_local_to_actors() -> int:
    editor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if editor_subsystem is None:
        return 0

    changed = 0
    for actor in editor_subsystem.get_all_level_actors():
        if actor.get_class().get_name() != "WorldSceneManagement":
            continue

        actor.modify()
        for prop_name, asset_path in LOCAL_MATERIALS.items():
            material = _load(asset_path)
            if material is not None:
                actor.set_editor_property(prop_name, material)

        actor.set_editor_property("DrawCanvasMaterial", None)

        jump_flood = actor.get_editor_property("JumpFloodComponent2D")
        if jump_flood is not None:
            jump_flood.modify()
            for prop_name in ("JumpStepMaterial", "BlurEdgesMaterial", "FindEdgesMaterial"):
                material = _load(LOCAL_MATERIALS[prop_name])
                if material is not None:
                    jump_flood.set_editor_property(prop_name, material)

        changed += 1
        unreal.log(f"[CopyBrushMaterials] Applied local materials on {actor.get_name()}")

    if changed > 0:
        unreal.EditorLevelLibrary.save_current_level()

    return changed


def main():
    dup_count = _duplicate_all()
    actor_count = _apply_local_to_actors()
    unreal.log(
        f"[CopyBrushMaterials] Done. Copied {dup_count} asset(s), updated {actor_count} WorldSceneManagement actor(s). "
        "Recompile MetaLandRoad, restart editor, rebuild WorldEditor layer."
    )


if __name__ == "__main__":
    main()
