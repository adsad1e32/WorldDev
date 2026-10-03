# Ctrl 画线 Undo

对照 skill `metal-androad-incremental-dev`。本文只记 **普通 Ctrl 落点 / 加点** 的 Undo，不审 Fig1、不审 C 嘴公式。

## 现象（已修）

Undo 后绿轨**由宽变窄**，而不是取消这一笔画线。

根因不是引导线自己改宽度，是交易记错了，会话还活着，hover 用坏掉的半宽把走廊重画窄。

## 错误做法（禁止再加回来）

| 位置 | 错在哪 |
|------|--------|
| `OnClickRelease` → `GEditor->EndTransaction()` | 点一下多关一笔。`FScopedTransaction` 出作用域已经结束。 |
| `AddSplinePoint` 末尾再 `Modify()` | `Modify()` 必须在**改状态之前**。写在加点后，Undo 还原点已经是「加完点」。 |
| `AddSplinePointAtIndex` 前不 `Modify` spline | 点进不了 Undo 栈。 |
| `HandleClickPressDrawOrExtend` 外层再开 `FScopedTransaction` | 一次点击两层。外层在 `!bCtrl` 时还会开空交易。 |
| 加点后 `PostEditChange` 整条 `SplineCurves` | 再广播一次整条曲线，和 `UpdateSplineKeepTangents` 叠。 |
| `PostEditUndo` 只 `ClearDrawGuidanceRails` | Actor 已被 Undo 掉时会话仍是 DrawPath，绿轨按 0.25×默认半宽重画 → 变窄。 |

## 正确契约

1. **一笔交易只在** `UMetaLandRoadInteractiveTool::AddSplinePoint`（`Add MoKu Spline Control Point`）。
2. 进函数立刻：`this->Modify()`；已有路则 `Actor->Modify()` + `Spline->Modify()`。
3. 加点用 `AddSplinePointAtIndex(..., bUpdateSpline=false)`，然后 `UpdateSplineKeepTangents`。禁止 `SetSplineComponent` → `UpdateSpline()`。
4. Mesh 用 `RefreshRoadMeshSuppressJunction`，不要 `PostEditChange` 整条 `SplineCurves`。
5. `OnClickRelease` **不许** `EndTransaction`。
6. `PostEditUndo`（非 Lane 续画）：
   - 路没了或点数 &lt; 1 → 清引导、Idle、`ActiveActor = nullptr`。
   - 路还在 → `UpdateDrawGuidanceRails()`（宽度跟当前嘴走）。

## 检查清单

- Undo 第一下 Ctrl：整条新路取消，绿轨消失，不再剩一条窄走廊。
- Undo 第二下及之后：只撤最后一点，绿轨宽度仍是当前路宽。
- Redo 能把点和引导加回来。

## 审核清理（2026-09-04）

| 项 | 结果 |
|----|------|
| Click 外层交易 | **已删**。只留 `AddSplinePoint` 一笔。 |
| 加点后 `IsInit` / `PostEditChange` 整条曲线 / 末尾再 `Modify` | **已删**。改 `RefreshRoadMeshSuppressJunction`。 |
| `InteractiveGizmo` 双点 + `SetSplineComponent` | **未删**。`SocketMarkbleGizmo.cpp` 仍 `AddSplinePoint(..., true, Tangent)`。Gizmo 改成 `AddSplinePointAtIndex` + `KeepTangents`，不再 `UpdateSpline()`。要删整支等 Gizmo 改调用。 |

## 相关函数

| 故事 | 文件 | 函数 |
|------|------|------|
| 加点交易 | `MetaLandRoadInteractiveTool.cpp` | `AddSplinePoint` |
| Undo 后会话 | 同上 | `PostEditUndo` |
| 点击分流（无交易） | `MetaLandRoadInteractiveToolClick.cpp` | `HandleClickPressDrawOrExtend` |
| 折角只写新段 | `MetaLandRoadInteractiveToolDraw.cpp` | `ApplyWidthAwareNewSpanTangents` |
