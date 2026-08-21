# Checkpoint: centerline profile before box stamp + junction height

Frozen so we can roll back the height path that already has landscape keys, hybrid falloff, and Combined ping-pong.

## Git

- Branch: `checkpoint/road-profile-v2-pre-box`
- Tag: `road-profile-v2-pre-box`
- Working branch after this: `feat/height-box-junction`

```
git checkout road-profile-v2-pre-box
```

or restore only the live shader:

```
copy Plugins\MetaLandRoad\Shaders\Private\RoadLandscapeProfile_Version02.usf Plugins\MetaLandRoad\Shaders\Private\RoadLandscapeProfile.usf
```

Version01 (older iso, no slope noise): `RoadLandscapeProfile_Version01.usf`.

## What this version is

- Height: CPU centerline samples + fullscreen PS `RoadLandscapeProfile.usf` (Version02).
- Keys: `SideOffset` / `SideFalloff` / `HeightOffset` / `LayerFactor` along road S.
- Junctions: **not** in the height stamp. `GetJunctionMeshGenerationSpans` cuts the hub out of the road. Intersection actors still fall through toward Capture/JumpFlood if they are in the brush list.
- Shader `RoadBounds` early-out exists, but the pass still draws the full Combined RT.

## Known editor limits (do not lose these)

1. Junction has **no landscape keys**. Height on the hub is only Actor `Terrain → RoadHeightmapSettings` (ZOffset / FalloffWidth). There is no key gizmo on the intersection.
2. That junction height **does not** change attached roads. Road stamps stop at the trim cut.
3. Do not invent a second key track on the junction in a rush. Keep road keys as the source of truth; junction Terrain is the first hub control.

## After this checkpoint

Box-region dispatch (copy Combined, draw only the influence rect) and junction hub samples + mouth blend into attached roads.
