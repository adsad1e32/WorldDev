# Render Dependency Graph (RDG) — 中英对照 + 当前 Stamp 逐步引导

- **英文原文：** [Render Dependency Graph in Unreal Engine](https://dev.epicgames.com/documentation/unreal-engine/render-dependency-graph-in-unreal-engine)
- **官方全文中译（概念图 / 步骤）：** [UE-Render-Dependency-Graph-Official-CN.md](./UE-Render-Dependency-Graph-Official-CN.md)
- **父页一句话：** *An immediate-mode API which records render commands into a graph data structure to be compiled and executed.*  
  （立即模式 API：先把渲染命令记进一张图，再编译、再执行。）
- **姊妹文档：** [UE-Graphics-Programming-Overview-Bilingual.md](./UE-Graphics-Programming-Overview-Bilingual.md)（Renderer / RHI / 双线程）
- **对照代码：** `Plugins/MetaLandRoad/.../EditSplineRoadInfo.cpp` 的 `StampRoadCenterlineProfileCS`  
  Shader：`Plugins/MetaLandRoad/Shaders/Private/RoadLandscapeProfile.usf`
- **术语：** 类名、API、模块名保留英文；中文只解释。

> 官方页当前可能需登录才能抓全文。本文按 UE 5.7 官方 RDG 页的结构 + 引擎头文件（`RenderGraph.h` / `RenderGraphUtils.h` / `PixelShaderUtils.h`）整理。专有定义以引擎源码为准。

---

## 0. 新手先建立的三张图

读 RDG 之前，先分清三层。混在一起就会觉得「我们不是已经在画图了吗」。

```
你写的 C++（Game Thread）
    │  准备数据、不能碰 GPU
    ▼
ENQUEUE_RENDER_COMMAND          ← 把工作丢给 Rendering Thread
    │
    ▼
FRDGBuilder  记录 pass / 资源     ← 还没真正画，只是「声明我要干什么」
    │  Execute()
    ▼
Compile（引擎自动）             ← 算依赖、插 Barrier、扔掉没用的 pass
    │
    ▼
RHI draw / dispatch             ← 这时才真正跟 DirectX / Vulkan 说话
    │
    ▼
GPU 跑 HLSL
```

| 层 | 英文 | 我们这条线里是谁 |
|----|------|------------------|
| 游戏逻辑 | Game Thread | `RenderLayer_Native`、`BuildRoadProfileSamples` |
| 渲染线程入口 | Rendering Thread + `FRHICommandListImmediate` | `ENQUEUE_RENDER_COMMAND` 的 lambda |
| 图 / 调度 | **RDG** `FRDGBuilder` | `RoadLandscapeProfile` 这一小段 |
| 硬件抽象 | **RHI** | `AddFullscreenPass` 最终发出的 draw |
| 着色器 | HLSL / `.usf` | `RoadLandscapeProfilePS`（**Pixel Shader**，不是 CS） |

**RDG 不是 shader。** 它是 Rendering Thread 上「登记资源、登记 pass、自动插同步」的调度器。  
**用了 HLSL ≠ 用了 Compute Shader ≠ 用了 RDG。** 三者独立。我们：HLSL 有，RDG 有，CS 没有。

---

## 1. 官方 RDG 在解决什么

老写法：你自己 `SetRenderTarget` → `Draw` → 手动 `RHITransitionResources`（Barrier）→ 自己想这张 RT 何时能释放。

DX12 / Vulkan 要求资源在「被读 / 被写成 RT / 被写成 UAV」之间显式改状态。漏一条 Barrier 就是花屏或驱动报错。

RDG 让你改成：

1. **声明** 资源（贴图、Buffer）和 pass（这一步读谁、写谁）。
2. `Execute()` 时引擎 **Compile**：看依赖、插 Barrier、给短命资源做池化 / alias、丢掉没人用的 pass。
3. 再真正发 RHI 命令。

官方一句话仍是：**immediate-mode API**（你按顺序 `Create*` / `Add*Pass`，写法像立即画），但内部是 **graph**（先记下来再编译）。不要和「每调一次就立刻 GPU 执行」搞混：`AddFullscreenPass` 当时几乎不画，`Execute()` 才画。

### 1.1 RDG 自动做的事

| 英文 | 中文 |
|------|------|
| Resource lifetime / pooling | 短命 RT、Buffer 的分配与回收；能重叠的可以 alias 同一块显存 |
| Barrier / resource transition | Combined 从「RT」变成「shader 可读」这类状态切换 |
| Pass culling | 声明了但没人读的 pass 可以不跑 |
| Async compute scheduling | 独立 Compute 队列插空（**我们没用**） |
| Debug / `profilegpu` 事件 | `RDG_EVENT_NAME` 会出现在 GPU 分析器里 |

### 1.2 Transient vs External 资源

| 英文 | 含义 | 我们用了？ |
|------|------|------------|
| **Transient**（`CreateTexture` / `CreateBuffer`） | 图自己造、图执行完即可回收 | **Buffer 用了**（`RoadProfile.Samples`） |
| **External**（`RegisterExternalTexture` / `RegisterExternalBuffer`） | 图外已有的 RHI 资源，RDG 只管状态，不负责创建/销毁 | **两张 RT 用了**（Combined、Dest） |

高度图不能 `CreateTexture` 出来就扔：Landscape 要拿 Dest 继续合并。所以 Combined / Dest 必须是 External。  
Sample 列表本帧用完即可，所以用 Transient Buffer。

---

## 2. 官方核心 API（中英）

### 2.1 入口：`FRDGBuilder`

在 **Rendering Thread** 上、拿着 `FRHICommandListImmediate` 构造，记完 pass 后必须 `Execute()`。

```cpp
FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("RoadLandscapeProfile"));
// ... Create / Register / AddPass ...
GraphBuilder.Execute();
```

| API | 官方含义 | 我们 |
|-----|----------|------|
| 构造 `FRDGBuilder(RHICmdList, EventName)` | 开始一张图 | `RoadLandscapeProfile` |
| `Execute()` | Compile + 跑剩下的 pass | 每条路 stamp 末尾调一次 |

`RDG_EVENT_NAME("...")` = 给 `profilegpu` / 调试看的名字，不是文件路径。

### 2.2 创建 / 登记资源

| API | 含义 | 我们 |
|-----|------|------|
| `CreateTexture(FRDGTextureDesc, Name)` | 图内短命贴图 | **未用**（高度图要还给 Landscape） |
| `CreateBuffer(FRDGBufferDesc, Name)` | 图内短命 Buffer | `CreateStructuredDesc(..., SampleCount)` → `RoadProfile.Samples` |
| `QueueBufferUpload(Buffer, Data, Bytes)` | Execute 时把 CPU 数据拷进 Buffer | 上传 `FRoadProfileSampleGPU[]` |
| `RegisterExternalTexture(Graph, FRHITexture*, Name)` | 把已有 RT 登记进图 | Combined、Dest |
| `CreateSRV(FRDGBufferSRVDesc)` | Buffer 只读视图，给 shader | `PassParameters->Samples` |
| `CreateUAV(...)` | 可写视图，给 CS | **未用**（PS 写的是 RT，不是 UAV） |

### 2.3 Shader 参数结构（官方要求 RDG 用这个才能推依赖）

RDG 靠参数结构体知道「这个 pass 读谁、写谁」，才能插 Barrier、做 culling。

官方宏（`ShaderParameterMacros.h`）：

| 宏 | 含义 | 我们 `FRoadLandscapeProfilePS::FParameters` |
|----|------|--------------------------------------------|
| `BEGIN_SHADER_PARAMETER_STRUCT` / `END_...` | 声明一组参数 | 有 |
| `SHADER_USE_PARAMETER_STRUCT` | shader 类使用这组参数 | 有 |
| `SHADER_PARAMETER(Type, Name)` | 普通常量（uint、float、矩阵） | `NumSamples`、`StampSize`、`RenderAreaToWorld`… |
| `SHADER_PARAMETER_RDG_TEXTURE` | 只读贴图 | `T_Combined` |
| `SHADER_PARAMETER_RDG_BUFFER_SRV` | 只读 Buffer | `Samples` |
| `SHADER_PARAMETER_RDG_TEXTURE_UAV` | CS 写贴图 | 无 |
| `SHADER_PARAMETER_RDG_BUFFER_UAV` | CS 写 Buffer | 无 |
| `RENDER_TARGET_BINDING_SLOTS()` | 光栅 pass 的 RT 槽 | 有 → `RenderTargets[0] = Dest` |
| `AllocParameters<T>()` | 从图的分配器拿参数（生命周期跟图走） | 有 |

对应 `.usf` 里必须同名：

```
Texture2D T_Combined;                              ↔ SHADER_PARAMETER_RDG_TEXTURE
StructuredBuffer<FRoadProfileSample> Samples;      ↔ SHADER_PARAMETER_RDG_BUFFER_SRV
uint NumSamples; int2 StampSize; ...               ↔ SHADER_PARAMETER(...)
```

### 2.4 加 Pass

| API | 含义 | 我们 |
|-----|------|------|
| `GraphBuilder.AddPass(Name, Params, ERDGPassFlags, lambda)` | 最底层：自己在 lambda 里绑 PSO、Draw | **没直接写** |
| `ERDGPassFlags::Raster` | 光栅（VS/PS，画三角形） | 间接用（fullscreen） |
| `ERDGPassFlags::Compute` | Compute dispatch | 无 |
| `ERDGPassFlags::AsyncCompute` | 异步 Compute 队列 | 无 |
| `ERDGPassFlags::Copy` | 拷贝 | 无 |
| `FPixelShaderUtils::AddFullscreenPass` | 官方封装：画全屏三角 + 你的 PS | **在用** |
| `FComputeShaderUtils::AddPass` | 官方封装：dispatch CS | 无 |
| `AddCopyTexturePass` | 贴图拷贝 | 无 |
| `AddClearRenderTargetPass` | 清 RT | 高度清屏走的是 Game Thread `UKismetRenderingLibrary`，**不在这张 RDG 里** |

`FPixelShaderUtils::AddFullscreenPass` 内部就是 `AddPass(..., Raster, ...)`：引擎帮你设 viewport、绑 `RENDER_TARGET_BINDING_SLOTS`、`SetShaderParameters`、画一个全屏三角形。每个被盖到的像素跑一次 `RoadLandscapeProfilePS`。

### 2.5 写目标怎么声明

```cpp
PassParameters->RenderTargets[0] = FRenderTargetBinding(Dest, ERenderTargetLoadAction::ENoAction);
```

| `ERenderTargetLoadAction` | 含义 | 我们为什么用 `ENoAction` |
|---------------------------|------|--------------------------|
| `EClear` | 开 pass 先清成某色 | 不需要整张清 |
| `ELoad` | 保留 Dest 旧内容再画 | 我们会覆盖所有要关心的像素 |
| `ENoAction` | 不保证旧内容；**每个像素必须自己写** | shader 里 `Id` 越界 / 路外都 `return Combined`，等于手动 copy |

若改成只画脏盒、盒外不跑 shader，就必须改 `ELoad` 或先 `AddCopyTexturePass(Combined → Dest)`，否则盒外是未定义颜色。

### 2.6 官方三阶段（对照我们这一次 stamp）

```
Setup     你写的 Create/Register/AllocParameters/AddFullscreenPass
Compile   Execute() 开头，引擎做，你插不进去
Execute   全屏三角真正 Draw，HLSL 跑起来
```

`QueueBufferUpload` 的拷贝发生在 Execute 前/中，保证 PS 读 Samples 时 GPU 上已有数据。RDG 知道「upload pass 写 Buffer、PS pass 读同一 Buffer」，会排好顺序。

---

## 3. 当前版本完整流程（从拖路到像素）

高度 Stamp **不是** 视口那帧的 `FDeferredShadingSceneRenderer::Render`。它是 Landscape **Edit Layer** 回调。RDG 只包住最里面那一小段 GPU。

```
[Game Thread]
  拖 spline / 改 key
    → OnRoadBrushActorChanged
    → AWorldLandscapeBlueprintBrush::RequestLandscapeUpdate
    → Landscape 合并 Edit Layer「WorldEditor」
    → AWorldSceneManagement::RenderLayer_Native          ← 仍在 Game Thread
         拿到 CombinedResult、RenderAreaWorldTransform、RenderAreaSize
         for 每条路:
           RenderBrushActorContext
             StampRoadCenterlineProfileCS                 ← 仍在 Game Thread
               BuildRoadProfileSamples                    ← CPU 沿路每 100cm
               选 ReadRT / 分配 Write Dest（UTextureRenderTarget2D）
               GameThread_GetRenderTargetResource()
               ENQUEUE_RENDER_COMMAND  ─────────────────┐
         返回 Dest 给 Landscape 继续合并                 │
                                                        │
[Rendering Thread]                                      ▼
  lambda(FRHICommandListImmediate& RHICmdList)
    FRDGBuilder  Setup
    RegisterExternalTexture Combined, Dest
    CreateBuffer + QueueBufferUpload Samples
    AllocParameters + AddFullscreenPass
    GraphBuilder.Execute()  → Compile → Draw
         │
         ▼
[GPU]  RoadLandscapeProfilePS 每个像素:
         Load Combined → 扫 Samples → PackHeight 写 Dest
```

权重路径（Capture + JumpFlood + `DrawBrushMaterial`）走 `UKismetRenderingLibrary` / 材质，**几乎不进这张 RDG**。不要在 JF 代码里找 `FRDGBuilder`。

---

## 4. 函数对照表（官方概念 ↔ 我们的名字）

按调用顺序读，文件都在 `EditSplineRoadInfo.cpp`，除非另标。

| 步骤 | 我们的函数 / 符号 | 线程 | 对应官方 RDG / 图形概念 | 新手怎么理解 |
|------|-------------------|------|-------------------------|--------------|
| 1 | `RequestLandscapeUpdate`（`WorldLandscapeBlueprintBrush.cpp`） | Game | 无（Editor / Landscape） | 「这层脏了，请再 render 一次」 |
| 2 | `GetRenderItems` | Game | 无 | 可选：缩小 dirty box。焦点为空则整图 |
| 3 | `RenderLayer_Native` | Game | 无；相当于「我们这层的 Render()」 | Landscape 把 Combined 和这块地的变换塞进来 |
| 4 | `GetRoadActors` + `CollectHeightStampRoads` | Game | 无 | 现在空实现 = **每条路都 stamp** |
| 5 | `RenderBrushActorContext` | Game | 无 | 高度走 stamp；权重走 JF |
| 6 | `StampRoadCenterlineProfileCS` | Game | 名字像 CS，**里面 enqueue 的是 PS** | 准备 CPU 数据 + 丢 GPU |
| 7 | `BuildRoadProfileSamples` | Game | 不是 RDG | 产出 StructuredBuffer 的 CPU 源 |
| 8 | `FWaterUtils::GetOrCreateTransientRenderTarget2D` | Game | 不是 RDG Transient；是 **UObject RT 池** | Dest 必须活过 enqueue，才能 RegisterExternal |
| 9 | `GameThread_GetRenderTargetResource` | Game | External 资源的「通行证」 | 不能把 `UTextureRenderTarget2D*` 带进 Render Thread |
| 10 | `ENQUEUE_RENDER_COMMAND(RoadLandscapeProfilePS)` | 交接 | Threaded Rendering | lambda 只捕获 POD / TArray / Resource 指针 |
| 11 | `FRDGBuilder GraphBuilder(...)` | Render | **RDG 入口** | 一张图只管这一条路这一次 stamp |
| 12 | `RegisterExternalTexture(..., Combined/Dest)` | Render | External Texture | 告诉图：读这张、写那张 |
| 13 | `CreateBuffer` + `QueueBufferUpload` | Render | Transient Buffer | sample 上传 |
| 14 | `AllocParameters` + 填字段 | Render | Shader Parameter Struct = 依赖声明 | 漏填 RDG 纹理，Barrier 会错 |
| 15 | `CreateSRV` | Render | Buffer SRV | `.usf` 的 `StructuredBuffer` |
| 16 | `FRenderTargetBinding(Dest, ENoAction)` | Render | Raster 写目标 | 等价「这 pass 写 Dest」 |
| 17 | `FPixelShaderUtils::AddFullscreenPass` | Render | 官方 Raster helper（内部 `AddPass`） | 全屏三角 + PS |
| 18 | `GraphBuilder.Execute()` | Render | Compile + Run | 这里之后 GPU 才干活 |
| 19 | `CSHeightWriteRT = WriteSlot` | Game | 无 | 下一条路的 Combined = 这次 Dest（ping-pong） |
| 20 | Landscape 收 ReturnRT | Game | 无 | 合并进 heightmap |

`TShaderMapRef<FRoadLandscapeProfilePS>` / `IMPLEMENT_GLOBAL_SHADER(..., SF_Pixel)` 属于 **Global Shader**，不是 RDG API；RDG 只是把这个 shader 放进一个 Raster pass。

---

## 5. 对着源码读一遍（建议调试时跟）

### 5.1 Game Thread：还没进入 RDG

`StampRoadCenterlineProfileCS` 前半段全部是普通 C++：

1. `BuildRoadProfileSamples` — 世界坐标中心、半宽、Offset/Falloff、packed 高度。  
2. `ReadRT` — 第一条路 = 引擎 `CombinedResult`；之后 = 上一条的 Dest。  
3. `WriteSlot` — `CSCombinedWriteRT` / `B` 交替，禁止读写同一张。  
4. 算 `RenderAreaToWorld`、`RoadMin/Max`、`StampSize`。  
5. `ENQUEUE` — **这里返回后 Game Thread 认为 stamp「提交了」**，GPU 可能还没跑完。所以不要立刻在 Game Thread 读 Dest 像素。

### 5.2 Rendering Thread：RDG Setup（声明，不画）

对应官方 Setup：

```text
FRDGBuilder                         // 建图
RegisterExternalTexture Combined    // 已有高度
RegisterExternalTexture Dest        // 本 pass 要写出的高度
CreateBuffer Samples                // 短命
QueueBufferUpload                   // 声明：先有一个 upload pass
AllocParameters
  T_Combined = Combined             // 读
  Samples = CreateSRV(...)          // 读
  RenderTargets[0] = Dest           // 写
AddFullscreenPass                   // 声明 Raster pass
Execute                             // 才 Compile + 画
```

依赖图其实只有两步：

```
[Upload Samples] ──写──► SampleBuffer ──读──► [Fullscreen PS] ──读──► Combined
                                              [Fullscreen PS] ──写──► Dest
```

引擎因此知道：必须先 upload，再画；Combined 在 PS 前要变成 SRV；Dest 要变成 RT。你不用手写 `RHITransitionResources`。

### 5.3 GPU：和 RDG 无关的算法

`EvaluateRoadProfile(Id)`：越界 copy Combined → 世界 XY → 扫最近路段 → lerp 高度 → `PackHeight`。  
这是业务；RDG 不关心 box 还是胶囊。

---

## 6. 官方「典型一帧 RDG」vs 我们「一次 Stamp」

官方文档里的大图是 **整帧场景**：Depth → Base → Lighting… 共用一个（或少量）`FRDGBuilder`，几百个 pass，Transient 是 GBuffer。

我们是 **编辑器刷子**：每条路 `ENQUEUE` **一张小图、一个 Raster pass、立刻 Execute**。

| | 官方场景 Renderer | 我们当前高度 Stamp |
|--|-------------------|-------------------|
| 谁建 `FRDGBuilder` | `FDeferredShadingSceneRenderer::Render` | 我们自己的 enqueue lambda |
| 一张图里几个 pass | 很多 | **1 个** raster（+ 隐含 upload） |
| 图的寿命 | 一帧视口 | **一条路的一次 stamp** |
| 路 N 条 | — | **N 张独立的小 RDG**（N 次 Execute） |
| Transient 贴图 | GBuffer 等 | 无（RT 都是 External） |
| Transient Buffer | 各种 | sample 列表 |
| External | 后缓冲、历史帧 | Combined、Dest |
| `AddPass` 手写 | 常见 | 用 `AddFullscreenPass` 包掉 |
| Compute / Async | 大量 | 无 |
| `CreateTexture` | 常见 | 无 |

所以：我们「用了 RDG」，但是 **最小用法**——当「带自动 Barrier 的一次全屏 PS」。官方 RDG 的池化、多 pass 排序、AsyncCompute，几乎还没吃到。

多路合成一次 stamp（前面优化方案）在 RDG 上就是：**一张图、一个 Buffer（所有路的 sample）、一个 FullscreenPass、一次 Execute**。API 不用换，只改 Setup 时塞多少数据。

---

## 7. 和权重 / Landmass 路径对比（避免找错地方）

| | 高度 Stamp | 权重 / 旧 Water 刷 |
|--|-----------|-------------------|
| 调度 | **RDG** `FRDGBuilder` | 基本 **不是 RDG** |
| 画法 | Global **Pixel Shader** 全屏 | Scene Capture + JumpFlood + 材质 `DrawMaterialToRenderTarget` |
| 数据 | StructuredBuffer samples | Depth/Shape RT + DF |
| 官方 RDG API | Register / CreateBuffer / AddFullscreenPass | 无 |

在 `FloodComponent2D::JumpFlood` 里搜不到 `FRDGBuilder` 是正常的。

---

## 8. 调试时怎么「看见」RDG

| 手段 | 看什么 |
|------|--------|
| `profilegpu` / `Ctrl+Shift+;` | 事件名 `RoadLandscapeProfile`、`RoadLandscapeProfilePS` |
| `stat unit` | Game vs Render vs GPU：采样卡在 Game，Execute 卡在 GPU |
| `VisualizeTexture` / `vis` | Combined、Dest 内容 |
| `r.RDG.Debug` / `r.RDG.ImmediateMode` | 引擎 RDG 调试（ImmediateMode = 每加一个 pass 立刻执行，方便抓 API 错误，会变慢） |
| 日志 `[RoadLandscape] Profile` | Game Thread 的 StampSize / Combined 尺寸，**不是** RDG 内部 |

断点建议：

1. Game：`StampRoadCenterlineProfileCS` 里 `ENQUEUE` 前，看 `SampleCount`、`ReadRT != WriteSlot`。  
2. Render：lambda 里 `Execute()` 前，看 `DispatchSize` 是否 2048、`StampSize` 是否 2017。  
3. 不要在 Game Thread 断 `EvaluateRoadProfile`——那是 HLSL，VS 要走 shader 调试。

---

## 9. 新手读代码清单（按这个顺序打开）

1. 本页第 0、3 节 — 先有线程和图。  
2. `FRoadLandscapeProfilePS` 参数宏（约 `EditSplineRoadInfo.cpp` 135 行）— 这就是 RDG 的「依赖声明」。  
3. `StampRoadCenterlineProfileCS` 从 `BuildRoadProfileSamples` 读到 `ENQUEUE` — Game Thread。  
4. lambda 里 11 行 RDG — 对照第 5.2 节，一行一个官方 API。  
5. `RoadLandscapeProfile.usf` 的 `RoadLandscapeProfilePS` — GPU。  
6. `RenderLayer_Native` 的 for 循环 — 为什么会有 **N 张小 RDG**。  
7. 回头看 [Graphics Overview](./UE-Graphics-Programming-Overview-Bilingual.md) 第 7～8 节 — RHI / 双线程纪律。

读 lambda 时记住：**`AddFullscreenPass` 那一行不会立刻出高度图；`Execute()` 才会。**  
`CSHeightWriteRT = WriteSlot` 在 enqueue **之后**、Execute **之前** 就执行了——它只是把「下一张读谁」记在 Game Thread 上，不表示 GPU 已写完。Landscape 合并会等渲染队列，所以最终 heightmap 仍是对的。

---

## 10. 我们还没用、以后优化才会碰到的官方能力

| 官方 API / 概念 | 何时才需要 |
|-----------------|------------|
| 一张图里多个 `AddPass` | 先 upload、再 tile mark、再 stamp；或 height+weight 同一图 |
| `CreateTexture` Transient RT | 中间 DF / tile mask，不必还给 Landscape |
| `FComputeShaderUtils::AddPass` + UAV | 真 CS、按 `StampSize` dispatch |
| `ERDGPassFlags::AsyncCompute` | 游戏帧里和别的图形重叠；编辑器 stamp 收益很小 |
| `GraphBuilder.QueueTextureExtraction` | 图结束后把 Transient 提出给下一帧 |
| Pass culling | 多 pass 且有的输出没人读 |
| 手写 `AddPass` lambda | helper 不够用（自定义 viewport、间接 dispatch） |

当前版本把官方 RDG 用成：**Register 两张 External RT + Create 一个 Buffer + 一次 Fullscreen Raster + Execute**。把这 6 个 API 对上，这条高度路径的 RDG 部分就读完了。
