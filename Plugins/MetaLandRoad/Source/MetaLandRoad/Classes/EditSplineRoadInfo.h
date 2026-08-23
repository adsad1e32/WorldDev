#pragma once
#include "CoreMinimal.h"
#include "Components/SplineComponent.h"
#include "Math/MathFwd.h"
#include "MoKuEditSplinesComponent.h"
#include "LineTypes.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "WorldLandscapeBlueprintBrush.h"
#include "UObject/WeakInterfacePtr.h"


#include "EditSplineRoadInfo.generated.h"
class AMoKuEditSplineActor;
class AMoKuEditIntersectionActor;
class AMoKuEditBaseActor;
class UFloodComponent2D;

using namespace UE::Geometry;

struct FSplineSegmentOctree
{
	UMoKuEditSplinesComponent* SplineComp;
	int Index;
	FSplineSegmentOctree(UMoKuEditSplinesComponent* InElement,int Index)
		: SplineComp(InElement),Index(Index){}

	bool operator == (const FSplineSegmentOctree& Other) const
	{
		return SplineComp==Other.SplineComp&&Index== Other.Index;
	}

	FBox GetBound()const
	{
		return SplineComp->GetProceduralPoints()[Index].GetBound();
	}
};


struct FSegmentsOctreeSemantics
{
	enum { MaxElementsPerLeaf = 16 };
	enum { MinInclusiveElementsPerNode = 7 };
	enum { MaxNodeDepth = 12 };

	typedef TInlineAllocator<MaxElementsPerLeaf> ElementAllocator;


	FORCEINLINE static FBoxCenterAndExtent GetBoundingBox(const FSplineSegmentOctree& Element)
	{
		return FBoxCenterAndExtent(Element.GetBound());
	}

	FORCEINLINE static bool AreElementsEqual(const FSplineSegmentOctree& A, const FSplineSegmentOctree& B)
	{
		return A== B;
	}

	static void SetElementId(const FSplineSegmentOctree& Element, FOctreeElementId2 Id)
	{
		const TArray<FMoKuSplineInterpPoint> Points = Element.SplineComp->GetProceduralPoints();
		Element.SplineComp->OctreeIds.SetNum(Points.Num());
		Element.SplineComp->OctreeIds[Element.Index] = Id;
	}
};

using FSplineRoadActorOctree = TOctree2<FSplineSegmentOctree, FSegmentsOctreeSemantics>;

USTRUCT()
struct FLandscapeInfoData
{
	GENERATED_BODY()

	UPROPERTY()
	FTransform LandscapeTransform;

	UPROPERTY()
	FIntPoint LandscapeQuads;

	UPROPERTY()
	FIntPoint RenderTargetResolution;
};


UCLASS(config = Engine, Blueprintable, BlueprintType)
class AWorldSceneManagement : public AWorldLandscapeBlueprintBrush
{

public:
	GENERATED_BODY()

	AWorldSceneManagement(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void Serialize(FArchive& Ar) override;
	virtual void OnConstruction(const FTransform& Transform)override;

	void OctreeAddRoad(AMoKuEditSplineActor* InElement);
	void OctreeRemoveRoad(AMoKuEditSplineActor* InElement);
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;


	FSplineRoadActorOctree Octree;

	virtual UTextureRenderTarget2D* RenderLayer_Native(const FLandscapeBrushParameters& InParameters) override;

#if WITH_EDITOR
	virtual TArray<UE::Landscape::EditLayers::FEditLayerRenderItem> GetRenderItems(const UE::Landscape::EditLayers::FMergeContext* InMergeContext) const override;
#endif

	AActor* ResolveHeightStampFocusActor() const;
	void CollectHeightStampRoads(TArray<AMoKuEditBaseActor*>& InOutRoads) const;
	void BuildLayerRoadList(bool bHeightmapRender, FName WeightmapLayerName, TArray<AMoKuEditBaseActor*>& OutRoads);
	FBox ComputeHeightStampWorldBounds(const TArray<AMoKuEditBaseActor*>& Roads) const;

	virtual void Initialize_Native(const FTransform& InLandscapeTransform,
		const FIntPoint& InLandscapeSize,
		const FIntPoint& InLandscapeRenderTargetSize) override;

	virtual void BlueprintRoadBodyChanged_Native(AActor* Actor) override;

#if WITH_EDITOR
	/** Allocate RTs + log sizes + open CombinedShapeAndHeight preview when Details shows None. */
	UFUNCTION(CallInEditor, Category = "Render Targets", meta = (DisplayName = "Debug Allocate And Show RTs"))
	void DebugForceAllocateRenderTargets();

	/** Open the height blender RT: solid core = Edge Offset, soft rim = Falloff Width. */
	UFUNCTION(CallInEditor, Category = "Render Targets", meta = (DisplayName = "Debug Open Height Falloff RT"))
	void DebugOpenHeightFalloffRT();

	/** Open the live profile height RT (no PNG). Drag a road first. */
	UFUNCTION(CallInEditor, Category = "Render Targets", meta = (DisplayName = "Debug Open Profile RT"))
	void DebugDumpProfileRT();
#endif

	UFUNCTION(BlueprintCallable, BlueprintPure, meta = (Category = "Debug"))
	virtual void GetRoadCacheKey(AActor* RoadBrush, URoadBodyBrushCacheContainer*& ContainerObject, FRoadBodyBrushCache& Value);

	UPROPERTY()
	TArray<AMoKuEditSplineActor*> EditRoadsList;

	UPROPERTY()
	FLandscapeInfoData LandscapeInfo;

	UPROPERTY()
	TObjectPtr<class USceneCaptureComponent2D> SceneCapture = nullptr;

	UPROPERTY()
	TObjectPtr<UFloodComponent2D> JumpFloodComponent2D = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta = (Category = "Render Targets"))
	TObjectPtr<UTextureRenderTarget2D> HeightmapRTA = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta = (Category = "Render Targets"))
	TObjectPtr<UTextureRenderTarget2D> HeightmapRTB = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta = (Category = "Render Targets"))
	TObjectPtr<UTextureRenderTarget2D> JumpFloodRTA = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta = (Category = "Render Targets"))
	TObjectPtr<UTextureRenderTarget2D> JumpFloodRTB = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta = (Category = "Render Targets"))
	TObjectPtr<UTextureRenderTarget2D> DepthAndShapeRT = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta = (Category = "Render Targets"))
	TObjectPtr<UTextureRenderTarget2D> RoadDepthRT = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta = (Category = "Render Targets"))
	TObjectPtr<UTextureRenderTarget2D> CombinedShapeAndHeightRTA = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta = (Category = "Render Targets"))
	TObjectPtr<UTextureRenderTarget2D> CombinedShapeAndHeightRTB = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta = (Category = "Render Targets"))
	TObjectPtr<UTextureRenderTarget2D> LandscapeRTRef = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta = (Category = "Render Targets"))
	TObjectPtr<UTextureRenderTarget2D> WeightmapRTA = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta = (Category = "Render Targets"))
	TObjectPtr<UTextureRenderTarget2D> WeightmapRTB = nullptr;

	/** Same size as CombinedResult — profile shader writes here so the engine merge gets a new RT, not the input. */
	UPROPERTY(VisibleAnywhere, Transient, meta = (Category = "Render Targets"))
	TObjectPtr<UTextureRenderTarget2D> CSCombinedWriteRT = nullptr;

	/** Ping-pong pair for multi-road profile stamps (avoid read/write of the same RT). */
	UPROPERTY(VisibleAnywhere, Transient, meta = (Category = "Render Targets"))
	TObjectPtr<UTextureRenderTarget2D> CSCombinedWriteRTB = nullptr;

	/** Flat elevation curve RT (256x1) so CacheDistanceField/MeshBrush CurveRT pins are never unbound. */
	UPROPERTY(VisibleAnywhere, Transient, meta = (Category = "Render Targets"))
	TObjectPtr<UTextureRenderTarget2D> DefaultCurveRT = nullptr;

	// Brush materials
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, meta = (Category = "Brush Materials"))
	TObjectPtr<UMaterialInterface> BrushAngleFalloffMaterial = nullptr;

	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, meta = (Category = "Brush Materials"))
	TObjectPtr<UMaterialInterface> BrushWidthFalloffMaterial = nullptr;

	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, meta = (Category = "Brush Materials"))
	TObjectPtr<UMaterialInterface> DistanceFieldCacheMaterial = nullptr;

	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, meta = (Category = "Brush Materials"))
	TObjectPtr<UMaterialInterface> RenderRoadSplineDepthMaterial = nullptr;

	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, meta = (Category = "Brush Materials"))
	TObjectPtr<UMaterialInterface> DebugDistanceFieldMaterial = nullptr;

	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, meta = (Category = "Brush Materials"))
	TObjectPtr<UMaterialInterface> WeightmapMaterial = nullptr;

	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, meta = (Category = "Brush Materials"))
	TObjectPtr<UMaterialInterface> DrawCanvasMaterial = nullptr;

	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, meta = (Category = "Brush Materials"))
	TObjectPtr<UMaterialInterface> CompositeRoadBodyTextureMaterial = nullptr;

	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, meta = (Category = "Brush Materials"))
	TObjectPtr<UMaterialInterface> IslandFalloffMaterial = nullptr;

	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, meta = (Category = "Brush Materials"))
	TObjectPtr<UMaterialInterface> JumpStepMaterial = nullptr;

	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, meta = (Category = "Brush Materials"))
	TObjectPtr<UMaterialInterface> FindEdgesMaterial = nullptr;

	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadWrite, meta = (Category = "Brush Materials"))
	TObjectPtr<UMaterialInterface> BlurEdgesMaterial = nullptr;
	// Brush materials end

		// MIDs
	UPROPERTY(EditInstanceOnly, AdvancedDisplay, BlueprintReadWrite, Transient, meta=(Category="Debug MIDs"))
	TObjectPtr<UMaterialInstanceDynamic> BrushAngleFalloffMID = nullptr;

	UPROPERTY(EditInstanceOnly, AdvancedDisplay, BlueprintReadWrite, Transient, meta=(Category="Debug MIDs"))
	TObjectPtr<UMaterialInstanceDynamic> BrushWidthFalloffMID = nullptr;

	UPROPERTY(EditInstanceOnly, AdvancedDisplay, BlueprintReadWrite, Transient, meta=(Category="Debug MIDs"))
	TObjectPtr<UMaterialInstanceDynamic> DistanceFieldCacheMID = nullptr;

	UPROPERTY(EditInstanceOnly, AdvancedDisplay, BlueprintReadWrite, Transient, meta = (Category = "Debug MIDs"))
	TObjectPtr<UMaterialInstanceDynamic> RoadSplineMID;

	UPROPERTY(EditInstanceOnly, AdvancedDisplay, BlueprintReadWrite, Transient, meta = (Category = "Debug MIDs"))
	TObjectPtr<UMaterialInstanceDynamic> DebugDistanceFieldMID = nullptr;

	UPROPERTY(EditInstanceOnly, AdvancedDisplay, BlueprintReadWrite, Transient, meta = (Category = "Debug MIDs"))
	TObjectPtr<UMaterialInstanceDynamic> WeightmapMID = nullptr;

	UPROPERTY(EditInstanceOnly, AdvancedDisplay, BlueprintReadWrite, Transient, meta = (Category = "Debug MIDs"))
	TObjectPtr<UMaterialInstanceDynamic> DrawCanvasMID = nullptr;

	UPROPERTY(EditInstanceOnly, AdvancedDisplay, BlueprintReadWrite, Transient, meta = (Category = "Debug MIDs"))
	TObjectPtr<UMaterialInstanceDynamic> CompositeRoadBodyTextureMID = nullptr;

	//UPROPERTY(EditInstanceOnly, AdvancedDisplay, BlueprintReadWrite, Transient, meta = (Category = "Debug MIDs"))
	//TObjectPtr<UMaterialInstanceDynamic> IslandFalloffMID = nullptr;
	// MIDs End






	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, meta = (Category = "Debug"))
	FIntPoint LandscapeRTRes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, meta = (Category = "Debug"))
	FIntPoint LandscapeQuads;


	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, meta = (Category = "Debug"))
	FVector WorldSize;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, meta = (Category = "Debug"))
	FTransform LandscapeTransform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Category="Debug"))
	bool ShowGradient = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Category="Debug"))
	float DistanceDivisor = 0.1f;
			
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Category="Debug"))
	bool ShowDistance = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Category="Debug"))
	bool ShowGrid = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Category="Settings"))
	float RoadClearHeight = -16384.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Category="Settings"))
	float SplineMeshExtension = 0.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Category="Debug"))
	bool UseDynamicPreviewRT = false;
		
	/** Off = allow Curl/Displacement/WeightmapInfluence noise. On (default) = clean edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(Category="Debug"))
	bool DisableBrushTextureEffects = true;

	/**
	 * Height and weight both stamp via centerline profile CS.
	 * JumpFlood is not used on either path.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Category = "Settings"))
	bool bUseCenterlineProfileCS = true;

	/**
	 * Kept for existing maps. Height always conforms to the visible road mesh Z;
	 * CombinedResult is only used to fade the shoulders.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Category = "Settings"))
	bool bRebuildHeightFromSpline = true;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Category = "Settings"))
	float CanvasSegmentSize = 1024.0f;

	bool DeprecateWorldLandscapeInfo(FVector& OutRTWorldLocation, FVector& OutRTWorldSizeVector);
	void SetupDefaultMaterials();

	/** Force DF cache rebuild on next RenderLayer (weight / landscape scale changes). */
	void InvalidateBrushDistanceFieldCache() { bKillCache = true; }

private:

	struct FEditorBrushRenderContext
	{
		bool bHeightmapRender = false;
		FName WeightmapLayerName;
		int32 RTIndex = 0;
		int32 RoadRTIndex = 0;
		FTransform RenderAreaWorldTransform;
		FIntPoint RenderAreaSize = FIntPoint::ZeroValue;
		UTextureRenderTarget2D* CSHeightWriteRT = nullptr;
		UTextureRenderTarget2D* CSWeightWriteRT = nullptr;
	};

	struct FBrushActorRenderContext
	{
		FBrushActorRenderContext(const TWeakInterfacePtr<IRoadBrushActorInterface>& InRoadBrushActor)
			: RoadBrushActor(InRoadBrushActor)
		{
		}

		template <typename T>
		T* TryGetActorAs() const { return Cast<T>(RoadBrushActor.GetObject()); }

		template <typename T>
		T* GetActorAs() const { return CastChecked<T>(RoadBrushActor.GetObject()); }

		AActor* GetActor() const { return GetActorAs<AActor>(); }

		TWeakInterfacePtr<IRoadBrushActorInterface> RoadBrushActor;
		URoadBodyBrushCacheContainer* CacheContainer = nullptr;
		UMaterialInstanceDynamic* MID = nullptr;
	};
	void InitSceneCaptureAndParam();
	void ModifyCaptureToTerrain(TObjectPtr<ALandscape> InOwningLandscape);
	void ComputeWorldLandscapeInfo(FVector& OutRTWorldLocation, FVector& OutRTWorldSizeVector);
	//void OctreeRemoveElement(UMoKuEditSplinesComponent* InElement)const;
	virtual void SetBrushMIDParams(const FEditorBrushRenderContext& BrushRenderContext, FBrushActorRenderContext& BrushActorRenderContext);
	virtual void DrawBrushMaterial(const FEditorBrushRenderContext& BrushRenderContext, const FBrushActorRenderContext& BrushActorRenderContext);
	virtual void DistanceFieldCaching(const FBrushActorRenderContext& BrushActorRenderContext);
	virtual void UpdateBrushCacheKeys();
	virtual bool BrushRenderSetup();
	virtual bool CreateMIDs();
	virtual void RenderBrushActorContext(FEditorBrushRenderContext& BrushRenderContext, FBrushActorRenderContext& BrushActorRenderContext);
	void StampOneRoadBrushPass(FEditorBrushRenderContext& BrushRenderContext, FBrushActorRenderContext& BrushActorRenderContext, bool bForceRecapture);
	bool StampRoadCenterlineProfileCS(
		FEditorBrushRenderContext& BrushRenderContext,
		AMoKuEditSplineActor* Road,
		uint32 WeightMode = 0,
		const FRoadBodyWeightmapSettings* WMSettings = nullptr);
	bool StampJunctionProfileCS(
		FEditorBrushRenderContext& BrushRenderContext,
		AMoKuEditIntersectionActor* Junction,
		uint32 WeightMode = 0,
		const FRoadBodyWeightmapSettings* WMSettings = nullptr);
	virtual bool SetupRoadSplineRenderMIDs(const FBrushActorRenderContext& BrushActorRenderContext, bool bRestoreMIDs, TArray<UMaterialInterface*>& InOutMIDs);
	virtual void CacheBrushDistanceField(const FBrushActorRenderContext& BrushActorRenderContext);
	virtual void CaptureRoadDepth(const FBrushActorRenderContext& BrushActorRenderContext);
	virtual void DrawCanvasShape(const FBrushActorRenderContext& BrushActorRenderContext);
	virtual void FalloffAndBlendMode(const FBrushActorRenderContext& BrushActorRenderContext);
	virtual void CurvesSmoothingAndTerracing(const FBrushActorRenderContext& BrushActorRenderContext);
	virtual void DisplacementSettings(const FBrushActorRenderContext& BrushActorRenderContext);
	virtual void GetRenderDependencies(TSet<UObject*>& OutDependencies) override;
	virtual void ApplyWeightmapSettings(const FEditorBrushRenderContext& BrushRenderContext, const FBrushActorRenderContext& BrushActorRenderContext, const FRoadBodyWeightmapSettings& WMSettings);
private:
	bool bKillCache = false;
	FDelegateHandle OnLevelAddedToWorldHandle;

	void SetMPCParams();
	bool AllocateRTs();
	void ApplyToCompositeRoadBodyTexture(FEditorBrushRenderContext& BrushRenderContext, const FBrushActorRenderContext& BrushActorRenderContext);


	UTextureRenderTarget2D* HeightPingPongRead(const FEditorBrushRenderContext& BrushRenderContext) const;
	UTextureRenderTarget2D* HeightPingPongWrite(const FEditorBrushRenderContext& BrushRenderContext) const;

	UTextureRenderTarget2D* WeightPingPongRead(const FEditorBrushRenderContext& BrushRenderContext) const;
	UTextureRenderTarget2D* WeightPingPongWrite(const FEditorBrushRenderContext& BrushRenderContext) const;

	UTextureRenderTarget2D* RoadDepthPingPongRead(const FEditorBrushRenderContext& BrushRenderContext) const;
	UTextureRenderTarget2D* RoadDepthPingPongWrite(const FEditorBrushRenderContext& BrushRenderContext) const;


	static void AddDependencyIfValid(UObject* Dependency, TSet<UObject*>& OutDependencies);

};