# Lane Anchor 续画 / Connect — 实现重点

> 用户自选锚点：路尾、闸口（Add Lane + Section 边界）、路口 arm lane，**统一 Lane Pick**，不硬编码三种模式。  
> 代码风格：**声明 `.h`、实现 `.cpp`、不新建 namespace 包裹、优先扩展现有文件**（见 [Agent-Conventions.md](./Agent-Conventions.md)）。

---

## 0. 术语

| 名称 | 含义 |
|------|------|
| **Anchor** | `FMetaLandRoadDrawAnchor` — 用户 Pick 的 lane + S + mouth |
| **DrawSession** | 一次从 StartAnchor 落点到 Finish 的画线会话 |
| **Bundle** | 相邻 lane、同 mouth、同方向 → 1 Actor 宽截面 |
| **Branch** | 不同 mouth / 不同目标 → 各 1 DrawSession、各 1 Actor |
| **EditTarget** | World Builder 内编辑对象，**不依赖** UE Actor Selection |

---

## 1. 阶段总览（严格顺序）

| 阶段 | 优先级 | 目标 | 用户可见 |
|------|--------|------|----------|
| **P0-A** | ★★★ | 类型 + EvalMouth + EditTarget | Pick 有反馈，Details 显示 Anchor |
| **P0-B** | ★★★ | World Builder 内 Lane Pick（不依赖选中 Actor） | Ctrl+Click / 端点环 |
| **P1-A** | ★★☆ | Connection API（ConnectTo / FitSpline） | Finish 可挂接 |
| **P1-B** | ★★☆ | DrawTool 从 Anchor Spawn + Profile 继承 | 从 lane 续画 |
| **P1-C** | ★★☆ | PropagateConnectionUpdates | 改主线，分叉口跟随 |
| **P2-A** | ★☆☆ | Blend v0 预览 + Shift 确认 | 图一 ghost |
| **P2-B** | ★☆☆ | Connection VisMode（Alt+C）+ RoadNetworkId | 逻辑一条路 |

**原则：** P0 不通不进入 P1；P1 不通不做 Blend。每阶段 **小 diff、可编译、可测**。

---

## 2. 数据类型 — `MetaLandRoadTypes.h` / `.cpp`

### 2.1 新增 enum / struct（仅声明在 `.h`）

**文件：** `Plugins/MetaLandRoad/Source/MetaLandRoad/Public/MetaLandRoadTypes.h`

```cpp
/** Which end of the lane span the user picked (路尾 / 闸口 / 路口 arm 统一用此字段). */
UENUM(BlueprintType)
enum class EMetaLandRoadMouthSide : uint8
{
    Start,
    End,
};

/** Finish 策略 — DrawTool 末点行为，不用于 Anchor Pick. */
UENUM(BlueprintType)
enum class EMetaLandRoadDrawFinishMode : uint8
{
    Direct,
    Blend,
    Junction,
};

/** 用户 Lane Pick 的完整锚点（路口 / 路尾 / 闸口 均为同一结构）. */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadDrawAnchor
{
    GENERATED_BODY()

    TWeakObjectPtr<UMetaLandRoadSplineComponent> SourceSpline;
    int32 SectionIndex = INDEX_NONE;
    int32 LaneIndex = METALANDROAD_LANE_INDEX_NONE;
    double AnchorS = 0.0;
    EMetaLandRoadMouthSide MouthSide = EMetaLandRoadMouthSide::End;
    /** Shift 合法扩选：相邻 lane、同 AnchorS、同 MouthSide. */
    TArray<int32> BundledLaneIndices;
    /** true = 从 Successor mouth 向外画；false = 从 Predecessor. */
    bool bFromSuccessor = true;
};

/** Spawn 时 Profile 继承快照（单 Branch Actor）. */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadDrawSeed
{
    GENERATED_BODY()

    FMetaLandRoadDrawAnchor Anchor;
    /** 从 Anchor 克隆的 lane 子集（Bundle 时多条）. */
    TArray<FMetaLandRoadLane> ProfileLanes;
};

/** World Builder 编辑上下文 — 与 UE Selection 解耦. */
USTRUCT(BlueprintType)
struct METALANDROAD_API FMetaLandRoadEditTarget
{
    GENERATED_BODY()

    TWeakObjectPtr<AMoKuEditSplineActor> RoadActor;
    TWeakObjectPtr<UMetaLandRoadSplineComponent> Spline;
    FMetaLandRoadDrawAnchor PendingAnchor;
    bool bHasPendingAnchor = false;
};
```

**实现：** 若需 helper（如 `IsAnchorValid()`），放在 `MetaLandRoadTypes.cpp`，**不要**写进 `.h` 函数体。

### 2.2 扩展 `UMetaLandRoadLaneConnection`（`.h` 声明）

**文件：** `MetaLandRoadTypes.h`（类已在此）

| 方法 | 职责 | 参考 |
|------|------|------|
| `FTransform EvalTransform() const` | 在 **AnchorS** 处求 lane mouth 位姿 | MetaRoad `ULaneConnection::EvalTransform` |
| `bool CanConnectTo(const UMetaLandRoadConnection* Other) const` | 是否已有外连、lane 合法 | MetaRoad |
| `void ConnectTo(UMetaLandRoadConnection* Other)` | 双向写 `OuterRoadConnections` / `OuterLaneConnection` | MetaRoad |

**实现文件：** `Private/MetaLandRoadTypes.cpp`

**注意：** 现有 `UpdateLayout` 里 `UpdateConnection` 只写 `SectionIndex/LaneIndex`；`EvalTransform` 必须读 **`FMetaLandRoadDrawAnchor::AnchorS`** 或 lane span 的 Start/End S，**不能**假设只有 S0/S1。

### 2.3 扩展 `UMetaLandRoadConnection`

同上文件，补：

```cpp
void ConnectTo(UMetaLandRoadConnection* OtherLaneConnection);
bool CanConnectTo(const UMetaLandRoadConnection* Other) const;
```

---

## 3. 采样 / Mouth — `MetaLandRoadSplineComponent`

**声明：** `Public/MetaLandRoadSplineComponent.h`  
**实现：** `Private/MetaLandRoadSplineComponent.cpp`

| 函数 | 签名（示意） | 说明 |
|------|--------------|------|
| EvalMouthTransform | `bool EvalMouthTransform(const FMetaLandRoadDrawAnchor& Anchor, FTransform& OutTransform) const` | 核心：S + LaneIndex → 世界 Transform |
| EvalLanePositionAtS | `bool EvalLanePositionAtS(int32 SectionIndex, int32 LaneIndex, double S, FMetaLandRoadPosition& Out) const` | 供 EvalMouth 内部调用 |
| InitProfileFromAnchor | `void InitProfileFromAnchor(const FMetaLandRoadDrawSeed& Seed)` | 新路 Spawn 后写 `RoadLayout` |
| FitSplineToAnchor | `void FitSplineToAnchor(const FMetaLandRoadDrawAnchor& Anchor, bool bAtSplineStart)` | 对齐首/尾控制点 + 切向 |
| PropagateConnectionUpdates | `static void PropagateConnectionUpdates(UMetaLandRoadSplineComponent* ModifiedSpline)` | 改主线后遍历 Connection 图 |

**依赖：** 已有 `GetRoadEdgePointsAtS`、`EvalLaneROffset`、`RoadLayout.FindSectionAtSOffset`。

**调用挂点（P1-C）：**

- `FMoKuEditSplinesComponentVisualizer` — Alt+W 拖点 **结束** 后
- `AMoKuEditSplineActor::RefreshDynamicRoadMesh()` — **之前**（用 `bSuppressJunctionRecookFromRoadRefresh` 防递归）

---

## 4. Lane Pick — `MetaLandRoadLaneEditPick`（扩展现有文件）

**声明：** `Public/MetaLandRoadLaneEditPick.h`  
**实现：** `Private/MetaLandRoadLaneEditPick.cpp`

> 该文件已有 `namespace MetaLandRoadLaneEditPick` — **新函数继续放在此 namespace 内**，不要另建文件或新 namespace。

### 4.1 新增 API

| 函数 | 职责 |
|------|------|
| `PickLaneAnchorAtMouse(...)` | Ctrl+Click / Connect 模式：面 + 端点环 → `FMetaLandRoadDrawAnchor` |
| `ResolveMouthSideFromMouse(...)` | 光标距 span StartS/EndS 近端判定 `MouthSide` |
| `TryExpandAnchorBundle(...)` | Shift+Click：仅当相邻 lane + 同 S + 同 MouthSide |
| `IsBundleCompatible(...)` | 第二次 Pick 是否应 **新开 DrawSession** |

### 4.2 Pick 规则（实现时必须遵守）

1. **端点环优先**于 lane 面中心  
2. **Bundle**：`|ΔLaneIndex|==1` 且同 `AnchorS`、同 `MouthSide`  
3. **Branch**：不同 `AnchorS` 或不同 `MouthSide` → 返回 `false`，由上层开新 Session  
4. 复用已有 `PickLaneWidthKeyAtMouse`、`IsLaneValidOnSpanSection`  
5. 常量：新增 `LaneMouthRingPickRadiusPx` 与 `MetaLandRoadNativeEditVisualization` 环半径对齐（当前 NativeEdit 36px）

### 4.3 与 Alt+T 多选分离

| 用途 | 状态所在 | 不得混用 |
|------|----------|----------|
| Alt+T 编辑 | `UMetaLandRoadSectionComponentVisualizerSelectionState::MultiSelectedLanes` | 改宽 / DeleteLane |
| 续画 Anchor | `FMetaLandRoadDrawAnchor::BundledLaneIndices` | Draw / Connect |

---

## 5. World Builder 会话 — `MetaLandRoadEditorMode` + `MetaLandRoadModule`

### 5.1 EditTarget（P0-A / P0-B）

**声明：** `Public/MetaLandRoadEditorMode.h`

```cpp
const FMetaLandRoadEditTarget& GetEditTarget() const;
void SetEditTarget(const FMetaLandRoadEditTarget& Target);
void ClearEditTarget();
```

**实现：** `Private/MetaLandRoadEditorMode.cpp`

| 改动点 | 行为 |
|--------|------|
| `Enter()` | **不要**在 Draw 中清 `EditTarget`；仅 `SelectNone` 清 viewport 选中（现有） |
| `HandleClick` / 新 `HandleAnchorPickClick` | Ctrl+LMB → `PickLaneAnchorAtMouse` → `SetEditTarget` |
| `InputKey` Escape | 结束 DrawSession，**保留** EditTarget |

**Module 侧：** `Private/MetaLandRoadModule.cpp`

| 改动 | 文件位置 |
|------|----------|
| `FindSelectedOrOwnedRoadSpline()` | **优先** `EditTarget.Spline`，再 Selection，再 `ActiveNativeEditSpline` |
| `MoKuEditorActorPicker::CanUsePickInput()` | World Builder Mode 也返回 true（当前仅 Default Mode） |
| `EnterLaneEditMode()` | 无 Selection 时允许 `EditTarget.Spline` 进入 Alt+T |

### 5.2 Connection VisMode（P2-B，可选）

**已有预留：** `EMetaLandRoadEditorVisMode::Connection` in `MetaLandRoadTypes.h`

- 热键：与 `RegisterSectionLineEditHotkey` 同路径注册 Alt+C（`MetaLandRoadEditorModeCommands`）
- `ApplyEditorVisModeSideEffects`：Connection 模式绘制 mouth 环，Pick 走 `PickLaneAnchorAtMouse`
- **不要**在 P0/P1 未完成前实现 Connection 模式全部 UI

---

## 6. 画路工具 — `MetaLandRoadInteractiveTool`

**声明：** `Private/Tools/MetaLandRoadInteractiveTool.h`  
**实现：** `Private/Tools/MetaLandRoadInteractiveTool.h` 对应 `.cpp`

### 6.1 新增成员（`.h`）

```cpp
FMetaLandRoadDrawAnchor StartAnchor;
FMetaLandRoadDrawAnchor HoverEndAnchor;
bool bHasStartAnchor = false;
bool bHasHoverEndAnchor = false;
EMetaLandRoadDrawFinishMode FinishMode = EMetaLandRoadDrawFinishMode::Direct;
/** 并行预览用；Finish 时各 Spawn 一个 Actor. */
TArray<FMetaLandRoadDrawAnchor> PendingBranchAnchors;
```

### 6.2 流程改造（P1-B）

| 步骤 | 函数 | 说明 |
|------|------|------|
| 1 | `TryPickStartAnchor()` | 无 `bIsEditing` 时 Ctrl+Click → 设 StartAnchor |
| 2 | `BeginDrawFromAnchor()` | 替代裸 `SpawnActor`：`DrawSeed` + `InitProfileFromAnchor` + `FitSplineToAnchor` |
| 3 | `OnClickPress` 第一下 | 有 StartAnchor 才落点；无 Anchor 时 **不** Spawn（或提示 Pick lane） |
| 4 | Hover | `UpdateHoverEndAnchor()` → ghost blend（P2） |
| 5 | `OnFinishedSplineEdit` | `ConnectTo` + `FinishMode` 分支 + `PropagateConnectionUpdates` |

### 6.3 与现有逻辑关系

| 现有 | 处理 |
|------|------|
| `AddSplinePoint` 内 `SpawnActor` | 有 `StartAnchor` 时走 `BeginDrawFromAnchor` |
| `CookJunctionForActiveRoad` | `FinishMode == Junction` 时保留 |
| `StateOfEditor` Forward/Defult | 与 `Anchor.bFromSuccessor` 统一；避免两套方向状态 |
| `GuidanceLeft/Right` | rubber-band 从 **EvalMouthTransform(StartAnchor)** 出 |

---

## 7. 传播与刷新 — `MoKuEditSplineActor`

**文件：** `Classes/MoKuEditSplineActor.h` / `Private/MoKuEditSplineActor.cpp`

| 改动 | 说明 |
|------|------|
| `RefreshDynamicRoadMesh()` | 末尾（或合适位置）调 `UMetaLandRoadSplineComponent::PropagateConnectionUpdates` |
| 递归 guard | 继续用 `bSuppressJunctionRecookFromRoadRefresh`；Propagation 内禁止再触发全图 Recook |
| `DebugRecookLinkedJunctions` | 不改语义；Propagation 与 Junction Recook **分工**：Connection 口 vs ≥3 Arm hub |

---

## 8. 可视化 — 复用现有 Visualizer / LaneViz

**不新建文件.**

| 内容 | 文件 |
|------|------|
| Anchor mouth 环（黄） | `MetaLandRoadNativeEditVisualization.cpp` 或 `MetaLandRoadLaneVisualization.cpp` |
| Pending Anchor HUD 文本 | `MetaLandRoadInteractiveTool::DrawHUD` |
| Connection 模式 mouth | `MetaLandRoadSectionComponentVisualizer.cpp` — 新分支 `IsConnectionEditMode()` |

**Draw 条件：** `EditTarget.Spline` 或 `StartAnchor.SourceSpline` 有效即画，**不要求** Actor selected。

---

## 9. 禁止项（整理代码时勿违反）

- ❌ 新建 `*DrawAnchor*` / `*ConnectionGraph*` 独立 `.cpp`（除非用户确认拆文件）
- ❌ 在 `.h` 写 `EvalTransform` / `PickLaneAnchor` 函数体
- ❌ 用 anonymous namespace 包裹新实现
- ❌ 路口 / 路尾 / 闸口 三个 UENUM 硬分支（只用 `AnchorS + MouthSide`）
- ❌ Alt+T `MultiSelectedLanes` 直接当 Draw Anchor
- ❌ 一次 Pick 多个不同方向 Branch 合并为一个 Actor
- ❌ Draw 时 `SelectNone` 清掉 `EditTarget`

---

## 10. 文件改动清单（按阶段）

### P0-A — 类型 + EvalMouth

| 文件 | 改动 |
|------|------|
| `MetaLandRoadTypes.h` | `EMetaLandRoadMouthSide`, `FMetaLandRoadDrawAnchor`, `FMetaLandRoadDrawSeed`, `FMetaLandRoadEditTarget` |
| `MetaLandRoadTypes.h` | `UMetaLandRoadLaneConnection::EvalTransform` 等声明 |
| `MetaLandRoadTypes.cpp` | `EvalTransform` / `ConnectTo` 桩或完整实现 |
| `MetaLandRoadSplineComponent.h/.cpp` | `EvalMouthTransform`, `EvalLanePositionAtS` |

### P0-B — Pick + EditTarget

| 文件 | 改动 |
|------|------|
| `MetaLandRoadLaneEditPick.h/.cpp` | `PickLaneAnchorAtMouse`, Bundle 规则 |
| `MetaLandRoadEditorMode.h/.cpp` | `EditTarget` 存取 + Ctrl+Click |
| `MetaLandRoadModule.cpp` | `FindSelectedOrOwnedRoadSpline` 优先 EditTarget；Picker 在 WB 可用 |
| `MetaLandRoadLaneVisualization.cpp` | Anchor 环绘制 |

### P1-A — Connection

| 文件 | 改动 |
|------|------|
| `MetaLandRoadTypes.cpp` | `ConnectTo` / `CanConnectTo` 完整 |
| `MetaLandRoadSplineComponent.cpp` | `FitSplineToAnchor`, `InitProfileFromAnchor` |

### P1-B — DrawTool

| 文件 | 改动 |
|------|------|
| `MetaLandRoadInteractiveTool.h/.cpp` | Start/End Anchor, `BeginDrawFromAnchor`, Finish Connect |
| `MetaLandRoadProfile.cpp` | 从 `ProfileLanes` 写 Layout（若现有 API 不够） |

### P1-C — Propagate

| 文件 | 改动 |
|------|------|
| `MetaLandRoadSplineComponent.cpp` | `PropagateConnectionUpdates` |
| `MoKuEditSplineActor.cpp` | Refresh 链路上调用 |
| `MetaLandRoadSectionComponentVisualizer.cpp` | Alt+W drag end 调用 |

### P2 — Blend + Network

| 文件 | 改动 |
|------|------|
| `MetaLandRoadInteractiveTool.cpp` | Shift + Blend preview / commit |
| `MoKuEditIntersectionActor.cpp` 或 SplineComponent | Blend v0 strip（复用 Fig1 bezier 思路） |
| `MetaLandRoadTypes.h` | `FGuid RoadNetworkId` on Actor 或 Spline（待选） |

---

## 11. 验收标准（每阶段）

### P0 完成

- [ ] World Builder 内 Ctrl+Click lane → Details / HUD 显示 `AnchorS`, `LaneIndex`, `MouthSide`
- [ ] 不选中 Actor 可 Alt+T 进入 lane 编辑（EditTarget 有效）
- [ ] 路尾 / Add Lane 闸口 / junction arm **同一 Pick API**

### P1 完成

- [ ] 从 Pick 的 lane 续画 → 新 Actor，Profile 与源 lane 一致
- [ ] Finish 到另一 lane mouth → Connection 图可查
- [ ] 拖主线近分叉处 → 分支首点跟随（Propagate）

### P2 完成

- [ ] Shift 结束 → Blend ghost + 提交
- [ ] 多 Branch 各独立 Actor，改主线分叉口联动

---

## 12. MetaRoad 对照（实现时必读）

| MetaRoad | MetaLandRoad 落点 |
|----------|-------------------|
| `DrawRoadTool::StartLaneConnection` | `FMetaLandRoadDrawAnchor StartAnchor` |
| `InitRoadProfile` | `InitProfileFromAnchor` |
| `FitSplineToBeginConnection` | `FitSplineToAnchor(..., bAtSplineStart=true)` |
| `ULaneConnection::EvalTransform` | `UMetaLandRoadLaneConnection::EvalTransform` |
| `TriangulateRoadTool` | P2 Blend v0 → 长期 Arrangement2d |

路径参考：`Docs/MetaRoad-vs-MetaLandRoad.md`

---

## 13. 后续整理代码时的模块边界建议

当前 **不拆文件**；若单文件超 ~300 行且职责独立，再与用户确认：

| 候选 | 职责 |
|------|------|
| Connection 相关仍在 `MetaLandRoadTypes.cpp` | LaneConnection / RoadConnection |
| Pick 仍在 `MetaLandRoadLaneEditPick.cpp` | 所有 screen pick |
| Draw 会话仍在 `MetaLandRoadInteractiveTool.cpp` | Spawn / Finish / Guidance |

**RoadNetworkId** 若加在 Actor 上，放 `MoKuEditSplineActor.h`；若加在 Spline 上，放 `MetaLandRoadSplineComponent.h`。

---

## 14. Debug 验证顺序（严格按序，上一步过再下一步）

> 每步在 **Output Log** 过滤 `[MetaLandRoad]` / `[LaneAnchor]`。编译后 **重启 Editor** 再测。

### 14.0 环境准备（一次性）

1. 编译 `WorldDevEditor`，启动 Editor，打开测试关卡（含至少一条 `MoKuEditSplineActor` 路）。
2. 进入 **World Builder** 模式（MetaLandRoad Editor Mode）。
3. 确认 **非 Game View**（按 `G` 若需要；Alt+T/W 依赖 ComponentVisualizer）。
4. Output Log → 过滤：`MetaLandRoad`、`LaneAnchor`、`Recook`。

---

### P0-A — 类型 + EvalMouth（无 Pick UI 时用控制台）

| 序 | 操作 | 预期 | 失败则查 |
|----|------|------|----------|
| **A1** | 选中一条路 → Details 看 `RoadLayout` Sections 数量 | ≥1 section，lane 索引 ±1,±2… | Profile 未初始化 |
| **A2** | 控制台（实现后）`MetaLandRoad.DebugEvalMouth` 或断点调 `EvalMouthTransform`：路尾 S=Length、Lane +1、MouthSide=End | Log 打出 `AnchorS=`, 世界坐标在路尾外缘 | `FindSectionAtSOffset` / `EvalLanePosition` |
| **A3** | 同命令：S=0、MouthSide=Start | 坐标在路头 mouth | 切向是否反向 |
| **A4** | Alt+E Split Section → Alt+T Add Lane → 再 EvalMouth：AnchorS=Section.SOffset、Add 的那条 lane | 坐标在 **闸口台阶** 处 | Section 边界 S 与 lane span |
| **A5** | 改 lane Width（Alt+T）→ 再 EvalMouth 同 AnchorS | 位置 lateral 变化，S 不变 | Width 曲线 / ROffset |

---

### P0-B — Pick + EditTarget

| 序 | 操作 | 预期 | 失败则查 |
|----|------|------|----------|
| **B1** | World Builder 内 **不选 Actor**，Ctrl+Click 路面 lane | Log：`[LaneAnchor] Pick` + `LaneIndex` / `AnchorS` / `MouthSide`；HUD 或 Details 显示 EditTarget | `PickLaneAnchorAtMouse`；mesh `bSelectable` |
| **B2** | 点击 lane **端点环**（近路尾） | `MouthSide=End`，`AnchorS`≈该 lane span 终点 | `ResolveMouthSideFromMouse` |
| **B3** | 点击 lane **另一端环** | `MouthSide=Start` | 环 pick 半径 |
| **B4** | Shift+Click **相邻** lane（同端） | `BundledLaneIndices` 2 条；同 `AnchorS` | `TryExpandAnchorBundle` |
| **B5** | 再 Shift+Click **不同 S** 或 **对向 mouth** 的 lane | **不合并**；Log 提示新 Session / 新 Anchor | Branch 误合并 |
| **B6** | 不选 Actor → 按 **Alt+T** | Lane 编辑 overlay 出现；Log：`Alt+T lane edit ON` | `FindSelectedOrOwnedRoadSpline` → EditTarget |
| **B7** | 按 **P** → 右键点路 Actor（World Builder 内） | 选中并同步 Spline；Log：`OnActorPicked` | `CanUsePickInput` WB |
| **B8** | 画路工具激活中 Esc | Draw 结束，**EditTarget 仍在** | Esc 误清 EditTarget |
| **B9** | Add Lane 闸口 lane → Ctrl+Pick → 看 Anchor | `AnchorS`=台阶 S，lane 为附加 lane | Add Lane section 索引 |

---

### P1-A — Connection API

| 序 | 操作 | 预期 | 失败则查 |
|----|------|------|----------|
| **C1** | 两条路，手动/调试 `ConnectTo` 两个 LaneConnection | `IsConnected()==true`，双向 Outer 指针 | `MetaLandRoadTypes.cpp` |
| **C2** | 对已连接 lane 再 Connect | `CanConnectTo` 返回 false | 重复连接 |
| **C3** | `FitSplineToAnchor` 后看分支首点 | 与 `EvalMouthTransform` 重合（±几 cm） | 切向 / 控制点索引 |

---

### P1-B — 从 Anchor 续画

| 序 | 操作 | 预期 | 失败则查 |
|----|------|------|----------|
| **D1** | Pick 路尾 lane → Ctrl+Click 落第一下 | **新 Actor** Spawn；Profile lane 数/宽 = 选中 lane | `BeginDrawFromAnchor` |
| **D2** | 落 2 个点 Finish | 新路 mesh 可见；绿色 rubber-band 从 mouth 出 | Guidance + EvalMouth |
| **D3** | Pick 闸口 Add Lane → 续画 | 新 Actor 从台阶 S mouth 出，**主线 spline 点数不变** | 误改主线 |
| **D4** | 同 lane Pick 两次不同方向 Finish 到两目标 | **2 个 Branch Actor**，各 1 Connection | Session 拆分 |
| **D5** | Bundle（Shift 选 2 相邻 lane）续画 | **1 个 Actor**，Profile 2 lane 宽 | Seed ProfileLanes |

---

### P1-C — 传播

| 序 | 操作 | 预期 | 失败则查 |
|----|------|------|----------|
| **E1** | 已挂接分支 → Alt+W 拖 **近分叉** 主线控制点 | 分支 **首点**跟随；Log：`PropagateConnectionUpdates` | Refresh 链 / 递归 guard |
| **E2** | 拖 **远离分叉** 的中段 | 分支内部点 **不动** | 过度 Fit |
| **E3** | 改 Add Lane 宽度 | 分叉口 lateral 更新（若启用 Profile 同步） | Propagate 范围 |
| **E4** | 触发 Junction Recook | 无死循环；`bSuppressJunctionRecookFromRoadRefresh` 生效 | 双 Recook |

---

### P2 — Blend / Network（后续）

| 序 | 操作 | 预期 |
|----|------|------|
| **F1** | 续画末点 hover 目标 lane + **Shift** | Ghost blend 预览，未写 mesh |
| **F2** | Shift 确认 Finish | Blend patch + Connection |
| **F3** | 主线+多 Branch 同 NetworkId | Outliner/选中高亮整组（若已实现） |

---

### 14.1 回归（每阶段合并前必跑）

1. **Junction**：画 T 字 → Cook → `[Recook] BEGIN/END tris=`，无叠面。
2. **Alt+W**：环切换、拖点、mesh 刷新。
3. **Alt+T**：Add Lane / Delete Lane / Width Key 仍正常。
4. **World Builder 画新路**（无 Anchor）：原 Ctrl 画路 + Junction 未退化。

---

### 14.2 建议 Log 前缀（实现 P0 时统一）

```text
[LaneAnchor] Pick  Spline=%s Section=%d Lane=%d AnchorS=%.1f Mouth=%s Bundle=%d
[LaneAnchor] EvalMouth  Loc=%s Rot=%s
[LaneAnchor] EditTarget set/clear  Road=%s
[LaneAnchor] Propagate  Modified=%s UpdatedBranches=%d
```

---

*文档版本：与 Lane Anchor 架构讨论同步；实现前请先读 `.cursor/skills/metal-androad-incremental-dev/SKILL.md`。*
