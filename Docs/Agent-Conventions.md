# WorldDev / MetaLandRoad 开发约定

本文档供 AI Agent 与项目成员共同遵守。我们采用**一点点来**的增量开发方式，避免一次改动过大、文件过散。

## 0. 术语

| 名称 | 含义 |
|------|------|
| **MetaRoad** | **目标插件 / 参考插件**（Epic Marketplace，对齐用） |
| **MetaLandRoad** | **本项目插件**（`Plugins/MetaLandRoad/`） |

Mesh 生成与文件结构对比见 **[MetaRoad-vs-MetaLandRoad.md](./MetaRoad-vs-MetaLandRoad.md)**。

## 1. 代码放置原则（默认）

**新功能优先写在已有类/组件对应的 `.h` + `.cpp` 里，不要另起新文件。**

### C++ 风格（强制 — 严格执行）

**函数声明写在 `.h`，函数实现写在 `.cpp`。不要把函数体写在头文件里。**

另外，实现函数不套命名空间。

| 规则 | 说明 |
|------|------|
| 声明位置 | `.h` — 类成员、函数声明、UPROPERTY/UFUNCTION |
| 实现位置 | **`.cpp` — 所有函数体**（新代码与修改过的代码均如此） |
| 头文件禁止 | 不要在 `.h` 里写成员函数实现、`inline` 辅助函数体 |
| 命名空间 | **禁止**用 anonymous / `*Private` namespace 包裹实现 |
| 私有辅助 | 类的 `private`/`static` 成员（`.h` 声明，`.cpp` 实现），或 `.cpp` 内 file-local `static` 函数 |

| 功能类型 | 声明 (`.h`) | 实现 (`.cpp`) |
|----------|-------------|---------------|
| Spline / Lane 采样、布局、网格生成 | `MetaLandRoadSplineComponent.h` | `MetaLandRoadSplineComponent.cpp` |
| 编辑工具交互 | `MetaLandRoadInteractiveTool.h` | `MetaLandRoadInteractiveTool.cpp` |
| Actor 级刷新、SideCurve、Junction trim | `MoKuEditSplineActor.h` | `MoKuEditSplineActor.cpp` |
| Junction 封面 / 点号审核（Fig1） | `MoKuEditIntersectionActor.h` | `MoKuEditIntersectionActor.cpp` |
| Lane 视口可视化 | `MetaLandRoadLaneVisualization.h` | `MetaLandRoadLaneVisualization.cpp` |
| Component Visualizer | `ComponentVisualizers/*.h` | `ComponentVisualizers/*.cpp` |
| 类型与 Layout 数据 | `MetaLandRoadTypes.h` | `MetaLandRoadTypes.cpp`（如需） |

### 禁止（默认）

- 为单个函数或小型工具**新建** `.h` / `.cpp` 文件
- 新建仅被一处调用的 `*Utils`、`*Helper`、`*Generation` 文件
- **把函数实现写在 `.h` 头文件里**（header-only）
- **用 namespace（含 anonymous namespace）包裹函数实现**
- Junction **用无序极角扇形乱拼切断点** 当封面边界（边界必须是 Cut→向内Arc 环）
- **不经点号/顶点顺序审核就发布** junction DynamicMesh

### Junction（封面 — 强制）

详见 skill `metal-androad-incremental-dev`。

- **Cook（MetaRoad Fig1）**：`BuildFig1JunctionCoverMesh` — LaneStrip（短进深、车道 UV）+ CornerFillet + CenterFill
- 角弧必须 **向内（朝 Hub）**，禁止外鼓包
- **一定要严格审核顶点顺序封面**；审核失败则 abort
- 4→5 / N 叉 = 整节点 recook + `ClearJunctionCoverMesh`，禁止局部打补丁

### 必须询问用户后才能做

- 确实需要新建源文件（例如：新 Tool、新 Editor 模块、新 Subsystem）
- 需要从现有 `.cpp` 拆出超过 ~300 行且职责完全独立的模块

## 2. 已执行的 refactor 示例

`GenerateCombinedLaneDynamicMesh` 曾短暂存在于独立文件：

- ~~`MetaLandRoadLaneMeshGeneration.h`~~
- ~~`MetaLandRoadLaneMeshGeneration.cpp`~~

**已删除**，实现应放在：

- `Public/MetaLandRoadSplineComponent.h` — **声明**
- `Private/MetaLandRoadSplineComponent.cpp` — **实现** + Unreal 宏等必须在 cpp 的内容

Actor 侧只保留调用：

```cpp
RoadSplineComponent->GenerateCombinedLaneDynamicMesh(*Mesh, RoadHeightOffset, bGenerateUVs);
```

## 3. 增量开发（一点点来）

1. 先让最小路径跑通（编译 + 视口可见）
2. 再补可视化、UV、Section 衔接等
3. 每步尽量**小 diff、少新文件**
4. 大重构前先和用户确认范围

## 4. 相关 Agent 资源

- 对比文档：[MetaRoad-vs-MetaLandRoad.md](./MetaRoad-vs-MetaLandRoad.md)（**目标插件 = MetaRoad**）
- Skill：`.cursor/skills/metal-androad-incremental-dev/SKILL.md`
- Rule（MetaLandRoad 插件内生效）：`.cursor/rules/metal-androad-incremental-dev.mdc`
