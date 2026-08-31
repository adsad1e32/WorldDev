# MetaLandRoad Runtime / Editor 打包格式

对照 MetaRoad 的 `MetaRoad` + `MetaRoadEditor`。游戏 cook **不能** 链编辑器模块。

双模块已落地：`MetaLandRoad`（Runtime）+ `MetaLandRoadEditor`（Editor）。以后新文件按本表放，不要再塞回单模块。

## 1. `.uplugin`

```
Modules:
  MetaLandRoad          Type: Runtime   LoadingPhase: PostConfigInit
  MetaLandRoadEditor    Type: Editor    LoadingPhase: Default
```

- **Runtime** 进 `WorldDev` / cooked 游戏（shader mapping 必须 PostConfigInit）。
- **Editor** 只进 `WorldDevEditor`。Cook 会丢掉 Mode / Tool / Visualizer。

## 2. 源码目录

```
Plugins/MetaLandRoad/Source/
  MetaLandRoad/                         Runtime：数据、网格、stamp
    Public/                             游戏可 #include
    Classes/                            UCLASS Actor / Component
    Private/
    MetaLandRoad.Build.cs
  MetaLandRoadEditor/                   Editor：交互、视口、属性面板
    Public/
    Private/
      Tools/                            Interactive Tool
      ComponentVisualizers/             视口 overlay
      Gizmos/                           Socket / Transform gizmo
      Slate/                            Toolkit / Details / 资源窗
      Utils/                            已有 CompVis（不要新造 *Utils）
    MetaLandRoadEditor.Build.cs
```

## 3. 故事放哪

| 故事 | 模块 | 路径 |
|------|------|------|
| Types / Layout 查询 | Runtime | `Public/MetaLandRoadTypes.h` |
| Spline 采样、铺路网格 | Runtime | `MetaLandRoadSplineComponent` |
| 路口 Fig1 / 点号审核 | Runtime | `MoKuEditIntersectionActor` |
| Stamp / RDG 调度 | Runtime | `EditSplineRoadInfo`；`.usf` 在 `Plugins/MetaLandRoad/Shaders` |
| 画路 Interactive Tool | Editor | `Private/Tools/*` |
| Editor Mode / 热键 | Editor | `MetaLandRoadEditorMode` + `FMetaLandRoadEditorModule` |
| Visualizer | Editor | `Private/ComponentVisualizers/*` |
| Gizmo | Editor | `Private/Gizmos/*` |
| Slate 属性条 | Editor | `Private/Slate/*` |

## 4. 头文件规则

- Runtime Public **禁止** `#include` `UnrealEd` / `Editor.h` / `LevelEditor` / Interactive Tools。
- 新 Editor API 放 `MetaLandRoadEditor/Public` 或 Editor `Private/`，不要塞进 Runtime Public。
- Runtime 需要「编辑器才有的回调」时：走 `FMetaLandRoadEditorHooks`（Editor 启动时绑定）。不要让 Runtime `.h` 依赖 Editor 类型。
- Runtime `.cpp` 里残留的 `GEditor` / `Editor.h` 必须 `#if WITH_EDITOR`。Runtime `Build.cs` 只在 `Target.bBuildEditor` 时链 UnrealEd。

## 5. Runtime → Editor 钩子

`Public/MetaLandRoadEditorHooks.h`（Runtime，无 Editor include）：

| 钩子 | Editor 绑定 |
|------|-------------|
| `RefreshLaneVisualization` | overlay 刷新 |
| `RecookJunction` | `UMetaLandRoadEditorMode::RecookJunctionActorStatic` |
| `TryGetSelectedLandscapeKey` | Visualizer 当前 Landscape key |
| `NotifySplineComponentSet` | Mode / Tool gizmo |

Runtime Actor / Profile / Spline 只调钩子。Editor 模块 `Startup` 绑定，`Shutdown` 清空。
