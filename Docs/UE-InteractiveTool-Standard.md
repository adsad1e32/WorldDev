# UE Interactive Tool / Editor Mode 设计规范

给 **所有** Editor Mode + Interactive Tool 用（不限 MetaLandRoad）。画路专项清单见 [InteractiveTool-CodeReview.md](./InteractiveTool-CodeReview.md)。

目标：其它 Mode 按同一套分层来写，避免再出现「一个 `OnClickPress` 里叠三条状态机」。

---

## 1. 职责分层（先定谁写什么）

| 层 | UE 类型 | 允许 | 禁止 |
|----|---------|------|------|
| **Mode** | `UEdMode` / `UBaseLegacyWidgetEdMode` | 进/出模式、Tool 切换、选中、Mode 级 Tick、把点击转给 Tool 或 Visualizer | 三角化、Cook、Stamp、在 Mode 里堆几何算法 |
| **Tool** | `UInteractiveTool` + Behavior | 输入状态机、PropertySet、写 Actor/Component、预览 | 在 Hover 刷 mesh；在热路径跑全关卡 Cook |
| **Behavior** | `UClickDragInputBehavior` / Hover / Wheel | 命中测试、把射线交给 Tool | 业务逻辑 |
| **Gizmo** | `UInteractiveGizmo` | 轴拖、约束、Widget | 创建 Actor、改拓扑 |
| **Visualizer** | `FComponentVisualizer` | 选中后的叠加绘制与拾取 | Recook / Stamp |
| **领域对象** | Actor / Component | Mesh、Cook、数据权威 | 读 Slate 修饰键、读 Viewport |

一句话：**Tool 翻译输入；Actor 改世界；Mode 只调度。**

---

## 2. 输入：`OnClickPress` 只分发

`IClickDragBehaviorTarget::OnClickPress`（以及 Mode 的 `HandleClick`）必须短：

```
OnClickPress(Ray)
  → 读修饰键（只在这一处解释 Shift / Alt / Ctrl）
  → 按当前会话枚举分发
  → return
```

每条会话一个函数（或一个 `.cpp`）：

| 会话例子 | 函数 |
|----------|------|
| 空点创建 | `HandleClickPressDrawPath` |
| 从已有对象延伸 | `HandleClickPressExtend` |
| 吸附 / mix 拾取 | `HandleClickPressSnapPick` |

禁止：

- 在 `OnClickPress` 里内联 spawn + Fit + Connect + Cook + 死代码 `if (0)`
- Hover 与 Click 对同一修饰键给出相反含义
- Mode 和 Tool 各写一套「这次点击是什么」

修饰键约定（可按产品改，但必须写在 Mode 的一页说明里）：

- **无修饰：** 交给 Gizmo / 默认选择，Tool 不要抢
- **主操作键（本项目 Ctrl）：** 进入/继续绘制会话
- **拾取键（本项目 Shift）：** 只改目标/高亮，不 spawn
- **附加键（本项目 Alt）：** 在已有拾取上叠加，不改主目标

---

## 3. 会话状态（一个 Mode 可以有多条，但要显式）

用 **具名 flag + 单一枚举**，不要 `bIsDraw` + `bIsEditing` + `bIsDrawHud` 三套重叠。

```
enum class EToolSession { Idle, DrawPath, Extend, Snap };
```

规则：

1. `Setup` 清会话；`Shutdown` / 退出 Mode 必须 `ResetSession`（无残留 Actor 预览、无残留 EditTarget）。
2. Undo（`PostEditUndo`）只恢复会话需要的锚点，不重跑 Cook。
3. 会话 A 的几何函数不要读会话 B 的成员，除非通过参数传入。

---

## 4. 写世界：事务与刷新

| 动作 | 要求 |
|------|------|
| Spawn Actor、改 Spline、改 UPROPERTY | `FScopedTransaction` |
| 预览 mesh（每点） | 允许 `RefreshComponent`；**禁止** 全图 Cook |
| 结束手势 / 明确确认 | 才允许重的 Cook / 重建 |
| Hover / Tick | 只改缓存与 PDI，不 `Refresh` 重网格 |

同一「刷新且不连锁重建」只保留 **一个** 拥有者函数（例如 `RefreshMeshSuppressRecook`）。第三份拷贝必须合并。

Gizmo 拖轴：只在 `IsTracking() && LMB && WidgetAxis != None` 时写变换；WASD / RMB 看视口不得改数据。

---

## 5. 性能

热路径（Hover、`OnClickPress` 每点、`OnClickDrag`）：

- 禁止：全关卡 `TActorIterator` 无距离剪枝、RDG/Stamp、多路口 Recook、加载材质
- 允许：一次射线、一次局部拾取、一次组件 Refresh
- 目标：**一次提交手势 = 一次网格刷新**

慢路径必须默认关，用 PropertySet 显式打开（例如 “Full World Recook”）。

`Render` / `DrawHUD` 只画 **已经算好的** 缓存，不在 Render 里重采样整条样条。

---

## 6. 文件与复用

- **按会话/故事拆 `.cpp`，不按行数。** `OnClickPress` 分发可以只有 20 行；某条会话可以 400 行。
- 声明在 `.h`，算法在 `.cpp`。Editor 专用放 `Private/`，不要进运行时 Public 头。
- 禁止为单次调用新建 `*Utils` / `*Helper` / `*Generation`。
- 第三份相同逻辑：收到拥有者类 `static` 或该故事的 `.cpp` file-local。
- Mode 之间复用：只抽 **无产品语义** 的东西（射线打地、修饰键读取、suppress 刷新）。产品会话不要做成万能 Utils。

建议骨架：

```
EditorMode.cpp          // 进出来、Tool 切换
FooTool.cpp             // Setup / Shutdown / PropertySet / OnClickPress 分发
FooToolClick.cpp        // 各 HandleClickPress*
FooToolDraw.cpp         // 预览几何、Refresh suppress
```

---

## 7. 审查清单（任意新 Mode）

- [ ] Mode 不实现 Cook / Stamp / 三角化
- [ ] `OnClickPress` 只分发，会话函数可单独读懂
- [ ] 修饰键只在一处定义
- [ ] Hover 不刷 mesh
- [ ] 热路径 Refresh 有唯一拥有者，且不连锁 Recook
- [ ] Shutdown 清会话
- [ ] 慢路径默认关
- [ ] 没有第三份拾取 / 刷新拷贝
- [ ] 废弃热键 / 死路径已标出并问过是否移除（没点头不删，也不往上加新功能）

新 Editor Mode 开工前先写一张「会话表」（Idle / Draw / Extend / …），再写 `OnClickPress` 分发，不要先堆 2000 行再拆。

---

## 8. MetaLandRoad 图一（参考实现）

画路 Tool 的 `OnClickPress` 按本规范拆好了，其它 Mode 抄这张表即可：

```
OnClickPress                         // 只分发，约 15 行
  !Ctrl  → HandleClickPressWithoutCtrl      // Shift mix / 嘴拾取，不 spawn
  Ctrl + pending mouth + !editing
         → HandleClickPressLaneAnchorStart  // 从嘴 spawn / resume + FromConnection
  else   → HandleClickPressDrawOrExtend     // 空地描点或继续延伸
```

文件：`MetaLandRoadInteractiveToolClick.cpp`。画路专项清单仍看 [InteractiveTool-CodeReview.md](./InteractiveTool-CodeReview.md)。

死路径（`if (0)` 八叉树 / 路口 spawn）不搬进新文件。
