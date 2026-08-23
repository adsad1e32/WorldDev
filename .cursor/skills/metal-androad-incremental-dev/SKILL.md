---
name: metal-androad-incremental-dev
description: WorldDev MetaLandRoad incremental C++ conventions — implementations in .cpp, declarations in .h, no namespace wrappers, MetaRoad reference alignment, Fig1 junction topology + strict vertex-order audit. Use when adding or editing C++ under Plugins/MetaLandRoad, comparing with MetaRoad, junction/FillMesh/UV/布线, or when the user mentions 头文件、cpp、命名空间、skill规范, or file organization.
---

# MetaLandRoad Incremental Development

## Terminology

| Name | Meaning |
|------|---------|
| **MetaRoad** | **Target / reference plugin** (Epic Marketplace) — compare against this |
| **MetaLandRoad** | **This project's plugin** — code we edit |

Read before mesh or structure work:

- [Docs/MetaRoad-vs-MetaLandRoad.md](../../../Docs/MetaRoad-vs-MetaLandRoad.md) — mesh pipelines + file layout comparison
- [Docs/Agent-Conventions.md](../../../Docs/Agent-Conventions.md) — where to put new code

## Agent greeting (mandatory)

When this skill applies, **start every user-facing reply with「大哥，」** before the substantive answer or action summary. This confirms the skill was loaded and followed.

Example openings:

- 「大哥，我先查圆环和 lane 高亮的实现。」
- 「大哥，编译错误修好了，改动如下。」

Do not skip the greeting on follow-up turns in the same MetaLandRoad task unless the user explicitly asks to stop.

## Junction cook cover — Figure 1 strips (mandatory)

**Primary cook (Fig3 MetaRoad / user): edge-lane links → fillmesh UV.**

```
ClearJunctionCoverMesh
Refresh linked roads (trim gap)
Ports polar θ; T through = ConnectSide half-CS
  → Edge lane = 绿箭头两点 (CS outer + next inward)
  → ONE shallow outer arc + **parallel** inner rail at laneW (path-normal); mesh strip
  → Forbidden: two independent chord-arcs / hub-only offset (交叉穿插、一点扇面、相对图二严重变形)
  → Reject mid-width outside [0.25,2.5]×laneW → straight lane fallback
  → fillmesh on mouth CS[1]..CS[N-2] + inner rails
```

**UE native tools:** help triangulate a correct hole; boundary/lane links are our job (+ MetaRoad Arrangement2d long-term).

**Recook / Debug (mandatory after every cook algorithm change):**
- `DebugRecookJunction` / `DebugRecookLinkedJunctions` / `RecookFromLinkedRoads` / `RecookJunctionActor` must: `ClearJunctionCoverMesh` → re-cut/link → road trim refresh → `BuildFig1`
- Mode inactive: fallback still calls `RefreshJunctionCoverMesh` (not bare `BuildFig1`)
- Log BEGIN/END with `tris=` so Output Log proves debug path ran

**Forbidden:** vertex-alpha overlap; hub-fan primary; edge stubs that cross; restoring full L↔R after T half snap; mouth inset into road body.

| Layer | UV |
|-------|-----|
| Mouth / curb strip | World XY / asphalt at cut |
| Center fill | World XY / tile |

## Junction topology — Figure 1 (strip wiring — critically important)

**Lane-aligned strip wiring for junctions (edit / future cook layers). Do NOT use unsorted polar fan of random cut points as the boundary.**

### Pipeline (one-click cook / 4→5 / N-way)

```
Arms (polar order around Anchor)
  → LaneStrips (per arm, cut → toward intersect; edit may overlap)
  → CornerFillets (exactly N arcs for N arms, between consecutive outer edges)
  → SectorClip or CenterFill (cook: no lasting double-coverage)
  → ValidateJunctionMeshPointIndices (FAIL = abort mesh)
  → UV
```

| Layer | Role | UV |
|-------|------|-----|
| **Arm strip** | Short last-module depth (leave hub vacancy). **T only** (1 through+1 stub): through `ConnectSide` Left|Right half. **Cross** (2 through arms): full CS — never treat as T | `MakeAsphaltUVAtDistance` |
| **Curb** | Outer aesthetic arc + **constant-width** inward offset (`BuildConstantWidthCurbInner`, depth=mouth edge link). Cross/N shallow sag (no hub tangle). Skip through-hub **and** same-spline | World XY | |
| **Blank fill** | Full mouth OuterCS (real cut width) + curb/open for **T and Cross/N**. Open/same-spline = OUTER chord. Ear-clip → poly tri. World UV | |
| **Overlap** | Strips must not dive through hub; fill sits in gap only | No permanent double faces |

### One-click and 4→5 / 5→6

- **Whole-node recook**, not local patches: re-detect arms → `FinalizeJunctionArmCuts` → polar sort → rebuild strips + N fillets + center.
- **T→Cross / 4→5 / 5→6**: same full recook; `ClearJunctionCoverMesh` first; `ClearLinks` refreshes unlinked road meshes (purge old trim gaps). Never keep Cross as diamond-only cover.

### Forbidden junction covers

- Hub-fan as *primary* asphalt cover, or through-bridge (same-spline / polar long chords)
- Ear-clip of OuterCS / silhouette alone **without** lane strips (hub-gap ear-clip of InnerCS welds is OK with strips; blank-fill may fan-fallback with world UV)
- Polar-sorting **individual** cut points so Side edge point order is destroyed
- Chaining `CornerInfo` with fragile `Dist<=50` without explicit Side→Arc→Side index order
- Skipping vertex-order audit when publishing junction DynamicMesh

### Strict vertex-order audit (mandatory every junction mesh)

**一定要严格审核顶点顺序封面。** Call `ValidateJunctionMeshPointIndices` (or equivalent) on **every** junction mesh publish. If audit fails → **abort** (do not write DynamicMesh).

Audit must verify:

1. **Side cross-section order**: left-outer → lane edges → center → right-outer when **facing toward junction** (`TowardJunction = InnerMid−OuterMid`, then L→R vs `Cross(Toward,Up)`). Do **not** use raw spline-forward (through-arm reverses). Contiguous local indices `0..N-1`; Outer/Inner must share the same L→R.
2. **Ring order**: `Side₀ → Arc₀ → Side₁ → Arc₁ → …` (polar-sorted arms, CCW). Strip CS stays L→R; ring may use a **reversed copy** of Side for CCW walk — never reverse strip CS for ring alone.
3. **Seam weld**: `CornerInfo[i].Last` ↔ `CornerInfo[i+1].First` (and last→first) within tight tolerance; log point indices on break
4. **Corner arc direction (mandatory)**: relative to **intersection/anchor point**, use 2D cross  
   `Cross(Chord, Mid→Intersect)` to know which side of the chord the hub is on. **Silhouette corner fillets bulge INWARD (向内)** — same side as Intersect/Hub (`CrossInt * CrossArc > 0`, arc mid closer to hub than chord mid). Do **not** build outer 鼓包 (opposite side). Verify with `Dot(BulgeDir, Mid→Intersect) > 0`.
5. **Winding**: closed `EdgePath` signed area CCW (or documented CW with consistent ear clip)
6. **Finite coords**, no unintended consecutive duplicates
7. **Triangle indices** in range after triangulation; normals up (+Z) after flip pass

Log format should include `CornerInfo[i].Side|Corner local=j` and `EdgePath[k]` so point numbers are reviewable in Output Log (`point-index audit`).

### Data ownership

- Authoritative: `UMetaLandRoadJunctionComponent` / `FMetaLandRoadJunctionNode` + arm cuts
- Framework sync: cut cross-section → `CornerInfo` (Side) + road `JunctionConnections`
- Road trim: `JunctionTrimZones` / mesh spans — refresh roads **before** final junction cover when cuts change
- Bidirectional: road width/layout change → `RecookJunctionActor`; junction recook → refresh linked roads (guard recursion)

## C++ Style (mandatory — strictly enforce)

**函数声明写在 `.h`，函数实现写在 `.cpp`。不要把函数体写在头文件里。**

另外，实现函数不套命名空间。

### Declarations in `.h`, implementations in `.cpp`

- **`.h`**: class/struct declarations, member function **declarations only**, enums, UPROPERTY/UFUNCTION, small trivial one-liners only when unavoidable (e.g. `FORCEINLINE` getters).
- **`.cpp`**: **all function bodies** — member functions, free functions, static helpers tied to that module.
- **Do not** add new member function implementations inside the class body in `.h`.
- **Do not** add new `inline` / `static inline` helper function bodies in `.h` for MetaLandRoad code.
- `.cpp` is also used for Unreal/engine glue that must live in one translation unit:
  - `GENERATED_BODY()` / module `IMPLEMENT_*` / `IMPLEMENT_HIT_PROXY`
  - `.generated.h` includes
  - Explicit template instantiations if unavoidable

### No namespace wrappers on implementations

- **Do not** wrap function implementations in `namespace { ... }`, anonymous namespaces, or named namespaces.
- **Do not** add new `*Private` / `*Utils` namespace blocks for helpers.
- Prefer instead:
  - `private` / `protected` **member functions** on the owning class (declared in `.h`, defined in `.cpp`)
  - `static` member functions on the owning class (declared in `.h`, defined in `.cpp`)
  - File-local `static` free functions at `.cpp` scope (no namespace wrapper)

### Examples

```cpp
// ✅ Header — declaration only
class UMetaLandRoadSplineComponent : public USplineComponent
{
public:
	int32 EditorAddLeftSection(float SplineKey);

private:
	static bool IsValidSplitDistance(double S, double Length);
};
```

```cpp
// ✅ Source — implementation
int32 UMetaLandRoadSplineComponent::EditorAddLeftSection(float SplineKey)
{
	return SplitSection(SplineKey, EMetaLandRoadLaneSectionSide::Left);
}
```

```cpp
// ❌ Implementation in header / ❌ namespace XxxPrivate { ... }
```

### When editing existing code

- **New functions**: declare in `.h`, define in matching `.cpp` — **always**.
- **Legacy header-only bodies**: do not move unrelated code; when you touch a function, migrate its body into the matching `.cpp` if it is still in `.h`.
- **Never** revert to header-only style unless the user explicitly asks.

## MetaRoad mesh entry points (reference only)

**Viewport preview:** `LaneProxy.cpp` → `MakeLaneProxysFromSpline` → `MakeLaneProxy` → `BuildRoadMesh` → `GetDynamicMeshElements`

**Editor bake:** `MetaRoadEditor` → `TriangulateRoadTool` + `RoadMeshTools/*`

## MetaLandRoad mesh entry points (project)

**Primary:** `RefreshDynamicRoadMesh` → `GenerateRoadFromLaneLayoutAsDynamicMesh` → `UMetaLandRoadSplineComponent::GenerateCombinedLaneDynamicMesh`

**Fallback:** `GenerateRoadFromSplineAsDynamicMesh` (fixed-width ribbon)

**Junction (Fig1):** `RecookJunctionActor` / `RefreshJunctionCoverMesh` → arm strips + corner fillets + center fill → `ValidateJunctionMeshPointIndices` → DynamicMesh

## Default: no new source files

1. Find the owning class (Component, Actor, Tool, Types).
2. Add declaration to the existing `.h`, implementation to the existing `.cpp`.
3. Keep the diff small; ship one vertical slice at a time.

## Where to put code

| Concern | Declaration (`.h`) | Implementation (`.cpp`) |
|---------|-------------------|-------------------------|
| Lane layout, sampling, mesh generation | `MetaLandRoadSplineComponent.h` | `MetaLandRoadSplineComponent.cpp` |
| Lane debug draw | `MetaLandRoadLaneVisualization.h` | `MetaLandRoadLaneVisualization.cpp` |
| Edit-mode road actor | `MoKuEditSplineActor.h` | `MoKuEditSplineActor.cpp` |
| Junction actor / cover / vertex audit | `MoKuEditIntersectionActor.h` | `MoKuEditIntersectionActor.cpp` |
| Interactive tool | `Tools/MetaLandRoadInteractiveTool.h` | `Tools/MetaLandRoadInteractiveTool.cpp` |
| Component visualizer | `ComponentVisualizers/*.h` | `ComponentVisualizers/*.cpp` |
| Data types / Layout | `MetaLandRoadTypes.h` | `MetaLandRoadTypes.cpp` (if needed) |

## Before creating a new `.h`/`.cpp`

**Stop and ask the user** unless they already approved a new module.

## Anti-pattern (do not repeat)

- Putting function bodies in `.h`
- Splitting logic into new `*Generation.*` / `*Utils.*` files for one-off helpers
- Wrapping helpers in `namespace XxxPrivate { ... }`
- Figure-3 fan / single-ring ear-clip as primary junction cover
- Publishing junction mesh without point-index / vertex-order audit

`GenerateCombinedLaneDynamicMesh` belongs on `UMetaLandRoadSplineComponent` — declare in `.h`, implement in `.cpp`.
