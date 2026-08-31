# MetaLandRoad × World Partition：Bake / 分流加载方向

对照当前 WorldDev 状态，讨论 **World Partition（WP）流式加载** 下，路网该怎么 bake、怎么切、怎么进游戏。  
不是实施清单。落地前按 skill 再标 `[审核]`。

相关契约：

- [MetaLandRoad-Runtime-Editor.md](./MetaLandRoad-Runtime-Editor.md) — Runtime 进 cook，Editor 不进游戏
- [MetaLandRoad-Height-Weight-Plan.md](./MetaLandRoad-Height-Weight-Plan.md) — 高度 = Landscape Key；Road 权 = Actor `RoadWeightFalloffWidth`
- [MetaRoad-vs-MetaLandRoad.md](./MetaRoad-vs-MetaLandRoad.md) — MetaRoad 有 `TriangulateRoadTool` 烘焙；我们还没有
- CutS / 剩余路 span **只有一个主人**：`GetJunctionMeshGenerationSpans`

引擎：UE 5.6。`DefaultEngine.ini` 已有 WP Editor 段，**路 Actor 尚未挂 `IsSpatiallyLoaded` / Runtime Grid / HLOD**。`NewMap1` 按普通关卡在编。下面按「以后要进大世界」写，不假设地图已经转 WP。

---

## 1. 现在三层各是什么

```
作者数据（可改）
  Spline + Lane Layout + Landscape Key + CutS / Fig1 拓扑
       │  Editor 热路径
       ▼
可见 mesh（DynamicMesh）
  RefreshDynamicRoadMesh / Fig1 路口封面
       │  并行
       ▼
地形（WorldEditor Edit Layer）
  AWorldSceneManagement → CS Profile 按盒 stamp
  高度：Key；权：RoadWeightFalloffWidth；只画 Road 层
```

| 层 | 现在 | 进 WP / 进包之后的问题 |
|----|------|------------------------|
| 作者数据 | 整条 spline 一个 Actor，数据跨路口仍在（Begin/End 不删） | 一条长路 bounds 跨很多 cell；要么整条常驻，要么被切碎后 **CutS / 吸附口** 对不上 |
| 可见 mesh | Actor 上 `UDynamicMeshComponent`，改完就刷新 | DynamicMesh **不好进 HLOD**，也不适合当几千条流式资产 |
| 地形 | 活 stamp 到 `WorldEditor` 层 | Stamp 要 **Landscape Streaming Proxy 已加载**；未加载 cell 上改路，高度/权会漏或写到错 proxy |

结论先写死：**编辑态可以继续「活 mesh + 活 stamp」；进游戏不要把这套当流式真值。**

---

## 2. World Partition 的硬约束（和路冲突的点）

### 2.1 Landscape 已经按格子切

WP 开了之后，地形是 `ALandscape` + 一堆 `LandscapeStreamingProxy`。高度/权的最终像素在 **proxy** 上。

- **利：** 远处地形本来就会卸；我们不用自己切 heightmap。
- **弊：** 活 stamp 必须先把相交 proxy **加载进内存**。一次改 2km 路 = 一次要 load 一串 cell。Editor 能忍，Runtime 不能每次开车都 stamp。

### 2.2 路是「长条图」，WP 是「方格图」

WP Runtime Grid 按 XY 方格决定 Actor 进出。路是沿 spline 的长条，路口常落在 **格子缝上**。

| 切法 | 对路网 | 利 | 弊 |
|------|--------|----|----|
| 不切，整条路一个 Actor | 数据/吸附/Fig1 最干净 | 口条、C 续接、CutS 仍是现在这套 | 长路 bounds 巨大 → 几乎常驻；或格子一靠近整条加载 |
| 按 WP 格子切 Actor | 和 proxy 对齐 | 加载范围好看 | **不要做作者层。** CutS / leftover / 吸附口会被格子切开；路口要跨 2～4 个 Actor |
| 只在路口切（图节点） | 一条剩余路一个 Actor | 和现有 `GetJunctionMeshGenerationSpans` 同构 | 仍可能跨多个 cell，但比「10km 一条」好很多 |
| 只切 **bake 出来的 mesh** | 作者 Actor 不动 | 加载切的是 StaticMesh，不动编辑契约 | 要做 clip/cook 工具；改路要重 bake 相交 cell |

**建议：作者数据按图切（路口），不要按 WP 格子切。Bake 产物可以按格子再 clip。**

### 2.3 Edit Layer 活刷 vs 已提交的地形

`WorldEditor` 是 Landscape Edit Layer。Editor 里改路 → brush 重画这一层。

- **利：** 和 Water 同一模型；高度/权契约已经落地；按盒 dispatch 已减小全屏浪费。
- **弊：** 游戏里再挂 `AWorldSceneManagement` 做全图 brush，等于每帧/每次加载重算 CS。WP 下未加载 proxy 没有合法 RT。  
  **进包方向：把 WorldEditor 层 bake 进各 proxy（或 Flatten 到一层），Runtime 不再跑 stamp。**

路口 hub **没有** Landscape Key（只有 Actor Terrain）。Bake 时 hub 高度必须有一份明确来源，不能「游戏里再补 stamp」。

---

## 3. Bake 方向（四种，只选一条主路）

「Bake」在本项目里其实是 **三件不同的事**。不要合成一个按钮。

### A. 不 Bake — Runtime 继续 DynamicMesh + 活 stamp

和现在 Editor 一样。

| | |
|--|--|
| 利 | 零工具成本；改路立刻可见 |
| 弊 | WP 下流式几乎不可用；DynamicMesh + CS stamp 吃内存/GPU；HLOD 弱；和「Runtime 不进 Editor 模块」还能共存，但 **性能和加载对不上大世界** |
| 适用 | 小图 / 演示 / 永远 Editor 预览 |

**不建议作为出包主路。**

### B. 只 Bake 地形 — mesh 仍动态

WorldEditor 层写入 Landscape proxy；路/路口仍 `RefreshDynamicRoadMesh`。

| | |
|--|--|
| 利 | 地形加载跟引擎走；改路 mesh 仍热；工具量小（提交/Flatten Edit Layer） |
| 弊 | 路 Actor 仍要流式或常驻；远处仍要画动态条带；路口 Fig1 仍在运行时占一份 DynamicMesh |
| 适用 | 中图、路不多、先把 PIE/包体地形稳定下来 |

**这是第一刀最合适的 bake。** 地形本来就是 WP 的强项。

### C. Bake mesh（对齐 MetaRoad `TriangulateRoadTool`）— 地形已提交

Editor 把每条剩余路 / 每个路口打成 StaticMesh（可 Nanite），挂在可流式 Actor 上；spline 变 Editor-only 或只留轻量查询。

| | |
|--|--|
| 利 | 和 WP / HLOD / Nanite 同语言；远处可合并；游戏不跑 loft / Fig1 |
| 弊 | 要写烘焙工具（我们现在没有）；改一条路要重 bake 自己 + 相邻路口；UV/材质 section 要一次定好 |
| 适用 | 大世界出包、车看得到远路 |

**这是可见路的目标态。** 对照 MetaRoad 管线 B，不要发明第三套三角化。作者层仍用现在的 DynamicMesh 预览。

### D. Runtime 重建（开车到了再 loft / 再 stamp）

| | |
|--|--|
| 利 | 包体小；永远「最新」 |
| 弊 | 卡顿、跨 cell 接缝、Fig1 审核失败会在游戏里爆；和 WP 异步加载抢线程 |
| 适用 | 不做 |

---

## 4. 优化 / 切割 / 加载 — 建议怎么叠

由近到远，上一层没稳不要做下一层。

### 4.1 Editor 现在就能守的（不改产品契约）

| 项 | 做法 | 利 / 弊 |
|----|------|---------|
| Stamp 按盒 | 已有 influence rect | 利：少画 RT。弊：相交 proxy 仍必须 loaded |
| 改路先加载 cell | WP 图里 stamp 前 `LoadIntersectingCells` | 利：不写漏。弊：Editor 会卡一下 |
| 路口局部 recook | 已有，禁止 `RebuildAllJunctionsFromWorld` | 利：和 WP 无关也该守 |
| 作者 Actor 不按格子切 | 保持一条 spline 到路口再断 | 利：吸附 / CutS / leftover 不动 |

### 4.2 进 WP 图之后的加载模型

```
建议 Runtime 看见的：
  LandscapeStreamingProxy          ← 引擎切
  RoadVisual Actor（bake 的 SM）   ← 按格子 clip 或按剩余路
  JunctionVisual Actor（bake 的 SM）

Editor 才看见的（Data Layer: Editor / 不进包）：
  AMoKuEditSplineActor             ← 整条作者路
  AMoKuEditIntersectionActor       ← Fig1 源
  AWorldSceneManagement            ← 活 stamp
```

| 模式 | 利 | 弊 |
|------|----|----|
| 作者路 `IsSpatiallyLoaded=false`（常驻） | 编辑/查询简单 | 大图吃内存；只适合「路很少」 |
| 作者路按 bounds 流式 | 和 WP 默认一致 | 长路几乎常驻；路口在缝上会双加载或双卸载 |
| 作者路进 Editor Data Layer，**不进游戏** | 包体干净；游戏只加载 bake | 游戏里不能再「现场画路」——若以后要玩家修路，另开 Runtime 子集 |
| Bake mesh 按 **Runtime Grid 再 clip** | 加载方和地形对齐 | clip 面要在格子缝上焊；路口 Actor 放在 **路口中心那个 cell**，不要拆成 4 份 Fig1 |

路口封面：**一个路口一个 bake Actor**，空间哈希用 Anchor。不要按格子把 Fig1 切成四块再缝。

### 4.3 远处：HLOD / RVT

| 手段 | 利 | 弊 |
|------|----|----|
| WP HLOD（bake SM 进层） | 远路合成低模，卸掉近处 Actor | DynamicMesh 源几乎不能直接建 HLOD；必须先有 C |
| Runtime Virtual Texture（地形+路烙一层远景） | 远看是贴在地上的路，不必加载条带 | 近景仍要 mesh；RVT 分辨率/更新范围要调 |
| 远景只留地形上的 Road 权，不加载路 mesh | 最便宜 | 远路没有路缘厚度；近处切过去会「路突然立起来」 |

**建议：** 近景 bake SM；中远 HLOD；极远 RVT 或只靠已 bake 的 Road 权。不要用 RVT 替代近处车道 mesh。

### 4.4 和现有切割的关系（不要第二套 trim）

已经有的切：

- `GetJunctionMeshGenerationSpans` — 剩余路 vs hub
- Stamp 按盒 — 地形脏区
- （以后）WP 格子 — 只切 **bake 网格 / proxy**

不要再写一套「按 cell 的 CutS」。格子 clip 是 bake 工具的事，不是 Layout 的事。

---

## 5. 推荐主路（分阶段）

```
现在（小图 Editor）
  活 DynamicMesh + 活 WorldEditor stamp
       │
阶段 1  地形 Bake
  提交/Flatten WorldEditor → Landscape proxy
  Runtime 不再 stamp（Manager 仅 Editor）
       │
阶段 2  作者图节点（可选，路变长再做）
  只在路口拆 Actor；不按 WP 格子拆 spline
       │
阶段 3  Mesh Bake（对标 MetaRoad Triangulate）
  剩余路 SM + 路口 SM；作者 Actor 进 Editor Data Layer
  WP 流式这些 Visual Actor；HLOD 建在 SM 上
       │
阶段 4  极远
  HLOD + 可选 RVT
```

| 阶段 | 做什么 | 不做什么 |
|------|--------|----------|
| 1 | 进包/PIE 地形稳定；WP 图 stamp 前 load cell | 不改 CutS / 吸附 / Fig1 |
| 2 | 超长路才按路口拆 | 不按 128m/256m 格子拆作者路 |
| 3 | 烘焙工具 + 材质 section 一次定 | Runtime 不跑 Fig1；不在游戏里 recook |
| 4 | 远景 | 不用 RVT 当近景车道 |

阶段 1 可以在 **还没转 WP** 的 `NewMap1` 上先做（Flatten 层），转 WP 后同一套提交逻辑落在 proxy 上。

---

## 6. 利弊总表（决策用）

| 选择 | 编辑体验 | WP 加载 | 包体/运行成本 | 和现有契约 |
|------|----------|---------|---------------|------------|
| 一直活 mesh+stamp | 最好 | 差 | 高 | 全兼容，但出不了大世界 |
| 只 bake 地形 | 改路仍热 | 地形好，路一般 | 中 | 高度/权契约不动 |
| bake 地形 + bake mesh | 改路要重 bake | 好 | 低 | 预览仍 DynamicMesh；游戏不碰 Layout |
| 按格子切作者路 | 吸附/路口易坏 | 表面整齐 | — | **违反「一份 CutS 主人」风险最高** |
| Runtime 重建 | 看起来「永远新」 | 差 | 尖峰卡 | 审核失败会进游戏 |

---

## 7. 明确先不做

- 用 WP 格子当 Layout 断点、第二套 trim
- 游戏里 `RebuildAllJunctionsFromWorld` / 全图 stamp
- 把 `AWorldSceneManagement` 当 Runtime 常驻系统
- 未 bake 就给 DynamicMesh 开 HLOD
- 路口 Fig1 按 cell 切成多块再焊
- 为了 WP 把 Begin/End 数据删掉或把 leftover key 清掉

---

## 8. 待讨论（做阶段 1 前拍板）

1. **第一张 WP 图** 是新建 Open World，还是把 `NewMap1` convert？Convert 会拆 Landscape，旧 External Actors 要验收。
2. **游戏里要不要现场修路？** 不要 → 作者 Actor 可以不进包。要 → 阶段 3 必须留 Runtime 子集（只 loft，不 stamp 或只 stamp 已加载 proxy）。
3. **Bake mesh 粒度：** 一条剩余路一个 SM，还是再按格子 clip？前者工具简单、加载略肥；后者对齐 WP、接缝要焊。
4. **路口高度进地形：** hub 仍无 Key。Bake 地形时用 Actor Terrain，还是先规定「hub 高度跟相邻路 CutS 采样」？

拍板后只开 **一个阶段**，不要同时切作者路和写烘焙器。
