#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ToolContextInterfaces.h"
#include "Components/ActorComponent.h"
#include "MetaLandRoadTypes.h"
#include "MoKuEditBaseActor.h"


#include "MoKuEditIntersectionActor.generated.h"

class AMoKuEditSplineActor;
class UMoKuEditSplinesComponent;
class UMetaLandRoadSplineComponent;
class UDynamicMeshComponent;

USTRUCT()
struct FIntersectionRoadInfo
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AMoKuEditSplineActor> RoadActor;

	UPROPERTY()
	TArray<FVector3d> SideCurvePath;

	UPROPERTY()
	FVector3d JunctionPosition;

};

UENUM(BlueprintType)
enum class ECornerTag : uint8
{
	Side,
	Corner,
	Edge,
	
};



USTRUCT()
struct FCornerInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TArray<FVector3d> CornerPoints;

	UPROPERTY()
	ECornerTag Tag = ECornerTag::Side;

	/** For Side: distance S along SampleSpline for asphalt UV weld. */
	UPROPERTY()
	double SampleDistanceS = -1.0;

	UPROPERTY()
	TWeakObjectPtr<UMetaLandRoadSplineComponent> SampleSpline;

	bool operator==(const FCornerInfo& Other) const
	{
		return CornerPoints == Other.CornerPoints;
	}

};



/** Authoritative junction node data for MetaLandRoad (lane cut + arm links). */
UCLASS(ClassGroup=(MetaLandRoad), meta=(BlueprintSpawnableComponent))
class METALANDROAD_API UMetaLandRoadJunctionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMetaLandRoadJunctionComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Junction)
	FMetaLandRoadJunctionNode JunctionNode;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	TArray<TObjectPtr<AMoKuEditSplineActor>> LinkedRoadActors;

	void ApplyNode(const FMetaLandRoadJunctionNode& InNode);
	void LinkRoadActor(AMoKuEditSplineActor* RoadActor, const FMetaLandRoadJunctionArmRef& Arm);
	void ClearLinks();
};


UCLASS(Blueprintable, hidecategories = (Replication, Input, LOD, Actor, Cooking, Rendering))
class METALANDROAD_API AMoKuEditIntersectionActor : public AMoKuEditBaseActor
{
	GENERATED_BODY()

	public:
		AMoKuEditIntersectionActor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
		//UPROPERTY(TextExportTransient)
		//TObjectPtr<UStaticMeshComponent> StaticMeshAsset;

		//UPROPERTY(VisibleAnywhere)
		//TObjectPtr<UDynamicMeshComponent> DynamicMeshComponent;

		virtual void OnConstruction(const FTransform& Transform) override;
		virtual TArray<FSocketGizmoTransformInfo> GetGizmoInfo() override;
		virtual FEditSplineIntersectInfo CheckIntersectionState(AMoKuEditBaseActor* InEditActor);
		void SetMeshTransform(const FTransform& InTransform);
		void UpdateSplineCornerInfo(bool bIsRecreate=true);
		void SortEdgePath();
		void MeshBuilder();
		bool FindClosedLotsByConnector(TArray<TArray<FVector>>& OutConnectionPoints);

		void UpdateJunctionInfo(class FCurvesRoadOp* InRoadOp);

		void FillMeshActor(bool bIsRecreate = true);

		/** Populate CornerInfo + JunctionConnections from JunctionData arm cuts. */
		void SyncJunctionFrameworkFromArms();

		/** Houdini silhouette cover: cut caps + N arcs + fill; audits vertex order. */
		void RefreshJunctionCoverMesh();

		/** Recalculate arm cuts from linked road geometry and rebuild fill mesh. */
		void RecookFromLinkedRoads();

		/** Write NewAnchor; stretch each arm's hub tip / through-intersect by ΔXY. */
		void ApplyAuthoringAnchorDelta(const FVector& OldAnchor, const FVector& NewAnchor);

		/** Recompute each arm IntersectDistanceS from the current hub XY. */
		void SyncArmIntersectSFromAnchor(const FVector& AnchorWorld);

		/** Rebuild linked road meshes so EvalLanePosition picks up this hub Z. */
		void RefreshLinkedRoadMeshesForAuthoring();

		/**
		 * Copy Height/Weight falloff from the first linked road when this junction still has
		 * empty / seed defaults — keeps Terrain settings on the shared base actor in sync.
		 */
		void SyncTerrainInfluenceFromLinkedRoads();

		/** Seed hub skirt: solid pad + fade (cm). Used when Terrain is still the actor seed. */
		static float GetDefaultTerrainEdgeOffsetCm();
		static float GetDefaultTerrainFalloffWidthCm();
		void ApplyJunctionTerrainDefaultsIfSeed();

		/** Offset / falloff from adjacent-arm angle and mouth width. */
		bool ComputeJunctionSampleFromGeometry(float& OutOffsetCm, float& OutFalloffCm) const;
		void RefreshJunctionSampleFromGeometryIfNeeded();
		void ApplyJunctionLandscapeValue(const FMetaLandRoadLandscapeAttributeValue& Value, bool bPushToLinkedRoads = true);
		void PushJunctionLandscapeToLinkedRoads();
		bool bSyncingJunctionLandscape = false;

		/** Debug: full junction recook + linked road refresh (Details button). */
		UFUNCTION(CallInEditor, Category = "MetaLandRoad|Debug", meta = (DisplayName = "Recook Junction"))
		void DebugRecookJunction();

#if WITH_EDITOR
		virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
		virtual void PostEditUndo() override;
		virtual void PostEditMove(bool bFinished) override;
		virtual void EditorApplyTranslation(const FVector& DeltaTranslation, bool bAltDown, bool bShiftDown, bool bCtrlDown) override;
#endif

		/** Drop previous cover DynamicMesh + ring caches (Recook / T→N must not keep 重面). */
		void ClearJunctionCoverMesh();

		UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
		TObjectPtr<UMetaLandRoadJunctionComponent> JunctionData;

		/** Larger = cut mouths farther from intersect (hub grows). Synced to JunctionNode. */
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaLandRoad|Junction", meta = (ClampMin = "0.9", ClampMax = "1.5", UIMin = "0.9", UIMax = "1.5"))
		float CutExpandScale = 1.15f;

		/**
		 * Solid extra width beyond the hub mesh (cm). Junction stamp: mouth→hub samples
		 * use road half-width + distance-to-mouth, then this pad, then Falloff Width fades out.
		 */
		UPROPERTY()
		float JunctionSampleEdgeOffset = 40.0f;

		UPROPERTY()
		float JunctionSampleFalloffWidth = 280.0f;

		/** True after the artist edits Sample Offset / Falloff in Details or the Landscape panel. */
		UPROPERTY()
		bool bJunctionSampleUserOverride = false;

		bool IsInit = false;

		/** Re-entrancy guard: Purge/SplitSection must not nest Recook during map load. */
		bool bIsRecookInProgress = false;

		/** Gizmo drag: skip per-tick recook; cover mesh rides the actor. */
		bool bJunctionAuthoringDrag = false;
		FVector JunctionDragStartAnchor = FVector::ZeroVector;
		FVector JunctionDragLastAnchor = FVector::ZeroVector;
		/** After a Z drag, do not rebuild cover from road samples (that shears the hub). */
		bool bHoldCoverAfterAuthoringMove = false;
		//void SPawnGizmoInfo(const FVector& InRefVector);
		UPROPERTY(TextExportTransient)
		TObjectPtr<UMoKuEditSplinesComponent> SplineComp;

		UPROPERTY(TextExportTransient)
		TArray<TObjectPtr<UMoKuEditSplinesComponent>> SplineComps;

		UPROPERTY(TextExportTransient)
		TArray<TObjectPtr<AMoKuEditSplineActor>>  ConnectionActors;

		UPROPERTY()
		TArray<FIntersectionRoadInfo>IntersectionRoadInfos;

		UPROPERTY()
		TArray<FCornerInfo> CornerInfo;

		UPROPERTY()
		TArray<FVector3d> EdgePath;
		//���?���
		UPROPERTY()
		FCornerInfo TestCornerInfo;

private:
	/** Reference silhouette: polar cut caps + inward arcs ? CornerInfo + EdgePath. */
	bool BuildJunctionBoundaryRing();

	/** Fig1: lane strips + corner fillets (+ optional center), then publish. */
	bool BuildFig1JunctionCoverMesh();

	/** Silhouette cover fill: ear-clip Cut?InwardArc boundary (no hub fan). */
	void TriangulateJunctionCoverMesh();

	/** Strict point-index audit before meshing (ring adjacency, winding, duplicates). */
	bool ValidateJunctionMeshPointIndices(const TCHAR* ContextTag) const;

	void PublishJunctionDynamicMesh(UE::Geometry::FDynamicMesh3& Mesh);

	/** Flip tris so world-space face normal has +Z (visible from above, culled from below). */
	void EnsureJunctionCoverFacesWorldUp(UE::Geometry::FDynamicMesh3& Mesh) const;

	void AppendCrossSectionStrip(
		UE::Geometry::FDynamicMesh3& Mesh,
		const TArray<FVector>& OuterWorld,
		const TArray<FVector>& InnerWorld,
		const UMetaLandRoadSplineComponent* OuterSpline,
		double OuterS,
		const UMetaLandRoadSplineComponent* InnerSpline,
		double InnerS,
		float MeshHeightOffset,
		const FTransform& ActorTransform) const;

	void AppendPolylineStrip(
		UE::Geometry::FDynamicMesh3& Mesh,
		const TArray<FVector3d>& OuterPoly,
		const TArray<FVector3d>& InnerPoly,
		const UMetaLandRoadSplineComponent* OuterSplineA,
		double OuterSA,
		const UMetaLandRoadSplineComponent* OuterSplineB,
		double OuterSB,
		const UMetaLandRoadSplineComponent* InnerSplineA,
		double InnerSA,
		const UMetaLandRoadSplineComponent* InnerSplineB,
		double InnerSB,
		float MeshHeightOffset,
		const FTransform& ActorTransform) const;

	/** Fill hub hole after strips/fillets. UVs must already be asphalt (nearest-arm / seam weld). */
	void AppendFig1CenterFill(
		UE::Geometry::FDynamicMesh3& Mesh,
		const TArray<FVector>& HoleWorldRing,
		const TArray<FVector2f>& HoleUVs,
		float MeshHeightOffset,
		const FTransform& ActorTransform);

	/** Circular corner arc. bBulgeTowardHub=true: curb-strip style (向内).
	 *  false: single-layer hub silhouette fills exterior wedges between arms (背离 hub). */
	static void BuildOutwardCircularArc(
		const FVector& ArcStart,
		const FVector& ArcEnd,
		const FVector& IntersectPoint,
		double RadiusCm,
		int32 NumSegments,
		TArray<FVector3d>& OutArcPoints,
		double PreferredSagittaCm = 0.0,
		bool bBulgeTowardHub = true);

	FVector2f ComputeJunctionVertexUV(const FVector& WorldPos) const;
	double GetCornerArcDepthForPoints(const FVector& PointA, const FVector& PointB) const;
	UMaterialInterface* ResolveJunctionMaterial() const;
	double Cross2d(const FVector3d& A,const FVector3d& B);
	bool PointInTriangle(const FVector& P,const FVector& A, const FVector& B, const FVector& C);
	double GetCircumRadiusValue(const FVector3d& P1, const FVector3d& P2, const FVector3d& P3);
	bool IsConvex(const FVector3d& A, const FVector3d& B, const FVector3d& C);
};

