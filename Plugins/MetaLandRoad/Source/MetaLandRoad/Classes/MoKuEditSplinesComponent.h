// Fill out your copyright notice in the Description page of Project Settings.
#pragma once

#include "CoreMinimal.h"
#include "Components/SplineComponent.h"
#include "Serialization/Archive.h"
#include "CurveOps/TriangulateCurvesOp.h"
#include "Components/SplineMeshComponent.h"


#include "MoKuEditSplinesComponent.generated.h"

class AMoKuEditSplineActor;

UENUM(BlueprintType)
namespace ESplineMeshLayoutMethod
{
	enum Type : uint8
	{
		SplineMesh,
		InstancedMesh,
	};
}

USTRUCT(BlueprintType)
struct FMoKuSplineInterpPoint 
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FVector StartPos;

	UPROPERTY()
	FVector EndPos;

	UPROPERTY()
	FVector StartTangent;

	UPROPERTY()
	FVector EndTangent;

	UPROPERTY()
	FTransform WorldTransform;
	UPROPERTY()
	UStaticMesh* Mesh;
	UPROPERTY()
	bool FlipModel;
	UPROPERTY()
	bool MirrorModel;
	UPROPERTY()
	TEnumAsByte<ESplineMeshLayoutMethod::Type> LayoutMethod;

	UPROPERTY()
	bool AlwaysFaceZ;

	UPROPERTY()
	float Scale;



	//UPROPERTY(Transient)
	//FRotator MinRotate;

	//UPROPERTY(Transient)
	//FRotator MaxRotate;
	void Serialize(FArchive& Ar)
	{
		Ar << StartPos;
		Ar << EndPos;
		Ar << StartTangent;
		Ar << EndTangent;
		Ar << WorldTransform;
		Ar << FlipModel;
		Ar << MirrorModel;
		Ar << AlwaysFaceZ;
		Ar << Scale;

		uint8 EnumValue = static_cast<uint8>(LayoutMethod);
		Ar << EnumValue; 
		if (Ar.IsLoading())
		{
			LayoutMethod = static_cast<ESplineMeshLayoutMethod::Type>(EnumValue);
		}

		//Mesh->Serialize(Ar);
	}

	friend FArchive& operator<<(FArchive& Ar, FMoKuSplineInterpPoint& I)
	{
		I.Serialize(Ar);
		return Ar;

	}

	FMoKuSplineInterpPoint():
		StartPos(ForceInitToZero),
		EndPos(ForceInitToZero),
		StartTangent(ForceInitToZero),
		EndTangent(ForceInitToZero),
		WorldTransform(FTransform::Identity),
		Mesh(nullptr),
		FlipModel(false),
		MirrorModel(false),
		LayoutMethod(ESplineMeshLayoutMethod::Type::SplineMesh),
		AlwaysFaceZ(AlwaysFaceZ),
		Scale(1.0f)
	{

	}
	FMoKuSplineInterpPoint(const FVector& InStartPos, const FVector& InEndPos, const FVector& InStartTangent = FVector(ForceInitToZero), const FVector& InEndTangent=FVector(ForceInitToZero), UStaticMesh* InMesh = nullptr, const FTransform& InTransform = FTransform::Identity, const bool InFlipModel = false, const bool InMirrorModel = false, const TEnumAsByte<ESplineMeshLayoutMethod::Type> InLayoutMethod = ESplineMeshLayoutMethod::Type::SplineMesh, bool InAlwaysFaceZ = false, float Inscale = 1) :
		StartPos(InStartPos),
		EndPos(InEndPos),
		StartTangent(InStartTangent),
		EndTangent(InEndTangent),
		WorldTransform(InTransform),
		Mesh(InMesh),
		FlipModel(InFlipModel),
		MirrorModel(InMirrorModel),
		LayoutMethod(InLayoutMethod),
		AlwaysFaceZ(InAlwaysFaceZ),
		Scale(Inscale)
	{}

	FMoKuSplineInterpPoint(const FMoKuSplineInterpPoint& InInterpPoint)
		: StartPos(InInterpPoint.StartPos)
		, EndPos(InInterpPoint.EndPos)
		, StartTangent(InInterpPoint.StartTangent)
		, EndTangent(InInterpPoint.EndTangent)
		, WorldTransform(InInterpPoint.WorldTransform)
		, Mesh(InInterpPoint.Mesh)
		, FlipModel(InInterpPoint.FlipModel)
		, MirrorModel(InInterpPoint.MirrorModel)
		, LayoutMethod(InInterpPoint.LayoutMethod)
		, AlwaysFaceZ(InInterpPoint.AlwaysFaceZ)
		, Scale(InInterpPoint.Scale)
	{}

	FBox GetBound() const
	{
		FBox Bound = FBox(ForceInitToZero);
		FBox NewBound = FBox(ForceInitToZero);
		if (StartPos != EndPos)
		{
			FVector3d Diff =  EndPos-StartPos;
			FVector3d Center = (StartPos + EndPos) / 2;

			FVector3d Direction = FMath::Lerp(StartTangent, EndTangent, 0.5);
			Direction = Direction.GetSafeNormal();

			//Distance(EndPos, StartPos) / 2;

			FVector3d bTangent(0, 1, 0);
			bTangent = FVector::CrossProduct(Direction, FVector::UpVector);
			Bound.Max.X = FVector::Distance(EndPos,StartPos)/2;
			Bound.Min.X = -FVector::Distance(EndPos, StartPos) / 2;
			Bound.Max.Y = 100.0f;
			Bound.Min.Y = -100.0f;
			Bound.Max.Z = 100.0f;
			Bound.Min.Z = -100.0f;

			FVector HalfExtents = Bound.GetExtent();
			FRotator Rot = WorldTransform.Rotator();
			Rot.Yaw /= 2.0f;
			FQuat Quat = Rot.Quaternion().Inverse();
			//bTangent = Quat.RotateVector(bTangent);

			Diff = Diff.GetSafeNormal();
			Diff = Quat.RotateVector(Diff);
			TArray<FVector> Vertices;
			const FVector Axes[3] = {
				Direction,  // 旋转后的X轴
				bTangent,  // 旋转后的Y轴
				FVector::UpVector  // 旋转后的Z轴
			};

			for (int32 i = 0; i < 2; i++)
			{
				for (int32 j = 0; j < 2; j++)
				{
					for (int32 k = 0; k < 2; k++)
					{
						FVector Vertex = Center;
						Vertex += (i == 0 ? -HalfExtents.X : HalfExtents.X) * Axes[0];
						Vertex += (j == 0 ? -HalfExtents.Y : HalfExtents.Y) * Axes[1];
						Vertex += (k == 0 ? -HalfExtents.Z : HalfExtents.Z) * Axes[2];
						Vertices.Add(Vertex);
					}
				}
			}

			NewBound = FBox(Vertices);

			//Bound += StartPos- bTangent * DOUBLE_KINDA_SMALL_NUMBER;
			////Bound += StartPos+ bTangent * DOUBLE_KINDA_SMALL_NUMBER ;
			////Bound+= EndPos - bTangent * DOUBLE_KINDA_SMALL_NUMBER;
			//Bound+= EndPos + bTangent * DOUBLE_KINDA_SMALL_NUMBER;
			//Bound.Min.Z = Center.Z-DOUBLE_KINDA_SMALL_NUMBER;
			//Bound.Max.Z = Center.Z+DOUBLE_KINDA_SMALL_NUMBER;
			//FVector C = StartPos;
			////const  FRotator Rot = WorldTransform.Rotator();
			////FQuat Quat = Rot.Quaternion();
			//FMatrix RotMatrix = Quat.ToMatrix();
			//
			////Bound = Bound.InverseTransformBy(WorldTransform);
			//NewBound = Bound.TransformProjectBy(RotMatrix);
			//FVector Size = NewBound.GetSize();
			//UE_LOG(LogTemp, Warning, TEXT("Size==%s"), *Size.ToString())
			//Bound = Bound.MoveTo(Center);
		}
		return NewBound;
	}

};


USTRUCT()
struct FMoKuSplinePreviewMeshInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FVector StartPos;

	UPROPERTY()
	FVector EndPos;

	UPROPERTY()
	FVector StartTangent;

	UPROPERTY()
	FVector EndTangent;

	UPROPERTY()
	USplineMeshComponent* SPlineMesh;

	FMoKuSplinePreviewMeshInfo() :
		StartPos(ForceInitToZero),
		EndPos(ForceInitToZero),
		StartTangent(ForceInitToZero),
		EndTangent(ForceInitToZero),
		SPlineMesh(nullptr)
	{

	}

	FMoKuSplinePreviewMeshInfo(const FVector& InStartPos, const FVector& InEndPos, const FVector& InStartTangent, const FVector& InEndTangent, USplineMeshComponent* InMesh) :
		StartPos(InStartPos),
		EndPos(InEndPos),
		StartTangent(InStartTangent),
		EndTangent(InEndTangent),
		SPlineMesh(InMesh)
	{
	}
	
public :
	void SetPreviewMesh()
	{
		if (SPlineMesh)
		{
			SPlineMesh->SetStartAndEnd(StartPos, StartTangent, EndPos, EndTangent, true);
		}

	}

};



//DECLARE_EVENT_OneParam(UMoKuEditSplinesComponent, FParamsChangedEvent, AActor*)

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class UMoKuEditSplinesComponent : public USplineComponent
{
	GENERATED_UCLASS_BODY()

public:

	#if WITH_EDITOR
		virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
		virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
		virtual void PostLoad() override;
		virtual void PostEditUndo() override;
	#endif

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> LocalMeshComponents;
	UPROPERTY()
	FString Tag = TEXT("MainRoad");
	const TArray<FMoKuSplineInterpPoint>& GetProceduralPoints() const { return ProceduralPoints; }

	void  SetProceduralPoints(const TArray<FMoKuSplineInterpPoint>& InProceduralPoints) { ProceduralPoints = InProceduralPoints; }
	void  UpdateProceduralPoints(const FMoKuSplineInterpPoint& InPoint);
	void  ClearProceduralPoints() { ProceduralPoints.Empty(); }
	void  UpdateSplineMesh();

	
	
	void DistributeSegmentToSpline(TArray<UStaticMesh*> InStaticMeshList = TArray<UStaticMesh*>());
	void DistributeSegmentByDist(float Distance=1000.0f);
	void AddSplinePointLocation(const FVector& InLocationPos,const int32 Index);

	TObjectPtr<AMoKuEditSplineActor> ParentActor;

	bool bIsRenderPreviewMesh = false;
	bool bIsAddPreviewMeshPoint = false;


	TArray<FOctreeElementId2> OctreeIds;

protected:

	UPROPERTY()
	TArray<FMoKuSplineInterpPoint> ProceduralPoints;
	

};

