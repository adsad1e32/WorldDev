#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ToolContextInterfaces.h"
#include "MoKuEditBaseActor.h"


#include "MoKuEditIntersectionActor.generated.h"

class AMoKuEditSplineActor;
class UMoKuEditSplinesComponent;
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

	bool operator==(const FCornerInfo& Other) const
	{
		return CornerPoints == Other.CornerPoints;
	}

};


UCLASS(Blueprintable, hidecategories = (Replication, Input, LOD, Actor, Cooking, Rendering))
class  AMoKuEditIntersectionActor : public AMoKuEditBaseActor
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

		bool IsInit = false;
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
		//≤‚ ‘±‰¡ø
		UPROPERTY()
		FCornerInfo TestCornerInfo;

private:
	double Cross2d(const FVector3d& A,const FVector3d& B);
	bool PointInTriangle(const FVector& P,const FVector& A, const FVector& B, const FVector& C);
	double GetCircumRadiusValue(const FVector3d& P1, const FVector3d& P2, const FVector3d& P3);
	bool IsConvex(const FVector3d& A, const FVector3d& B, const FVector3d& C);
};

