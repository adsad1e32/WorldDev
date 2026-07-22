// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/UEdMode.h"
#include "Tools/LegacyEdModeWidgetHelpers.h"

#include "MetaLandRoadEditorMode.generated.h"

#define SPLINETOOLNAME "MetaLandRoad_SplineTool"
#define SHAPESTOOLNAME "MetaLandRoad_ShapesGenerationTool"


class AWorldSceneManagement;

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

	//virtual bool UsesTransformWidget() const override;
	//virtual bool UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const override;

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
	virtual void ModeTick(float DeltaTime)override;
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual void BindCommands() override;
	

	void UpdateAssetGizmoImpl();
	void CreateActiveGizmos(AActor* InActor);
	MoKuEditDisplayMode GetDisplayMode() const {return DisplayMode;}
	void RefreshGizmoVisual(AActor* InActor);
	void CreateGizmoHandle(UStaticMeshComponent* InMesh,const FGizmoStoredInfoParams& InGizmoInfoParams);
	
protected:
	virtual void OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool) override;

private:
		
	TArray<UMoKuEditorGizmoBase*> ActiveGizmos;
	MoKuEditDisplayMode DisplayMode = MoKuEditDisplayMode::EditMode;
	
	void OnLevelActorAddedToWorld(AActor* Actor);

};
