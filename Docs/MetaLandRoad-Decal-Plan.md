# MetaLandRoad 路面 / 路口 Decal 方案

对照 skill `metal-androad-incremental-dev`、[Agent-Conventions.md](./Agent-Conventions.md)、[UE-InteractiveTool-Standard.md](./UE-InteractiveTool-Standard.md)、[MetaLandRoad-Junction-Spec.md](./MetaLandRoad-Junction-Spec.md)、[MetaLandRoad-Guidance-vs-Mesh-Review.md](./MetaLandRoad-Guidance-vs-Mesh-Review.md)。

**本文是 Decal 的方案正文，尚未落地。** 实现前先读完。和本文冲突 → `[审核]` 停手。

本文不审 Fig1 孔环、不审 Landscape stamp、不审 sweep 拐点。Decal **不得**为了贴图去改 loft 拓扑或 Recook 路口。

---

## 0. 理念

路面是载体，decal 是贴在上面的图章。

| 原则 | 含义 |
|------|------|
| 数据在路上，不在世界 | 真值是路框坐标，不是关卡里手摆的 `UDecalComponent` |
| 鼠标改姿态，不改拓扑 | 点击 / 拖只改 `S`、横向、Yaw、尺寸、Profile |
| 显示跟现有 UV | 路身沿 S × 半宽；路口跟 Fig1 嘴框，不另开一套世界 XY |
| 自定义宽度自动生效 | 横向走 `EvalLaneROffset`，和 lane `Width` 同一套 |
| Tool 不 cook | 投影刷新 ≠ Recook Fig1 ≠ stamp |

**不要**用世界坐标存。路一拖、C 一续、路口一切臂，世界 XY 会漂。

---

## 1. 两种贴花（必须分开）

| 种类 | 像什么 | 挂哪 | 现有钩子 |
|------|--------|------|----------|
| **条带** | 标线、路缘虚线 | Lane，沿 S 一段 | `TypeMark`（Stepped + `Profile`） |
| **图章** | 箭头、人行横道、井盖、污渍 | 一个姿态 | `TypePolygon`（建议扩成完整 Decal） |

全程 2 个中线 key 控不了 U 弯，也控不了一路边的箭头。图章必须是**自己的点**，不能指望中线 key 顺便带着。

Mark 和 Polygon **同一套拾取**，Value 结构不同。禁止各写一套鼠标。

---

## 2. 路身：路框坐标

每个图章一条记录（attribute key 或等价条目），至少：

| 字段 | 含义 |
|------|------|
| `S` | 沿样条弧长 |
| `R` 或 `Alpha` | 横向。`Alpha` 0 = 该 lane 中线侧，1 = 该 lane 外沿。与 `EvalLaneROffset` 同口径 |
| `Yaw` | 相对路前进方向。0 = 顺着走 |
| `Size` | 长 × 宽（cm） |
| `Profile` | 哪张贴图 / 材质（箭头、横道、污渍…） |
| `Sort` | 叠层 |

世界位置只在显示时算：

```
Center(S) + Right(S) × Off(S, Lane, Alpha)
```

`Right` 与 sweep 路身同一套（相邻中线叉出的副切线）。路一弯，图章跟着转。

鼠标：射线打沥青 → 现有 lane pick / `GetRoadEdgePointsAtS` 收成 `(Road, Section, Lane, S, R)` → 拖这两个数。钳在该 lane 可见 span 内（`GetJunctionMeshGenerationSpans`）。Hub 内部不是路身 display 点，**不要**把图章写进 CutS 以内的路 S。

---

## 3. 路口：独立宿主

Hub **没有可靠的 S**。CutS 以内的点不是 display key，Fig1 封面不走 sweep。

路口图章挂在 **Junction Actor** 上，不用某条路的 S。

| 用法 | 坐标 |
|------|------|
| 贴在嘴上（停止线、横道一端） | `Port` + 沿嘴横向（同一套 OuterCS L→R） |
| 贴在 Hub 里（中心箭头、导流） | 相对 Anchor 的 XY + 朝向 |

斑马线跨 Hub 的，用 Junction 自己的一张。**禁止**写进某条路的 S，否则切臂 / 续画会丢或穿进走廊。

Fig1 孔环、同路外弦、T / Cross / N 策略 **一字不改**。Decal 是封面之上的投影，不是封面顶点。

---

## 4. 怎么画

### 4.1 第一步（本文默认）

每个图章一张贴地 quad（或小 mesh）：

- 位置 = 当时的路框 / 嘴框姿态  
- 法线贴路面  
- 材质：U 横向、V 沿路（与沥青 `AsphaltUvTileCm` 同一习惯）  
- 路身和 Fig1 **各画各的**  

刷新：路 mesh / Fig1 更新后只**重投影**图章，不重三角化路口。

### 4.2 第二步（可选）

从同一份 attribute **生成** `UDecalComponent`，只当显示后端。不手摆 Actor。图章一多、路口一斜时再评估。

### 4.3 禁止

- 把箭头画进沥青大 mesh 的顶点色 / 改 loft 环  
- 为贴花加密 sweep 或改 Fig1 孔  
- 每个箭头一个关卡 Decal Actor（和路脱节）  
- Hover 热路径刷 DynamicMesh / Recook  

---

## 5. 编辑器分层

对照 [UE-InteractiveTool-Standard.md](./UE-InteractiveTool-Standard.md)。

| 层 | 做什么 | 不做什么 |
|----|--------|----------|
| **Tool** | 单独 Decal 会话：点击落章、拖 S/R、滚轮 Yaw/缩放 | 不塞进画路 `OnClickPress`；不 cook |
| **Visualizer** | 选中线框、命中代理 | 不 Recook / 不 stamp |
| **PropertySet / Details** | Profile、尺寸、Sort | — |
| **Runtime** | 从 attribute 投影 quad（或生成 decal） | 不读视口修饰键 |

和 Landscape key 一样：点在路上，面板改数值。差别是 Landscape 沿 S **插值**，Decal 图章是**离散姿态**。

建议输入：

- 点击空路面 / 封面：落当前 Profile  
- 拖图章：改 S/R（路）或嘴横向 / Anchor XY（路口）  
- 滚轮或柄：Yaw、缩放  
- 钳制：路身可见 span；路口不出封面外沿过远（具体容差实现时再定）  

---

## 6. 代码放哪（实现时）

按故事拆，不按行数。对照 [Agent-Conventions.md](./Agent-Conventions.md) §2。

| 故事 | 模块 | 放哪 |
|------|------|------|
| 值类型 / Registry | Runtime | `MetaLandRoadLaneAttribute.h`：扩 `FMetaLandRoadPolygonAttributeValue` 或新 `TypeDecal` |
| 路口图章数组 | Runtime | Junction Actor / `UMetaLandRoadJunctionComponent`（新字段，不进 Fig1 孔算法） |
| 投影 / 刷新 | Runtime | 路：SplineComponent 旁路；路口：IntersectionActor 旁路。**不要**写进 `BuildLinearApproximation` |
| 鼠标会话 | Editor | 新 Tool 或现有 Tool 的独立 `*ToolDecal.cpp`；`OnClickPress` 只分发 |
| 线框拾取 | Editor | Visualizer，对标 Landscape attribute overlay |

新 `UCLASS` / `UInteractiveTool` 可以新文件。从旧文件抽故事须先 `[审核]`。

`TypeMark` 继续表示条带。图章不要复用 Mark 的 Stepped 段语义。

---

## 7. 落地顺序

| 步 | 内容 | 验收 |
|----|------|------|
| 1 | 路身值：`S + R/Alpha + Yaw + Size + Profile` | 能序列化，Details 能改 |
| 2 | 路身：点击放置、拖、投影 quad | 拖点贴地；改 lane 宽，横向跟着 Off |
| 3 | 路身：C / 挪点 / CutS 后图章仍在路上 | 不进 hub；不丢 |
| 4 | 路口：Junction 宿主 + 嘴框 / Anchor | 切臂、Recook 后还在；Fig1 孔不变 |
| 5 | （可选）atlas / 合并 / 生成 `UDecalComponent` | 另开一轮 |

一次只做一步。第 1–3 步不动 Fig1。

---

## 8. 和现有契约的关系

| 契约 | Decal 怎么守 |
|------|----------------|
| Landscape 只影响高度 | Decal 不写 height / weight key |
| 可见路段只问 `GetJunctionMeshGenerationSpans` | 路身图章钳在这些 span |
| 显示按 CutS | Hub 内不画路身图章 |
| Fig1 + 点号审核 | Decal 不改封面顶点、不改孔环 |
| sweep：`Center + Right × Off` | 图章用同一套 Center/Right；不替换 PathCenter |
| `EvalLanePosition` 生样条 | 半宽 / 嘴查询仍走它；图章显示可用 loft 后的姿态 |

---

## 9. 请你拍板（实现前）

1. 图章类型：扩 `TypePolygon`，还是新 `TypeDecal`？  
2. 横向：存 `Alpha`（随 lane 宽变），还是存绝对 `R`（cm）？  
3. 第一步显示：只做投影 quad，还是一开始就生成 `UDecalComponent`？  

没点头的项不要写代码。
