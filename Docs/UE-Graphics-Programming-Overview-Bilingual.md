# Graphics Programming Overview — 中英对照

- **英文原文：** [Graphics Programming Overview for Unreal Engine](https://dev.epicgames.com/documentation/unreal-engine/graphics-programming-overview-for-unreal-engine)（UE 5.7）
- **父页（图一）：** [Graphics Programming](https://dev.epicgames.com/documentation/en-us/unreal-engine/graphics-programming-for-unreal-engine)
- **对照对象：** MetaLandRoad 道路高度 Stamp 流程（`StampRoadCenterlineProfileCS` → `FRoadLandscapeProfilePS`）
- **RDG 逐步引导：** [UE-Render-Dependency-Graph-Bilingual.md](./UE-Render-Dependency-Graph-Bilingual.md)
- **术语约定：** 专有名词、类名、API、模块名保留英文；中文只解释含义。

---

## 0. 图一（Graphics Programming 父页）在说什么

父页只有两根柱子，后面 Overview 都是展开这两根柱子：

| 英文 | 中文解释 |
|------|----------|
| **Renderer module** | 引擎里管理、绘制 **Scene** 的模块。Scene 保存每个 World 的可渲染信息，并定义全部 **drawing policies** 和 **shader**。 |
| **RHI (Rendering Hardware Interface)** | 对 DirectX / Vulkan / Metal 等图形 API 的薄封装。图形编程的另一关键模块。 |
| **Graphics Programming Overview** | 关键类、设置、变量的总览；子页再讲 Shader、RDG、Threaded Rendering 等专题。 |

父页还列出和我们相关的子专题：

| 子专题 | 和我们 Stamp 的关系 |
|--------|---------------------|
| Shader Development / Shaders in Plugins | 我们用 Plugin Global Shader（`.usf` + `IMPLEMENT_GLOBAL_SHADER`） |
| Render Dependency Graph (RDG) | 我们在 Render Thread 里用 `FRDGBuilder` 组 pass |
| Threaded Rendering | Game Thread 采样，`ENQUEUE_RENDER_COMMAND` 丢给 Render Thread |
| AsyncCompute | **当前没用**。函数名带 CS，实际是 Pixel Shader fullscreen pass，不是 Compute Shader |

---

## 1. Getting Started / 从哪里读代码

引擎渲染代码量大，很难一眼看全。建议从 **`FDeferredShadingSceneRenderer::Render`** 读起——这是 **Rendering Thread** 上画新一帧的入口。也可以跑 `profilegpu`，对着 draw event 在 Visual Studio 里 Find in Files，找到对应 C++。

- Shader 编写见 **Shader Development**
- 坐标系术语见 **Coordinate Spaces**

我们这条 Stamp **不走** `FDeferredShadingSceneRenderer::Render` 的视口帧路径，而是 Landscape Edit Layer 在 Game Thread 上回调 `RenderLayer_Native` → 自己 enqueue 一条 RDG pass。但线程模型、RHI、shader 注册规则完全一样。

### 1.1 Useful console commands / 常用控制台命令

| Console Command | Description / 说明 |
| --- | --- |
| `stat unit` | 显示整帧时间，以及 Game Thread、Rendering Thread、GPU。最长的是瓶颈。GPU 时间含空闲，只有它最长且明显独立时才算 GPU 瓶颈。 |
| `Ctrl+Shift+.` 或 `recompileshaders changed` | 重编译自上次保存 `.usf` 以来改过的 shader。加载时也会自动发生。 |
| `Ctrl+Shift+;` 或 `profilegpu` | 测量当前 View 的 GPU 耗时。结果在弹出 UI 或 engine log。 |
| `Vis` / `VisualizeTexture` | 可视化各类 Render Target，可存成 bmp。**查 Combined / Dest 高度图必用。** |
| `show x` | 开关指定 show flag。`show` 列出全部及当前状态。编辑器里优先用视口 UI。 |
| `pause` | 暂停游戏，但继续渲染。模拟相关的渲染会停。 |
| `slomo x` | 改游戏速度。分析时可不跳过模拟就放慢，例如 `slomo .01`。 |
| `debugcreateplayer 1` | 测 splitscreen。 |
| `r.CompositionGraphDebug` | dump 一帧 composition graph（后处理和光照）。 |
| `r.DumpShaderDebugInfo` | 设为 1 后，之后编译的 shader 会把 debug info 写到 `GameName/Saved/ShaderDebugInfo`。 |
| `r.RenderTargetPoolTest` | 用特殊颜色清空 rendertarget pool 取出的贴图，抓 color leaking。 |
| `r.SetRes` | 设当前游戏 View 分辨率。编辑器无效。 |
| `r.ViewportTest` | 测试不同 Viewport 矩形配置（仅游戏）。 |

### 1.2 Useful command lines / 常用启动参数

| Commandline | Description / 说明 |
| --- | --- |
| `-d3ddebug` | 开 D3D11 debug layer，抓 API 错误。 |
| `-sm4` | 强制 Feature Level SM4（D3D11 RHI）。 |
| `-opengl3` / `-opengl4` | 强制 OpenGL RHI 到指定 Feature Level。 |
| `-dx11` | Windows 上曾是默认。 |
| `-dx12` | 现为常见默认（文档原文写 experimental，以本机引擎为准）。 |
| `-featureleveles2` | 带编辑器运行时忽略，须用 UI。 |
| `-featureleveles31` | 带编辑器运行时忽略，须在 Editor Preferences 打开。 |
| `-ddc=noshared` | 不用网络（shared）Derived Data Cache。调试 shader cache 时有用。 |

---

## 2. Modules / 模块

Renderer 代码在独立 module 里，非 monolithic 构建会编成 dll。改渲染代码不必 relink 整个程序，迭代更快。

- **Renderer** 依赖 **Engine**（大量回调进 Engine）。
- Engine 要调 Renderer 时，走接口，通常是 **`IRendererModule`** 或 **`FSceneInterface`**。

我们的 Stamp 在 **Plugin**（`MetaLandRoad`）里，不改 Renderer.dll。通过 Engine 已公开的 **Global Shader + RDG + Transient RenderTarget** 接入 Landscape brush，避免直接依赖 Renderer 内部类（如 `FViewInfo`）。

---

## 3. Scene representation / 场景表示

Renderer 眼里的 Scene，由 **Primitive Component** 以及 `FScene` 里各类列表定义。图元用 **Octree** 加速空间查询。

### 3.1 Primary scene classes / 核心场景类

存在一条与 Game Thread 并行的 **Rendering Thread**。跨线程的类通常拆成两半，按**谁拥有这份 state** 划分。

| Class | Description / 说明 |
| --- | --- |
| `UWorld` | 一组可交互的 Actor / Component。关卡可 stream 进出；程序里可同时有多个 World。 |
| `ULevel` | 一起加载/卸载、存在单个 map 文件里的 Actor / Component 集合。 |
| `USceneComponent` | 任何要加入 `FScene` 的对象的基类（灯、网格、雾等）。 |
| `UPrimitiveComponent` | 可渲染或参与物理的基类，也是可见性剔除和渲染属性（投影等）的粒度。和所有 `UObject` 一样，**变量归 Game Thread，Rendering Thread 不能直接读。** |
| `ULightComponent` | 光源。由 Renderer 计算并叠加到 Scene。 |
| `FScene` | `UWorld` 的 Renderer 版本。对象只有 **register component** 进 `FScene` 后，Renderer 才看得见。`FScene` 的 state **归 Rendering Thread**，Game Thread 不能直接改。 |
| `FPrimitiveSceneProxy` | `UPrimitiveComponent` 的 Renderer 版本，把 Component state 镜像给 Rendering Thread。在 Engine module，按图元类型派生（skeletal / rigid / BSP）。实现 `GetViewRelevance`、`DrawDynamicElements` 等。 |
| `FPrimitiveSceneInfo` | 对应 Component / Proxy 的 Renderer 内部 state，在 Renderer module，Engine 看不见。 |
| `FSceneView` | Engine 侧「往 `FScene` 看一眼」。同一 Scene 可多次 `FSceneRenderer::Render`（多个编辑器视口），或一次 Render 多个 View（splitscreen）。**每帧新建一个 View。** |
| `FViewInfo` | View 的 Renderer 内部表示，在 Renderer module。 |
| `FSceneViewState` | 跨帧保存的 View 私有信息。游戏里每个 `ULocalPlayer` 一份。 |
| `FSceneRenderer` | **每帧新建**，封装帧间临时数据。 |

**按 module 划分（linker 报错时看这里）：**

| Engine Module | Renderer Module |
| --- | --- |
| `UWorld` | `FScene` |
| `UPrimitiveComponent` / `FPrimitiveSceneProxy` | `FPrimitiveSceneInfo` |
| `FSceneView` | `FViewInfo` |
| `ULocalPlayer` | `FSceneViewState` |
| `ULightComponent` / `FLightSceneProxy` | `FLightSceneInfo` |

**按线程所有权划分（写代码必须时刻清楚，否则 race）：**

| Game Thread | Rendering Thread |
| --- | --- |
| `UWorld` | `FScene` |
| `UPrimitiveComponent` | `FPrimitiveSceneProxy` / `FPrimitiveSceneInfo` |
| `FSceneView` / `FViewInfo` | （View 在两线程间交接，勿在 Game Thread 缓存进下一帧） |
| `ULocalPlayer` | `FSceneViewState` |
| `ULightComponent` | `FLightSceneProxy` / `FLightSceneInfo` |

### 3.2 Material classes / 材质类

| Class | Description / 说明 |
| --- | --- |
| `FMaterial` | 渲染用材质接口。提供 blend mode 等属性，内含 shader map，Renderer 用它取单条 shader。 |
| `FMaterialResource` | `UMaterial` 对 `FMaterial` 的实现。 |
| `FMaterialRenderProxy` | 材质在 Rendering Thread 上的表示。提供 `FMaterial` 以及当前 scalar / vector / texture 参数。 |
| `UMaterialInterface` | Game Thread 材质功能抽象。用来取 `FMaterialRenderProxy` 和作为源的 `UMaterial`。 |
| `UMaterial` | 材质资产。节点图，算 shading 属性、设 blend mode。 |
| `UMaterialInstance` | `UMaterial` 的实例。复用节点图，换参数。Parent 可以是 `UMaterial` 或另一 Instance，最终链到一张 `UMaterial`。 |
| `UMaterialInstanceConstant` | 仅编辑器可改的 Instance（含 static switch）。 |
| `UMaterialInstanceDynamic` | 运行时可改。不能提供 static switch，也不能当别的 Instance 的 parent。 |

道路 **mesh 材质**走这套；高度 Stamp **不走 Material**，用的是 **Global Shader** 直接绑 Texture / StructuredBuffer。

### 3.3 Primitive components and proxies / 图元与 Proxy

Primitive 是可见性与 relevance 的基本单位。遮挡、视锥剔除都按 primitive。设计系统时要考虑 Component 该多大。每个 Component 有 bounds，用于剔除、投影、光照影响。

Component **只有 register 之后** Scene / Renderer 才看得见。Game Thread 改了属性必须 `MarkRenderStateDirty()`，才能传到 Rendering Thread。

- `FPrimitiveSceneProxy`：Rendering Thread 版 Component，按类型派生，住在 Engine module，渲染 pass 里会被回调。
- `FPrimitiveSceneInfo`：Renderer module 私有的 primitive state。

### 3.4 Important `FPrimitiveSceneProxy` methods

| Function | Description / 说明 |
| --- | --- |
| `GetViewRelevance` | 帧初 `InitViews` 调用，返回填好的 `FPrimitiveViewRelevance`。 |
| `DrawDynamicElements` | 在 proxy 相关的 pass 里画。仅当声明了 dynamic relevance。 |
| `DrawStaticElements` | Game Thread attach 时提交 StaticMesh elements。仅当声明了 static relevance。 |

**我们的高度 Stamp 不实现 Proxy / Drawing Policy。** 路 mesh 才走这条视口绘制路径。

### 3.5 Scene rendering order / 场景绘制顺序

Renderer 按「往 Render Target 合成数据」的顺序处理 Scene。例如 Depth only pass 在 Base pass 之前，先填 **Hierarchical Z (HiZ)**，降低 Base pass 着色成本。这个顺序由 C++ 里调用 pass 函数的静态顺序决定。

### 3.6 Relevance

`FPrimitiveViewRelevance` 说明该 primitive 和哪些效果（因此哪些 pass）相关。一个 primitive 可有多种 element、多种 relevance，结构体相当于对各 element 做逻辑 OR。所以可以同时有 opaque + translucent，或 dynamic + static，并不互斥。

`bStaticRelevance` / `bDynamicRelevance` 决定走 static 还是 dynamic 渲染路径。

### 3.7 Drawing Policies

Drawing Policy 用 **pass 专用 shader** 画 mesh。用 `FVertexFactory` 抽象网格类型，用 `FMaterial` 抽象材质。最底层：拿一组 mesh material shader + vertex factory → 把 VB 绑到 **RHI** → 绑 shader → 设参数 → 发 RHI draw call。

| Function | Description / 说明 |
| --- | --- |
| Constructor | 从 vertex factory 和 material shader map 找合适 shader 并保存引用。 |
| `CreateBoundShaderState` | 为该 policy 创建 RHI bound shader state。 |
| `Matches` / `Compare` | 在 static draw list 里排序。`Matches` 必须覆盖 `DrawShared` 依赖的全部因子。 |
| `DrawShared` | 设置在 `Matches==true` 的 policy 之间不变的 RHI state（材质参数、VF 顶点缓冲）。能放这里就不要放 `SetMeshRenderState`。 |
| `SetMeshRenderState` | 设本 mesh 特有、或 `DrawShared` 没设的 state。调用次数远多于 `DrawShared`，性能更敏感。 |
| `DrawMesh` | 真正发 RHI draw call。 |

我们的 Stamp 等价物是 **`FPixelShaderUtils::AddFullscreenPass`**：不走 mesh Drawing Policy，而是画一个全屏三角形，每个 texel 跑一次 `RoadLandscapeProfilePS`。

---

## 4. Rendering paths / 两条渲染路径

引擎有：

- **Dynamic path**：控制更多，遍历更慢。
- **Static path**：把 Scene 遍历缓存到尽量靠近 RHI。

两者底层都用 Drawing Policy。每个 pass 如果需要，必须同时支持两条路径。

### 4.1 Dynamic rendering path

用 `TDynamicPrimitiveDrawer`，对每个 proxy 调 `DrawDynamicElements`。要用 dynamic path 的 primitive 记在 `FViewInfo::VisibleDynamicPrimitives`。每个 pass 遍历该数组。proxy 在 `DrawDynamicElements` 里组装 `FMeshElement`，用 `DrawRichMesh` 或 `TDynamicPrimitiveDrawer::DrawMesh` 提交 → 临时 Drawing Policy → `CreateBoundShaderState` → `DrawShared` → `SetMeshRenderState` → `DrawMesh`。

灵活（每种 Component 可在回调里写自己的逻辑），插入成本低，遍历成本高：无 state 排序、无缓存。

### 4.2 Static rendering path

通过 **static draw lists**。mesh attach 到 Scene 时插入列表，此时调 `DrawStaticElements` 收集 `FStaticMeshElement`，创建 Drawing Policy 并缓存 `CreateBoundShaderState` 结果，按 `Compare`/`Matches` 插入（`TStaticMeshDrawList::AddMesh`）。`InitViews` 里初始化可见性 bitarray，交给 `TStaticMeshDrawList::DrawVisible`。`DrawShared` 对同一 state bucket 只调一次，`SetMeshRenderState` / `DrawMesh` 对每个 element 调。

把工作挪到 attach 时，渲染时遍历快很多。Static Mesh 在 Rendering Thread 上大约 **3x**。只能缓存 **与 View 无关** 的 state。很少 reattach、经常被画的 primitive 适合进 static list。

`DrawShared` 每 bucket 只调一次，容易藏 bug，且依赖绘制顺序和 attach 顺序，难查。Lighting only / Unlit 等会强制全部走 dynamic；若强制 dynamic 后 bug 消失，多半是 `DrawShared` 或 `Matches` 写错。

---

## 5. High level Rendering order / 一帧高层顺序

从 `FDeferredShadingSceneRenderer::Render` 起（简化）：

| Operation | Description / 说明 |
| --- | --- |
| `GSceneRenderTargets.Allocate` | 必要时把全局 Scene RT 扩到当前 View 够用。 |
| `InitViews` | 多种剔除算可见性；准备本帧可见动态阴影；必要时阴影锥与世界求交。 |
| PrePass / Depth only | `RenderPrePass` / `FDepthDrawingPolicy`。只写 depth。模式：关 / 仅遮挡 / 完整 depth。通常为了填 HiZ，减轻 Base pass 贵像素着色。 |
| Base pass | `RenderBasePass` / `TBasePassDrawingPolicy`。不透明/Masked 材质，属性写入 GBuffer；lightmap 与天空光写入 scene color。 |
| Occlusion Queries | `BeginOcclusionTests`。延迟遮挡查询，供**下一帧** `InitViews`。画包围盒，有时合并以减 draw call。 |
| Lighting | 每盏灯画 shadowmap，用 deferred / tiled deferred 累加到 scene color；半透明光照体积也累加。 |
| Fog | 不透明表面延迟算雾和大气。 |
| Translucency | 累加到离屏 RT，逐顶点雾，再合成。Lit 半透明单 pass 算最终光。 |
| Post Processing | 用 GBuffer 做后处理；半透明合成进 Scene。 |

更细看代码或 `profilegpu` 日志。

**我们的 Stamp 不在这张表里。** 它是 Landscape Edit Layer 的离线/编辑器 pass：读 Combined 高度 RT，写 Dest RT，再交回引擎合并进 heightmap。和视口 Base pass 无关，但同样吃 GPU、同样走 RHI。

---

## 6. Render Hardware Interface (RHI)

RHI 是平台图形 API 之上尽量薄的一层。意图：功能用平台无关代码写，在支持所需 Feature Level 的平台上直接能跑。

功能集量化为 **`ERHIFeatureLevel`**。平台缺能力就降级。

| Feature Level | Description / 说明 |
| --- | --- |
| **SM5** | 大致对应 D3D11 Shader Model 5（因 OpenGL 4.3 限制最多 16 张贴图）。支持 tessellation、**compute shaders**、cubemap arrays。支持 deferred。 |
| **SM4** | D3D11 Shader Model 4。无 tessellation / compute / cubemap arrays。支持 deferred。Eye Adaptation 因用 CS 而不支持。 |
| **ES3_1** | 对应 OpenGL ES3.1 / Vulkan / Metal 能力。 |

我们用 `GetGlobalShaderMap(GMaxRHIFeatureLevel)` 取 shader。当前是 **Pixel Shader**，不要求 Compute。若以后真改成 CS dispatch，必须保证 Feature Level ≥ SM5，并改 `SF_Compute` + `FComputeShaderUtils::AddPass`。

### 6.1 Rendering state grouping / 状态分组

Render state 按影响管线的哪一段分组。例如 `RHISetDepthState` 一次设完所有 depth 相关状态。

### 6.2 Rendering state defaults / 隐式默认值

状态太多，不可能每次 draw 全设。引擎假定大部分已是默认（改完必须还原），只有一小撮必须显式设置：

- `RHISetRenderTargets`
- `RHISetBoundShaderState`
- `RHISetDepthState`
- `RHISetBlendState`
- `RHISetRasterizerState`
- `RHISetBoundShaderState` 所绑 shader 的依赖

其余假定为默认（由对应 `TStaticState` 定义，例如默认 stencil 是 `RHISetStencilState(TStaticStencilState<>::GetRHI())`）。

RDG 的 `FRenderTargetBinding` + `AddFullscreenPass` 会帮我们设 RT / shader / viewport。不要在 Game Thread 直接调这些 RHI setter。

---

## 7. 当前 Stamp /「CS」流程用到了哪些知识点

> 命名提醒：`StampRoadCenterlineProfileCS`、日志里的 CS，指的是 **Compute-style 数据处理**（读 Combined、写高度）。GPU 上实际是 **`FRoadLandscapeProfilePS`（`SF_Pixel`）+ `FPixelShaderUtils::AddFullscreenPass`**，**不是** `FComputeShaderUtils` dispatch。

### 7.1 流水线对照

```
Game Thread                         Render Thread / RHI
───────────                         ───────────────────
UWorld / Landscape Edit Layer
  RenderLayer_Native
    Collect roads, BuildRoadProfileSamples
    MakeVertexAlignedRenderAreaToWorld
    GetOrCreateTransientRenderTarget2D (Dest)
    ENQUEUE_RENDER_COMMAND  ─────────►  FRHICommandListImmediate
                                          FRDGBuilder
                                          RegisterExternalTexture (Combined, Dest)
                                          CreateStructuredBuffer + QueueBufferUpload
                                          TShaderMapRef<FRoadLandscapeProfilePS>
                                          AddFullscreenPass → RHI draw
                                          GraphBuilder.Execute()
    CSHeightWriteRT = Dest
引擎再把 Dest 合并回 heightmap
```

### 7.2 图一 + Overview 知识点 ↔ 我们的代码

| 文档知识点 | 我们用了？ | 落点 |
| --- | --- | --- |
| **Renderer / Scene** | 间接 | Landscape 是已 register 的 primitive；Stamp 改的是它的 **heightmap RT**，不是自己画一条 Scene pass。 |
| **RHI** | 是 | `FRHICommandListImmediate`、RT format、Feature Level、shader bind。 |
| **Game Thread vs Rendering Thread** | **核心** | 采样、key、AABB 在 Game Thread；GPU pass 必须 enqueue。`UObject` / Actor 指针不能在 Render Thread 解引用。 |
| **Global Shader（Plugins）** | 是 | `FRoadLandscapeProfilePS`，`IMPLEMENT_GLOBAL_SHADER(..., SF_Pixel)`，`.usf` 在 `Plugins/MetaLandRoad/Shaders`。`PostConfigInit` 注册，避免序列化前没 shader。 |
| **RDG** | 是 | `FRDGBuilder`、`RegisterExternalTexture`、`CreateBuffer` / SRV、`AllocParameters`。 |
| **Render Target / 离屏合成** | 是 | Combined（引擎 scratch，常 POT 对齐 2048）→ Dest（ping-pong write）。`StampSize`（2017）才是顶点栅格。 |
| **Drawing Policy / Mesh pass** | 否（高度）/ 是（路 mesh） | 高度用 fullscreen PS；路表面才走 material + VF。 |
| **FSceneView / FViewInfo** | 基本不用 | 没有视口投影。坐标是 `Id → RenderAreaToWorld → WorldXY`。 |
| **Material / FMaterialRenderProxy** | 高度不用 | 参数全是 `SHADER_PARAMETER_*`。 |
| **Static / Dynamic path** | 不用 | 无 mesh draw list。 |
| **Deferred 一帧顺序** | 不用 | 编辑器 Edit Layer，不是 GBuffer。 |
| **Compute Shader / AsyncCompute** | **尚未使用** | 若改真 CS：UAV 写 Dest、`groupthread` 覆盖 `StampSize`，勿覆盖 POT padding。 |
| **Packed height** | 引擎约定 | `UnpackHeight` / `PackHeight`（`LandscapeCommon.ush`）。格式错就是悬崖/平地。 |

### 7.3 和「图形渲染」相邻、但属于我们自己的点

这些 Overview 没写，但是 Stamp 对错的命门：

| 点 | 为什么重要 |
| --- | --- |
| **Vertex-aligned mapping** | `WorldXY = mul(float4(Id.x, Id.y, 0, 1), RenderAreaToWorld)`，不要 `Id+0.5`，否则半格错位锯齿。 |
| **StampSize vs Combined Extent** | 引擎 Combined 常 pad 到 2048；只处理 `Id < StampSize`，padding 原样 copy。 |
| **Game Thread 采样密度** | `GRoadProfileMaxSamples`、步长 100cm。GPU 只吃 StructuredBuffer，插值质量取决于 CPU 样本。 |
| **Artist Falloff / Offset** | 必须原样进 `Falloff`；不要用 16 quad / 15° CombinedH 覆盖。 |
| **写后合并** | 只把 `RenderAreaSize` 个 texel 拷回；POT 尾巴不是顶点。 |

---

## 8. 做这条图形流程必须盯的重点

按 Overview 的纪律，加上我们踩过的坑：

### 8.1 线程（文档强调最多的一条）

- Game Thread：**采样、读 Actor/Spline、算矩阵、分配 Transient RT 槽位**。
- Render Thread：**只碰 RHI 资源、RDG、已拷贝进 lambda 的 POD / TArray**。
- `ENQUEUE_RENDER_COMMAND` 的捕获：`MoveTemp(Samples)`，不要捕获 `this` / `AMoKuEditSplineActor*`。
- 改完 Component 可见性或 mesh：`MarkRenderStateDirty()`，否则视口和数据脱节。

### 8.2 RHI / RT 资源

- Combined 与 Dest **同 format、同 extent**，否则 RDG / 合并会静默坏数据。
- `ERenderTargetLoadAction::ENoAction`：每个像素必须自己写；`Id >= StampSize` 要 `return Combined`。
- 不要在同一 pass 里 UAV 读写同一张图；我们用 **读 Combined、写 Dest** 的 ping-pong。
- `VisualizeTexture` 看 Combined / Dest；`r.RenderTargetPoolTest` 查是否拿到池子里脏图。

### 8.3 Shader

- 改 `.usf`：`recompileshaders changed` 或重启编辑器。
- 改 C++ shader 声明（参数、entry、`SF_*`）：必须重编插件；Global Shader 要在 **PostConfigInit** 注册。
- `r.DumpShaderDebugInfo 1` 查编译失败。
- Feature Level：PS 路径对 SM5/SM4 都友好；真 CS 必须 SM5+。

### 8.4 坐标与精度（Coordinate Spaces + 我们的栅格）

- Landscape 高度是 **packed RG**，世界厘米要经 `LandscapeZScale` / `LANDSCAPE_INV_ZSCALE`。
- RenderArea 原点对齐 **vertex**，不是 texel center。
- Influence AABB = HalfWidth + SideOffset + SideFalloff（相加，不是 max）。
- 斜坡噪声波长要大于顶点间距（当前 ~50cm），否则 alias 成新锯齿。

### 8.5 性能

- `stat unit`：Stamp 卡的是 GPU 还是 Game Thread 采样。
- `profilegpu` 里找 `RoadLandscapeProfile` / `RoadLandscapeProfilePS`。
- Fullscreen 按 Combined Extent（2048²）派发时，padding 像素应早出；脏框缩小（dirty box）只在没 `bForceFullHeightStamp` 时做。
- StructuredBuffer 上传：`SampleCount * sizeof(FRoadProfileSampleGPU)`，上限 4096。

### 8.6 不要和视口帧搞混

- 高度 Stamp **不是** Base pass，改 Drawing Policy / HiZ / GBuffer 解决不了路堤锯齿。
- 路 mesh 对不齐 spline 是 **DynamicMesh / 组件变换**，走 Primitive / Proxy，不是这张 PS。
- Alt+W 选不中 Actor 是编辑器交互 / `bSelectable`，不是 RHI。

---

## 9. 一句话对照

图一说：图形编程 = **Renderer 管 Scene** + **RHI 对硬件说话**。

我们的 Stamp 是：**Game Thread 准备数据 → Rendering Thread / RDG 调 RHI → Global Pixel Shader 写离屏高度 RT**。用到了 Overview 里的 **双线程、RHI、Global Shader、Render Target、Feature Level**；没用到 **FScene 视口绘制、Drawing Policy、Static/Dynamic mesh path、Deferred 一帧顺序、Compute Shader**。

以后若把 PS 改成真 CS，才会用上 Overview 的 SM5 Compute 和文档树里的 AsyncCompute；线程与 RT 纪律不变。
