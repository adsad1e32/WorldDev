# WorldDev / MetaLandRoad 开发约定

本文档与 skill `metal-androad-incremental-dev`、rule `.cursor/rules/metal-androad-incremental-dev.mdc` 对齐。增量开发，但**按职责拆故事**，不按行数卡功能。

## 0. 术语

| 名称 | 含义 |
|------|------|
| **MetaRoad** | **目标插件 / 参考插件**（Epic Marketplace） |
| **MetaLandRoad** | **本项目插件**（`Plugins/MetaLandRoad/`） |

对比见 **[MetaRoad-vs-MetaLandRoad.md](./MetaRoad-vs-MetaLandRoad.md)**。

## 1. 审核再改（强制）

简化或重构前：**先标注，再说结果，一起定案**。不要默默大改。

```
[审核]
位置：path:line 或函数名
现象：
为何不合理：重复 / 一文件多故事 / 违反契约 / 非 UE 分层 / 死路径
建议结果：留下 | 合并到 X | 抽到拥有者 | 新文件 | 删除死路径
待讨论：是
```

一次只抛一小批标注。同意后再改同意的那些。

**死路径（热键、预览、`#if 0`、已注释的 spawn）：** 审核时标 `[审核]` 栏目「死路径」，**问用户是否移除**。用户没点头就不要删。新功能不要往已标记的死路径上加。清单先停住，不要把加功能的会话做成大扫除。

## 2. 代码放哪（按故事，不按行数）

**不要用「1500 行」这类尺子。** 功能可长可短；标准是 **一个文件一条故事**。

| 故事 | `.h` | `.cpp` |
|------|------|--------|
| Spline / Lane 采样、铺路网格 | `MetaLandRoadSplineComponent.h` | 同名 `.cpp` |
| 路口 cook / Fig1 / 点号审核 | `MoKuEditIntersectionActor.h` | 同名 `.cpp` |
| Landscape stamp / RDG | `EditSplineRoadInfo.h` | 同名 `.cpp`（再拆 stamp 文件须先讨论） |
| 画路交互 | `MetaLandRoadInteractiveTool.h` | 生命周期 `*Tool.cpp`；图一点击 `*ToolClick.cpp`；预览几何 `*ToolDraw.cpp`（不写 cook / Recook） |
| 视口叠加 | `ComponentVisualizers/*.h` | 对应 `.cpp`（不写 Recook） |
| 类型 / Layout | `MetaLandRoadTypes.h` | `MetaLandRoadTypes.cpp` |

- 小改动：加到拥有者类（声明 `.h`，实现 `.cpp`）。
- **可以不先问就新建文件：** 新的 `UCLASS` / `UInteractiveTool` / Global Shader + `.usf`。
- **从旧文件抽出独立故事：** 先 `[审核]`，用户同意再拆。
- 同一辅助逻辑出现**第三处**：合并到拥有者 `static` 或该 `.cpp` 的 file-local `static`。
- **禁止：** 只被一处调用的新 `*Utils` / `*Helper` / `*Generation` 文件。

## 3. C++ 风格

**声明在 `.h`，实现在 `.cpp`。** 不要在头文件里堆算法。

### 3.0 重构后废函数（强制 code review）

从 MetaRoad 搬过来或改完调用方之后，**零调用的包装不要留着**。

Lane / Section 活 API 是 **index + SOffset**：

- 留下：`GetStartSectionIndex` / `GetEndSectionIndex` / `GetLaneIndex` / `GetStartOffset` / `GetEndOffset` / `FindSideSection` / `FindSectionAtSOffset` / `GetLaneByIndex` / `CheckLaneIndex`
- 禁止留下：无人调用的 `GetStartSection` / `GetEndSection` / `GetLeftRightSections` 这类「走 `OwnedRoadLayout` 再返回对象引用」的 const/非 const 对
- **禁止** 用函数内 `static Fallback` 当失败返回值（假引用，误调会改到同一份数据）

Code review 步骤：

1. 先 `[审核]` 标 **死路径**（位置、零调用证据、建议删）
2. 用户点头后再删声明 + 实现
3. 对称 API（如 `PinAttribute` / `UnpinAttribute`）即使暂时无调用，先标再议，不要当废函数误删
4. 一次只清一类；不要把加功能会话做成全插件大扫除，除非用户明确要求清扫

`.cpp` 里允许：类 `private`/`static` 成员、file-local `static`、**匿名命名空间**（UE 常见）。

禁止：`namespace XxxPrivate` 当第二模块；新代码把 Editor API 塞进运行时 Public 头。

## 3.1 Runtime / Editor 打包

对照 MetaRoad 双模块。**完整表：** [MetaLandRoad-Runtime-Editor.md](./MetaLandRoad-Runtime-Editor.md)。

| 模块 | `.uplugin` Type | 进谁 |
|------|-----------------|------|
| `MetaLandRoad` | **Runtime** | 游戏 cook |
| `MetaLandRoadEditor` | **Editor** | 仅 Editor |

双模块已落地。新代码按上表放。Runtime 调 Editor 用 `FMetaLandRoadEditorHooks`。

Runtime Public 禁止 `#include` UnrealEd / LevelEditor / Interactive Tools。

## 4. Interactive Tool / RDG / 材质

- **其它 Editor Mode 也按同一套写：** [UE-InteractiveTool-Standard.md](./UE-InteractiveTool-Standard.md)（Mode / Tool / Behavior / Gizmo / Visualizer；`OnClickPress` 只分发）。
- Tool：输入 + PropertySet，写回 Actor/Component；不三角化路口、不 stamp 地形。
- RDG：Game Thread 采样 → `ENQUEUE_RENDER_COMMAND` + `FRDGBuilder`；Actor 只调度谁、何时 stamp。
- 材质：MID 创建和 `Set*` 集中一处，参数名有表。

## 5. 产品契约

- Landscape 参数只影响**高度**（Height Side Falloff / Height Side Offset）。
- Road 权图软边吃 Landscape Key `RoadWeightFalloffWidth`（0 = Actor 默认）。路口无 key 时用 Actor。Profile PS 只画 **Road** 层。不画 Dirty，不吃 JF TMap。
- Begin/End **数据保留**，显示按 **CutS**。
- 可见路段只问 `GetJunctionMeshGenerationSpans`。
- 路口 Fig1 + 点号审核失败则 abort。正文：[MetaLandRoad-Junction-Spec.md](./MetaLandRoad-Junction-Spec.md)。
- 路面 / 路口 Decal（未落地）：[MetaLandRoad-Decal-Plan.md](./MetaLandRoad-Decal-Plan.md)。数据在路框，不手摆世界 Decal Actor。
- 道路排布 PropSetting（未落地）：[MetaLandRoad-Scatter-Plan.md](./MetaLandRoad-Scatter-Plan.md)。图一 `TypePropSetting`；点地 / 沿边加轨，不是新 spline；SplineMesh+HISM 混用；不用 PCG Graph。

## 6. 增量

1. 最小路径：编译 + 视口可见  
2. 再补可视化 / UV / 衔接  
3. 小 diff  
4. 大拆文件或删死路径：先审核标注，再动手  

## 7. 相关资源

- Skill：`.cursor/skills/metal-androad-incremental-dev/SKILL.md`
- Rule：`.cursor/rules/metal-androad-incremental-dev.mdc`
- 对照：[MetaRoad-vs-MetaLandRoad.md](./MetaRoad-vs-MetaLandRoad.md)
- 广泛 UE Interactive 规范：[UE-InteractiveTool-Standard.md](./UE-InteractiveTool-Standard.md)
- 画路 Tool 审核：[InteractiveTool-CodeReview.md](./InteractiveTool-CodeReview.md)
- Runtime / Editor 打包：[MetaLandRoad-Runtime-Editor.md](./MetaLandRoad-Runtime-Editor.md)
- Height / Weight 拆 JF：[MetaLandRoad-Height-Weight-Plan.md](./MetaLandRoad-Height-Weight-Plan.md)
- 道路排布 PropSetting（未落地）：[MetaLandRoad-Scatter-Plan.md](./MetaLandRoad-Scatter-Plan.md)
