# Ctrl 画线折角（封面贝塞尔）

对照 skill `metal-androad-incremental-dev`。本文只记 **普通 Ctrl 折线封面**，不审 Fig1、不审 C / 嘴。

Undo 仍认 [MetaLandRoad-Ctrl-Draw-Undo.md](./MetaLandRoad-Ctrl-Draw-Undo.md)。

## 契约

- 作者样条控制点不改位置，不往中心线插折角 keys。
- 路身禁止 `NeedAlong` 推 tip。
- `EvalLanePosition` 仍生样条（路口 Fig1）。
- **封面 mesh** 在 `NeedAlong` 窗口里：中线 / 内边 / 外边走**同一段**三次贝塞尔。`PathCenter` 跟中线，不跟折线尖角。

## 算法

1. 提出带角度的样条点（跳过 CutS / 嘴）。
2. `NeedAlong = CornerFilletNeedAlongFitCm(半宽, θ, 较短腿)`。
3. 内侧交出入法线得 `FilletCenter`，`R ≥ 1.15×半宽`。
4. 参数 `U` 沿窗口 `KeyS ± NeedAlong`。
5. `Off = 0` → 封面中线；`Off ≠ 0` → 该侧轨。手柄 `(4/3)×R_rail×tan(θ/4)`，内边 `R_rail` 至少 `0.25×|Off|`。

禁止再加回来：

| 做法 | 为何不行 |
|------|----------|
| 往中心线插 T1/T2 / 弧 key | 改已确认点，毒 Fig1 |
| `R = HalfW` 再被 `NeedAlongMin` 封顶 | 内边 R=0，星状折叠 |
| 只转 `Right` + `1/sin(θ/2)` 外扩 | 环挤到一点再竖起来（图二扇骨） |
| 内边钉死一个 miter 尖点 | 只有外边圆 |
| `Off≈0` 不写 `PathCenter` | 边圆了、封面中线还是折的 |

## 相关函数

| 故事 | 文件 | 函数 |
|------|------|------|
| 提出折角 + 窗口 | `MetaLandRoadSplineComponent.cpp` | `CollectBodyCornerBeziers` |
| 中线 / 轨求值 | 同上 | `TryEvalBodyCornerBezier` |
| 转角加密采样 | 同上 | `AppendCornerKeySampleParams` |
| loft 写入封面 | 同上 | `BuildLinearApproximation` |
| Ctrl 只写新段切线 | `MetaLandRoadInteractiveToolDraw.cpp` | `ApplyWidthAwareNewSpanTangents` |
| 公式 | `MetaLandRoadTypes.h` | `CornerFilletNeedAlongFitCm` / `CornerFilletRadiusCm` |

`#if 0`：`CollectCornerFillets` / `TryEvalFilletFrame`。零调用：`ApplyAngleAwareCornerTangent`。本轮不接、不删。

## 检查

- L / 钝角：内外边都圆，封面中线跟弯走。
- 路宽在弯里大致保住。
- 已确认的前一段形状不变。
- 旧路口 Recook 仅当碰到 Fig1。
