#include "MoKuEditSplinesComponent.h"
#include "MoKuEditSplineActor.h"
#include "Async/ParallelFor.h"
#include "Async/Async.h"
#include "MoKuEditorUtils.h"
#include "LevelEditor.h"
#include "EditorModeManager.h"
#include "Tools/LegacyEdModeWidgetHelpers.h"
#define LOCTEXT_NAMESPACE "SplineComponent"



UMoKuEditSplinesComponent::UMoKuEditSplinesComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	//预览状态暂且未用到
	//MoKuSplinePreviewMeshInfo = new FMoKuSplinePreviewMeshInfo();

}

#if WITH_EDITOR
void UMoKuEditSplinesComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	

	FProperty* EditProperty = PropertyChangedEvent.Property;

	FName PropertyName = EditProperty->GetFName();
	//FString EditCategory = EditProperty->GetMetaData(TEXT("Category"));
	if (AMoKuEditSplineActor* AttachActor = Cast<AMoKuEditSplineActor>(GetOwner()))
	{
		AttachActor->RefreshDynamicRoadMesh();
		if (AttachActor->SceneManagement.IsValid())
		{
			AWorldSceneManagement* SceneManager = AttachActor->SceneManagement.Get();
			if (SceneManager)
			{
				// 调用 Manager 里现成的接口
				SceneManager->BlueprintRoadBodyChanged_Native(AttachActor);
			}
		}
	}
}

void UMoKuEditSplinesComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

}



#endif




void UMoKuEditSplinesComponent::PostLoad()
{
	Super::PostLoad();
}

void UMoKuEditSplinesComponent::UpdateProceduralPoints(const FMoKuSplineInterpPoint& InPoint)
{
	ProceduralPoints.Add(InPoint);
}
void UMoKuEditSplinesComponent::UpdateSplineMesh()
{

	if (LocalMeshComponents.Num() > 0)
	{
		for (auto& MeshComponent : LocalMeshComponents)
		{
			if (MeshComponent != nullptr)
			{
				MeshComponent->Modify();
				MeshComponent->DestroyComponent();
			}
		}
	}

	LocalMeshComponents.SetNumUninitialized(ProceduralPoints.Num());
	for (int32 Index = 0; Index < ProceduralPoints.Num(); ++Index)
	{
		if (ProceduralPoints[Index].Mesh)
		{
			TObjectPtr<USplineMeshComponent> SplineMesh = NewObject<USplineMeshComponent>();
			SplineMesh->SetStaticMesh(ProceduralPoints[Index].Mesh);
			SplineMesh->SetForwardAxis(ESplineMeshAxis::X, true);
			SplineMesh->CreationMethod = EComponentCreationMethod::UserConstructionScript;
			SplineMesh->SetMobility(EComponentMobility::Movable);
			SplineMesh->SetStartAndEnd(ProceduralPoints[Index].StartPos, ProceduralPoints[Index].StartTangent, ProceduralPoints[Index].EndPos, ProceduralPoints[Index].EndTangent, true);
			SplineMesh->SetCollisionEnabled(ECollisionEnabled::Type::QueryAndPhysics);
			SplineMesh->UpdateMesh();
			LocalMeshComponents[Index] = MoveTemp(SplineMesh);
			LocalMeshComponents[Index]->RegisterComponentWithWorld(GetWorld());
			LocalMeshComponents[Index]->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
			LocalMeshComponents[Index]->Modify();
		}
	}

	this->MarkRenderStateDirty();

}

void UMoKuEditSplinesComponent::AddSplinePointLocation(const FVector& InLocationPos,const int32 Index)
{
	FScopedTransaction Transaction(LOCTEXT("MoKuEdit_Add Point", "Add MoKu Spline Control Point"));
	Modify();
	AddSplinePointAtIndex(InLocationPos, Index,ESplineCoordinateSpace::World);

}

//暂用方案后续替换
void UMoKuEditSplinesComponent::DistributeSegmentToSpline(TArray<UStaticMesh*> InStaticMeshArray)
{

	AMoKuEditSplineActor* OwningActor = Cast<AMoKuEditSplineActor>(this->GetOwner());
	if (OwningActor)
	{
		//获取当前所在组件的点数以及Actor的参数单位
		int SplineCount = this->GetNumberOfSplinePoints();
		float CurrentSplineLength = GetSplineLength();
		TArray<FSplineMeshInfoDetails> CurrentSplineMeshInfoList = OwningActor->SplineMeshInfoList;
		int32 SplineMeshInfoCount = CurrentSplineMeshInfoList.Num();
		auto SelectInfo = [=](int32 Seed)->FSplineMeshInfoDetails
		{
			FRandomStream RandomStream;
			RandomStream.Initialize(Seed + OwningActor->SplineMeshSeed);
			float RandomFloat = RandomStream.FRandRange(0.0f, 1.0f);
			int32 SelectIdx = int32(float(SplineMeshInfoCount) * RandomFloat);
			UStaticMesh* Mesh = OwningActor->SplineMeshInfoList[SelectIdx].Mesh;
			if (Mesh)
			{
				return OwningActor->SplineMeshInfoList[SelectIdx];
			}

			return FSplineMeshInfoDetails();
		};


		auto SelectExtraMeshInfo = [](TArray<UStaticMesh*> InStaticMesh)->UStaticMesh*
		{
			UStaticMesh* Mesh = InStaticMesh[0];
			if (Mesh) return Mesh;
			return nullptr;
		};

		auto GetDimensionsX = [](UStaticMesh* Mesh)->float
		{
			if (Mesh)
			{
				FVector Dimensions = Mesh->GetBoundingBox().GetSize();
				return Dimensions.X;
			}
			return 0.0f;
		};

		int32 SegmentCount = 0;
		float SumLength = 0;

		TArray<FSplineMeshInfoDetails> PlaceMeshList;
		if (SplineMeshInfoCount > 0)
		{
			int32 SelectIndex = 0;
			while (SumLength < CurrentSplineLength)
			{
				SelectIndex++;
				FSplineMeshInfoDetails SplineMeshInfo = SelectInfo(SelectIndex);
				// 目前此处未定义关于slot未空的情况 后续会补上
				//UE_LOG(LogTemp, Warning, TEXT("Xsize = %f"),GetDimensionsX(SplineMeshInfo.Mesh))
				if (!SplineMeshInfo.Mesh)
				{
					continue;
				}
				if (abs(CurrentSplineLength - SumLength) <= CurrentSplineLength * 0.1 && SegmentCount > 0)break;
				SegmentCount++;
				if (InStaticMeshArray.Num()==0)
				{
					SumLength += GetDimensionsX(SplineMeshInfo.Mesh);
					PlaceMeshList.Add(SplineMeshInfo);
				}
				else
				{		
					SumLength += GetDimensionsX(InStaticMeshArray[0]);
				}
			
			
			}

			float ScalarRatio = CurrentSplineLength / SumLength;
			float StartLength = 0;
			float EndLength = 0;
			ProceduralPoints.Empty();
			for (int i = 0; i < SegmentCount; i++)
			{
				UStaticMesh* StaticMesh;
				if (InStaticMeshArray.Num() > 0)
				{
					StaticMesh = InStaticMeshArray[0];
				}
				else
				{
					StaticMesh = PlaceMeshList[i].Mesh;
				}
				float SplineSegmentLength = GetDimensionsX(StaticMesh) * ScalarRatio;
				StartLength = EndLength;
				EndLength += SplineSegmentLength;
				StartLength = FMath::Min(StartLength, CurrentSplineLength);
				EndLength = FMath::Min(EndLength, CurrentSplineLength);

				double MidLength = (EndLength - StartLength) / 2 + StartLength;


				FVector StartPos = GetLocationAtDistanceAlongSpline(StartLength, ESplineCoordinateSpace::Local);
				FVector StartTangent = GetDirectionAtDistanceAlongSpline(StartLength, ESplineCoordinateSpace::Local);
				FVector EndPos = GetLocationAtDistanceAlongSpline(EndLength, ESplineCoordinateSpace::Local);
				FVector EndTangent = GetDirectionAtDistanceAlongSpline(EndLength, ESplineCoordinateSpace::Local);
				FVector MidPos = GetLocationAtDistanceAlongSpline(MidLength, ESplineCoordinateSpace::Local);
				
				FVector A = (StartPos - MidPos).GetSafeNormal();
				FVector B = (EndPos - MidPos).GetSafeNormal();

				float Result = A.Dot(B);
				float Angle = FMath::Acos(Result)*180/PI;
				FString LowStaticMeshPath;
				if (FMath::Abs(Angle - 180) < 2.5)
				{
					TArray<FString> Parts;
					StaticMesh->GetName().ParseIntoArray(Parts, TEXT("_"), true);
					
					Parts[Parts.Num()-2] = "L";

					for (int j = 0; j < Parts.Num(); ++j)
					{
						LowStaticMeshPath += Parts[j];
						if (j < Parts.Num() - 1)
						{
							LowStaticMeshPath += TEXT("_");
						}
					}
					FString Directory = FPaths::GetPath(StaticMesh->GetPathName());
					LowStaticMeshPath = Directory/LowStaticMeshPath;
					if (FPackageName::DoesPackageExist(LowStaticMeshPath))
					{
						StaticMesh = LoadObject<UStaticMesh>(nullptr, *LowStaticMeshPath);
					}

				}

				FTransform WorldTransform = GetTransformAtDistanceAlongSpline(MidLength, ESplineCoordinateSpace::World);

				//暂时未用上
				//FVector  StartUpVector = GetUpVectorAtDistanceAlongSpline(StartLength, ESplineCoordinateSpace::Local);
				//FRotator StartRotator = GetRotationAtDistanceAlongSpline(StartLength, ESplineCoordinateSpace::Local);
				//FVector  MidUpVector = GetUpVectorAtDistanceAlongSpline(MidLength, ESplineCoordinateSpace::Local);
				//FRotator MidRotator = GetRotationAtDistanceAlongSpline(MidLength, ESplineCoordinateSpace::Local);
				//FVector  EndUpVector = GetUpVectorAtDistanceAlongSpline(EndLength, ESplineCoordinateSpace::Local);
				//FRotator EndRotator = GetRotationAtDistanceAlongSpline(EndLength, ESplineCoordinateSpace::Local);



				StartLength = EndLength;
				FMoKuSplineInterpPoint MoKuSplineInterpPoint = FMoKuSplineInterpPoint(StartPos, EndPos, StartTangent * SplineSegmentLength, EndTangent * SplineSegmentLength, StaticMesh, WorldTransform);
				ProceduralPoints.Add(MoKuSplineInterpPoint);
			}

		}
	}

	UpdateSplineMesh();
	this->Modify();
		
}

void UMoKuEditSplinesComponent::DistributeSegmentByDist(float Distance)
{	
	ProceduralPoints.Empty();
	int PointCount = this->GetNumberOfSplinePoints();
	if (PointCount < 2)return;
	//float SplineLength = GetSplineLength();
	for (int i = 0; i < PointCount-1; i++)
	{
		float d1 = this->GetDistanceAlongSplineAtSplinePoint(i);
		float d2 = this->GetDistanceAlongSplineAtSplinePoint(i+1);
		float Diff = d2 - d1;
		int Segments = FMath::RoundToInt(Diff / Distance);
		Segments = 	FMath::Max(1,Segments);
		float AvgDist = Diff / float(Segments);
		for (int j = 0; j < Segments; j++)
		{
			float sum0 = d1 + j * AvgDist;
			float sum1 = d1 +( j +1)* AvgDist;
			FVector StartPos = GetLocationAtDistanceAlongSpline(sum0, ESplineCoordinateSpace::World);
			FVector EndPos = GetLocationAtDistanceAlongSpline(sum1, ESplineCoordinateSpace::World);
			FTransform  Transform = GetTransformAtDistanceAlongSpline(sum0, ESplineCoordinateSpace::World);
			FMoKuSplineInterpPoint SplineInterpPoint = FMoKuSplineInterpPoint(StartPos, EndPos);
			SplineInterpPoint.WorldTransform = Transform;
			SplineInterpPoint.StartTangent = GetTangentAtDistanceAlongSpline(sum0, ESplineCoordinateSpace::World);
			SplineInterpPoint.EndTangent = GetTangentAtDistanceAlongSpline(sum1, ESplineCoordinateSpace::World);

			ProceduralPoints.Add(SplineInterpPoint);
		}
		
	}

}


void UMoKuEditSplinesComponent::PostEditUndo()
{
	Super::PostEditUndo();
	if (GetOwner()->IsA<AMoKuEditSplineActor>())
	{
		AMoKuEditSplineActor* AttachActor = Cast<AMoKuEditSplineActor>(GetOwner());
		if (AttachActor->EnableEndAssetMesh || AttachActor->EnableStartAssetMesh)
		{
			AttachActor->AddCapAssetToSpline();
		}
	}
}

#undef LOCTEXT_NAMESPACE