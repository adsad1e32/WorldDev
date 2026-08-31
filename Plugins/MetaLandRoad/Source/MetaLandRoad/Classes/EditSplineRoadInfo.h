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
class METALANDROAD_API AWorldSceneManagement : public AWorldLandscapeBlueprintBrush
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

	AActor* ResolveHeightStampFocusActor() const;
	void CollectHeightStampRoads(TArray<AMoKuEditBaseActor*>& InOutRoads) const;
	void BuildLayerRoadList(bool bHeightmapRender, FName WeightmapLayerName, TArray<AMoKuEditBaseActor*>& OutRoads);
	FBox ComputeHeightStampWorldBounds(const TArray<AMoKuEditBaseActor*>& Roads) const;

	virtual void Initialize_Native(const FTransform& InLandscapeTransform,
		const FIntPoint& InLandscapeSize,
		const FIntPoint& InLandscapeRenderTargetSize) override;

	virtual void BlueprintRoadBodyChanged_Native(AActor* Actor) override;

#if WITH_EDITOR
	UFUNCTION(CallInEditor, Category = "Render Targets", meta = (DisplayName = "Debug Allocate And Show RTs"))
	void DebugForceAllocateRenderTargets();

	UFUNCTION(CallInEditor, Category = "Render Targets", meta = (DisplayName = "Debug Open Profile RT"))
	void DebugDumpProfileRT();
#endif

	UPROPERTY()
	TArray<AMoKuEditSplineActor*> EditRoadsList;

	UPROPERTY()
	FLandscapeInfoData LandscapeInfo;

	UPROPERTY(VisibleAnywhere, Transient, meta = (Category = "Render Targets"))
	TObjectPtr<UTextureRenderTarget2D> LandscapeRTRef = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta = (Category = "Render Targets"))
	TObjectPtr<UTextureRenderTarget2D> WeightmapRTA = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta = (Category = "Render Targets"))
	TObjectPtr<UTextureRenderTarget2D> WeightmapRTB = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta = (Category = "Render Targets"))
	TObjectPtr<UTextureRenderTarget2D> CSCombinedWriteRT = nullptr;

	UPROPERTY(VisibleAnywhere, Transient, meta = (Category = "Render Targets"))
	TObjectPtr<UTextureRenderTarget2D> CSCombinedWriteRTB = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, meta = (Category = "Debug"))
	FIntPoint LandscapeRTRes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, meta = (Category = "Debug"))
	FIntPoint LandscapeQuads;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, meta = (Category = "Debug"))
	FVector WorldSize;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, meta = (Category = "Debug"))
	FTransform LandscapeTransform;

	bool DeprecateWorldLandscapeInfo(FVector& OutRTWorldLocation, FVector& OutRTWorldSizeVector);
	void SetupDefaultMaterials();

	void InvalidateBrushDistanceFieldCache() {}

private:

	struct FEditorBrushRenderContext
	{
		bool bHeightmapRender = false;
		FName WeightmapLayerName;
		int32 RTIndex = 0;
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
	};
	void ComputeWorldLandscapeInfo(FVector& OutRTWorldLocation, FVector& OutRTWorldSizeVector);
	virtual bool BrushRenderSetup();
	virtual void RenderBrushActorContext(FEditorBrushRenderContext& BrushRenderContext, FBrushActorRenderContext& BrushActorRenderContext);
	bool StampRoadCenterlineProfileCS(
		FEditorBrushRenderContext& BrushRenderContext,
		AMoKuEditSplineActor* Road,
		uint32 WeightMode = 0);
	bool StampJunctionProfileCS(
		FEditorBrushRenderContext& BrushRenderContext,
		AMoKuEditIntersectionActor* Junction,
		uint32 WeightMode = 0);
	virtual void GetRenderDependencies(TSet<UObject*>& OutDependencies) override;
private:
	FDelegateHandle OnLevelAddedToWorldHandle;

	void SetMPCParams();
	bool AllocateRTs();

	static void AddDependencyIfValid(UObject* Dependency, TSet<UObject*>& OutDependencies);

};
