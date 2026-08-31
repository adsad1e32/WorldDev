# MetaLandRoad Junction Spec

**本文是路口 cook 的唯一正文。** skill / `.mdc` 只作索引。改 `BuildFig1JunctionCoverMesh`、孔环、圆角、CutS、点号审核之前，先读完。

和本文冲突时：**停，标 `[审核]`，等大哥拍板。** 禁止用修 N 路的办法去改 T，也禁止用修 T 的办法去改 N 路。

相关入口：`AMoKuEditIntersectionActor::BuildFig1JunctionCoverMesh`、`ValidateJunctionMeshPointIndices`、`RecookJunctionActor`。

---

## 0. 改动协议（每次必做）

1. 先判定拓扑：`T` / `Cross` / `N`。只动这一种的分支。
2. 改完必须 Recook 并目视：**T 对侧远半幅、Cross/图二圆角、一条 5 路。** 只看当前那张图 = 没验完。
3. 热编译 **Ctrl+Alt+F11**。旧 Actor 要 Recook，不是只看新画的。
4. Output Log 要有 `[Fig1] BEGIN` / `[Fig1/Hub]` / `tris=`。
5. 整节点重煮：`ClearJunctionCoverMesh` → 重新切臂 → 路 trim 刷新 → `BuildFig1`。禁止局部补丁。

---

## 1. 拓扑（一个节点一种）

| 种类 | 判定（`FMetaLandRoadJunctionNode`） | 嘴截面 | 圆角 | 孔环 |
|------|--------------------------------------|--------|------|------|
| **T** | `Arms==2` 且 through=1、stub=1 | 直路 **Both / 全宽**；勿当半幅 Cross | 只在 **不同路** 的两角 | **整嘴 OuterCS + 外沿**；同路远半幅 = **外弦** |
| **Cross** | 两条 through，不是 T | 全宽 CS | 不同路相邻才圆角 | **内沿** `CS[1]..CS[N-2]` + 内轨 |
| **N** | polar 后 ≥3 个嘴 | 全宽 CS | 同上 | 与 Cross 相同 |

禁止：

- 把 Cross / N 当成 T（半幅、远半幅 ExtraFar、整嘴孔）。
- 把 T 当成 Cross（孔只走内沿 → 对侧方缺口）。
- 一次 cook 里有的嘴走整嘴外沿、有的走内沿（孔环自交、扇面）。

`bTJunction` 只允许用上表那一行。不要用「看起来像丁字」猜。

---

## 2. 嘴截面点序

面向路口：`Toward = Anchor − OuterMid`（勿用 spline 前进方向；through 会反）。

`CS[0] = 左外` → 车道边 → `CS.Last() = 右外`，用 `Cross(Toward, Up)` 比投影。

- 条带 CS 保持这套 L→R。
- 孔环若要 CCW，只许 **拷贝后反转**，不许为了环去改条带 CS。
- **禁止** 2 点嘴把对侧外沿当车道内沿（斜穿路口）。不够 4 点就补 `L, lerp(L,R,0.25), lerp(L,R,0.75), R`。
- `GetEdgeLanePair`：右外=`Last`，右内=`N-2`（或向外 25%）；左对称。

---

## 3. Polar 与角

嘴中点 `Atan2` 绕 Anchor 排序。角 `i` 只连 `Port i` 的右外/右内 → `Port i+1` 的左外/左内。

禁止把一堆 cut 点单独 polar 排序打乱 Side 的 L→R。

---

## 4. 同路相邻 = 不是角（硬规则）

`Port.Spline == NextPort.Spline`：

- **不是圆角。** 禁止朝 Hub 做贝塞尔 / 内凹弧。
- 只许 **外弦**（T 对侧远半幅走这里）。
- 禁止用 LaneW / Probe /「路面是否占缝」再猜一遍（会把 T 对侧当圆角，路腰被掐进去）。

不同路且弦 > 10cm 且内弦 > 10cm → 才是真角。

---

## 5. 真角条带

真角：一条浅外弧 + **平行**内轨（路法向，宽≈laneW）。

禁止：两条独立弦弧、只朝 Hub 偏置、穿插、一点扇面。

中线宽不在 `[0.25, 2.5]×laneW` → **直弦条带仍要铺**（`bMeshFillet` 保持 true）。  
关条带又让孔只走内沿 = 图二三角漏地。

同路 / through-hub 长弦：不铺圆角条带。

---

## 6. 孔环（硬规则）

**一次 cook、一种孔。** 所有嘴同一套。

| 拓扑 | 孔环 | 远半幅 / 同路缝 |
|------|------|-----------------|
| T | 每嘴 **全部** OuterCS，再接下一段 **外轨/外弦** | 同路外弦盖对侧；半幅 T 才 ExtraFar |
| Cross / N | `CS[1]..CS[N-2]` + **内轨** | 外楔子由真角条带盖，不靠改 T 孔 |

禁止：

- 自交检测后把孔改成「只留内点直弦」（T 对侧被剪掉）。
- 耳切失败就当 **主封面** 从一点扇出去。
- 没有条带、只 ear-clip 外轮廓当主封面。

耳切可作孔内三角化。扇面只许当「完全没面」的最后手段，且不得当 N 路主封面。

---

## 7. 对症（禁止连坐）

| 看到 | 禁止 | 只许 |
|------|------|------|
| N / 图二三角缝 | 改 T 孔、给同路加圆角、统一成一种孔 | 不同路真角条带（圆角或直弦）；嘴 L→R |
| T 对侧方缺口 | 把 T 改成内沿孔 | 整嘴外沿 + 同路外弦 |
| T 对侧路腰内掐 | 同路当圆角 | 同路外弦 |
| 线框中心交叉扇 | 关审核硬出网 | 修环序；审核失败 abort |

---

## 8. 点号审核

每次 `PublishJunctionDynamicMesh` 前 `ValidateJunctionMeshPointIndices`。

必须查：Side L→R（朝路口）、环 `Side→Arc→Side`、接缝焊接、弧朝 Hub 内凹、EdgePath CCW、有限坐标、三角索引、法线 +Z。

**契约：失败 → 不写 DynamicMesh。**  
现状：`BuildFig1` 里失败仍 `still publishing`。要改成硬 abort 必须先 `[审核]`，并同时 Recook T（硬 abort 曾掏空丁字口）。

---

## 9. 数据所有权

- 节点 / 臂 / CutS：`UMetaLandRoadJunctionComponent` / `FMetaLandRoadJunctionNode`。
- 路可见段：只问 `GetJunctionMeshGenerationSpans`。
- 先刷新路 trim，再铺路口封面。
- Tool / Visualizer **不**三角化路口、不 `Recook`。

吸附交点只决定落点。Cook 仍按本文切臂、polar、条带、孔环。禁止为了吸附改孔策略。
