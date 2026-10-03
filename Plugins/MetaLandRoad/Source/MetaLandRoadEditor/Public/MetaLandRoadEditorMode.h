// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/UEdMode.h"
#include "Tools/LegacyEdModeWidgetHelpers.h"
#include "MetaLandRoadTypes.h"

#include "MetaLandRoadEditorMode.generated.h"

#define SPLINETOOLNAME "MetaLandRoad_SplineTool"
#define SHAPESTOOLNAME "MetaLandRoad_ShapesGenerationTool"


class AWorldSceneManagement;
struct FGizmoStoredInfoParams;
class UMoKuEditorGizmoBase;
class AMoKuEditSplineActor;

UENUM()
enum MoKuEditDisplayMode : int
{
	GizmoMode,
	EditMode,

};



UCLASS(Transient)
class UMetaLandRoadEditorMode : public UBaseLegacyWidgetEdMode
{
	GENERATED_BODY()

public:
	const static FEditorModeID EM_MetaLandRoadEditorModeId;
	static FString SimpleToolName;
	static FString InteractiveToolName;
	static FString ShapesGenerationName;

	TObjectPtr<AWorldSceneManagement> SceneManagement;

	virtual bool UsesTransformWidget() const override;
	virtual bool UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const override;
	virtual FVector GetWidgetLocation() const override;

	UMetaLandRoadEditorMode(FVTableHelper& Helper);
	UMetaLandRoadEditorMode();
	virtual ~UMetaLandRoadEditorMode();

	virtual bool ShouldDrawWidget() const override;
	/** UEdMode interface */
	virtual void Enter() override;
	virtual void Exit()  override;
	virtual void ActorSelectionChangeNotify() override;
	virtual void CreateToolkit() override;
	virtual TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> GetModeCommands() const override;
	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) override;
	virtual bool InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent)override;
	virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDelta, FRotator& InRot, FVector& InScale) override;
	virtual void ModeTick(float DeltaTime)override;
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual void BindCommands() override;
	

	void UpdateAssetGizmoImpl();
	void CreateActiveGizmos(AActor* InActor);
	MoKuEditDisplayMode GetDisplayMode() const {return DisplayMode;}
	void RefreshGizmoVisual(AActor* InActor);
	void CreateGizmoHandle(UStaticMeshComponent* InMesh,const FGizmoStoredInfoParams& InGizmoInfoParams);

	/** True when the MetaLandRoad spline draw tool is the active left tool in this mode. */
	bool IsInteractiveSplineToolActive() const;

	/** Clear junction session data (Enter/Exit). */
	void ResetInteractiveJunctionSession();

	/**
	 * Interactive draw finish: local junction cook on DrawnRoad (default).
	 * MaxHeightDeltaCm: 0 = ignore height; >0 = overpass gate (|ΔZ| must be ≤ this).
	 */
	void ProcessInteractiveFinishedRoadDraw(
		AMoKuEditSplineActor* DrawnRoad,
		bool bFullWorldRebuild = false,
		float MaxHeightDeltaCm = 0.0f,
		bool bSelectCookedJunction = true,
		AMoKuEditSplineActor* ForcedOtherRoad = nullptr,
		FVector ForcedIntersectWorld = FVector::ZeroVector);

	/** Recalculate arm cuts + trim zones; optionally refresh all linked road meshes. */
	void RecookJunctionActor(class AMoKuEditIntersectionActor* JunctionActor, bool bRefreshLinkedRoadMeshes = true);

	/**
	 * Full junction recook (Finalize/Topology/ConnectSide/Link/road mesh/cover).
	 * Does NOT require MetaLandRoad Editor Mode to be active — Details Debug buttons use this.
	 */
	static void RecookJunctionActorStatic(class AMoKuEditIntersectionActor* JunctionActor, bool bRefreshLinkedRoadMeshes = true);

	const TArray<FMetaLandRoadJunctionNode>& GetInteractiveJunctions() const { return InteractiveJunctions; }

	const FMetaLandRoadEditTarget& GetEditTarget() const { return EditTarget; }
	void SetEditTarget(const FMetaLandRoadEditTarget& Target);
	void ClearEditTarget();

	/** LMB lane pick in World Builder (Shift = multi/toggle, Shift+miss = clear). */
	bool TryHandleLaneAnchorPickClick(FEditorViewportClient* InViewportClient, const FViewportClick& Click);

protected:
	virtual void OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool) override;
	virtual void OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool) override;

private:
		
	TArray<UMoKuEditorGizmoBase*> ActiveGizmos;
	MoKuEditDisplayMode DisplayMode = MoKuEditDisplayMode::EditMode;
	
	void OnLevelActorAddedToWorld(AActor* Actor);

	TArray<FMetaLandRoadJunctionNode> InteractiveJunctions;
	double InteractiveJunctionForwardIndent = 600.0;
	/** Must cover a whole N-way footprint. 200cm split one 5-way into multiple actors (叠面). */
	double InteractiveJunctionClusterRadius = 1500.0;

	FMetaLandRoadEditTarget EditTarget;

};
