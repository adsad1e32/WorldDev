#include "MoKuEditorUtils.h"
#include "Async/Async.h"
#include "Misc/Guid.h" 
#include "Engine/Level.h"
#include "UObject/UObjectGlobals.h"
#include "ActorFolder.h"
#include "WorldPersistentFolders.h"
#include "EditorActorFolders.h"
#include "CompGeom/PolygonTriangulation.h"
#include "CurveOps/TriangulateCurvesOp.h"
#include "DynamicMesh/MeshNormals.h"
#include "Landscape.h"
#include "LandscapeEditLayer.h"
#include "LandscapeLayerInfoObject.h"
#include "EditSplineRoadInfo.h"
#include "GroupTopology.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "TextureResource.h"

TArray<FMoKuSplineInterpPoint> MoKuSplineEditorUtils::SplitSegmentToSpline(UMoKuEditSplinesComponent* InSplineComponent, TArray<UStaticMesh*> InStaticMeshArray, const FSplineExtraMesh& InExtraMeshInfo)
{
	UMoKuEditSplinesComponent* RefSplineComp = nullptr;
	if (!InExtraMeshInfo.EnableMirror)
	{
		RefSplineComp = NewObject<UMoKuEditSplinesComponent>();
		RefSplineComp->ClearSplinePoints();
		RefSplineComp->SetWorldLocation(InSplineComponent->GetComponentLocation());
		RefSplineComp->SetWorldRotation(InSplineComponent->GetComponentRotation());
		RefSplineComp->SetWorldScale3D(InSplineComponent->GetComponentScale());

		for (int32 i = 0; i < InSplineComponent->GetNumberOfSplinePoints(); i++)
		{
			FVector Location = InSplineComponent->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
			ESplinePointType::Type PointType = InSplineComponent->GetSplinePointType(i);
			RefSplineComp->AddSplinePoint(Location, ESplineCoordinateSpace::World);
			RefSplineComp->SetSplinePointType(i, PointType);
		}
		RefSplineComp->UpdateSpline();
		TArray<FVector> OutPolyLinePoints;
		RefSplineComp->ConvertSplineToPolyLine(ESplineCoordinateSpace::World, 400, OutPolyLinePoints);
		RefSplineComp->SetSplineWorldPoints(OutPolyLinePoints);
		RefSplineComp->UpdateSpline();
		TArray<FVector>OutPoints;
		
		for (int i = 0; i < RefSplineComp->GetNumberOfSplinePoints(); i++)
		{
			FVector Location = RefSplineComp->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
			FVector OffsetDirection = RefSplineComp->GetRightVectorAtSplinePoint(i, ESplineCoordinateSpace::World).GetSafeNormal();
			Location += OffsetDirection*InExtraMeshInfo.PointOffset.OffsetY;
			OutPoints.Add(Location);
		}

		RefSplineComp->SetSplineWorldPoints(OutPoints);
		RefSplineComp->UpdateSpline();
	}
	else
	{
		RefSplineComp = InSplineComponent;
	}

	//函数还未重构整合归纳
	auto GetDimensionsX = [](UStaticMesh* Mesh)->float
		{
			if (Mesh)
			{
				FVector Dimensions = Mesh->GetBoundingBox().GetSize();
				return Dimensions.X;
			}
			return 0.0f;
		};

	auto SelectInfo = [=](int Seed)->UStaticMesh*
		{
			FRandomStream RandomStream;
			RandomStream.Initialize(InExtraMeshInfo.AssetSeed + Seed);
			float Result = RandomStream.FRandRange(0.0f, InStaticMeshArray.Num());
			int32 SelectIdx = int32(Result);
			if (InStaticMeshArray[SelectIdx])
			{
				return InStaticMeshArray[SelectIdx];
			}
			return nullptr;
		};

	auto SelectGapInfo = [=](int InSeed)->float
		{
			FVector2D GapSize = InExtraMeshInfo.GapSize;
			float GapSeed = InExtraMeshInfo.GapSeed;
			FRandomStream RandomStream;
			RandomStream.Initialize(InSeed + GapSeed);
			float RandomFloat = RandomStream.FRandRange(InExtraMeshInfo.GapSize.X, InExtraMeshInfo.GapSize.Y);
			return RandomFloat;
		};



	int32 SegmentCount = 0;
	int32 SelecIndex = 0;
	TArray<float>GapInfoList;
	float SumLength = 0;
	float SumGapSize = 0;
	TArray<FMoKuSplineInterpPoint> ProceduralPoints;
	float CurrentSplineLength = RefSplineComp->GetSplineLength() - (InExtraMeshInfo.Offset.X + InExtraMeshInfo.Offset.Y);
	TArray<UStaticMesh*> PlaceMeshList;
	if (InStaticMeshArray.Num() > 0)
	{
		float RemainLength = CurrentSplineLength;
		while (RemainLength > 0)
		{
			UStaticMesh* SplineMesh = SelectInfo(SelecIndex);
			float GapSize = SelectGapInfo(SelecIndex);
			if (InStaticMeshArray.Num() == 0)break;
			if (!SplineMesh)
			{
				SelecIndex++;
				continue;
			}
			if (RemainLength < GapSize)
			{
				if (RemainLength / GapSize > 0.5)
				{
					PlaceMeshList.Add(SplineMesh);
					GapInfoList.Add(GapSize);
					SegmentCount++;
					SumGapSize += GapSize;
				}
				break;
			}

			SumLength += GetDimensionsX(SplineMesh);
			PlaceMeshList.Add(SplineMesh);

			SumGapSize += GapSize;
			RemainLength = CurrentSplineLength - (SumLength + SumGapSize);

			SegmentCount++;
			GapInfoList.Add(GapSize);
			SelecIndex++;
		}
		float AverageGap = 0;
		if (InExtraMeshInfo.LayoutMethod == ESplineMeshLayoutMethod::Type::InstancedMesh)
		{
			AverageGap = (CurrentSplineLength - SumGapSize) / float(SegmentCount);
		}
		float StartLength = InExtraMeshInfo.Offset.X;
		float EndLength = 0;
		for (int i = 0; i < SegmentCount; i++)
		{
			float NewGapSize = GapInfoList[i] + AverageGap;			
			UStaticMesh* StaticMesh;
			StaticMesh = PlaceMeshList[i];
			float ScalarRatio = (CurrentSplineLength - SumGapSize) / SumLength;
			if (InExtraMeshInfo.EnableEndCaps)
			{
				ScalarRatio = 1;
			}
			float SplineSegmentLength = GetDimensionsX(StaticMesh) * ScalarRatio;
			if (i == 0)
			{
				EndLength += StartLength;
				StartLength = FMath::Min(StartLength, CurrentSplineLength);
			}

			if (InExtraMeshInfo.LayoutMethod == ESplineMeshLayoutMethod::Type::InstancedMesh)
			{
				SplineSegmentLength = GetDimensionsX(StaticMesh);
				EndLength += NewGapSize;
			}
			else
			{
				EndLength = StartLength + SplineSegmentLength;
			}

			EndLength = FMath::Min(EndLength, RefSplineComp->GetSplineLength());
			double MidLength = (EndLength - StartLength) / 2 + StartLength;
			FVector StartPos = RefSplineComp->GetLocationAtDistanceAlongSpline(StartLength, ESplineCoordinateSpace::Local);
			FVector StartTangent = RefSplineComp->GetDirectionAtDistanceAlongSpline(StartLength, ESplineCoordinateSpace::Local);
			FVector EndPos = RefSplineComp->GetLocationAtDistanceAlongSpline(EndLength, ESplineCoordinateSpace::Local);
			FVector EndTangent = RefSplineComp->GetDirectionAtDistanceAlongSpline(EndLength, ESplineCoordinateSpace::Local);
			FVector MidPos = RefSplineComp->GetLocationAtDistanceAlongSpline(MidLength, ESplineCoordinateSpace::Local);
			FVector MidTangent = RefSplineComp->GetLocationAtDistanceAlongSpline(MidLength, ESplineCoordinateSpace::Local);
			FTransform WorldTransform = RefSplineComp->GetTransformAtDistanceAlongSpline(StartLength, ESplineCoordinateSpace::World);
			FVector OffsetDirectionStart = FVector::CrossProduct(StartTangent.GetSafeNormal(), FVector(0, 0, 1)).GetSafeNormal();
			FVector OffsetDirectionEnd = FVector::CrossProduct(EndTangent.GetSafeNormal(), FVector(0, 0, 1)).GetSafeNormal();
			FVector OffsetUpStart = FVector::CrossProduct(OffsetDirectionStart, StartTangent.GetSafeNormal()).GetSafeNormal();
			FVector OffsetUpEnd = FVector::CrossProduct(OffsetDirectionEnd, EndTangent.GetSafeNormal()).GetSafeNormal();

			FVector A = (StartPos - MidPos).GetSafeNormal();
			FVector B = (EndPos - MidPos).GetSafeNormal();

			float Result = A.Dot(B);
			float Angle = FMath::Acos(Result) * 180 / PI;
			FString LowStaticMeshPath;
			if (FMath::Abs(Angle - 180) < 2.5)
			{
				TArray<FString> Parts;
				StaticMesh->GetName().ParseIntoArray(Parts, TEXT("_"), true);

				Parts[Parts.Num() - 2] = "L";

				for (int j = 0; j < Parts.Num(); ++j)
				{
					LowStaticMeshPath += Parts[j];
					if (j < Parts.Num() - 1)
					{
						LowStaticMeshPath += TEXT("_");
					}
				}
				FString Directory = FPaths::GetPath(StaticMesh->GetPathName());
				LowStaticMeshPath = Directory / LowStaticMeshPath;
				if (FPackageName::DoesPackageExist(LowStaticMeshPath))
				{
					StaticMesh = LoadObject<UStaticMesh>(nullptr, *LowStaticMeshPath);
				}

			}

			FVector OffsetAmpStart;
			FVector OffsetAmpEnd;
			FVector OffsetAmpStartY = InExtraMeshInfo.PointOffset.OffsetY * OffsetDirectionStart;
			FVector OffsetAmpEndY = InExtraMeshInfo.PointOffset.OffsetY * OffsetDirectionEnd;
			FVector OffsetAmpStartX = InExtraMeshInfo.PointOffset.OffsetX * StartTangent.GetSafeNormal();
			FVector OffsetAmpEndX = InExtraMeshInfo.PointOffset.OffsetX * StartTangent.GetSafeNormal();
			bool bIsFlipModel = InExtraMeshInfo.InvertModel;
			FVector OffsetAmpStartZ = InExtraMeshInfo.PointOffset.OffsetZ * OffsetUpStart;
			FVector OffsetAmpEndZ = InExtraMeshInfo.PointOffset.OffsetZ * OffsetUpEnd;
			bool bIsAWlaysZ = InExtraMeshInfo.AlwaysFaceZ;


			TEnumAsByte<ESplineMeshLayoutMethod::Type> Method = InExtraMeshInfo.LayoutMethod;

			if (InExtraMeshInfo.LayoutMethod == ESplineMeshLayoutMethod::Type::InstancedMesh)
			{

				StartTangent = (EndPos - StartPos).GetSafeNormal();			
				StartLength = EndLength;
			}
			else
			{
				StartLength = EndLength + NewGapSize;
			}
			//代码未整合


			if (InExtraMeshInfo.EnableMirror)
			{
				OffsetAmpStart = OffsetAmpStartY + OffsetAmpStartX + OffsetAmpStartZ;
				OffsetAmpEnd = OffsetAmpEndY + OffsetAmpEndX + OffsetAmpEndZ;


				//函数参数尚未整合
				FMoKuSplineInterpPoint MoKuSplineInterpPoint = FMoKuSplineInterpPoint(StartPos + OffsetAmpStart*-1, EndPos + OffsetAmpEnd, StartTangent * SplineSegmentLength, EndTangent * SplineSegmentLength, StaticMesh, WorldTransform, false, true, Method, bIsAWlaysZ, ScalarRatio);
				ProceduralPoints.Add(MoKuSplineInterpPoint);
				FMoKuSplineInterpPoint MirrorMoKuSplineInterpPoint = FMoKuSplineInterpPoint(StartPos + OffsetAmpStart, EndPos + OffsetAmpEnd, StartTangent * SplineSegmentLength, EndTangent * SplineSegmentLength, StaticMesh, WorldTransform, true, true, Method, bIsAWlaysZ, ScalarRatio);
				ProceduralPoints.Add(MirrorMoKuSplineInterpPoint);
				if (InExtraMeshInfo.EnableEndCaps)
				{
					if (i == SegmentCount - 1 && InExtraMeshInfo.LayoutMethod == ESplineMeshLayoutMethod::Type::InstancedMesh)
					{
						StartLength = EndLength;
						WorldTransform = RefSplineComp->GetTransformAtDistanceAlongSpline(StartLength, ESplineCoordinateSpace::World);
						StartPos = RefSplineComp->GetLocationAtDistanceAlongSpline(StartLength, ESplineCoordinateSpace::Local);
						StartTangent = RefSplineComp->GetDirectionAtDistanceAlongSpline(StartLength, ESplineCoordinateSpace::Local);
						EndPos = RefSplineComp->GetLocationAtDistanceAlongSpline(EndLength, ESplineCoordinateSpace::Local);
						EndTangent = RefSplineComp->GetDirectionAtDistanceAlongSpline(EndLength, ESplineCoordinateSpace::Local);


						FMoKuSplineInterpPoint EndMoKuSplineInterpPoint= FMoKuSplineInterpPoint(StartPos + OffsetAmpStart*-1, EndPos + OffsetAmpEnd*-1, StartTangent * SplineSegmentLength, EndTangent * SplineSegmentLength, StaticMesh, WorldTransform, false, true, Method, bIsAWlaysZ);
						ProceduralPoints.Add(EndMoKuSplineInterpPoint);
						FMoKuSplineInterpPoint EndMirrorMoKuSplineInterpPoint = FMoKuSplineInterpPoint(StartPos + OffsetAmpStart *-1, EndPos + OffsetAmpEnd *-1, StartTangent * SplineSegmentLength, EndTangent * SplineSegmentLength, StaticMesh, WorldTransform, true, true, Method, bIsAWlaysZ);
						ProceduralPoints.Add(EndMirrorMoKuSplineInterpPoint);
					}
				}
			}
			else
			{
				FMoKuSplineInterpPoint SplineInterpPoint(StartPos, EndPos, StartTangent, EndTangent, StaticMesh, WorldTransform, bIsFlipModel, false, Method, bIsAWlaysZ, ScalarRatio);
				ProceduralPoints.Add(SplineInterpPoint);
				if (InExtraMeshInfo.EnableEndCaps)
				{
					if (i == SegmentCount - 1 && InExtraMeshInfo.LayoutMethod == ESplineMeshLayoutMethod::Type::InstancedMesh)
					{
						StartLength = EndLength;
						WorldTransform = RefSplineComp->GetTransformAtDistanceAlongSpline(StartLength, ESplineCoordinateSpace::World);
						StartPos = RefSplineComp->GetLocationAtDistanceAlongSpline(StartLength, ESplineCoordinateSpace::Local);
						StartTangent = RefSplineComp->GetDirectionAtDistanceAlongSpline(StartLength, ESplineCoordinateSpace::Local);
						EndPos = RefSplineComp->GetLocationAtDistanceAlongSpline(EndLength, ESplineCoordinateSpace::Local);
						EndTangent = RefSplineComp->GetDirectionAtDistanceAlongSpline(EndLength, ESplineCoordinateSpace::Local);
						FMoKuSplineInterpPoint EndMoKuSplineInterpPoint = FMoKuSplineInterpPoint(StartPos, EndPos, StartTangent, EndTangent, StaticMesh, WorldTransform, false, true, Method, bIsAWlaysZ);
						ProceduralPoints.Add(EndMoKuSplineInterpPoint);
					}
				}
			}


		}
		

	}
	return ProceduralPoints;

}

void MoKuSplineEditorUtils::UpdateSplineMesh(TArray<UStaticMeshComponent*>& InLocalMeshComponents, const TArray<FMoKuSplineInterpPoint>& InIterpPoints, USceneComponent* InComponent)
{
	if (InLocalMeshComponents.Num() > 0)
	{
		for (auto MeshComponent : InLocalMeshComponents)
		{
			if (MeshComponent != nullptr)
			{
				MeshComponent->Modify();
				MeshComponent->DestroyComponent();
			}
		}
	}

	InLocalMeshComponents.SetNumUninitialized(InIterpPoints.Num());

	AActor* ParentActor  = InComponent->GetOwner();

	ParentActor->ClearInstanceComponents(true);
	FVector StartTangent;
	FVector EndTangent;
	for (int32 Index = 0; Index < InIterpPoints.Num(); ++Index)
	{
		
		if (InIterpPoints[Index].Mesh)
		{
			UStaticMesh* InstancedMesh = InIterpPoints[Index].Mesh;
			StartTangent = InIterpPoints[Index].StartTangent;
			EndTangent = InIterpPoints[Index].EndTangent;
			ESplineMeshLayoutMethod::Type LayoutMethod = InIterpPoints[Index].LayoutMethod;
			if (InIterpPoints[Index].LayoutMethod == ESplineMeshLayoutMethod::Type::SplineMesh)
			{
				TObjectPtr<USplineMeshComponent> SplineMesh = NewObject<USplineMeshComponent>(InComponent);
				bool bInvertModel = InIterpPoints[Index].FlipModel;
				FVector2D SplineMeshScale2D(1.0f, 1.0f);
				if (InIterpPoints[Index].FlipModel == true)
				{
					SplineMeshScale2D.X *= -1;
				}
				SplineMesh->SetStaticMesh(InstancedMesh);
				SplineMesh->SetForwardAxis(ESplineMeshAxis::X, true);
				SplineMesh->CreationMethod = EComponentCreationMethod::UserConstructionScript;
				SplineMesh->SetMobility(EComponentMobility::Movable);
				SplineMesh->SetCollisionEnabled(ECollisionEnabled::Type::QueryAndPhysics);
				if (InIterpPoints[Index].AlwaysFaceZ)
				{
					StartTangent.Z = 0;
					EndTangent.Z = 0;
				}
				SplineMesh->SetStartAndEnd(InIterpPoints[Index].StartPos, StartTangent,InIterpPoints[Index].EndPos,EndTangent, true);
				SplineMesh->SetStartScale(SplineMeshScale2D);
				SplineMesh->SetEndScale(SplineMeshScale2D);
				SplineMesh->RegisterComponentWithWorld(InComponent->GetWorld());
				SplineMesh->AttachToComponent(InComponent, FAttachmentTransformRules::KeepRelativeTransform);
				SplineMesh->UpdateMesh();
				SplineMesh->Modify();
				SplineMesh->MarkRenderStateDirty();
				InLocalMeshComponents[Index] = Cast<UStaticMeshComponent>(SplineMesh);
				//if (InComponent->GetOwner()->IsA<AMoKuEditSplineActor>())
				//{
				//	ParentActor->AddInstanceComponent(SplineMesh);
				//}


			}
			else
			{
				UStaticMeshComponent* NewStaticMeshComponent = NewObject<UStaticMeshComponent>(InComponent);
				NewStaticMeshComponent->SetStaticMesh(InstancedMesh);
				FVector Scale(InIterpPoints[Index].Scale, 1.0, 1.0);			
				FRotator SplineRotator = InIterpPoints[Index].WorldTransform.Rotator();
				FRotator ParentRotator = InComponent->GetRelativeRotation();

				if (InIterpPoints[Index].FlipModel == true)
				{
					Scale.Y = -1;
				}

				if(InIterpPoints[Index].AlwaysFaceZ)
				{
					SplineRotator.Pitch = 0;
				}
				FVector ForwardVector = StartTangent.GetSafeNormal();
				FVector UpVector = FVector::UpVector;
				FMatrix RotationMatrix = FRotationMatrix::MakeFromXZ(ForwardVector, UpVector);
				SplineRotator = RotationMatrix.Rotator();
				NewStaticMeshComponent->SetWorldRotation(SplineRotator);
				NewStaticMeshComponent->SetWorldLocation(InIterpPoints[Index].StartPos);
				NewStaticMeshComponent->SetWorldScale3D(Scale);
				NewStaticMeshComponent->AttachToComponent(InComponent, FAttachmentTransformRules::KeepRelativeTransform);
				NewStaticMeshComponent->RegisterComponent();
				NewStaticMeshComponent->MarkRenderStateDirty();
	/*			if (InComponent->GetOwner()->IsA<AMoKuEditSplineActor>())
				{
					ParentActor->AddInstanceComponent(NewStaticMeshComponent);
				}*/

				InLocalMeshComponents[Index] = MoveTemp(NewStaticMeshComponent);
			
			}

		}
	}
}

void MoKuSplineEditorUtils::SplitSpline(USplineComponent* InSplineComponent, float SplitRatio,TArray<FTransform>& InFirstHalfSplitPostion, TArray<FTransform>& InEndHalfSplitPostion)
{
	if (InSplineComponent == nullptr)return;
	int32 NumPoints = InSplineComponent->GetNumberOfSplinePoints();
	float SplineLength = InSplineComponent->GetSplineLength();
	FTransform InSertTransform = InSplineComponent->GetTransformAtDistanceAlongSpline(SplitRatio, ESplineCoordinateSpace::World);
	TArray<FVector> OutSplinePoints;
	TArray<double>  OutSplinePointsDistance;

	int32 SegNum = InSplineComponent->GetNumberOfSplineSegments();
	for (int idx = 0; idx < SegNum; idx++)
	{
		TArray<FVector> OutSegmentPoints;
		InSplineComponent->ConvertSplineSegmentToPolyLine(idx, ESplineCoordinateSpace::World,5000,OutSegmentPoints);
		if (idx == 0)
		{
			OutSplinePoints = OutSegmentPoints;
			continue;
		}
		FVector PrvePosition = OutSplinePoints[OutSplinePoints.Num() - 1];
		FVector EndOfPosition = OutSegmentPoints[OutSegmentPoints.Num() - 1];
		FVector FirstOfPosition = OutSegmentPoints[0];
		float Dist0 = FVector::Dist(PrvePosition, EndOfPosition);
		float Dist1 = FVector::Dist(PrvePosition, FirstOfPosition);

		if (Dist0 >= Dist1)
		{
			OutSegmentPoints.RemoveAt(0);
		}
		else
		{
			OutSegmentPoints.RemoveAt(OutSegmentPoints.Num() - 1);
		}
		OutSplinePoints.Append(OutSegmentPoints);
	}

	//if (Converted)
	//{
		NumPoints = OutSplinePoints.Num();
	//}

	for (int32 i = 0; i < NumPoints; ++i)
	{
		float PointOfDist = InSplineComponent->GetDistanceAlongSplineAtLocation(OutSplinePoints[i], ESplineCoordinateSpace::World);
		//OutSplinePoints
		//float PointOfDist = InSplineComponent->GetDistanceAlongSplineAtSplinePoint(i);
		//FTransform PointTransform = InSplineComponent->GetTransformAtSplinePoint(PointOfDist, ESplineCoordinateSpace::World);
		FTransform PointTransform = InSplineComponent->GetTransformAtDistanceAlongSpline(PointOfDist, ESplineCoordinateSpace::World);
		if (PointOfDist < SplitRatio)
		{
			if (i == 0)
			{
				InFirstHalfSplitPostion.Add(PointTransform);
			}
			else
			{
				InFirstHalfSplitPostion.Insert(PointTransform, 0);
			}
		}
		else
		{

			InEndHalfSplitPostion.Add(PointTransform);
		}
	}
	InFirstHalfSplitPostion.Insert(InSertTransform,0);
	InEndHalfSplitPostion.Insert(InSertTransform,0);


}


void MoKuSplineEditorUtils::SplitSpline(USplineComponent* InSplineComponent, float StartValue,float EndValue,TArray<FTransform>& InFirstHalfSplitPosition, TArray<FTransform>& InEndHalfSplitPosition)
{
	if (!InSplineComponent)return;


	int32 NumPoints = InSplineComponent->GetNumberOfSplinePoints();
	//float SplineLength = InSplineComponent->GetSplineLength();
	FTransform InSertFirstTransform = InSplineComponent->GetTransformAtDistanceAlongSpline(StartValue, ESplineCoordinateSpace::World);
	FTransform InSertEndTransform = InSplineComponent->GetTransformAtDistanceAlongSpline(EndValue, ESplineCoordinateSpace::World);


	TArray<FVector> OutSplinePoints;
	int32 SegNum = InSplineComponent->GetNumberOfSplineSegments();
	for (int idx = 0; idx < SegNum; idx++)
	{
		TArray<FVector> OutSegmentPoints;
		InSplineComponent->ConvertSplineSegmentToPolyLine(idx, ESplineCoordinateSpace::World, 5000, OutSegmentPoints);
		if (idx == 0)
		{
			OutSplinePoints = OutSegmentPoints;
			continue;
		}
		FVector PrvePosition = OutSplinePoints[OutSplinePoints.Num() - 1];
		FVector EndOfPosition = OutSegmentPoints[OutSegmentPoints.Num() - 1];
		FVector FirstOfPosition = OutSegmentPoints[0];
		float Dist0 = FVector::Dist(PrvePosition, EndOfPosition);
		float Dist1 = FVector::Dist(PrvePosition, FirstOfPosition);

		if (Dist0 >= Dist1)
		{
			OutSegmentPoints.RemoveAt(0);
		}
		else
		{
			OutSegmentPoints.RemoveAt(OutSegmentPoints.Num() - 1);
		}
		OutSplinePoints.Append(OutSegmentPoints);
	}

	//if (Converted)
	//{
	NumPoints = OutSplinePoints.Num();
	//}

	for (int32 i = 0; i < NumPoints; ++i)
	{
		float PointOfDist = InSplineComponent->GetDistanceAlongSplineAtLocation(OutSplinePoints[i], ESplineCoordinateSpace::World);
		FTransform PointTransform = InSplineComponent->GetTransformAtDistanceAlongSpline(PointOfDist, ESplineCoordinateSpace::World);
		if (PointOfDist < StartValue)
		{
			if (i == 0)
			{
				InFirstHalfSplitPosition.Add(PointTransform);
			}
			else
			{
				InFirstHalfSplitPosition.Insert(PointTransform, 0);
			}
		}

		if (PointOfDist > EndValue)
		{
			InEndHalfSplitPosition.Add(PointTransform);
		}

		//else
		//{

		//	InEndHalfSplitPosition.Add(PointTransform);
		//}
	}
	InFirstHalfSplitPosition.Insert(InSertFirstTransform, 0);
	InEndHalfSplitPosition.Insert(InSertEndTransform, 0);


}
void MoKuSplineEditorUtils::CutSplineFromRange(USplineComponent* InSplineComponent, float StartValue, float EndValue, TArray<FTransform>& OutSplitSplinePosition)
{

	if (!InSplineComponent) return;

	const float TotalLength = InSplineComponent->GetSplineLength();
	if (StartValue < 0 || EndValue > TotalLength)return;

	int32 NumPoints = InSplineComponent->GetNumberOfSplinePoints();
	FTransform InSertStartTransform = InSplineComponent->GetTransformAtDistanceAlongSpline(StartValue, ESplineCoordinateSpace::World);
	float Inputkey = InSplineComponent->FindInputKeyClosestToWorldLocation(InSertStartTransform.GetLocation());

	FTransform ExtentStartTransform = InSplineComponent->GetTransformAtDistanceAlongSpline(StartValue+100.0f, ESplineCoordinateSpace::World);

	OutSplitSplinePosition.Add(InSertStartTransform);
	//OutSplitSplinePostion.Add(ExtentStartTransform);

	for (int32 i = 0; i < NumPoints; ++i)
	{
		float Splinelength = InSplineComponent->GetSplineLength();
		float PointOfDist = InSplineComponent->GetDistanceAlongSplineAtSplinePoint(i);
		FTransform PointTransform = InSplineComponent->GetTransformAtSplinePoint(i, ESplineCoordinateSpace::World);

		
		if (PointOfDist > StartValue&& PointOfDist < EndValue)
		{
			OutSplitSplinePosition.Add(PointTransform);	
		}
	}
	FTransform ExtentEndTransform = InSplineComponent->GetTransformAtDistanceAlongSpline(EndValue - 100.0f, ESplineCoordinateSpace::World);
	FTransform InSertEndTransform = InSplineComponent->GetTransformAtDistanceAlongSpline(EndValue, ESplineCoordinateSpace::World);
	//OutSplitSplinePostion.Add(ExtentEndTransform);
	OutSplitSplinePosition.Add(InSertEndTransform);

	if (OutSplitSplinePosition.Num() == 2)
	{
		float MidValue =  (EndValue - StartValue)/2;
		FTransform MidTransform = InSplineComponent->GetTransformAtDistanceAlongSpline(MidValue, ESplineCoordinateSpace::World);
		OutSplitSplinePosition.Insert(MidTransform,1);
	}

}

AMoKuEditSplineActor* MoKuSplineEditorUtils::CreateNewMokuSplineActor(AActor* InSrcActor, const TArray<FTransform>& InTransforms, bool SetDefultPath)
{
	if (!InSrcActor) return nullptr;

	UWorld* World = InSrcActor->GetWorld();
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters SpawnParams;
	SpawnParams.OverrideLevel = World->PersistentLevel;
	SpawnParams.bNoFail = true;
	SpawnParams.ObjectFlags |= RF_Transactional;
	AMoKuEditSplineActor* SrcSplineActor = Cast<AMoKuEditSplineActor>(InSrcActor);
	if (SrcSplineActor)
	{
		TObjectPtr<UMoKuEditSplinesComponent> SrcSplineComp = SrcSplineActor->GetSplineComponent();
		AMoKuEditSplineActor* NewSplineActor = World->SpawnActor<AMoKuEditSplineActor>(InTransforms[0].GetLocation(), FRotator(0.0f,0.0f,0.0f), SpawnParams);
		if (NewSplineActor)
		{
			NewSplineActor->EnableStartAssetMesh = SrcSplineActor->EnableStartAssetMesh;
			NewSplineActor->EnableEndAssetMesh = SrcSplineActor->EnableEndAssetMesh;
			NewSplineActor->StartAssetMesh = SrcSplineActor->StartAssetMesh;
			NewSplineActor->EndAssetMesh = SrcSplineActor->EndAssetMesh;
			NewSplineActor->EnableExtraMesh = SrcSplineActor->EnableExtraMesh;
			NewSplineActor->ExtraSplineInfo = SrcSplineActor->ExtraSplineInfo;
			NewSplineActor->StartOfJunction = SrcSplineActor->StartOfJunction;
			NewSplineActor->EndOfJunction = SrcSplineActor->EndOfJunction;

			UMoKuEditSplinesComponent* SplineComp = NewSplineActor->GetSplineComponent();

			SplineComp->ClearSplinePoints();

			TArray<FVector> SplinePositions;
			for (const auto& Trans : InTransforms)
			{
				SplineComp->AddSplinePointLocation(Trans.GetLocation(), SplineComp->GetNumberOfSplinePoints());
				SplinePositions.Add(Trans.GetLocation());
			}


			float SrcSplineLength = SplineComp->GetSplineLength();
			for (int i = 0; i < SplineComp->GetNumberOfSplinePoints(); i++)
			{
				FVector Pos = SplineComp->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
				float Dist = SrcSplineComp->GetDistanceAlongSplineAtLocation(Pos,ESplineCoordinateSpace::World);
				float Ratio = Dist / SrcSplineLength;
				FVector Tangent = SrcSplineComp->GetTangentAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World);
				if (Ratio > 0)
				{
					Tangent *= (1- Ratio);
				}
				SplineComp->SetTangentAtSplinePoint(i, Tangent, ESplineCoordinateSpace::World);
				SplineComp->SetSplinePointType(i,ESplinePointType::Curve);
			}
			SplineComp->SetSplineWorldPoints(SplinePositions);
			NewSplineActor->SetSplineComponent(SplineComp);
			//NewSplineActor->RefreshExtraMesh();
			NewSplineActor->RefreshDynamicRoadMesh();
			if (SetDefultPath)
			{
				NewSplineActor->SetFolderPath(SCENE_ROAD_PATH);
			}

			return NewSplineActor;
		}
	}
	return nullptr;
}

float MoKuSplineEditorUtils::GetAssetDimensions(UStaticMesh* InStaticMesh, EAssetDimensionAxis InAxis)
{
	if (InStaticMesh)
	{
		FVector Dimensions = InStaticMesh->GetBoundingBox().GetSize();
		if (InAxis == EAssetDimensionAxis::X)
		{
			return Dimensions.X;

		}
		if (InAxis == EAssetDimensionAxis::Y)
		{
			return Dimensions.Y;
		}
		if (InAxis == EAssetDimensionAxis::Z)
		{
			return Dimensions.Z;

		}

	}
	return 0.0f;
};

void MoKuSplineEditorUtils::SplitMoKuSplineActor(AMoKuEditBaseActor* InSplineActor, float SplitRatio, UStaticMesh* InCrossRoad, bool bIsInvertCross)
{
	if (!InSplineActor)return;
	if (InSplineActor->IsA<AMoKuEditSplineActor>())
	{
		AMoKuEditSplineActor* SplineActor = Cast<AMoKuEditSplineActor>(InSplineActor);
		UMoKuEditSplinesComponent* SplineComponent = SplineActor->GetSplineComponent();
		TArray<FTransform>FirstHalfSplitTransform;
		TArray<FTransform>EndHalfSplitTransform;
		SplitSpline(SplineComponent, SplitRatio, FirstHalfSplitTransform, EndHalfSplitTransform);
		UStaticMesh* ConnectMesh = InCrossRoad;
		FVector InSertTangent = SplineComponent->GetTangentAtDistanceAlongSpline(SplitRatio, ESplineCoordinateSpace::World);
		FVector InSertPostion = EndHalfSplitTransform[0].GetLocation();
		FRotator Rotator = InSertTangent.GetSafeNormal().Rotation();
		AMoKuEditIntersectionActor* IntersectionActor = SplineActor->GetWorld()->SpawnActor<AMoKuEditIntersectionActor>(InSertPostion, Rotator);

		if (IntersectionActor)
		{
			FTransform IntersectionTransform;

			if (bIsInvertCross)
			{
				IntersectionTransform.SetScale3D(FVector(1.0,-1.0f,1.0f));
			}
			IntersectionTransform.SetLocation(InSertPostion);
			IntersectionTransform.SetRotation(Rotator.Quaternion());
			//IntersectionActor->StaticMeshAsset->SetStaticMesh(ConnectMesh);
			IntersectionActor->SetMeshTransform(IntersectionTransform);
			IntersectionActor->OnInitGizmo().ExecuteIfBound(IntersectionActor);


			float StartValue = SplitRatio - 2500.0f  - 100.0f;
			float EndValue = SplitRatio + 2500.0f  + 100.0f;

			StartValue = FMath::Max(0, StartValue);
			float SplineLength = SplineComponent->GetSplineLength();
			EndValue = FMath::Min(EndValue, SplineLength);


			FVector A = SplineComponent->GetLocationAtDistanceAlongSpline(StartValue, ESplineCoordinateSpace::World);
			FVector C = SplineComponent->GetLocationAtDistanceAlongSpline(EndValue, ESplineCoordinateSpace::World);
	

			FVector TangentA = SplineComponent->GetTangentAtDistanceAlongSpline(EndValue, ESplineCoordinateSpace::World);

			float  EndRatio = EndValue / SplineLength;

			TangentA = TangentA *(1 - EndRatio);

			FVector A0 = SplineComponent->GetTangentAtDistanceAlongSpline(SplitRatio, ESplineCoordinateSpace::World);

			//SplineComponent->GetTangentAtDistanceAlongSp
			//EndValue

			//A0 = A0.GetSafeNormal().Cross(FVector(0, 0, 1));
			//FVector EndPosition = InSertPostion+(4000) * A0.GetSafeNormal() ;

			UMoKuEditSplinesComponent* NewSpline = NewObject<UMoKuEditSplinesComponent>(IntersectionActor);

			// 设置必要属性
			NewSpline->SetMobility(EComponentMobility::Movable);
			NewSpline->CreationMethod = EComponentCreationMethod::Instance;

			// 附加到Actor
			NewSpline->AttachToComponent(IntersectionActor->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
			NewSpline->RegisterComponent();
			NewSpline->SetLocationAtSplinePoint(0,InSertPostion, ESplineCoordinateSpace::World);
			//NewSpline->SetLocationAtSplinePoint(1, EndPosition, ESplineCoordinateSpace::World);
			// 添加到数组
			IntersectionActor->SplineComps.Add(NewSpline);

			IntersectionActor->SplineComp->SetLocationAtSplinePoint(0, A, ESplineCoordinateSpace::Type::World);
			IntersectionActor->SplineComp->SetLocationAtSplinePoint(1, InSertPostion, ESplineCoordinateSpace::Type::World);
			IntersectionActor->SplineComp->AddSplinePoint(C, ESplineCoordinateSpace::Type::World);
			IntersectionActor->SplineComps.Add(IntersectionActor->SplineComp);

		}


	/*	TArray<FComponentSocketDescription> SocketDescriptions;
		IntersectionActor->StaticMeshAsset->QuerySupportedSockets(SocketDescriptions);*/


		/*for (const FComponentSocketDescription& SocketDesc : SocketDescriptions)
		{
			FName SocketName = SocketDesc.Name;
			FVector Tangent(1, 0, 0);
			FTransform SocketTransform = IntersectionActor->StaticMeshAsset->GetSocketTransform(SocketName, ERelativeTransformSpace::RTS_World);
			FVector SocketPosition = SocketTransform.GetLocation();
			FVector Direction = SocketTransform.TransformVector(Tangent);
			FVector InterPos = SocketPosition + Direction.GetSafeNormal() * 10;

			if (SocketName.ToString().ToLower() == TEXT("back"))
			{
				if (FirstHalfSplitTransform.Num() >= 2)
				{
					for (int i = 0; i < FirstHalfSplitTransform.Num(); i++)
					{
						FVector Diff = FirstHalfSplitTransform[i].GetLocation() - SocketPosition;
						if (FVector::DotProduct(Direction, Diff.GetSafeNormal()) < -0.5)
						{
							FirstHalfSplitTransform.RemoveAt(i);
							i--;
						}
					}

					if (FirstHalfSplitTransform.Num()>0)
					{
						AMoKuEditSplineActor* FirstOfActor = MoKuSplineEditorUtils::CreateNewMokuSplineActor(InSplineActor, FirstHalfSplitTransform);
						FirstOfActor->EnableEndAssetMesh = false;
						UMoKuEditSplinesComponent* StartSplineComp = FirstOfActor->GetSplineComponent();
						StartSplineComp->AddSplinePointAtIndex(InterPos, StartSplineComp->GetNumberOfSplinePoints() - 1, ESplineCoordinateSpace::World);
						StartSplineComp->SetTangentAtSplinePoint(StartSplineComp->GetNumberOfSplinePoints() - 2, Direction, ESplineCoordinateSpace::World);
						FirstOfActor->SetSplineComponent(StartSplineComp);
						FirstOfActor->OnInitGizmo().ExecuteIfBound(FirstOfActor);
						FirstOfActor->RefreshExtraMesh();
						FirstOfActor->RefreshDynamicRoadMesh();

					}


				}
			}
			if (SocketName.ToString().ToLower() == TEXT("front"))
			{
				if (EndHalfSplitTransform.Num() >= 2)
				{
					for (int i = 0; i < EndHalfSplitTransform.Num(); i++)
					{
						FVector Diff = EndHalfSplitTransform[i].GetLocation() - SocketPosition;
						if (FVector::DotProduct(Direction, Diff.GetSafeNormal())<-0.5)
						{
							EndHalfSplitTransform.RemoveAt(i);
							i--;
						}
					}
					if (EndHalfSplitTransform.Num() > 0)
					{
						AMoKuEditSplineActor* EndofActor = MoKuSplineEditorUtils::CreateNewMokuSplineActor(InSplineActor, EndHalfSplitTransform);
						EndofActor->EnableStartAssetMesh = false;
						UMoKuEditSplinesComponent* EndOfplineComp = EndofActor->GetSplineComponent();
						EndOfplineComp->AddSplinePointAtIndex(SocketPosition, 0, ESplineCoordinateSpace::World);
						EndOfplineComp->AddSplinePointAtIndex(InterPos, 1, ESplineCoordinateSpace::World);
						EndOfplineComp->SetTangentAtSplinePoint(0, Direction, ESplineCoordinateSpace::World);
						EndOfplineComp->SetTangentAtSplinePoint(1, Direction, ESplineCoordinateSpace::World);
						EndofActor->SetSplineComponent(EndOfplineComp);
						EndofActor->OnInitGizmo().ExecuteIfBound(EndofActor);
						EndofActor->RefreshExtraMesh();
						EndofActor->RefreshDynamicRoadMesh();

					}

				}*/
			}

		//}
	//}
}

void MoKuSplineEditorUtils::OctreeSegmentsIntersect(FSplineRoadActorOctree Octree, AMoKuEditSplineActor* InSplineActor, TArray<FInteresectionPointInfo>& OutIntersectionInfo,TArray<FBox>& OutDebugBox)
{
	if (!InSplineActor) return;
	TSet<UMoKuEditSplinesComponent*> CurveList = {InSplineActor->SplineComponent,InSplineActor->LeftCurve,InSplineActor->RightCurve};
	for (UMoKuEditSplinesComponent* Curve : CurveList)
	{
		if (Curve)
		{
			const TArray<FMoKuSplineInterpPoint> ProcedualPoints = Curve->GetProceduralPoints();
			for (int32 i = 0; i < ProcedualPoints.Num(); i++)
			{
				FSplineSegmentOctree Segment(Curve, i);
				FVector StartPos = ProcedualPoints[i].StartPos;
				FVector EndPos = ProcedualPoints[i].EndPos;
				FBox Bound = Segment.GetBound();
				FVector2d SegStart1(StartPos.X, StartPos.Y);
				FVector2d SegEnd1(EndPos.X, EndPos.Y);

				OutDebugBox.Add(Bound);
				Octree.FindElementsWithBoundsTest(Bound, [&](const FSplineSegmentOctree& Element)
				{
					if (Curve == Element.SplineComp&&abs(Segment.Index-Element.Index)<=1)return;
					if (Curve->ParentActor == Element.SplineComp->ParentActor)return;
					if (Element.SplineComp->Tag == Curve->Tag&& Curve->Tag!=TEXT("MainRoad"))return;
					FVector2D IntersectionPoint;
					const TArray<FMoKuSplineInterpPoint>& ProcedualPoints0 = Element.SplineComp->GetProceduralPoints();
					FVector2d SegStart2(ProcedualPoints0[Element.Index].StartPos.X, ProcedualPoints0[Element.Index].StartPos.Y);
					FVector2d SegEnd2(ProcedualPoints0[Element.Index].EndPos.X, ProcedualPoints0[Element.Index].EndPos.Y);
					double OutSeg1Intersection;
					double OutSeg2Intersection;
					if (DoSegmentsIntersect(SegStart1, SegEnd1, SegStart2, SegEnd2, IntersectionPoint,OutSeg1Intersection, OutSeg2Intersection))
					{
						FVector  OutIntersectionPoint(ForceInitToZero);
						OutIntersectionPoint.Z = StartPos.Z;
						OutIntersectionPoint.X = IntersectionPoint.X; 
						OutIntersectionPoint.Y = IntersectionPoint.Y;
						FInteresectionPointInfo InteresectionPointInfo;
						float S_Dist = Curve->GetDistanceAlongSplineAtLocation(StartPos, ESplineCoordinateSpace::World);
						float E_Dist = Curve->GetDistanceAlongSplineAtLocation(EndPos, ESplineCoordinateSpace::World);
						double Dist0 = FMath::Lerp(S_Dist, E_Dist, OutSeg1Intersection);


						float S_Dist1 = Element.SplineComp->GetDistanceAlongSplineAtLocation(ProcedualPoints0[Element.Index].StartPos, ESplineCoordinateSpace::World);
						float E_Dist1 = Element.SplineComp->GetDistanceAlongSplineAtLocation(ProcedualPoints0[Element.Index].EndPos, ESplineCoordinateSpace::World);
						double Dist1 = FMath::Lerp(S_Dist1, E_Dist1, OutSeg2Intersection);

						InteresectionPointInfo.IntersectPoint = Curve->GetLocationAtDistanceAlongSpline(Dist0, ESplineCoordinateSpace::World);
						InteresectionPointInfo.Dist = {Dist0,Dist1};
						InteresectionPointInfo.Spline = { Curve,Element.SplineComp };
						InteresectionPointInfo.IntersectRoadList = { InSplineActor,Element.SplineComp->ParentActor };

						if (Element.SplineComp->Tag == Curve->Tag&& Curve->Tag == TEXT("MainRoad"))
						{
							InteresectionPointInfo.Desc = TEXT("Cross");
						}
						OutIntersectionInfo.Add(InteresectionPointInfo);

					}
				});
			}

		}
	}

}

 bool MoKuSplineEditorUtils::DoSegmentsIntersect(const FVector2D& Segment1Start, const FVector2D& Segment1End, const FVector2D& Segment2Start, const FVector2D& Segment2End, FVector2D& OutIntersectionPoint,double& OutSeg1Intersection, double& OutSeg2Intersection)
{
	const FVector2D Segment1Dir = Segment1End - Segment1Start;
	const FVector2D Segment2Dir = Segment2End - Segment2Start;

	const double Determinant = FVector2D::CrossProduct(Segment1Dir, Segment2Dir);
	if (!FMath::IsNearlyZero(Determinant))
	{
		const FVector2D SegmentStartDelta = Segment2Start - Segment1Start;
		const double OneOverDet = 1.0 / Determinant;
		const double Seg1Intersection = FVector2D::CrossProduct(SegmentStartDelta, Segment2Dir) * OneOverDet;
		const double Seg2Intersection = FVector2D::CrossProduct(SegmentStartDelta, Segment1Dir) * OneOverDet;

		if (Seg1Intersection > -SMALL_NUMBER && Seg1Intersection < 1.0 + SMALL_NUMBER && Seg2Intersection > - SMALL_NUMBER && Seg2Intersection < 1.0 + SMALL_NUMBER)
		{
			OutIntersectionPoint = Segment1Start + Segment1Dir * Seg1Intersection;
			OutSeg1Intersection = Seg1Intersection;
			OutSeg2Intersection = Seg2Intersection;
			return true;

		}
	}

	return false;
}

 AMoKuEditIntersectionActor* MoKuSplineEditorUtils::SplitSplineActorSegment(AMoKuEditBaseActor* InSplineActor, float SplitRatio, bool bIsInvertCross, FEditSplineIntersectInfo IntersectedActorInfo)
{
	if (!InSplineActor)return nullptr;
	if (InSplineActor->IsA<AMoKuEditSplineActor>())
	{
		AMoKuEditSplineActor* SplineActor = Cast<AMoKuEditSplineActor>(InSplineActor);
		UMoKuEditSplinesComponent* SplineComponent = SplineActor->GetSplineComponent();

		float SplineLength = SplineComponent->GetSplineLength();
		TArray<FTransform>FirstHalfSplitTransform;
		TArray<FTransform>EndHalfSplitTransform;

		TArray<FVector> IntersectedPoint;
		if (IntersectedActorInfo.State == EIntersectionState::Left)
		{
			SplineComponent = SplineActor->LeftCurve;
			IntersectedPoint = IntersectedActorInfo.LeftIntersectPositions;
		}
		else if(IntersectedActorInfo.State == EIntersectionState::Right)
		{
			SplineComponent = SplineActor->RightCurve;	
			IntersectedPoint = IntersectedActorInfo.RightIntersectPositions;
		}
		if (!SplineComponent)return nullptr;

		TArray<float> RatioList;
		for (auto& Point:IntersectedPoint)
		{
			float Ratio = SplineComponent->GetDistanceAlongSplineAtLocation(Point, ESplineCoordinateSpace::World);
			RatioList.Add(Ratio);
		}
		RatioList.Sort();
		SplineComponent = SplineActor->GetSplineComponent();
		//SplitSpline(SplineComponent, SplitRatio, FirstHalfSplitTransform, EndHalfSplitTransform);

		SplitSpline(SplineComponent, RatioList[0], RatioList[1], FirstHalfSplitTransform, EndHalfSplitTransform);

		FVector InsertTangent = SplineComponent->GetTangentAtDistanceAlongSpline(SplitRatio, ESplineCoordinateSpace::World);
		FVector InsertPostion = EndHalfSplitTransform[0].GetLocation();
		FRotator Rotator = InsertTangent.GetSafeNormal().Rotation();
		AMoKuEditIntersectionActor* IntersectionActor = SplineActor->GetWorld()->SpawnActor<AMoKuEditIntersectionActor>(IntersectedActorInfo.IntersectPosition, Rotator);
		//FCornerInfo Temp;
		//for (auto& Trans : FirstHalfSplitTransform)
		//{
		//	Temp.CornerPoints.Add(Trans.GetLocation());
		//}
		//for (auto& Trans : EndHalfSplitTransform)
		//{
		//	Temp.CornerPoints.Add(Trans.GetLocation());
		//}
		if (IntersectionActor)
		{
			IntersectionActor->SetFolderPath(SCENE_JUNCTION_PATH);
		}
		float StartValue = RatioList[0]-1000.0f;
		float EndValue = RatioList[1]+1000.0f;
		if (IntersectionActor)
		{
			FTransform IntersectionTransform;

			if (bIsInvertCross)
			{
				IntersectionTransform.SetScale3D(FVector(1.0, -1.0f, 1.0f));
			}
			IntersectionTransform.SetLocation(InsertPostion);
			IntersectionActor->SetMeshTransform(IntersectionTransform);
			//IntersectionActor->OnInitGizmo().ExecuteIfBound(IntersectionActor);

			

			StartValue = FMath::Max(0, StartValue);
			EndValue = FMath::Min(EndValue, SplineLength);
			

			// 修改过程中 切记更新
			FVector A = SplineComponent->GetLocationAtDistanceAlongSpline(StartValue, ESplineCoordinateSpace::World);
			FVector C = SplineComponent->GetLocationAtDistanceAlongSpline(EndValue, ESplineCoordinateSpace::World);

			IntersectionActor->SplineComp->ClearSplinePoints();
			IntersectionActor->SplineComp->AddSplinePointAtIndex(A, 0, ESplineCoordinateSpace::World);
			IntersectionActor->SplineComp->AddSplinePointAtIndex((A+C)/2, 1, ESplineCoordinateSpace::World);
			IntersectionActor->SplineComp->AddSplinePointAtIndex(C, 2, ESplineCoordinateSpace::World);
			float DistC = SplineComponent->GetDistanceAlongSplineAtLocation(C, ESplineCoordinateSpace::World);
			float DistA = SplineComponent->GetDistanceAlongSplineAtLocation(A, ESplineCoordinateSpace::World);

			if (FirstHalfSplitTransform.Num() >= 2)
			{
				for (int i = 0; i < FirstHalfSplitTransform.Num(); i++)
				{
					float RefA = SplineComponent->GetDistanceAlongSplineAtLocation(FirstHalfSplitTransform[i].GetLocation(), ESplineCoordinateSpace::World);
					if (RefA > DistA)
					{
						FirstHalfSplitTransform.RemoveAt(i);
						i--;
					}
				}
				if (FirstHalfSplitTransform.Num() > 0)
				{
					AMoKuEditSplineActor* FirstOfActor = MoKuSplineEditorUtils::CreateNewMokuSplineActor(SplineActor, FirstHalfSplitTransform);
					if (FirstOfActor)
					{
							FirstOfActor->EnableEndAssetMesh = false;
							UMoKuEditSplinesComponent* StartSplineComp = FirstOfActor->GetSplineComponent();
							StartSplineComp->AddSplinePointAtIndex(A, 0, ESplineCoordinateSpace::World);
							StartSplineComp->SetTangentsAtSplinePoint(
								0,
								FVector(0, 0, 0),
								StartSplineComp->GetLocationAtSplinePoint(1, ESplineCoordinateSpace::World) - A,
								ESplineCoordinateSpace::World);
							StartSplineComp->SetSplinePointType(0, ESplinePointType::Linear);
							//StartSplineComp->SetTangentsAtSplinePoint(
							//1,
							//FVector(0,0,0),
							//StartSplineComp->GetLocationAtSplinePoint(1, ESplineCoordinateSpace::World) - A,
							//ESplineCoordinateSpace::World);	
							FirstOfActor->SetSplineComponent(StartSplineComp);
							FirstOfActor->OnInitGizmo().ExecuteIfBound(FirstOfActor);
							//FirstOfActor->RefreshExtraMesh();
							FirstOfActor->RefreshDynamicRoadMesh();
							if (FirstOfActor->EndOfJunction.IsValid())
							{
								FirstOfActor->StartOfJunction = IntersectionActor;
							}
							else
							{
								FirstOfActor->EndOfJunction = IntersectionActor;
							}
							
							IntersectionActor->ConnectionActors.Add(FirstOfActor);
							FirstOfActor->SetFolderPath(SCENE_ROAD_PATH);
					}
				}
			}
			if (EndHalfSplitTransform.Num() >= 2)
			{
				for (int i = 0; i < EndHalfSplitTransform.Num(); i++)
				{
					float RefC = SplineComponent->GetDistanceAlongSplineAtLocation(EndHalfSplitTransform[i].GetLocation(), ESplineCoordinateSpace::World);
					if(RefC<DistC)
					{
						EndHalfSplitTransform.RemoveAt(i);

						i--;
					}
					
				}
				if (EndHalfSplitTransform.Num() > 0)
				{
					AMoKuEditSplineActor* EndOfActor = MoKuSplineEditorUtils::CreateNewMokuSplineActor(SplineActor, EndHalfSplitTransform);
					if (EndOfActor)
					{
						EndOfActor->EnableEndAssetMesh = false;
						UMoKuEditSplinesComponent* EndOfSplineComp = EndOfActor->GetSplineComponent();
						EndOfSplineComp->AddSplinePointAtIndex(C, 0, ESplineCoordinateSpace::World);

						EndOfSplineComp->SetTangentsAtSplinePoint(
							0,
							FVector(0, 0, 0),
							EndOfSplineComp->GetLocationAtSplinePoint(1, ESplineCoordinateSpace::World) - C,
							ESplineCoordinateSpace::World);
						EndOfSplineComp->SetSplinePointType(0, ESplinePointType::Linear);
						//EndOfSplineComp->SetTangentsAtSplinePoint(
						//	1,
						//	FVector(0, 0, 0),
						//	EndOfSplineComp->GetLocationAtSplinePoint(1, ESplineCoordinateSpace::World) - C,
						//	ESplineCoordinateSpace::World);
						EndOfActor->SetSplineComponent(EndOfSplineComp);
						EndOfActor->OnInitGizmo().ExecuteIfBound(EndOfActor);
						//FirstOfActor->RefreshExtraMesh();
						EndOfActor->RefreshDynamicRoadMesh();
						IntersectionActor->ConnectionActors.Add(EndOfActor);
						if (EndOfActor->EndOfJunction.IsValid())
						{
							EndOfActor->EndOfJunction = EndOfActor->StartOfJunction;
						}
						EndOfActor->StartOfJunction = IntersectionActor;
						EndOfActor->SetFolderPath(SCENE_ROAD_PATH);

					}
				}
			}



		}

		return IntersectionActor;
	}
	return nullptr;
}

bool MoKuSplineEditorUtils::DoSplineIntersect(USplineComponent* Spline1, USplineComponent* Spline2, FVector& OutIntersectionPoint)
{
	if (!Spline1 || !Spline2)return false;
	if(Spline1->GetNumberOfSplinePoints()<2)return false;
	// 遍历第一条曲线的线段
	for (int i = 0; i < Spline1->GetNumberOfSplinePoints()-1; i++)
	{
		FVector Start1 = Spline1->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
		FVector End1 = Spline1->GetLocationAtSplinePoint(i+1, ESplineCoordinateSpace::World);
		FVector2d SegStart1(Start1.X,Start1.Y);
		FVector2d SegEnd1(End1.X, End1.Y);
		int SplineCount = Spline2->GetNumberOfSplinePoints();

		if (SplineCount > 1)
		{
			for (int j = 0; j < Spline2->GetNumberOfSplinePoints() - 1; j++)
			{
				FVector Start2 = Spline2->GetLocationAtSplinePoint(j, ESplineCoordinateSpace::World);
				FVector End2 = Spline2->GetLocationAtSplinePoint(j+1, ESplineCoordinateSpace::World);
				FVector2d SegStart2(Start2.X, Start2.Y);
				FVector2d SegEnd2(End2.X, End2.Y);
				FVector2d Intersection;
				double  OutSeg1Intersection ;
				double OutSeg2Intersection;
				bool bIsIntersected = DoSegmentsIntersect(SegStart1, SegEnd1, SegStart2, SegEnd2, Intersection,OutSeg1Intersection, OutSeg2Intersection);
				if (bIsIntersected)
				{
					//三维方向还需修改有待检验
					OutIntersectionPoint.Z = Start1.Z;
					OutIntersectionPoint.X = Intersection.X;
					OutIntersectionPoint.Y = Intersection.Y;
					return true;
				}
			}

		}
	}

	return false;
}
TArray<FVector3d> MoKuSplineEditorUtils::BezierLineConvert(const TArray<FVector3d>& InBaseCurve, uint32 Segment)
{
	if (InBaseCurve.Num() < 3)return TArray<FVector3d>();
	TArray<FVector3d> OutCurve;
	for (int idx = 0; idx < InBaseCurve.Num(); idx++)
	{
		if(idx + 1 >= InBaseCurve.Num() - 1)break;
		FVector3d P0 = InBaseCurve[idx];	
		FVector3d P1=  InBaseCurve[idx+1];
		FVector3d P2 = InBaseCurve[idx+2];

		for (uint32 i = 0; i <= Segment; i++)
		{
			float t = float(i) / float(Segment);
			FVector3d  Lt = (1 - t) * P0 + P1 * t;

			FVector3d OutPut = P0*FMath::Pow(1 - t, 2) + P1 * (2 * (1 - t) * t) + P2 * FMath::Pow(t, 2);
			OutCurve.Add(OutPut);
		}
	}
	return OutCurve;
}
bool MoKuSplineEditorUtils::CreateSceneStructure()
{
#if WITH_EDITOR
	if (!GEditor) return false;
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) return false;
	FFolder RootFolder(FFolder::FRootObject(World->GetCurrentLevel()), SCENE_ROOT_PATH);
	UActorFolder* SceneRootFolder = FWorldPersistentFolders::GetActorFolder(RootFolder, World, true);
	if (SceneRootFolder)
	{		
		FFolder JunctionFolder(FFolder::FRootObject(World->GetCurrentLevel()),SCENE_JUNCTION_PATH);
		FFolder RoadFolder(FFolder::FRootObject(World->GetCurrentLevel()), SCENE_ROAD_PATH);
		FFolder GroundFolder(FFolder::FRootObject(World->GetCurrentLevel()), SCENE_LOT_PATH);

		UActorFolder* JunctionFolderActor = FWorldPersistentFolders::GetActorFolder(JunctionFolder, World, true);
		UActorFolder* RoadFolderActor = FWorldPersistentFolders::GetActorFolder(RoadFolder, World, true);
		UActorFolder* GroundFolderActor = FWorldPersistentFolders::GetActorFolder(GroundFolder, World, true);
		if (JunctionFolderActor && RoadFolderActor && GroundFolderActor) return true;
		GEditor->BroadcastLevelActorListChanged();
		return true;
	}
#endif
	return false;

}
TArray<AActor*> MoKuSplineEditorUtils::GetActorListByFolderPath(const FString& InFolderPath)
{
#if WITH_EDITOR
	TArray<AActor*> OutActors;
	if (!GEditor) return OutActors;
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World)
	{
		TArray<FName> Paths;
		Paths.Add(FName(InFolderPath));
		FActorFolders::GetActorsFromFolders(*World, Paths, OutActors);
	}
	return OutActors;
#endif
}
TArray<FVector> MoKuSplineEditorUtils::SortEdgePathByList(const TArray<TArray<FVector>>& InPostionList)
{
	FVector StartPos;
	TArray<TArray<FVector>> Mark;
	TArray<FVector> EdgePath;
	if (InPostionList.Num() > 0)
	{
		if (InPostionList[0].Num() > 1)
		{
			StartPos = InPostionList[0][0];
		}
		TArray<FVector>Temp = InPostionList[0];
		while (Mark.Num() < InPostionList.Num())
		{
			int idx = 0;
			for (const auto& Item : InPostionList)
			{
				if (Temp == Item)continue;
				FVector P0 = Item[0];
				FVector P1 = Item.Last();
				float DistA = FVector::Dist(StartPos, P0);
				float DistB = FVector::Dist(StartPos, P1);
				if (DistA <= 50)
				{
					StartPos = P1;
					Mark.Add(Item);
					for (int i = 0; i < Item.Num(); i++)
					{
						if (!EdgePath.Contains(Item[i]))
						{
							EdgePath.Add(Item[i]);
						}

					}
					Temp = Item;
				}
				else if (DistB <= 50)
				{
					StartPos = P0;
					Mark.Add(Item);
					for (int i = Item.Num()- 1; i >= 0; i--)
					{
						if (!EdgePath.Contains(Item[i]))
						{
							EdgePath.Add(Item[i]);
						}

					}
					Temp = Item;
				}
				idx++;
			}
		}

	}

	return EdgePath;
}
UDynamicMeshComponent* MoKuSplineEditorUtils::SplineMeshBuilder(const TArray<FVector>& InEdgePath)
{
	FTriangulateCurvesOp TriangulateCurvesOp;
	TriangulateCurvesOp.Thickness = 0.0f;
	TriangulateCurvesOp.FlattenMethod = EFlattenCurveMethod::DoNotFlatten;
	TriangulateCurvesOp.CurveOffset = 0.0f;
	TriangulateCurvesOp.OffsetOpenMethod = EOffsetOpenCurvesMethod::TreatAsClosed;
	TriangulateCurvesOp.OffsetJoinMethod = EOffsetJoinMethod::Square;
	TriangulateCurvesOp.OpenEndShape = EOpenCurveEndShapes::Square;
	TriangulateCurvesOp.MiterLimit = 1.0;
	TriangulateCurvesOp.bFlipResult = false;
	TriangulateCurvesOp.OffsetClosedMethod = EOffsetClosedCurvesMethod::DoNotOffset;
	FTransform TransfromInfo;

	FVector SumValue;
	for (int i = 0; i < InEdgePath.Num(); i++)
	{
		SumValue += InEdgePath[i];
	}

	SumValue /= InEdgePath.Num();

	TransfromInfo.SetTranslation(SumValue);

	TriangulateCurvesOp.AddWorldCurve(InEdgePath, true, TransfromInfo);

	TriangulateCurvesOp.CalculateResult(nullptr);
	TUniquePtr<FDynamicMesh3>ResultMesh = TriangulateCurvesOp.ExtractResult();
	if (!ResultMesh || ResultMesh->TriangleCount() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("TriangulationFailed"));
	}

	
	FDynamicMesh3* DynamicMeshPtr = ResultMesh.Release();
	FVector3d Normal = DynamicMeshPtr->GetTriNormal(0);
	if (Normal.Z < 0)
	{
		DynamicMeshPtr->ReverseOrientation(true);
		FMeshNormals::InitializeMeshToPerTriangleNormals(DynamicMeshPtr);
	}
	UDynamicMeshComponent* DynamicMeshComponent = NewObject<UDynamicMeshComponent>();
	UDynamicMesh* DynamicMesh = DynamicMeshComponent->GetDynamicMesh();


	DynamicMesh->SetMesh(MoveTemp(*DynamicMeshPtr));

	TUniquePtr<FGroupTopology> Topology = MakeUnique<FGroupTopology>(DynamicMesh->GetMeshPtr(), true);
	
	int32 Num = Topology->Edges.Num();
	TArray<int> Edges = Topology->Edges[0].Span.Edges;

	DynamicMeshComponent->SetWorldTransform(TransfromInfo);
	DynamicMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DynamicMeshComponent->GetBodySetup()->CollisionTraceFlag = CTF_UseSimpleAndComplex;
	DynamicMeshComponent->bEnableComplexCollision = true;
	DynamicMeshComponent->CollisionType = CTF_UseSimpleAndComplex;
	DynamicMeshComponent->UpdateCollision(false);
	DynamicMeshComponent->NotifyMeshUpdated();

	return DynamicMeshComponent;
}
void MoKuSplineEditorUtils::GetSplineInterpInfo(USplineComponent* InCurve, const FVector& IntersectionPos, float& OutClosestKey, float& OutDist, float& OutRatio)
{
	if (!InCurve)return;
	FVector Location = InCurve->FindLocationClosestToWorldLocation(IntersectionPos, ESplineCoordinateSpace::World);
	float Closestkey = InCurve->FindInputKeyClosestToWorldLocation(IntersectionPos);
	float ClosestDist = InCurve->GetDistanceAlongSplineAtLocation(Location, ESplineCoordinateSpace::World);
	//float ClosestDist = InCurve->GetDistanceAlongSplineAtSplineInputKey(Closestkey);
	float SplineLength = InCurve->GetSplineLength();
	float Ratio = ClosestDist / SplineLength;

	OutClosestKey = Closestkey;
	OutDist = ClosestDist;
	OutRatio = Ratio;
}

FCornerInfo MoKuSplineEditorUtils::CutJunctionCornerInfo(USplineComponent* InCurve, TArray<FVector> IntersectionPosList,uint32 Segments)
{
	FCornerInfo CornerConnect;
	if (!InCurve) return CornerConnect;
	float  StartValue = 1000000;
	float  EndValue = 0;
	for (const auto& Pos : IntersectionPosList)
	{
		float InputKey = InCurve->FindInputKeyClosestToWorldLocation(Pos);
		float Tmp = InCurve->GetDistanceAlongSplineAtSplineInputKey(InputKey);
		StartValue = FMath::Min(StartValue, Tmp);
		EndValue = FMath::Max(EndValue, Tmp);

	}

	float SampleRange = EndValue - StartValue;
	float  Step = SampleRange / float(Segments);
	FVector S1 = InCurve->GetLocationAtDistanceAlongSpline(StartValue, ESplineCoordinateSpace::World);

	CornerConnect.CornerPoints.Add(IntersectionPosList[0]);
	for (uint32 k = 0; k < Segments; k++)
	{
		float Sample = StartValue + float(k) * Step;
		FVector S0 = InCurve->GetLocationAtDistanceAlongSpline(Sample, ESplineCoordinateSpace::World);
		CornerConnect.CornerPoints.Add(S0);
	};
	CornerConnect.CornerPoints.Add(IntersectionPosList[1]);
	CornerConnect.Tag = ECornerTag::Corner;
	return CornerConnect;



}

void  MoKuSplineEditorUtils::CheckAndSetEditLayer(UEdMode* ModeIn, const FString& InLayerName)
{
	if (!ModeIn)return;

	UWorld* TargetWorld = ModeIn->GetWorld();
	ALandscape* TargetLandscape = nullptr;
	if (TargetWorld)
	{
		for (TActorIterator<ALandscape> LandscapeIterator(TargetWorld); LandscapeIterator; ++LandscapeIterator)
		{
			TargetLandscape = *LandscapeIterator;
			break;
		}
	}
	if (TargetLandscape)
	{
		int32 ExistingLayerIdx = TargetLandscape->GetLayerIndex(FName(InLayerName));
		if (ExistingLayerIdx != INDEX_NONE)
		{	
			return;
		}
		else
		{
			int32 LayerIdx = TargetLandscape->CreateLayer(FName(InLayerName), ULandscapeLayerInfoObject::StaticClass());
			if (LayerIdx == INDEX_NONE)
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to create layer '%s'!"), *InLayerName);
			}
		}

	}


}

void MoKuSplineEditorUtils::ConvertPointInfoData(const TArray<FInteresectionPointInfo>& IntersectionInfo, TMap<TObjectPtr<AMoKuEditBaseActor>, TMap<TObjectPtr<UMoKuEditSplinesComponent>, TArray<FActorInteresectionPointInfo>>>& OutActorInteresectionMapPointInfo)
{
	TMap<TObjectPtr<AMoKuEditBaseActor>, TArray<FActorInteresectionPointInfo>> ActorInteresectionMapPointInfo;
	for (const FInteresectionPointInfo& Info : IntersectionInfo)
	{
		int32 Index = 0;
		for (auto Road : Info.IntersectRoadList)
		{
			if (Road)
			{
				FActorInteresectionPointInfo Data;
				TArray<FActorInteresectionPointInfo>* PairValue = ActorInteresectionMapPointInfo.Find(Road);
				Data.IntersectPoint = Info.IntersectPoint;
				Data.Spline = Info.Spline[Index];
				Data.Dist = Info.Dist[Index];
				Data.Desc = Info.Desc;
				if (!PairValue)
				{
					ActorInteresectionMapPointInfo.Add(Road,{Data});
				}
				else
				{
					TArray<FActorInteresectionPointInfo> CurrentData= ActorInteresectionMapPointInfo[Road];

					if (!CurrentData.Contains(Data))
					{
						ActorInteresectionMapPointInfo[Road].Add(Data);
					}
				}
			}
			Index++;
		}
	}


	TArray<TObjectPtr<AMoKuEditBaseActor>> OutKeys;
	ActorInteresectionMapPointInfo.GetKeys(OutKeys);
	for (const auto& Key : OutKeys)
	{
		TMap<TObjectPtr<UMoKuEditSplinesComponent>, TArray<FActorInteresectionPointInfo>> SortInfo;
		TArray<FActorInteresectionPointInfo> InfoValue = ActorInteresectionMapPointInfo[Key];
		for (const auto& Info : InfoValue)
		{
			if (Info.Spline->GetOwner())continue;
			TArray<FActorInteresectionPointInfo>* InteresectPointInfo = SortInfo.Find(Info.Spline);
			if (!InteresectPointInfo)
			{
				SortInfo.Add(Info.Spline, { Info });
			}
			else
			{
				SortInfo[Info.Spline].Add(Info);
			}
		}
		for (auto& Pair : SortInfo)
		{
			TArray<FActorInteresectionPointInfo>& PointArray = Pair.Value;
			Algo::Sort(PointArray, [](const FActorInteresectionPointInfo& A, const FActorInteresectionPointInfo& B)
			{
				return A.Dist < B.Dist;
			});
		}
		OutActorInteresectionMapPointInfo.Add(Key, { SortInfo });
	}


}

void MoKuSplineEditorUtils::DrawHelpCanvasTileItem(FCanvas* Canvas, const FString& InText)
{
	TArray<FString> TextLines;
	InText.ParseIntoArray(TextLines, TEXT("\n"), false);
	UFont* DrawFont = GEngine->GetSmallFont();
	int32 MaxLineWidth = 0;
	int32 TextHeight = 0;
	for (const FString& Line : TextLines)
	{
		int32 CurrentLineHeight = 0;
		int32 CurrentLineWidth = 0;
		DrawFont->GetStringHeightAndWidth(Line, CurrentLineHeight, CurrentLineWidth);
		MaxLineWidth = FMath::Max(MaxLineWidth, CurrentLineWidth);
		TextHeight = CurrentLineHeight;
	}
	int32 TextWidth = MaxLineWidth;
	int32 TotalLines = TextLines.Num();
	float PaddingX = 12.0f;
	float PaddingY = 12.0f;

	FVector2D BackgroundSize(MaxLineWidth + (PaddingX * 2), TextHeight * TotalLines + (PaddingY * 2));
	FVector2D BackgroundPos(20.0f, 20.0f);
	FCanvasTileItem BackgroundItem(BackgroundPos, GWhiteTexture, BackgroundSize, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
	BackgroundItem.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(BackgroundItem);

	FVector2D TextPos(BackgroundPos.X + PaddingX, BackgroundPos.Y + PaddingY);
	FCanvasTextItem TextItem(TextPos, FText::FromString(InText), DrawFont, FLinearColor::White);
	TextItem.bOutlined = false;
	Canvas->DrawItem(TextItem);



}