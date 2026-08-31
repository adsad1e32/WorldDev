# MetaLandRoad 道路排布（PropSetting）方案

对照 skill `metal-androad-incremental-dev`、[Agent-Conventions.md](./Agent-Conventions.md)、[UE-InteractiveTool-Standard.md](./UE-InteractiveTool-Standard.md)、[MetaLandRoad-Junction-Spec.md](./MetaLandRoad-Junction-Spec.md)、[MetaLandRoad-Decal-Plan.md](./MetaLandRoad-Decal-Plan.md)。

**本文是排布方案正文，尚未落地。** 实现前先读完。和本文冲突 → `[审核]` 停手。

本文不审 Fig1 孔环、不审 Landscape stamp、不审 sweep 拐点。排布 **不得** 为了摆道具去改 loft 拓扑或 Recook 路口。

图一属性面板是**同一入口**。新行 **PropSetting**。选中后：当前 **轨** + 该轨的 Key + Basic Setting。

口语里「这条线上的 section」= 本文 **Span**。路数据里的 **Section** 仍是 lane 分段宿主，两套词不要混。

---

## 0. 已拍板

| 项 | 决定 |
|----|------|
| 面板名 / TypeId | **PropSetting**（`TypePropSetting`） |
| Host | **Lane + SectionCenter** 都能 Pin |
| 轨怎么来 | 零默认中轨。Add / 热键点地，或点已有边高亮 |
| 不是新 Spline | Attribute。无作者 `USplineComponent` |
| Key | **每条轨自己的 Key。** 先选中轨，再 Add 分段。轨 A 加刀不影响轨 B |
| Span 布局 | **每段独立选布局算法。** 参数各算各的，不共用一套间隔 |
| 显示后端 | SplineMesh + HISM **混用**。用户选的是布局算法，不是「这段勾 SplineMesh」 |
| 直/弯 | **不要按整段 Key 判一次。** 算法在轨上按曲率开窗口，用户看不见这些刀 |
| 横向 | **锁边（EdgeRef）+ 自由偏移。** 见 §2.0。裸 Alpha / 裸 R 不当唯一真值 |
| Override id | **`RailId + 量化 S`**，不用 `SampleIndex` |
| PCG Graph | 不当作者界面 |
| Z | 路床跟路 mesh；Pin 在 Lane 跟人行高度；另加厘米 Offset |

---

## 1. 对你这版设计的判断

**按轨选中 → 再 Add 分段：对，锁死。**  
和 Landscape 同一手势，只是对象从「整条中线」变成「当前轨」。左边栏开口、右边灯继续密，必须能分开切。

**每段自选布局算法：对，但收成命名枚举，不要做成插件图。**  
这正是 PCG 做不到、我们该做的：同一条物理轨上，前 80m 等距灯、中间空、后 40m 贴弯栏，三段各跑各的采样。算法是 C++ 里一小撮 `EPropLayout`，Basic Setting 下拉切换。不是每段挂一张 PCG Graph，也不是用户勾 HISM/SplineMesh。

后端（怎么画）和布局（怎么取点）分开：

| 层 | 谁决定 | 例 |
|----|--------|-----|
| **布局算法** | 该 Span 的枚举 + 参数 | 等距、关闭、成对、贴条 |
| **画法** | C++ 看资产标记 + 曲率窗口 | 灯→HISM；栏直→HISM 短板；栏弯→SplineMesh |

v1 算法先少：`Off` / `EvenSpacing` / `DeformStrip`。成对、错位、看权图以后再加，加一种是新枚举值，不是新 Graph。

---

## 2. 数据（track 上挂轨，Key 不进「全家一份」）

现有 `TypeId → Keys[]` **塞不下** Rail 表。PropSetting 的 track 是：

```
TypePropSetting（Pin 在路 Section 或某条 Lane）
 └── Rails[]                          点出来的线
      ├── RailId / Name
      ├── EdgeRef + 自由横向          锁边或点击处
      ├── Keys[]                      仅此轨，Stepped
      │    └── Span：LayoutAlgo + 参数 + Asset
      └── Overrides[]                 仅此轨
```

**禁止**一份全局 `Keys[]` 切所有轨。  
**禁止**把 `Rails[]` 复制进每一颗 Key Value。

**Pin Section**：横向相对整幅路。  
**Pin Lane**：横向相对那条 lane。  
点在哪条 lane 宽度里就 Pin 那条；只有点到没有独立 lane 的路床才 Pin Section。悬停写出宿主。

显示：`Center(S) + Right(S) × Off(宿主, 横向)`。世界 Transform 不存盘。

### 2.0 横向：锁边优先

| EdgeRef | 含义 |
|---------|------|
| `Free` | 点击落的位置。自由偏移用 Alpha（随宽变）或厘米，实现时二选一作字段，契约是 Free |
| `SectionLeft` / `SectionRight` | 锁路床外沿（`GetRoadEdgePointsAtS`） |
| `LaneLeft` / `LaneRight` | 锁该 lane 两侧（`EvalLaneROffset`） |

沿边生成 = 写 EdgeRef，路一改宽跟着边走。自由点击 = `Free` + 偏移。拖轨：`Free` 改偏移；锁边轨先变成 `Free` 再拖，或只能沿法向微调（实现时定一种）。

### 2.1 加轨

空 PropSetting = 零条轨。

| 方式 | 操作 |
|------|------|
| Add Rail / 热键 + 点地 | 预览平行线 → 再点提交。近边吸附则写 EdgeRef |
| 点高亮边 | 直接锁那条边 |

**默认点击不加轨。** Add 没按下：点选轨 / Key / 实例。和画路会话分开。点 PropSetting 行则收掉 Landscape overlay（同时只开一种）。

钳 `GetJunctionMeshGenerationSpans`。Hub 内不加。

### 2.2 选中轨，再 Add Span

面板和视口都有 **当前轨**。

1. 点已有轨（或列表选一行）→ 当前轨  
2. **Add Span**（或热键）→ 在该轨上打 Key，切开下一段  
3. 拖 / 删 Key 只动 **这一条轨**

新轨默认可以只有 Begin/End 两颗 Key、中间一个 Span（算法默认 `Off` 或上次用的，实现时定）。不加刀 = 整轨一段。

### 2.3 Span：独立布局算法

**Stepped**，从 Key_i 持有到 Key_{i+1}。

| 字段 | 含义 |
|------|------|
| `SOffset` | 段起点 |
| `Layout` | `EPropLayout`，见下 |
| 该算法参数 | 间隔、相位、资产… **只属于这段** |
| `Asset` / `Profile` | 这段用的资产 |

v1 枚举：

| `EPropLayout` | 做什么 | 典型资产 |
|---------------|--------|----------|
| `Off` | 这段不排 | — |
| `EvenSpacing` | 沿轨弧长等距 | 灯、桩、箱 |
| `DeformStrip` | 沿轨连续条 | 栏板 |

以后可加：`Pair`、`Stagger`、`DensityFromWeight`。每种自己的参数结构，互不读取。

资产 DataAsset 仍标 `RigidInstance` / `DeformStrip`（画法能力）。`Layout = DeformStrip` 但资产是灯 → 按 Rigid 画并打日志，不要默默拉成条。

### 2.4 Override

```
Id = Hash(Host, RailId, Quantize(S))
```

| 动作 | 存 |
|------|-----|
| 删 | Tombstone |
| 拖 | ΔS + Δ横向 + ΔYaw |
| 换资产 | SoftObjectPath |

改间隔：对得上量化 S 的留下；对不上进失效列表，**不整轨清空**。不是 `StaticMeshActor`。

### 2.5 Z

| 宿主 | 默认 Z |
|------|--------|
| Section 路床 | 当时路 mesh |
| Lane | 该 lane 高度（含 Sidewalk Height） |
| 另加 | 厘米 Offset |

### 2.6 C / Split / Trim

和 Landscape 一样：Key 的 S 随段平移 / 裁切。Rail 的 EdgeRef 不跟 S。Override 对不上量化 S 的进失效。续画不丢轨。

---

## 3. 画法（和布局算法分开）

用户不选 SplineMesh。rebuild 时：

```
Layout == Off                 → 不画
资产 RigidInstance            → 全程 HISM
资产 DeformStrip
    曲率窗口够直              → HISM 短板
    曲率窗口在转弯            → SplineMesh
```

窗口在 **该轨采样折线** 上切，不是「这个 Key Span 整段一个布尔」。直↔弯共端点，阈值加滞回，避免拖路时后端闪。

内弯半宽大于半径：钳横向或跳过交叉采样。**不改 loft。**

v1 碰撞 / 导航关。Tool 不 cook。Hover 不刷 DynamicMesh。无作者 spline；SplineMesh 切线当时从路框算。

---

## 4. PCG Graph

不要。按轨 Key + 每段 `EPropLayout` 已经是布局选择器。PCG 以后最多当某种 Layout 的可选后端，不进图一。

---

## 5. 编辑器分层

对照 [UE-InteractiveTool-Standard.md](./UE-InteractiveTool-Standard.md)。

| 层 | 做什么 |
|----|--------|
| 图一 PropSetting | Pin 到点击所在 Section 或 Lane |
| Basic Setting | 当前轨、该轨 Key、当前 Span 的 `Layout` + 参数、选中实例 |
| Visualizer | 边高亮、轨、**仅当前轨的 Key**、实例 proxy |
| Tool | Add-Rail / Add-Span / 点边 / 拖轨 / 点实例；`OnClickPress` 只分发 |
| Runtime | 每轨每 Span 跑自己的 Layout → 曲率窗口选 HISM/SplineMesh |

| 点在 | 会话 |
|------|------|
| Add Rail 已按下 | 落轨 |
| 高亮边 | 锁边成轨 |
| 已有轨 | 设为当前轨；可拖横向 |
| Add Span 已按下且已有当前轨 | 在该轨打 Key |
| 当前轨上的 Key | 拖 / 选中，改这段 Layout |
| 实例 | Nudge / Del / Swap |
| Hub 内 | 忽略 |

---

## 6. 路口

v1 只路身。以后 Junction Actor。禁止 CutS 以内。Fig1 不改。

---

## 7. 代码放哪（实现时）

| 故事 | 放哪 |
|------|------|
| `TypePropSetting` + `FPropRail`（Keys+Overrides） | Runtime `MetaLandRoadLaneAttribute.h`（track 扩展，不是每 Key 一份 Rail 表） |
| `EPropLayout` 采样 | SplineComponent **旁路**。不进 `BuildLinearApproximation` |
| 曲率窗口 + HISM/SplineMesh | 同上旁路 |
| 组件刷新 | 路 Actor 子组件 |
| Add Rail / Add Span | Editor `*ToolPropSetting.cpp` |
| 边高亮 / 当前轨 Key | Visualizer |
| 资产库 + Tag / Rigid·Deform | Runtime `MetaLandRoadPropLibrary` + DataTable `/MetaLandRoad/PropSetting/DT_PropSettingAssets`。Editor 命令 `MetaLandRoad.RebuildPropAssetTable` 扫 `/Game/Fab/Megascans/3D` |

新 `UCLASS` / Tool 可新文件。抽旧文件先 `[审核]`。

---

## 8. 落地顺序

| 步 | 内容 | 验收 |
|----|------|------|
| 0 | DataTable 资产库 + Tag | 编过跑 `MetaLandRoad.RebuildPropAssetTable`；Content Browser 能按 Tag 筛 |
| 1 | 图一 PropSetting；零默认轨；Add Rail 点地；选中轨 + Add Span；Key 按轨 | 两条轨各切各的；切 A 不动 B。HISM 还没画 |
| 2 | 锁边生成 + EdgeRef | 点外沿，改宽轨还在边上 |
| 3 | `DeformStrip` + 曲率窗口混用 | 直栏短板、弯栏贴弧、灯始终 HISM |
| 4 | Override（量化 S） | 删一盏不拆该 Span 配方 |
| 5 | （可选）路口 | Fig1 不变 |

第 1–4 步不动 Fig1、不动 stamp。

---

## 9. 契约

| 契约 | 怎么守 |
|------|--------|
| Landscape 只影响高度 | 新 Type |
| 可见路段 | `GetJunctionMeshGenerationSpans` |
| CutS | Hub 内不画 |
| Fig1 | 不改封面 |
| sweep | 同一套 Center/Right |
| Tool 不 cook | rebuild ≠ Recook |
| 无作者样条 | 轨不是 `USplineComponent` |
| Key 按轨 | 无全家一把刀 |
| 布局按 Span | 无轨级「全局间隔」盖住各段 |

---

## 10. 结论

你的建议 **按这个用**：每条线自己的 Key；先选线再 Add 分段；每段自己的布局算法。  
立刻补进正文的：锁边、量化 S、Z、track 结构、曲率窗口、默认点击不加轨。  
算法枚举先短，画法仍由 C++ 混用，不把 PCG 或「勾后端」放进 Span。

---

## 11. 实现时再钉（不当产品方向争议）

- `Free` 的偏移字段：Alpha 或 cm，选一个写进 USTRUCT  
- Add Span 后新段默认 `Off` 还是复制上一段 Layout  
- 锁边轨被拖：转 `Free` 还是只允许法向微调  
- 点在人行外沿像素级：已写「落在 lane 宽度里 Pin Lane」；吸附阈值实现时定
