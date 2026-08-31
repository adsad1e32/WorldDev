# 引导线 vs 路 mesh：接缝审核

对照 skill `metal-androad-incremental-dev`、[Agent-Conventions.md](./Agent-Conventions.md)。  
**本文只审「嘴对嘴引导 + A 烘焙 + 路身采样」**，不审 Fig1 路口封面、不审 Landscape stamp。

一次只定下面标了「待讨论：是」的项。没点头的不要拆文件、不要再叠一套半径公式。

---

## 0. 这次为什么全是红线

更新后「接缝路径无效（红线）」铺开，**不是嘴口朝向突然全坏了**，是把**路身直角 fillet 公式**当成了 **C / U 接缝的判废标准**。

| 公式 | 适用 | 误用后果 |
|------|------|----------|
| `R = 半宽 × (1+tan(θ/2)) × (1+sinθ)` | **一个**样条拐点（图四 90°） | 对 C 弯 `θ≈180°`：`tan` 被夹到 1.15 → `R≈3.2×半宽` |
| `CornerDiffuseRadiusCm` 再按弦长短放大到 3.4 倍 | 弦短于 `2×NeedAlong` 时再扩 | C 弯弦往往短 → `InvertR` 可到 **10×半宽** |
| Dubins `InvertR = DiffuseR`，找不到 word 就 `BuildErrorMouthLinkRails` | 预览失败 | **整条绿线变红，A 被拦住，中间没有 mesh** |
| `MinCenterlineRadiusCm < NeedR` | 折线 `Seg/转角` 估半径 | 密采样仍会估低 → 绿走廊也被判废 |

图：红线画在两嘴之间、中间没有沥青 —— 引导判废，烘焙根本没跑。

---

## 1. 两条故事（必须分开）

```
引导（Editor / ToolDraw）                         路 mesh（Runtime / SplineComponent）
─────────────────────────                         ────────────────────────────────
嘴框 L/R + Leave/Arrive
    → FLeaveArrivePlan（现在几乎只有 DubinsCSC）
    → SampleLeaveArrivePlan（密点，~45cm）
    → OffsetParallelWidthRails（中线 ± 半宽）
    → 交叉 / 半径检查 → 绿或红
    → 写入绿折线 Linear key                         → BuildLinearApproximation
                                                  样条 Center + Right × Off（sweep）
                                                  EvalLanePosition = 生样条（Fig1）
```

**契约（已点头，sweep 试跑）：**

1. **红线标准（引导）**：左右轨在 XY **相交**，或中线最小半径 **&lt; 半宽**（内边几何上为负）。这是「穿插」。  
   **不是**「C 弯总转角套路身 fillet 公式」。
2. **绿线几何**：一条中线走廊，左右是它的等宽偏置。预览点密，只负责看见。
3. **A 烘焙**：落**同一条绿折线**（同一 Plan），Linear key，弯处按 ribbon step 留点。禁止再砍成 2–4 个再靠三次曲线圆。
4. **路 mesh**：只扫已写入的样条。`CollectCornerFillets` / `TryEvalFilletFrame` 不再换中线。  
   `EvalLanePosition` 保持生样条（Fig1 / 半宽查询禁止进第二套中线）。

---

## 2. 逐段审核

### 2.1 转角公式被用到接缝上

```
[审核]
位置：MetaLandRoadTypes.h CornerFilletRadiusCm / CornerDiffuseRadiusCm / CornerTurnAngleRad
现象：路身 90° 用 (1+tan(θ/2))×(1+sinθ)。接缝用 CornerTurnAngleRad = max(Leave·Arrive, Leave·Chord+Chord·Arrive)，C 弯经常接近 π。
为何不合理：一个拐点的 fillet 和 Dubins CSC（两段弧+直线）不是同一几何。θ=π 时 tan 夹死、sinπ=0，再乘弦短放大，InvertR 大到 Dubins 无解。
建议结果：公式只留给 CollectCornerFillets / ApplyAngleAware。接缝 InvertR 下限 = 半宽（或 1.15×半宽）。C 弯能连就绿，连不上（轨交叉或 R&lt;半宽）才红。
待讨论：是
```

### 2.2 引导判废叠了三层且口径不一致

```
[审核]
位置：ToolDraw.cpp IsLeaveArrivePlanUnusable / IsGuidanceCorridorUnusable
现象：Unusable 曾要求 Plan.Radius ≥ CornerFilletRadiusCm(半宽, 整段θ)。Corridor 又要求 MinCenterlineRadius ≥ 同一 NeedR。
为何不合理：MinCenterlineRadius = 相邻折线 Seg/Δheading，永远偏小。NeedR 对 C 弯又偏大。绿走廊也会被判红。
建议结果：留下交叉检测；半径只跟半宽比（内边 R_inner = R−半宽 &gt; 0）。NeedR 闸门撤回。
待讨论：否（与 §0 同源，本轮已撤回 NeedR 闸门，避免全红）
```

### 2.3 预览密、烘焙稀（真穿插）

```
[审核]
位置：ToolDraw.cpp KeyLeaveArrivePlan
现象：SampleLeaveArrivePlan 可 24+ 点画绿线。旧逻辑 while(Num&gt;4) 删点。A 之后三次样条弦切，内边挤成蝴蝶结。
为何不合理：引导和 mesh 不是同一条折线。用户看到「preview 很好、mesh 穿插」。
建议结果：烘焙按弧约 23° 一点，上限 16。不要砍回 4。
待讨论：否（已改，应保留）
```

### 2.4 红线 X 是失败回退，不是走廊

```
[审核]
位置：ToolDraw.cpp BuildErrorMouthLinkRails
现象：Dubins 失败后画 TipL→EndL、TipR→EndR 直线。两嘴相对时直线本来就交叉，屏幕上「接缝路径无效」。
为何不合理：X 只表示「没找到等宽走廊」，不是左右接反。用路身公式抬 InvertR 后，失败变多，X 变默认。
建议结果：先保证 Dubins 在 R≥半宽 时能出走廊。X 只作失败可视化；配对优先不交叉的一组。
待讨论：是（失败时要不要改画「应有的 C 弯虚线」而不是直线 X）
```

### 2.5 路 mesh 不读 FLeaveArrivePlan

```
[审核]
位置：MetaLandRoadSplineComponent.cpp BuildLinearApproximation / CollectCornerFillets / EvalLanePosition
现象：mesh 只看样条拐点。EvalLanePosition 故意生采样（防 fillet↔半宽循环）。Fillet 只在 BuildLinearApproximation。
为何不合理：引导的圆在 Plan 里，烘焙若点少，mesh 再也拼不回那条圆。Fillet 窗口靠近 CutS 还会被裁。
建议结果：A 把 Plan 中线落到样条（密 key + 切线）。mesh 继续只对拐点 fillet。不要把 Dubins 再写进 Runtime。
待讨论：是（要不要烘焙后把 Plan 半径写进 spline 元数据，mesh 只读）
```

### 2.6 一文件多条接缝故事

```
[审核]
位置：MetaLandRoadInteractiveToolDraw.cpp
现象：吸附、Dubins、Hermite、红线、烘焙 key、Offset rails、C 续画 pin 都在同一 Draw.cpp。
为何不合理：改半径会同时打到预览判废和 A 烘焙，难回滚。
建议结果：先不拆文件。半径/判废只留一处「接缝走廊」file-local（InvertR + Unusable）。同意后再抽。
待讨论：是
```

---

## 3. 建议合同（实现对照）

| 检查 | 绿 / 可 A | 红 / 拦 A |
|------|-----------|-----------|
| 左右轨 XY | 不相交 | `CountGuidanceRailCrossings &gt; 0` |
| 中线局部半径 | `MinR + 1 ≥ 半宽` | 内边会翻面 |
| Dubins | 在 `R ≥ 半宽` 找到 CSC | 失败 → 红直线，不写缝 |
| 路身弯 | A 写出绿折线 Linear key；mesh 只扫这条。不再用 NeedAlong 另画圆 | 与接缝 InvertR 无关 |

**不要：** 用 `Leave·Chord + Chord·Arrive` 当 Dubins 最小半径。  
**不要：** 用折线估的 `MinR` 去比 `3×半宽` 的 NeedR。

---

## 4. 本轮已改 / 未改

| 项 | 状态 |
|----|------|
| 红线 = 左右相交或中线 R&lt;半宽 | 已按契约落地 |
| 接缝 InvertR ≥ 1.15×半宽（可解 Dubins） | 已按契约落地 |
| A 烘焙同一 Plan 中线（Linear 密点，不再三次弦切） | 已按契约落地 |
| 接缝步长按 R、半宽算（弦误差 + 半 fillet-skip）；256 只防爆 | 已按实时几何落地 |
| 绿线与 A 同一 Plan、同一段数；Linear 缝不套路身 fillet | 已按「preview 对齐 mesh」落地 |
| A 写绿线 GuidanceCenterline，不从最后一个样条点重算 Dubins | 已按图一对齐落地 |
| 红线按 A 不写缝 | 保持 |
| 两嘴切线锁到嘴框法向（不跟弦走） | 已按图三落地 |
| 嘴口预留直段：按夹角 NeedAlong，装不下按比例缩，不清零 | 已按图三落地 |
| EvalLanePosition 生样条 | 保持 |
| 路身 fillet 只用于拐点 | **已改：loft 只扫作者样条。A 写 Linear 绿折线。禁止再往拐点插 T1/T2 / CustomTangent。CollectCornerFillets 已卸，待删** |
| 拆 Draw.cpp | 未做 |
| 失败时画 C 弯虚线代替 X | 未做 |

---

## 5. 请你拍板

1. 红线是否**只**表示「左右穿了 / 找不到 R≥半宽 的走廊」？  
2. 失败时保持直线 X，还是画一条「本该有的」C 弯虚线？  
3. A 烘焙是否继续「只写 spline key」，mesh 不读 Plan？  
4. Draw.cpp 要不要在下一轮按「走廊 / 吸附 / C-pin」拆文件？
