# MetaLandRoad Landscape Brush Materials

## Waiting for edit layer resources

UE shows this when brush materials in `GetRenderDependencies()` **never finish compiling**.

Common cause in this project: loading broken `/MetaLandRoad/Material/Landscape/*` forks that still embed `/TestMode/` soft paths.

**Runtime policy:** `SetupDefaultMaterials()` loads **official Water/Landmass first**. Local MetaLandRoad copies are fallback only.

## Official paths (current default)

| Slot | Path |
|------|------|
| MPC | `/Landmass/Landscape/BlueprintBrushes/MPC/MPC_Landscape` |
| Angle / Width / Weightmap | `/Water/Materials/Brushes/MeshBrush_*` |
| Composite | `/Water/Materials/Brushes/CompositeWaterBodyTexture` |
| Cache DF / Render depth | `/Landmass/Landscape/BlueprintBrushes/Materials/...` |
| JumpFlood | `/Landmass/DistanceFields/Materials/...` |

## Optional local copies

Only after fixing / overwriting broken forks:

1. Tools → Execute Python Script → `CopyBrushMaterialsToMetaLandRoad.py`
2. Open each copied material, confirm no Missing / TestMode
3. Then you may switch defaults to `/MetaLandRoad/...` if desired

Until then, keep using official Water/Landmass.
