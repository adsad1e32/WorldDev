# MetaRoad（目标插件） vs MetaLandRoad（项目插件）

## 术语（必读）

| 名称 | 含义 | 路径 |
|------|------|------|
| **MetaRoad** | **目标插件 / 参考插件**（Epic Marketplace 商业插件，对齐 MetaRoad 数据模型与 mesh 思路） | UE 5.7 安装目录：<br>`Engine/Plugins/Marketplace/.../Source/MetaRoad/`<br>`Engine/Plugins/Marketplace/.../Source/MetaRoadEditor/` |
| **MetaLandRoad** | **本项目插件**（WorldDev 自研，含 MoKu 编辑工具链 + Lane Layout） | `Plugins/MetaLandRoad/Source/MetaLandRoad/` |

> 对话中说「目标插件」= **MetaRoad**，不是 MetaLandRoad。

---

## 一、模块 / 文件结构对比

### MetaRoad（目标插件）— 双模块

```
MetaRoad/                          ← 运行时 + 视口渲染（约 24 个源文件）
├── Public/
│   ├── RoadSplineComponent.h      ← Spline + Lane Layout + 采样
│   ├── MetaRoadTypes.h            ← FRoadLayout / FRoadLaneSection
│   ├── RoadSceneProxy.h           ← SceneProxy 声明
│   ├── MetaRoadSettings.h         ← 采样密度、Lane 材质
│   ├── MetaRoadActor.h            ← 空 Actor 壳
│   ├── MetaRoadSubsystem.h
│   └── RoadGraphComponent.h
└── Private/
    ├── RoadSplineComponent.cpp    ← BuildLinearApproximation, EvalROffset
    ├── LaneProxy.h / .cpp         ← ★ 每条 Lane 的 mesh 生成 + 渲染 Proxy
    ├── RoadSceneProxy.cpp         ← ★ 汇总 Lane Proxy，GetDynamicMeshElements
    └── MetaRoadTypes.cpp

MetaRoadEditor/                    ← 编辑器专用（约 113+ 个源文件）
├── ModelingTools/
│   ├── TriangulateRoadTool.h      ← ★ 烘焙整条路网 → StaticMesh / DynamicMesh Actor
│   ├── DrawRoadTool.h
│   └── IntersectionSolverTool.h
├── RoadMeshTools/
│   ├── RoadLanePolygone.h         ← Lane 多边形 / Delaunay
│   ├── RoadLanePolylineArrangement.h
│   ├── RoadSplineCache.h
│   ├── SplineMeshOpHelpers.h
│   └── IRoadOpCompute.h
└── Utils/
    ├── MeshUtils.h                ← Group / Section 合并
    └── OpUtils.h                  ← AppendMesh, EnableDefaultAttributes
```

### MetaLandRoad（项目插件）— 单模块

```
Plugins/MetaLandRoad/Source/MetaLandRoad/   ← 约 75 个源文件，Editor + Runtime 混在一起
├── Public/
│   ├── MetaLandRoadSplineComponent.h   ← Spline + Layout + ★ GenerateCombinedLaneDynamicMesh
│   ├── MetaLandRoadTypes.h             ← FMetaLandRoadLayout（对齐 MetaRoad 模型）
│   ├── MetaLandRoadProfile.h           ← 默认 Lane Profile
│   └── MetaLandRoadLaneVisualization.h
├── Private/
│   ├── MetaLandRoadSplineComponent.cpp ← BuildLinearApproximation + per-lane mesh（匿名 namespace）
│   ├── MetaLandRoadLaneVisualization.cpp
│   ├── MetaLandRoadLaneComponentVisualizer.cpp
│   ├── MetaLandRoadProfile.cpp
│   └── MetaLandRoadTypes.cpp
├── Classes/
│   ├── MoKuEditSplineActor.h/.cpp      ← ★ RefreshDynamicRoadMesh + UDynamicMeshComponent
│   ├── MoKuEditSplinesComponent.h      ← 继承 UMetaLandRoadSplineComponent
│   ├── MoKuEditIntersectionActor.cpp   ← 路口 mesh（耳切 / TriangulateCurvesOp）
│   └── MoKuEditBaseActor.h
├── Private/Tools/
│   ├── MetaLandRoadInteractiveTool.cpp ← 画路工具
│   └── ShapesGenerationTool.cpp
└── Private/MoKuEditorUtils.cpp         ← SplineMeshBuilder（TriangulateCurvesOp）
```

### 结构差异摘要

| 维度 | MetaRoad | MetaLandRoad |
|------|----------|--------------|
| 模块划分 | **MetaRoad + MetaRoadEditor** 分离 | **单模块** MetaLandRoad |
| Mesh 渲染载体 | `FLaneProxy` + `FRoadSplineSceneProxy`（Component 自带 SceneProxy） | `UDynamicMeshComponent` 挂在 `AMoKuEditSplineActor` |
| Mesh 烘焙工具 | `UTriangulateRoadTool` + `RoadMeshTools/*` | 无对等工具；画路时直接 `RefreshDynamicRoadMesh` |
| Lane 可视化 | `FLaneProxy::DrawLane`（SceneProxy PDI） | `LineBatchComponent` + `ComponentVisualizer` |
| 遗留代码 | 无 MoKu | 含 MoKu 画刷 / Landscape / 区域规划等 |

---

## 二、Mesh 生成对比

MetaRoad 有 **两条** mesh 管线；MetaLandRoad 目前主要是 **一条**（DynamicMesh Actor），加若干辅助路径。

### MetaRoad 管线 A：视口预览（Spline 上直接显示）

```
URoadSplineComponent
  └── FRoadSplineSceneProxy 构造
        └── FLaneProxy::MakeLaneProxysFromSpline()     LaneProxy.cpp ~302
              └── MakeLaneProxy()                       LaneProxy.cpp ~114
                    ├── BuildLinearApproximation()      RoadSplineComponent.cpp ~873
                    └── BuildRoadMesh()                 LaneProxy.cpp ~38（quad strip）
                          └── InitMesh() → FDynamicMeshVertex / IndexBuffer
                                └── GetDynamicMeshElements()  LaneProxy.cpp ~227
                                      └── 每条 Lane 独立 Material（LaneInstance）
```

**特点：**

- 每条 Lane **独立** mesh + **独立材质**
- 输出到 **渲染线程**（`FMeshBatch`），不是 `UDynamicMeshComponent`
- 闭环 Spline 可走 `MakeLoopProxyFromSpline` + `FConstrainedDelaunay2d` 多边形三角化
- 采样参数来自 `UMetaRoadSettings`（`NumPointPerSegmaent` 等）

### MetaRoad 管线 B：编辑器烘焙（生产用 mesh）

```
UTriangulateRoadTool                          TriangulateRoadTool.h
  └── RoadMeshTools/
        ├── RoadLanePolygone                  ← Lane 多边形、UV Layer
        ├── Arrangement2d                     ← 平面排列 / 重叠处理
        ├── RoadSplineCache
        └── GenericDataBackgroundCompute      ← 后台计算
  └── 输出：StaticMesh Actor 或 DynamicMesh Actor（ECreateRoadObjectType）
  └── MeshUtils::MergeGroupByArea 等          ← Section / Group 后处理
```

**特点：**

- 面向 **整条路网 / 多 Actor** 的正式 mesh 产出
- 支持 StaticMesh 烘焙（项目 MetaLandRoad 目前未对齐）
- 比视口 preview 复杂得多（路口、重叠、Group ID）

---

### MetaLandRoad 管线：DynamicMesh Actor（当前主路径）

```
编辑 / Spline 变更
  └── AMoKuEditSplineActor::RefreshDynamicRoadMesh()          MoKuEditSplineActor.cpp ~716
        └── GenerateRoadFromLaneLayoutAsDynamicMesh()         ~605
              ├── SideCurvePaths / JunctionConnections（Actor 层）
              └── UMetaLandRoadSplineComponent::
                    GenerateCombinedLaneDynamicMesh()           MetaLandRoadSplineComponent.cpp ~359
                          ├── BuildLinearApproximation()
                          ├── BuildLaneRoadMesh()（匿名 namespace）
                          └── 所有 Lane 合并进 **一个** FDynamicMesh3
        └── DynamicMeshComponent->SetMesh(...)
```

**Fallback（无 Lane Section）：**

```
GenerateRoadFromSplineAsDynamicMesh()   MoKuEditSplineActor.cpp ~470
  └── 固定宽度 ribbon（2000cm），老 MoKu 方法
```

### MetaLandRoad 其他 mesh 路径

| 函数 | 文件 | 用途 |
|------|------|------|
| `FillMeshActor()` | `MoKuEditIntersectionActor.cpp` ~84 | 路口：耳切法三角化 |
| `MeshBuilder()` | 同上 ~446 | 路口：`FTriangulateCurvesOp`（当前较少用） |
| `SplineMeshBuilder()` | `MoKuEditorUtils.cpp` ~1298 | 任意 EdgePath → DynamicMesh |

---

## 三、Mesh 生成差异表

| 项目 | MetaRoad | MetaLandRoad | 对齐状态 |
|------|----------|--------------|----------|
| Per-lane quad strip | `BuildRoadMesh` | `BuildLaneRoadMesh` | ✅ 已对齐思路 |
| 采样 API | `BuildLinearApproximation` | 同名成员函数 | ✅ |
| Lane 合并方式 | 每条 Lane 独立 Proxy | 合并为一个 `FDynamicMesh3` | ⚠️ 结构不同 |
| 每 Lane 材质 | `UMetaRoadSettings::GetLaneMatrtial` | 统一 DynamicMesh 材质 | ❌ 未做 |
| TriangleGroup / Section ID | Editor 烘焙管线 | 未使用 | ❌ |
| 视口 mesh 载体 | SceneProxy | UDynamicMeshComponent | ⚠️ 架构不同 |
| 正式烘焙工具 | `TriangulateRoadTool` | 无 | ❌ |
| 闭环 Delaunay | `MakeLoopProxyFromSpline` | 无 | ❌ |
| Section Side 延续 | `MakeLaneProxysFromSpline` | `GenerateCombinedLaneDynamicMesh` | ✅ 已移植逻辑 |
| UV 方向 | Lane Direction + RoadLayout.Direction | 同公式 | ✅ |
| 默认 Lane 数 | 取决于 Preset | 左右各 2 条 × 512cm | 项目自定义 |

---

## 四、关键函数对照表

| 职责 | MetaRoad | MetaLandRoad |
|------|----------|--------------|
| Lane 布局数据 | `FRoadLayout` / `URoadSplineComponent` | `FMetaLandRoadLayout` / `UMetaLandRoadSplineComponent` |
| 沿 Spline 采样 | `URoadSplineComponent::BuildLinearApproximation` | `UMetaLandRoadSplineComponent::BuildLinearApproximation` |
| 单 Lane 三角带 | `BuildRoadMesh()` static, LaneProxy.cpp:38 | `BuildLaneRoadMesh()` 匿名 namespace, MetaLandRoadSplineComponent.cpp:34 |
| 遍历 Section/Lane | `FLaneProxy::MakeLaneProxysFromSpline` | `GenerateCombinedLaneDynamicMesh` |
| 视口绘制 mesh | `FLaneProxy::GetDynamicMeshElements` | （无 Proxy）→ `UDynamicMeshComponent` |
| SceneProxy 入口 | `FRoadSplineSceneProxy::GetDynamicMeshElements` | 无 |
| 刷新 mesh 总入口 | （Spline 属性变更 → Proxy 重建） | `RefreshDynamicRoadMesh` |
| 烘焙/导出 mesh | `UTriangulateRoadTool` | 无 |
| 默认 Profile | MetaRoad Preset / Settings | `MetaLandRoadProfile::GetDefaultRoadProfile` |
| Section 切分 | `URoadSplineComponent::SplitSection(SplineKey, Side)` | `UMetaLandRoadSplineComponent::SplitSection` + 距离/控制点便捷 API |
| Section 边界更新 | `UpdateLaneSectionBounds` | 同名 + `bAutoMaintainMidpointSectionSplit` |

---

## 五、后续对齐建议（一点点来）

按优先级：

1. **Per-lane TriangleGroup + 材质** — 参考 MetaRoad `LaneInstance` + `MeshUtils`
2. **采样参数可配置** — 参考 `UMetaRoadSettings`，避免硬编码 `NumPointPerSegment = 4`
3. **路口** — 参考 MetaRoadEditor `IntersectionSolverTool` / `Arrangement2d`
4. **烘焙工具** — 长期可参考 `TriangulateRoadTool`；短期继续用 `RefreshDynamicRoadMesh`

---

## 七、SplitSection API（MetaRoad 对齐）

MetaLandRoad 已移植 MetaRoad 的纵向 Section 切分逻辑，入口在 `UMetaLandRoadSplineComponent`：

| API | 说明 |
|-----|------|
| `SplitSection(SplineKey, Side)` | 与 MetaRoad 相同：按 **Spline Input Key** 切分 |
| `SplitSectionAtDistanceAlongSpline(Distance, Side)` | 按沿 Spline 的 **距离 (cm)** 切分 |
| `SplitSectionAtSplinePoint(PointIndex, Side)` | 在 **控制点** 处切分（Input Key = PointIndex） |
| `FindRoadSectionOnSplineKey(SplineKey)` | 查找包含该 Key 的 Section 索引 |

`Side` 枚举 `EMetaLandRoadLaneSectionSide`：`Both` / `Left` / `Right`，与 MetaRoad 一致。

**行为要点：**

- 复制 Lane 宽度曲线与 Attribute，并对新 Section 做 `ShiftLane` / `Trim`（与 `RoadSplineComponent.cpp::SplitSection` 同序）
- 手动切分后设置 `bAutoMaintainMidpointSectionSplit = false`，避免默认 50% 中点切分被自动覆盖
- `UMoKuEditSplinesComponent::UpdateRoadLayout()` 会触发 `RefreshDynamicRoadMesh()`，mesh 与橙色切分线会同步刷新
- 编辑器内带 `FScopedTransaction`，支持 Undo

**Blueprint / C++ 示例：**

```cpp
// 在 Spline 中点切分（Both 侧）
SplineComp->SplitSectionAtDistanceAlongSpline(SplineComp->GetSplineLength() * 0.5f);

// 在第 2 个控制点处切分
SplineComp->SplitSectionAtSplinePoint(2);
```

**已对齐（MetaRoadEditor）：**

- `FMetaLandRoadSectionComponentVisualizer` — 视口 Section/Lane 选中、黄色横向切分线、右键 **Split Full / Split Side / Delete Section / Add Lane**
- 文件结构对齐 MetaRoad：`ComponentVisualizers/`、`Utils/DrawUtils`、`Utils/CompVisUtils`、`SelectionState` UObject + `FUICommandList`

**尚未对齐：**

- MetaRoad 多模式 Visualizer 切换（Spline / Section / Width / Attribute / Offset）

---

## 八、参考路径（本机）

```
MetaRoad 源码（UE 5.7 Marketplace）:
C:\Program Files\Epic Games\UE_5.7\Engine\Plugins\Marketplace\UnrealDrb8742c1e7b25V3\Source\MetaRoad\
C:\Program Files\Epic Games\UE_5.7\Engine\Plugins\Marketplace\UnrealDrb8742c1e7b25V3\Source\MetaRoadEditor\

MetaLandRoad 源码（本项目）:
D:\UE_Project\WorldDev\Plugins\MetaLandRoad\Source\MetaLandRoad\
```
