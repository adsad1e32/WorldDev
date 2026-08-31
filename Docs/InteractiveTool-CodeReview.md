# Interactive Tool Code Review

专门审 `UMetaLandRoadInteractiveTool`（画路输入）。广泛 UE 分层见 [UE-InteractiveTool-Standard.md](./UE-InteractiveTool-Standard.md)。与 [Agent-Conventions.md](./Agent-Conventions.md)、skill `metal-androad-incremental-dev` 对齐。

**文件故事：** Tool = 输入 + PropertySet + 写回 Actor/Component。不三角化路口，不 stamp 地形，不在热路径 `RecookJunctionActor`。

对照审核 7：文件过宽是因为叠了多条交互状态机；描点时 `RefreshDynamicRoadMesh` + `bSuppressJunctionRecook` **合理**；缺的是按状态拆开、Refresh 只走一处。

---

## 0. 怎么用这份规范

审一处改动时，按下面四栏打勾。任何一栏「否」就标 `[审核]`，不要默默大改。

```
[审核]
位置：Tools/MetaLandRoadInteractiveTool*.cpp 函数名
现象：
栏目：精简 / 规范 / 复用 / 性能
建议结果：留下 | 收到拥有者 | 抽到 Draw / LaneAnchor / FromConnection | 删除死路径
待讨论：是
```

一次只标一小批。同意后再改同意的那些。

**死路径：** 审核时标出来，问用户是否移除。没点头不删。新功能不要往已标记的死路径上加。本文件 §7 是已缩小的停放清单，不挡后面加功能。

---

## 1. 精简（先问「这是哪条状态机」）

Tool 里同时活着三条会话，不要写进同一个函数：

| 状态 | 入口 | 允许写什么 | 禁止 |
|------|------|------------|------|
| **DrawPath** | 空地描点 / `AddSplinePoint` | 生成路 Actor、加点、Refresh mesh | Recook 全图路口 |
| **LaneAnchor** | 从车道嘴 Ctrl 延伸 | 绿轨、Hermite/Bezier、嘴钉死 | 改源路 trim / Fig1 |
| **FromConnection** | spawn 后 `FitSpline` + `ConnectTo` | 对齐源嘴、打 `LaneBranch` tag | 热路径 Recook |

审精简时查：

1. **一个函数只服务一条状态。** `OnClickPress` 可以 *分发*，不要把三条会话的几何都内联进去。
2. **Setup / Shutdown / Render / HUD** 只做框架：行为注册、画已算好的轨。几何采样不进 `Render`。
3. **死路径：** 热键 / 预览 / `#if 0` / 已注释 spawn ——标 `[审核]`，问是否移除。§7 那批已删。新发现的先标，不要在加功能时顺手清一整文件。
4. **不要用行数当尺子。** 800 行的 LaneAnchor 几何可以留在 `MetaLandRoadInteractiveToolDraw.cpp`；10 行的 `Recook` 不能进 Tool 热路径。
5. **拆文件按状态标，不按「太长了」。** `OnClickPress`（图一）已抽到 `MetaLandRoadInteractiveToolClick.cpp`。下一刀：`FinalizeLaneDrawFromConnection`、空地 `AddSplinePoint` 仅当它们自己成故事。

---

## 2. 规范（C++ / UE / 本项目）

1. **声明 `.h`，实现 `.cpp`。** 新算法不要塞进 `MetaLandRoadInteractiveTool.h`。Editor 专用 API 放 `Private/Tools/`。
2. **`.cpp` 允许：** 类 private/static、file-local `static`、匿名命名空间。禁止 `namespace XxxPrivate` 当第二模块。
3. **命名：** 会话只认 `EMetaLandRoadToolSession`（Idle / DrawPath / LaneAnchor）。`FromConnection` 是 LaneAnchor 的 spawn 写入，不是第四个会话。`bSplineSnapActive` 只表示吸附到别的样条/嘴；`bIsDrawHud` 只开关帮助字。
4. **输入：** 修饰键只在一处解读（Shift = EndSnap mix，Alt = add lane，Ctrl = 画/延伸）。同一键不要在 Hover 和 Click 里各写一套相反含义。
5. **事务：** 改 spline / spawn Actor 必须 `FScopedTransaction`；Refresh mesh 包在 `TGuardValue` suppress Recook 里（见 §4）。
6. **禁写：** 路口 Fig1、`ValidateJunctionMeshPointIndices`、Landscape stamp / RDG、`PrepareLandscapeKeys` 的 cook 口（那是路口 Actor 的事）。
7. **可以调度、不能实现：** `CookJunctionForActiveRoad` 只在 **A 结束** 或 **Ctrl+交叉检测** 调用路口 Actor；默认 `bCookJunctionOnAFinish == false`。`bFullWorldJunctionRecookOnDrawFinish` 是慢路径，默认关。
8. **产品契约不要在 Tool 里另写一份 trim。** 可见路段只问 `GetJunctionMeshGenerationSpans`。

---

## 3. 复用（第三份相同逻辑必须合并）

| 故事 | 拥有者 | 不要再复制 |
|------|--------|------------|
| 画路时 Refresh mesh 且不 Recook | `RefreshRoadMeshSuppressJunction` | 再写一遍 `TGuardValue` + `RefreshDynamicRoadMesh` |
| 绿轨 / 嘴条 PDI | Draw 层 `DrawAnchor*MouthBarPDI` / `BuildLaneWidthGuidanceRails` | 在 `Render` 里重算一遍轨 |
| 嘴坐标系 | `MetaLandRoadLaneEditPick::ComputeAnchorMouthRailFrame` | Tool 里手写 L/R/Center |
| 从源嘴套 profile | `MetaLandRoadProfile::ApplyRoadProfileFromDrawAnchor` | 手拷 lane Width keys |
| 钉嘴点 | `StabilizeLaneExtendMouthPoint` | `SetLocation` + 自己估 tangent |
| EditorMode | `ResolveMetaLandRoadEditorMode` | `GLevelEditorModeTools` 再抄三条 |

规则：

- 同一辅助出现 **第三处** → 收到上表拥有者，不要新建 `*Utils` / `*Helper` / `*Generation`。
- 只被一处用的几何可以留在 `MetaLandRoadInteractiveToolDraw.cpp` 的 file-local `static`。
- 车道拾取、宽度 key、section 线属于 Visualizer / `LaneEditPick`，Tool 只消费结果。

---

## 4. 性能

画路卡顿几乎都来自 **每点 Refresh** 或 **误 Recook**。

**允许（描点热路径）：**

```
TGuardValue SuppressJunctionRecook (bSuppressJunctionRecookFromRoadRefresh = true)
RefreshDynamicRoadMesh
RefreshLaneVisualization   // 可选，跟 mesh
```

只走 `RefreshRoadMeshSuppressJunction`，不要在 `AddSplinePoint` / `AddLaneDrawSegmentFromCursor` / FromConnection spawn 里直接 `RecookJunctionActor` 或全图 `RecookFromLinkedRoads`。

**禁止（热路径）：**

- `RecookJunctionActor` / `RefreshJunctionCoverMesh` / Fig1
- `RequestLandscapeUpdate` / stamp / SceneCapture
- `bFullWorldJunctionRecookOnDrawFinish == true` 时扫全关卡路对
- Hover 里 `RefreshDynamicRoadMesh`（Hover 只更新绿轨缓存）
- `OnClickPress` 里对所有 `AMoKuEditSplineActor` 做重量级迭代（EndSnap 拾取除外，且要有距离剪枝）

**Finish / A / 交叉才允许 cook：**

- `TryCookJunctionOnCtrlPressIfCrossed`：已检测到 XY 交叉
- `CookJunctionForActiveRoad`：用户明确结束，且设置允许
- 默认 A 只收 mesh，不 cook

审性能时数：这一次点击会触发几次 `RefreshDynamicRoadMesh`？目标 **每个提交点 1 次**；FromConnection 对源路 + 新路最多各 1 次。

---

## 5. 审查清单（PR / 本机）

- [ ] 改动落在 DrawPath / LaneAnchor / FromConnection 其中一条，或只是分发
- [ ] 热路径没有新的 Recook / stamp
- [ ] Refresh 走 `RefreshRoadMeshSuppressJunction`（或同等一处拥有者）
- [ ] 没有第三份嘴条 / profile / EditorMode 查找
- [ ] 没有在 Tool 头文件堆算法
- [ ] Hover 不刷 mesh
- [ ] 默认不开启 Full World Recook
- [ ] 编译 + 视口：空地描点、从嘴 Ctrl 延伸、A 结束，三条会话各走一遍
- [ ] 新碰到的废弃热键 / 死路径已标 `[审核]`，并问过是否移除（没点头则只进 §7，不删）

---

## 6. 文件放哪

| 文件 | 故事 |
|------|------|
| `MetaLandRoadInteractiveTool.h/.cpp` | UInteractiveTool 生命周期、PropertySet、Setup / Render / HUD |
| `MetaLandRoadInteractiveToolClick.cpp` | **图一：** `OnClickPress` 分发 + `HandleClickPressWithoutCtrl` / `LaneAnchorStart` / `DrawOrExtend` |
| `MetaLandRoadInteractiveToolDraw.h/.cpp` | 绿轨几何、嘴钉、Refresh suppress、射线投影 |
| 以后：`*FromConnection*` | 仅当 `FinalizeLaneDrawFromConnection` 自己成故事再抽 |

路口 cook 仍在 `MoKuEditIntersectionActor`。Visualizer 仍不 Recook。

---

## 7. 已知死路径（2026-08 已按用户同意移除）

| 项 | 处理 |
|----|------|
| Y / B + `InvertPreviewMesh` / `SwitchInsertCrossState` / `PreviewActor` | **已删** |
| 滚轮 DataTable 后半 + `GetSelectedAsset` | **已删**；滚轮只留 EndSnap 加减车道 |
| `UpdateSpline*` / `UpdateJunctionSideInfo` / `RoadOp` | **已删** |
| Setup 全图 Gizmo / `Test` / `TEST_AddGizmo` | **已删**；Undo 不再全图重建 Gizmo，也不再按表清空 `ActiveActor` |
| A 双绑 | **已收口**：只留 Mode `InputKey(A)` → `OnFinishedSplineEdit`。Behavior 只留 Shift+F1 |

**活热键：** A 结束、Esc、Ctrl/Shift/Alt+LMB、滚轮（EndSnap 车道数）、Shift+F1 帮助。新功能走 DrawPath / LaneAnchor / FromConnection。以后再碰到死路径：标 `[审核]`，问是否移除，没点头不删。
