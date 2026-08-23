"""
Fix embedded /TestMode/ soft paths inside MetaLandRoad landscape materials.

Usage (UE Editor):
  Tools -> Execute Python Script -> select this file

Or Output Log:
  py "D:/UE_Project/WorldDev/Plugins/MetaLandRoad/Scripts/FixTestModeMaterialReferences.py"
"""

import unreal

CRITICAL_ASSETS = [
    "/MetaLandRoad/Landscape/BlueprintBrushes/MPC/MPC_Landscape",
    "/MetaLandRoad/Material/Landscape/Brush/CompositeMeshBodyTexture",
    "/MetaLandRoad/Material/Landscape/RenderMeshDepths",
    "/MetaLandRoad/Material/Landscape/MF/ReadCachedDistanceField",
    "/MetaLandRoad/Material/Landscape/DummyTexture",
    "/MetaLandRoad/Material/Landscape/CacheDistanceField",
    "/MetaLandRoad/Material/Landscape/LandmassBrush_Blender",
    "/MetaLandRoad/Material/Landscape/Brush/RoadMeshBrush_Weightmap",
]

MPC_PATH = "/MetaLandRoad/Landscape/BlueprintBrushes/MPC/MPC_Landscape"


def _load_asset(path: str):
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.log_warning(f"[FixTestMode] Missing asset: {path}")
        return None
    return unreal.EditorAssetLibrary.load_asset(path)


def _fix_material_collection_params(material: unreal.Material) -> int:
    fixed = 0
    mpc = _load_asset(MPC_PATH)
    if mpc is None:
        return fixed

    expressions = unreal.MaterialEditingLibrary.get_all_function_inputs_or_outputs(material)
    # Fallback: iterate material expressions via asset registry of expressions is limited in Python.
    # Re-save after redirect is the main fix; this ensures MPC assignment on known broken materials.
    for expr in unreal.MaterialEditingLibrary.get_material_expressions(material):
        if isinstance(expr, unreal.MaterialExpressionCollectionParameter):
            if expr.collection is None or "TestMode" in str(expr.collection.get_path_name()):
                expr.modify()
                expr.set_editor_property("collection", mpc)
                fixed += 1
                unreal.log(f"[FixTestMode] Set MPC on {material.get_name()}: {expr.get_name()}")
    return fixed


def main():
    total_fixed = 0
    saved = []

    for asset_path in CRITICAL_ASSETS:
        asset = _load_asset(asset_path)
        if asset is None:
            continue

        if isinstance(asset, unreal.Material):
            total_fixed += _fix_material_collection_params(asset)

        unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
        saved.append(asset_path)
        unreal.log(f"[FixTestMode] Saved: {asset_path}")

    # Resave entire landscape content tree so redirector fixup rewrites soft paths.
    landscape_root = "/MetaLandRoad/Material/Landscape"
    if unreal.EditorAssetLibrary.does_directory_exist(landscape_root):
        paths = unreal.EditorAssetLibrary.list_assets(landscape_root, recursive=True, include_folder=False)
        for path in paths:
            if path in saved:
                continue
            asset = _load_asset(path)
            if asset is not None:
                unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)

    unreal.log(f"[FixTestMode] Done. MPC nodes fixed: {total_fixed}. Critical assets saved: {len(saved)}")
    unreal.log("[FixTestMode] Restart editor, then verify Output Log has no /TestMode/ warnings.")


if __name__ == "__main__":
    main()
