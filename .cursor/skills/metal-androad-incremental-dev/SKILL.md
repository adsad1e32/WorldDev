---
name: metal-androad-incremental-dev
description: WorldDev MetaLandRoad conventions — .h decls / .cpp impl, split by responsibility not line count, review marks issues for discussion, MetaRoad reference, Fig1 junction + Junction Spec. Use when editing C++ under Plugins/MetaLandRoad, comparing MetaRoad, junction/FillMesh/UV/布线, RDG/stamp, Interactive Tool, or when the user mentions skill规范、审核、拆文件、路口规范.
---

# MetaLandRoad Incremental Development

## Terminology

| Name | Meaning |
|------|---------|
| **MetaRoad** | **Target / reference plugin** (Epic Marketplace) — compare against this |
| **MetaLandRoad** | **This project's plugin** — code we edit |

Read before mesh or structure work:

- [Docs/MetaRoad-vs-MetaLandRoad.md](../../../Docs/MetaRoad-vs-MetaLandRoad.md)
- [Docs/Agent-Conventions.md](../../../Docs/Agent-Conventions.md)
- [Docs/UE-InteractiveTool-Standard.md](../../../Docs/UE-InteractiveTool-Standard.md) — **any** Editor Mode / Interactive Tool
- [Docs/MetaLandRoad-Runtime-Editor.md](../../../Docs/MetaLandRoad-Runtime-Editor.md) — Runtime vs Editor cook
- [Docs/MetaLandRoad-Junction-Spec.md](../../../Docs/MetaLandRoad-Junction-Spec.md) — **路口 cook 唯一正文**（T / Cross / N）

## Agent greeting (mandatory)

When this skill applies, **start every user-facing reply with「大哥，」** before the substantive answer or action summary.

Do not skip the greeting on follow-up turns in the same MetaLandRoad task unless the user explicitly asks to stop.

## Product contracts (do not undo)

- Landscape params (Layer Factor / Height Side Falloff / Height Side Offset / Height Offset) affect **height only**.
- Road weight falloff is Landscape Key **`RoadWeightFalloffWidth`** (0 = inherit Actor default). Junction / unset keys use Actor **`RoadWeightFalloffWidth`**. Profile PS draws **Road** only. No Dirty layer. No JF TMap.
- Begin/End **data stays**; overlay **draws at CutS**. Hub-interior keys are not display keys.
- Junction cook is **Fig1** + vertex-order audit; fail → abort mesh. **Follow [Junction Spec](../../../Docs/MetaLandRoad-Junction-Spec.md) exactly.**
- Attribute UI (Landscape, PropSetting, Sidewalk, later rows) stays on `FMetaLandRoadEditorModule` + the existing component visualizer. Do not add a module. Enter = overlay flag only. Hover = visualizer tick. Click = visualizer, miss returns false. Draw uses `GetJunctionMeshGenerationSpans`.
- CutS / remaining-road spans have **one owner**: `GetJunctionMeshGenerationSpans`. Do not copy a second trim test.
- Do not fix N-way by changing T hole/fillet, or T by changing Cross/N hole. Same-spline adjacent = outer chord, never inward fillet.
- Body-corner mesh: loft sweeps the authored spline (`Center + Right × Off`). Do not replace PathCenter with a second fillet curve. `EvalLanePosition` stays raw (Fig1 / half-width). Junction holes stay Fig1. Contract: [Docs/MetaLandRoad-Guidance-vs-Mesh-Review.md](../../../Docs/MetaLandRoad-Guidance-vs-Mesh-Review.md) §1 / §3.
- **Ctrl 画线（普通落点，不是 C / 嘴）：**
  - **落点：** 射线第一命中的世界坐标就是点击。路上才吸附 / junction（`InSertTransform`）。地形只改 Z 贴地，XY 不换、不走中心线吸附。
  - **Undo：** 见 [Docs/MetaLandRoad-Ctrl-Draw-Undo.md](../../../Docs/MetaLandRoad-Ctrl-Draw-Undo.md)。加点前 `Modify`；一笔交易只在 `AddSplinePoint`；禁止 Release `EndTransaction`；Undo 后路没了就结束会话，还在就按当前路宽重画引导。
  - **折线：** 见 [Docs/MetaLandRoad-Ctrl-Draw-Corner.md](../../../Docs/MetaLandRoad-Ctrl-Draw-Corner.md)。已确认点不改；只写新段；禁止插折角 keys；封面中线/内外边同一段贝塞尔。`EvalLanePosition` 仍生样条。C / 嘴另一套。

## Review before rewrite (mandatory)

When auditing or simplifying, **do not silently rewrite**. Mark the case, state the result, wait to discuss.

Template:

```
[审核]
位置：path:line（或函数名）
现象：现在代码在做什么
为何不合理：重复 / 一文件多故事 / 违反产品契约 / 非 UE 分层 / 死路径
建议结果：留下 | 合并到 X | 抽到拥有者类 | 新文件（新 UCLASS/Tool/Shader）| 删除死路径
待讨论：是 / 否
```

- One slice = a few marks, then stop for the user.
- After agreement, implement only the agreed items.
- Prefer small diffs. Do not “clean the whole plugin” in one pass.

**Dead / unused during code review (hotkeys, preview, `#if 0`, commented spawn, leftover MetaRoad accessors):**

- Mark `[审核]` with 栏目：**死路径**.
- Tell the user what it is and **ask 是否移除**. Do not delete until they say yes.
- Do not attach new features to a marked dead path.
- Park the list. Do not turn a feature session into a cleanup sprint.

**Refactor leftover accessors (Lane / Section / Layout):**

- Live API is **index + SOffset** (`GetStartSectionIndex`, `GetStartOffset`, `FindSideSection`, `GetLaneByIndex`).
- After a refactor, delete unused wrappers in the same agreed pass: `GetStartSection` / `GetEndSection` / `GetLeftRightSections` style object-ref getters with **zero callers**.
- Never return a function-local `static Fallback` object by reference.
- Symmetric APIs (`Pin`/`Unpin`, generic `EvalAttributeAt`) — mark first; do not delete as “waste” without asking.
- Full text: `Docs/Agent-Conventions.md` §3.0.

## Where code lives (by story, not by line count)

**Do not use a line-count threshold** (e.g. 1500). A feature may be 80 lines or 3000; the test is **one file, one story**.

| Story | Declaration | Implementation |
|-------|-------------|----------------|
| Lane layout, sampling, road mesh | `MetaLandRoadSplineComponent.h` | matching `.cpp` |
| Junction cook / cover / audit | `MoKuEditIntersectionActor.h` | matching `.cpp` |
| Landscape stamp / RDG | `EditSplineRoadInfo.h` (`AWorldSceneManagement` schedules) | stamp/RDG in that `.cpp` or a dedicated stamp `.cpp` **after discussion** |
| Draw-road input | `MetaLandRoadEditor/Private/Tools/MetaLandRoadInteractiveTool.h` | `*Tool.cpp` lifecycle + `*ToolClick.cpp` (Fig1 `OnClickPress`) + `*ToolDraw.cpp` — **no mesh cook, no Recook** |
| Viewport overlay | `MetaLandRoadEditor/Private/ComponentVisualizers/*` | Visualizer `.cpp` — **no Recook** |
| Types / layout data | `MetaLandRoadTypes.h` | `MetaLandRoadTypes.cpp` |

**Small change:** add to the owning class (`.h` decl, `.cpp` body).

**New file is allowed without asking** when it is a new `UCLASS` / `UInteractiveTool` / Global Shader + `.usf`.

**Extract from an existing file:** only after a `[审核]` mark and user OK. Split by **story** (e.g. Landscape overlay vs native key edit), not by size.

**Third copy of the same helper:** merge into the owning class `static` / file-local `static`. A `*Utils` file only if several stories share it **and** the user agreed.

**Forbidden:** new `*Generation` / `*Helper` / `*Utils` for a function used once.

## C++ style

**Declarations in `.h`, implementations in `.cpp`.** No new function bodies in headers (except unavoidable one-line `FORCEINLINE` getters).

`.cpp` may use:

- class `private` / `static` members (decl in `.h`)
- file-local `static` functions
- **anonymous namespace** for file-local helpers (UE-normal)

Do **not**:

- wrap implementations in `namespace XxxPrivate` as a second module
- put algorithms in headers
- put Editor-only APIs in runtime-facing Public headers when adding new code

**Runtime / Editor 打包（对照 MetaRoad）：** [Docs/MetaLandRoad-Runtime-Editor.md](../../../Docs/MetaLandRoad-Runtime-Editor.md)

| 模块 | Type | 进谁 |
|------|------|------|
| `MetaLandRoad` | Runtime | 游戏 cook |
| `MetaLandRoadEditor` | Editor | 仅 Editor |

双模块已落地。新代码按表放：Runtime = 数据/网格/stamp；Editor = Tool/Mode/Visualizer/Gizmo/Slate。Runtime Public 禁止 `#include` UnrealEd / LevelEditor / Interactive Tools。Runtime 调 Editor 用 `FMetaLandRoadEditorHooks`。

## Interactive Tool / RDG / materials

- **Tool / Mode (all editor modes):** follow [UE-InteractiveTool-Standard.md](../../../Docs/UE-InteractiveTool-Standard.md). `OnClickPress` dispatches only; one session per function/file.
- **Tool:** `UInteractiveTool` + PropertySet + input. Writes actors/components. Does not triangulate junctions or stamp landscape.
- **RDG:** Game Thread samples; `ENQUEUE_RENDER_COMMAND` + `FRDGBuilder` + Global Shader / `.usf`. Actor only says *who* and *when* to stamp.
- **Materials:** one place creates MIDs and sets parameters; keep a name list, do not scatter `SetScalarParameterValue`.

## Junction cook (mandatory)

**正文只认一份：** [Docs/MetaLandRoad-Junction-Spec.md](../../../Docs/MetaLandRoad-Junction-Spec.md)。改 cook 前读完。和 spec 冲突 → `[审核]` 停手。

硬规则：先定 T / Cross / N 只改一种；验完 Recook T 对侧 + Cross/图二 + 一条 5 路。同路相邻 = 外弦禁止朝 Hub 圆角。T 孔 = 整嘴 OuterCS + 外沿。Cross/N 孔 = `CS[1]..CS[N-2]` + 内轨。禁止混用。真角失败仍铺直弦。审核失败 abort（现状 soft publish 先 `[审核]`）。

## Mesh entry points

**MetaRoad (reference):** `LaneProxy` viewport preview; `TriangulateRoadTool` + `RoadMeshTools` bake.

**MetaLandRoad:** `RefreshDynamicRoadMesh` → `GenerateCombinedLaneDynamicMesh`. Junction: `RecookJunctionActor` / `RefreshJunctionCoverMesh` → Fig1 → `ValidateJunctionMeshPointIndices`.

`GenerateCombinedLaneDynamicMesh` stays on `UMetaLandRoadSplineComponent`.
