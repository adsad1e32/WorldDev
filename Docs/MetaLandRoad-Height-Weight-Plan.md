# MetaLandRoad Height / Weight（方案 B 已落地）

对照图一 Landscape Key、图二/图三旧 Actor `Road Body Weightmap Settings`。  
**方案 B 已定：** 高度只活在 Landscape Key。Road 权软边也挂在 Landscape Key（`RoadWeightFalloffWidth`，0 = 用 Actor 默认）。路口无 key，仍读 Actor。权图由 Profile PS 画。

相关契约：`Docs/Agent-Conventions.md`、skill `metal-androad-incremental-dev`。

---

## 1. 现在的真值（一份高度、一份 Road 权）

```
Landscape Key（图一）
  LayerFactor
  SideFalloff     面板：Height Side Falloff
  SideOffset      面板：Height Side Offset
  HeightOffset
  RoadWeightFalloffWidth   面板：Road Weight Falloff Width（0 = inherit Actor）

Actor.Terrain
  bAffectsLandscape
  RoadWeightFalloffWidth    默认 / 路口 / 未设 key 的回退（默认 180）
  RoadHeightmapSettings     隐藏，不再当高度权威
  RoadBodyWeightmapSettings 隐藏；加载时把旧 Road.FalloffWidth 迁到上面那个 float，然后清空

Stamp
  高度：只读 Key 四个高度字段（路口 hub 仍用 JunctionSample* + HeightOffset）
  Road 权：Key.RoadWeightFalloffWidth → Sample.Falloff.w，WeightMode==1 写入 Falloff.y；<1 则 Actor
  Dirty：已停。AffectedWeightmapLayers 只留 Road。
```

旧三套真值（Key + Actor Heightmap + Actor TMap Road/Dirty×7）已从热路径拆掉。

---

## 2. 已落地

| 项 | 结果 |
|---|---|
| 图一 Side* 改名 | DisplayName = **Height Side Falloff / Height Side Offset**。字段名仍是 `SideFalloff` / `SideOffset`（不迁档） |
| 方案 B | `AMoKuEditBaseActor::RoadWeightFalloffWidth` |
| 图二 TMap | Details 不再显示。`PostLoad` / `EnsureDefault` 迁完即 `Empty()` |
| Actor Heightmap / Effects | Details 不再显示 |
| Profile 高度 | `BuildRoadProfileSamples` 不再读 `RoadHeightmapSettings.FalloffSettings` |
| Profile 权图 | `ApplyRoadWeightFalloffToProfileSamples`；`WeightMode==1` 用 FalloffWidth 做软边 |
| 层过滤 | `AffectsWeightmapLayer`（只认 Road），不再查 TMap |

---

## 3. PS 怎么吃

`RoadLandscapeProfile.usf`：

- `Sample.Falloff.xy` = Offset / Falloff
- 高度：Height Side Offset / Height Side Falloff
- Road 权（`WeightMode==1`）：Offset=0，Falloff=`RoadWeightFalloffWidth`
- Dirty：不再 stamp。Toolkit Landscape Key 的 **Road Weight Falloff Width** 写回该 key（0 显示为 Actor 默认）

---

## 4. 已物理删除（本刀）

`AWorldSceneManagement` 不再挂 JumpFlood / MeshBrush 热路径。删 UPROPERTY 会让旧关卡丢掉这些子对象和材质软引用，重开关卡后按 Profile 重画。

| 位置 | 结果 |
|---|---|
| `UFloodComponent2D` / JumpFlood RTA·B / SceneCapture | 类与子对象创建已删 |
| `StampOneRoadBrushPass` / `SetBrushMIDParams` / `ApplyWeightmapSettings` / Weight MID | 已删 |
| JumpFlood / MeshBrush / DF cache 材质软引用 | `UWorldEditorSettings` 与 ini 已清 |
| Key `FalloffSettings` 影子 | `PostSerialize` 仍迁一次；`ApplyFalloffToKey` 给路口写 Key 用 |
| `SyncHeightmapFalloffFromLandscapeKey` / `PushHeightmapFalloffToLandscapeKeys` | 还在，Details 已看不到 Heightmap 所以不再从面板互相同步 |
| `URoadBodyBrushCacheContainer` | 类留下，给旧图加载；热路径不再写 |

---

## 5. 不在本刀

- `UnpinAttribute` / `EvalAttributeAt`
- Runtime 里残留 Editor 副本、旧 Split
- Dirty 权图第二套 Falloff
- 路口 Fig1 / 路网格
- Content 里闲置的 MeshBrush / JumpFlood 材质资产（引擎 Water/Landmass 或插件 fork）
