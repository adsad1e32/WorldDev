#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "LandscapeBlueprintBrushBase.h"
#include "RoadBrushActorInterface.h"


#include "MoKuEditBaseActor.generated.h"

DECLARE_DELEGATE_OneParam(FInitGizmoEvent, AMoKuEditBaseActor*)


USTRUCT(BlueprintType)
struct FSocketGizmoTransformInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FTransform Transform;
	UPROPERTY()
	FVector Tangent;
	UPROPERTY()
	int32 IndexOfPoint;

	FSocketGizmoTransformInfo()
		: Transform(FTransform::Identity),
		Tangent(FVector::ZeroVector),
		IndexOfPoint(-1)
	{
	}
};

UENUM(BlueprintType)
enum class EIntersectionState : uint8
{
	Right,
	Left,
	Both,
	None
};

USTRUCT()
struct FEditSplineIntersectInfo
{
	GENERATED_BODY()

	EIntersectionState State;

	TArray<FVector> LeftIntersectPositions;
	TArray<FVector> RightIntersectPositions;
	FVector IntersectPosition;
};


USTRUCT(BlueprintType)
struct FMaterialStateAndParam
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	bool ClosedShape = true;
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	float WidthSize = 512;
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	float  EdgeWidthOffset = 256;
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	float OffsetZ = 16.0;

};


struct FOnRoadBodyChangedParams
{
	FOnRoadBodyChangedParams(const FPropertyChangedEvent& InPropertyChangedEvent = FPropertyChangedEvent(/*InProperty = */nullptr))
		: PropertyChangedEvent(InPropertyChangedEvent)
	{
	}

	/** Provides some additional context about how the water body data has changed (property, type of change...) */
	FPropertyChangedEvent PropertyChangedEvent;

	/** Indicates that property related to the water body's visual shape has changed */
	bool bShapeOrPositionChanged = false;

	/** Indicates that a property affecting the terrain weightmaps has changed */
	bool bWeightmapSettingsChanged = false;

	/** Indicates user initiated Parameter change */
	bool bUserTriggered = false;
};


UCLASS(MinimalAPI, Blueprintable, config = Engine, Abstract, HideCategories = (Tags, Activation, Cooking, Replication, Input, Actor, AssetUserData))
class AMoKuEditBaseActor : public AActor, public IRoadBrushActorInterface
{
	GENERATED_BODY()

public:
	//~ Begin IWaterBrushActorInterface interface
	virtual bool AffectsLandscape() const override { return true; }
#if WITH_EDITOR
	virtual void PostActorCreated() override;
	virtual void OnPostEditChangeProperty(FOnRoadBodyChangedParams& InOutOnWaterBodyChangedParams);
	virtual TObjectPtr<UPrimitiveComponent> GetBrushRenderableComponents() const override { return DynamicMeshComponent; }
#endif 
	virtual const FSplineRoadHeightmapSettings& GetRoadHeightmapSettings() const override
	{
		return RoadHeightmapSettings;
	}

	virtual const TMap<FName, FRoadBodyWeightmapSettings>& GetLayerWeightmapSettings() const override
	{
		return RoadBodyWeightmapSettings;
	}


	UPROPERTY(EditAnywhere)
	TObjectPtr<UDynamicMeshComponent> DynamicMeshComponent = nullptr;

	virtual void OnConstruction(const FTransform& Transform)override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo()override;
	virtual void Tick(float DeltaTime) {};
	virtual TArray<FSocketGizmoTransformInfo> GetGizmoInfo() { return GizmoTransformInfos; }

	TArray<FSocketGizmoTransformInfo> GizmoTransformInfos;
	virtual void RefreshExtraMesh(){};
	virtual void RefreshDynamicRoadMesh(){};
	virtual void InitGizmoSetting(TArray<FSocketGizmoTransformInfo>& OutGizmoTransform) {};
	virtual void InitSceneManagement();
	virtual void UpdateSceneManagementData(){};
	virtual void Destroyed();
	virtual FEditSplineIntersectInfo CheckIntersectionState(AMoKuEditBaseActor* InEditActor) { return FEditSplineIntersectInfo(); };
	virtual void UpdateBrushMaterial();


	FInitGizmoEvent& OnInitGizmo() { return OnInitGizmoEvent; }
	bool IsInit = false;

	UPROPERTY()
	TWeakObjectPtr<class AWorldSceneManagement> SceneManagement;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DrawBrushMID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,Category = "Terrain")
	bool bAffectsLandscape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain",meta = (EditCondition = "bAffectsLandscape"))
	FSplineRoadHeightmapSettings RoadHeightmapSettings;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain",meta = (EditCondition = "bAffectsLandscape"))
	TMap<FName, FRoadBodyWeightmapSettings> RoadBodyWeightmapSettings;


	void OnRoadBodyChanged(const FOnRoadBodyChangedParams& InParams);

protected:

	FInitGizmoEvent OnInitGizmoEvent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BrushSetting")
	TObjectPtr<UMaterialInterface> BrushMaterialBase;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BrushSetting")
	FMaterialStateAndParam   StateParam;
	 
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BrushSetting")
	//UTextureRenderTarget2D* WeightmapRenderRT;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BrushSetting")
	UTextureRenderTarget2D* HeightmapRenderRT;



};