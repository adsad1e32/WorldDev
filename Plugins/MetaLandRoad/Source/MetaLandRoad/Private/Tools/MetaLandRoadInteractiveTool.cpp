#include "MetaLandRoadInteractiveTool.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "ToolBuilderUtil.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "MoKuEditSplineActor.h"
#include "UnrealEdGlobals.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "SceneManagement.h"
#include "Components/SplineComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "Algo/Copy.h"
#include "MoKuEditSplinesComponent.h"
#include "BaseGizmos/TransformGizmoUtil.h"
#include "ToolSceneQueriesUtil.h"
#include "SocketMarkbleGizmo.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshSocket.h"
#include "MoKuEditorUtils.h"
#include "MoKuEditIntersectionActor.h"
#include "MoKuEditBaseActor.h"
#include "BaseBehaviors/MouseWheelBehavior.h"
#include "CurveOp.h"
#include "EditorModeManager.h"
#include "Tools/UEdMode.h"
#include "EditSplineRoadInfo.h"
#include "PrimitiveDrawingUtils.h"


#define LOCTEXT_NAMESPACE "UMetaLandRoadInteractiveTool"

UInteractiveTool* UMetaLandRoadInteractiveToolBuilder::BuildTool(const FToolBuilderState & SceneState) const
{
	UMetaLandRoadInteractiveTool* NewTool = NewObject<UMetaLandRoadInteractiveTool>(SceneState.ToolManager);
	NewTool->SetWorld(SceneState.World);
	NewTool->SetGizmoManager(SceneState.GizmoManager);
	return NewTool;
}
void UMetaLandRoadInteractiveTool::SetWorld(UWorld* World)
{
	check(World);
	this->TargetWorld = World;
}
void UMetaLandRoadInteractiveTool::Setup()
{
	UInteractiveTool::Setup();
	if (!TargetWorld)return;
	UEdMode* ParentMode = GLevelEditorModeTools().GetActiveScriptableMode(UMetaLandRoadEditorMode::EM_MetaLandRoadEditorModeId);
	if (UMetaLandRoadEditorMode* ParentEdMode = Cast<UMetaLandRoadEditorMode>(ParentMode))
	{
		SceneManagement = ParentEdMode->SceneManagement;
	}

	SetUpBehaviors();
	OutputProperties = NewObject<UInteractiveToolSettings>(this);
	AddToolPropertySource(OutputProperties);
	GEditor->OnLevelActorAdded().AddUObject(this, &UMetaLandRoadInteractiveTool::Test);
	for (TActorIterator<AMoKuEditBaseActor>It(TargetWorld); It; ++It)
	{
		AMoKuEditBaseActor* SplineActor = *It;
		TArray<FSocketGizmoTransformInfo> TransformInfo;
		TArray<USPlineMarkGizmo*> GizmoInfos;
		TArray<FSocketGizmoTransformInfo>GizmoTransformInfos= SplineActor->GetGizmoInfo();
		for (const auto& Info:GizmoTransformInfos)
		{
			USPlineMarkGizmo* SPlineMarkGizmo = GetToolManager()->GetPairedGizmoManager()->CreateGizmo<USPlineMarkGizmo>(TEXT("SocketGizmo"), FString(), this);
			SPlineMarkGizmo->CreateGizmoHandle(Info);
			SPlineMarkGizmo->SetSelectedObject(SplineActor);
			GizmoInfos.Add(SPlineMarkGizmo);
		}
		//GizmoListInfo.Add(SplineActor,GizmoInfos);
	}
	RoadOp = MakeUnique<FCurvesRoadOp>();
	//��ʱ����
	//CornerDataTable = LoadObject<UDataTable>(nullptr, *DataTablePath);
	//GetSelectedAsset();
}
void UMetaLandRoadInteractiveTool::SetUpBehaviors()
{
	UClickDragInputBehavior* MouseBehavior = NewObject<UClickDragInputBehavior>();
	UMouseHoverBehavior* HoverMouseBehavior = NewObject<UMouseHoverBehavior>();
	UMouseWheelInputBehavior* WheelMouseBehavior = NewObject<UMouseWheelInputBehavior>();
	USplineFinishedBehavior* SplineFinishedBehavior = NewObject<USplineFinishedBehavior>();
	MouseBehavior->Modifiers.RegisterModifier(CtrlModifierID, FInputDeviceState::IsCtrlKeyDown);
	MouseBehavior->Modifiers.RegisterModifier(ShiftModifierID, FInputDeviceState::IsShiftKeyDown);
	MouseBehavior->Initialize(this);
	HoverMouseBehavior->Initialize(this);
	WheelMouseBehavior->Initialize(this);
	SplineFinishedBehavior->Initialize(this);
	AddInputBehavior(SplineFinishedBehavior);
	AddInputBehavior(MouseBehavior);
	AddInputBehavior(HoverMouseBehavior);
	AddInputBehavior(WheelMouseBehavior);

}
void UMetaLandRoadInteractiveTool::Shutdown(EToolShutdownType ShutdownType)
{
	OnFinishedSplineEdit();
	GEditor->OnLevelActorAdded().RemoveAll(this);
	//for (const TPair<TObjectPtr<AMoKuEditBaseActor>, TArray<USPlineMarkGizmo*>>& KeyValuePair :GizmoListInfo)
	//{

	//	for (auto Item : KeyValuePair.Value)
	//	{
	//		GetToolManager()->GetPairedGizmoManager()->DestroyGizmo(Item);
	//	}
	//}
	CornerDataTable = nullptr;
	if (PreviewActor->IsValidLowLevel())
	{
		// ����Destroy����Actor
		PreviewActor->Destroy();
		PreviewActor = nullptr;
		bIsDraw = false;

	}
	bIsInvert = false;
	bIsInsertCross = false;
}
FInputRayHit UMetaLandRoadInteractiveTool::ShouldRespondToMouseWheel(const FInputDeviceRay& CurrentPos)
{
	FInputRayHit ToReturn;
	ToReturn.bHit = true;
	if (!bIsDraw)
	{
		ToReturn.bHit = false;
	
	}	
	return ToReturn;
}
void UMetaLandRoadInteractiveTool::OnUpdateModifierState(int ModifierID, bool bIsOn)
{
	if (ModifierID == CtrlModifierID)
	{
		bCtrlModifierOn = bIsOn;
	}
	if (ModifierID == ShiftModifierID)
	{
		bShiftModifierOn = bIsOn;
	}
}
FInputRayHit UMetaLandRoadInteractiveTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	FVector Temp;
	FInputRayHit Result = FindRayHit(PressPos.WorldRay, Temp);
	return Result;
}
FInputRayHit UMetaLandRoadInteractiveTool::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	return CanBeginClickDragSequence(PressPos);
}
void UMetaLandRoadInteractiveTool::OnClickPress(const FInputDeviceRay& PressPos)
{
	//������ɴ��������
	FScopedTransaction Transaction(LOCTEXT("BasicAddPoint", "Basic Add Point"));
	const FName TraceTagName = FName("WorldCollision");
	FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
	FHitResult Result;
	FCollisionQueryParams QueryCollisionParams(TraceTagName, true);
	bool bHitWorld = TargetWorld->LineTraceSingleByObjectType(Result, PressPos.WorldRay.Origin, PressPos.WorldRay.PointAt(HALF_WORLD_MAX), QueryParams, QueryCollisionParams);
	if (bCtrlModifierOn)
	{
		AMoKuEditSplineActor* ActiveSplineActor = Cast<AMoKuEditSplineActor>(ActiveActor);
		HitActor = Result.GetActor();
		FVector3d HitPos = Result.ImpactPoint;
		FRotator HitRot = FRotator::ZeroRotator;
		AMoKuEditIntersectionActor* JunctionActor;
		if (HitActor->IsA<AMoKuEditBaseActor>())
		{
			AMoKuEditSplineActor* SourceActor = Cast<AMoKuEditSplineActor>(HitActor);
			if (!bInsertJunction && !GetEditState())
			{
				ActiveActor = SourceActor;
				GetSplitIntersectionData(ActiveActor, ResultHitPos);
				PreIntersectData = IntersectData;
				PreIntersectData.PreHitActor = Cast<AMoKuEditBaseActor>(HitActor);
				bInsertJunction = true;
				if (bIsDraw)
				{
					UMoKuEditSplinesComponent* SplineComp = SourceActor->GetSplineComponent();
					FTransform InSertTransform = SplineComp->GetTransformAtDistanceAlongSpline(IntersectData.TValue, ESplineCoordinateSpace::World);
					AddSplinePoint(InSertTransform.GetLocation(), HitRot, false);
				}
				else
				{
					AddSplinePoint(ResultHitPos, HitRot, false);
				}
			}
			//���
			else if (!bInsertJunction && GetEditState())
			{
				GetSplitIntersectionData(SourceActor, ResultHitPos);
				if (bIsDraw)
				{
					FTransform InSertTransform = SourceActor->GetSplineComponent()->GetTransformAtDistanceAlongSpline(IntersectData.TValue, ESplineCoordinateSpace::World);
					AddSplinePoint(InSertTransform.GetLocation(), HitRot, false);
				}
				else
				{
					AddSplinePoint(IntersectData.HitTransform.GetLocation(), HitRot, false);
				}
				FEditSplineIntersectInfo IntersectedActorInfo;

				AMoKuEditBaseActor* CurrentHitActor;
				if (HitActor->IsA<AMoKuEditIntersectionActor>())
				{
					JunctionActor = Cast<AMoKuEditIntersectionActor>(HitActor);
					IntersectedActorInfo = JunctionActor->CheckIntersectionState(ActiveSplineActor);
					JunctionActor->ConnectionActors.Add(ActiveSplineActor);
					FVector IntersectPos = UpdateSplineRoad(JunctionActor, JunctionActor, true);
					//JunctionActor->UpdateSplineCornerInfo();
					JunctionActor->FillMeshActor();
					bIsDraw = false;
					//CornerInfo = JunctionActor->CornerInfo;
				}
				else
				{
					IntersectedActorInfo = SourceActor->CheckIntersectionState(ActiveSplineActor);
					JunctionActor = MoKuSplineEditorUtils::SplitSplineActorSegment(SourceActor, IntersectData.TValue, bIsInvert, IntersectedActorInfo);

					if (!JunctionActor)
					{
						return;
					}
					ActiveSplineActor->EndOfJunction = JunctionActor;
					CurrentHitActor = Cast<AMoKuEditBaseActor>(SourceActor);
					PreIntersectData.PreHitActor = SourceActor;
					UpdateSplineAndJunction(RoadOp.Get(), JunctionActor, CurrentHitActor, true);
					JunctionActor->FillMeshActor(false);
					//CornerInfo = JunctionActor->CornerInfo;
					CornerInfo.Add(JunctionActor->TestCornerInfo);
					if (PreIntersectData.PreHitActor->IsValidLowLevel())
					{
						PreIntersectData.PreHitActor->Destroy();
						PreIntersectData.PreHitActor = nullptr;
						bIsDraw = false;
					}
				}
				if (PreviewComponent)
				{
					if (PreviewComponent->GetOwner() == ActiveActor)
					{
						PreviewComponent->UnregisterComponent();
						PreviewComponent->DestroyComponent();
					}

				}
				GEditor->SelectNone(true, false);
				UpdateEditState(false);
			}
			else if (bInsertJunction && GetEditState())
			{
				GEditor->SelectNone(true, false);
				GEditor->SelectActor(ActiveActor, true, true, false, true);
				AddSplinePoint(HitPos, HitRot, false);
				FEditSplineIntersectInfo IntersectedActorInfo;
				IntersectedActorInfo = PreIntersectData.PreHitActor->CheckIntersectionState(ActiveSplineActor);
				JunctionActor = MoKuSplineEditorUtils::SplitSplineActorSegment(PreIntersectData.PreHitActor, PreIntersectData.TValue, bIsInvert, IntersectedActorInfo);
				AMoKuEditBaseActor* InHitActor = Cast<AMoKuEditBaseActor>(PreIntersectData.PreHitActor);
				if (!JunctionActor)
				{
					return;
				}


				//JunctionActor = Cast<AMoKuEditIntersectionActor>(HitActor);
				IntersectedActorInfo = JunctionActor->CheckIntersectionState(ActiveSplineActor);
				JunctionActor->ConnectionActors.Add(ActiveSplineActor);
				UpdateSplineAndJunction(RoadOp.Get(), JunctionActor, InHitActor, false);
				JunctionActor->SortEdgePath();



				//UpdateSplineAndJunction(RoadOp.Get(), JunctionActor, InHitActor);
				//JunctionActor->SortEdgePath();
				//JunctionActor->UpdateSplineCornerInfo();
				//JunctionActor->FillMeshActor();
				if (PreIntersectData.PreHitActor->IsValidLowLevel())
				{
					PreIntersectData.PreHitActor->Destroy();
					PreIntersectData.PreHitActor = nullptr;
					bIsDraw = false;
				}
				if (PreviewComponent)
				{
					if (PreviewComponent->GetOwner() == ActiveActor)
					{
						PreviewComponent->UnregisterComponent();
						PreviewComponent->DestroyComponent();
					}
				}

				////--------------------------------------------------------------------�ָ���-----------------------------------------------------------------------
				GetSplitIntersectionData(SourceActor, ResultHitPos);
				FEditSplineIntersectInfo NewIntersectedActorInfo;
				NewIntersectedActorInfo = SourceActor->CheckIntersectionState(ActiveSplineActor);
				JunctionActor = MoKuSplineEditorUtils::SplitSplineActorSegment(SourceActor, IntersectData.TValue, bIsInvert, NewIntersectedActorInfo);
				if (!JunctionActor)
				{
					return;
				}
				InHitActor = Cast<AMoKuEditBaseActor>(SourceActor);
				UpdateSplineAndJunction(RoadOp.Get(), JunctionActor, InHitActor, true);
				JunctionActor->SortEdgePath();
				PreIntersectData.PreHitActor = SourceActor;
				if (PreIntersectData.PreHitActor->IsValidLowLevel())
				{
					PreIntersectData.PreHitActor->Destroy();
					PreIntersectData.PreHitActor = nullptr;
					bIsDraw = false;
				}
				if (PreviewComponent)
				{
					if (PreviewComponent->GetOwner() == ActiveActor)
					{
						PreviewComponent->UnregisterComponent();
						PreviewComponent->DestroyComponent();
					}
				}
				GEditor->SelectNone(true, false);
				bInsertJunction = false;
				UpdateEditState(false);

			}
		}
		//��ʱ���
		else if (!HitActor->IsA<AMoKuEditSplineActor>() && bInsertJunction)
		{
			GEditor->SelectNone(true, false);
			AddSplinePoint(HitPos, HitRot, false);
			FEditSplineIntersectInfo IntersectedActorInfo;
			if (PreIntersectData.PreHitActor->IsA<AMoKuEditIntersectionActor>())
			{
				JunctionActor = Cast<AMoKuEditIntersectionActor>(PreIntersectData.PreHitActor);
				IntersectedActorInfo = JunctionActor->CheckIntersectionState(ActiveSplineActor);
				JunctionActor->ConnectionActors.Add(ActiveSplineActor);
				FVector IntersectPos = UpdateSplineRoad(JunctionActor, JunctionActor, false);
				JunctionActor->FillMeshActor();
				CornerInfo.Add(JunctionActor->TestCornerInfo);
				//CornerInfo = JunctionActor->CornerInfo;
				bInsertJunction = false;
				bIsDraw = false;
			}
			else
			{
				IntersectedActorInfo = PreIntersectData.PreHitActor->CheckIntersectionState(ActiveSplineActor);
				JunctionActor = MoKuSplineEditorUtils::SplitSplineActorSegment(PreIntersectData.PreHitActor, PreIntersectData.TValue, bIsInvert, IntersectedActorInfo);
				AMoKuEditBaseActor* InHitActor = Cast<AMoKuEditBaseActor>(PreIntersectData.PreHitActor);
				if (!JunctionActor)
				{
					return;
				}
				IntersectedActorInfo = JunctionActor->CheckIntersectionState(ActiveSplineActor);
				UpdateSplineAndJunction(RoadOp.Get(), JunctionActor, InHitActor, false);
				JunctionActor->FillMeshActor(false);

				CornerInfo.Add(JunctionActor->TestCornerInfo);
				bInsertJunction = false;
				if (PreIntersectData.PreHitActor->IsValidLowLevel())
				{
					// ����Destroy����Actor
					PreIntersectData.PreHitActor->Destroy();
					PreIntersectData.PreHitActor = nullptr;
					bIsDraw = false;
				}
			}

		}
		//��ʱ���
		else if (!HitActor->IsA<AMoKuEditSplineActor>() && !bInsertJunction)
		{
			AddSplinePoint(HitPos, HitRot, false);
			AMoKuEditSplineActor* SplineActor = Cast<AMoKuEditSplineActor>(ActiveActor);
			int32 PointIndex = SplineActor->GetSplineComponent()->GetNumberOfSplinePoints()-2;
			TArray<FVector> Test = SplineActor->UpdateInterpolateType(PointIndex);
		}
		if (0)
		{

			if (ActiveSplineActor)
			{
				UMoKuEditSplinesComponent* AciveEditComp = ActiveSplineActor->GetSplineComponent();
				if (SceneManagement)
				{
					//SceneManagement->SetActiveActor(ActiveSplineActor);
					SceneManagement->OctreeAddRoad(ActiveSplineActor);
					ActiveSplineActor->UpdateOctreeInfo();
					TArray<FInteresectionPointInfo> OutIntersectionInfo;
					OutDebugBox.Empty();
					MoKuSplineEditorUtils::OctreeSegmentsIntersect(SceneManagement->Octree, ActiveSplineActor, OutIntersectionInfo, OutDebugBox);

					for (auto& A : OutIntersectionInfo)
					{

						DebugCorner.Add(A.IntersectPoint);

					}
					TMap<TObjectPtr<AMoKuEditBaseActor>, TMap<TObjectPtr<UMoKuEditSplinesComponent>, TArray<FActorInteresectionPointInfo>>> OutActorInteresectionMapPointInfo;
					MoKuSplineEditorUtils::ConvertPointInfoData(OutIntersectionInfo, OutActorInteresectionMapPointInfo);
					TArray<TObjectPtr<AMoKuEditBaseActor>> OutKeys;
					OutActorInteresectionMapPointInfo.GetKeys(OutKeys);
					TMap<FVector, TObjectPtr<AMoKuEditIntersectionActor>> RecordJunctionAndActorMap;
					for (const auto& Key : OutKeys)
					{
						AMoKuEditSplineActor* SplineActor = Cast<AMoKuEditSplineActor>(Key);
						TMap<TObjectPtr<UMoKuEditSplinesComponent>, TArray<FActorInteresectionPointInfo>> CurvePointInfoMap = OutActorInteresectionMapPointInfo[Key];
						TArray<TObjectPtr<UMoKuEditSplinesComponent>>OutSplines;
						TMap<int32, TArray<FVector>> GatesInfo;
						CurvePointInfoMap.GetKeys(OutSplines);
						TObjectPtr<AMoKuEditIntersectionActor> CurrentJunctionActor;
						int32 Index = 0;
						for (const auto& Spline : OutSplines)
						{
							TArray<FActorInteresectionPointInfo> PointInfos = CurvePointInfoMap[Spline];
							for (int i = 0; i < PointInfos.Num(); i++)
							{
								FVector IntersectPos = PointInfos[i].IntersectPoint;
								if (PointInfos[i].Desc == TEXT("Cross"))
								{
									
									if (!RecordJunctionAndActorMap.Contains(IntersectPos))
									{
										AMoKuEditIntersectionActor* IntersectionActor = GetWorld()->SpawnActor<AMoKuEditIntersectionActor>(PointInfos[i].IntersectPoint, { 0,0,0 });
										if (IntersectionActor)
										{
											IntersectionActor->SetFolderPath(SCENE_JUNCTION_PATH);
											RecordJunctionAndActorMap.Add(IntersectPos, IntersectionActor);
										}
									}

									CurrentJunctionActor = *RecordJunctionAndActorMap.Find(IntersectPos);
									continue;
								}
								else
								{
									double SampleDist = PointInfos[i].Dist;
									if (Index % 2 == 0)
									{
										SampleDist -= 2250.0;
									}
									else
									{
										SampleDist += 2250.0;
									}
									FVector Pos = Spline->GetLocationAtDistanceAlongSpline(SampleDist, ESplineCoordinateSpace::World);
									if (!GatesInfo.Find(Index))
									{
										GatesInfo.Add(Index, { Pos });
									}
									else
									{
										GatesInfo[Index].Add(Pos);
									}
									Index++;
								}
							}
						}

						int32 CurIndex = 0;
						for (auto& Pair : GatesInfo)
						{

							//UE_LOG(LogTemp, Warning, TEXT("Key==%i"),Pair.Key)
							//if (Pair.Key % 2 == 0 && Pair.Key != GatesInfo.Num())
							//{
							//	CurIndex++;
							//	continue;
							//}
							FVector Sum(ForceInit);
							for (auto& Value : Pair.Value)
							{
								Sum += Value;
							}
							Sum /= Pair.Value.Num();

							if (AMoKuEditSplineActor* CurActor = Cast<AMoKuEditSplineActor>(Key))
							{
								TArray<FTransform> OutSplitSplinePosition;
								UMoKuEditSplinesComponent* EditComp = CurActor->GetSplineComponent();
								AMoKuEditSplineActor* NewSplineActor;
								float Dist = EditComp->GetDistanceAlongSplineAtLocation(Sum, ESplineCoordinateSpace::World);
								if (Pair.Key%2==0)
								{
									MoKuSplineEditorUtils::CutSplineFromRange(EditComp, 0, Dist, OutSplitSplinePosition);
									NewSplineActor = MoKuSplineEditorUtils::CreateNewMokuSplineActor(CurActor, OutSplitSplinePosition, true);
									SceneManagement->OctreeAddRoad(NewSplineActor);										
								}
								else
								{
									float EndDist = EditComp->GetSplineLength();
									FVector NextSum(ForceInit);
									if (CurIndex < GatesInfo.Num() - 1)
									{
										for (auto& Value : GatesInfo[CurIndex])
										{
											NextSum += Value;
										}
										NextSum /= GatesInfo[CurIndex].Num();
										EndDist = EditComp->GetDistanceAlongSplineAtLocation(NextSum, ESplineCoordinateSpace::World);
									}

									MoKuSplineEditorUtils::CutSplineFromRange(EditComp, Dist, EndDist, OutSplitSplinePosition);
									NewSplineActor = MoKuSplineEditorUtils::CreateNewMokuSplineActor(CurActor, OutSplitSplinePosition, true);
									SceneManagement->OctreeAddRoad(NewSplineActor);
								}
								if (CurrentJunctionActor)
								{
									CurrentJunctionActor->ConnectionActors.Add(NewSplineActor);
								}
							}
							CurIndex++;
						}
							
						
					}

					if (OutIntersectionInfo.Num() > 0)
					{
						for (auto& Item : OutIntersectionInfo)
						{
							for (auto& Road : Item.IntersectRoadList)
							{
								if (PreviewComponent)
								{
									if (PreviewComponent->GetOwner() == Road)
									{
										PreviewComponent->UnregisterComponent();
										PreviewComponent->DestroyComponent();
										GEditor->SelectNone(true, false);
										UpdateEditState(false);
									}
								}
								Road->Destroy();
							}
						}
					}

					OutIntersectPoints.Empty();
					for (auto& Pair : RecordJunctionAndActorMap)
					{
						TObjectPtr<AMoKuEditIntersectionActor> Junction = Pair.Value;
						Junction->UpdateSplineCornerInfo();
						Junction->FillMeshActor();
						for (auto& Info : Junction->CornerInfo)
						{
							CornerInfo.Add(Info);
						}
					}
				}
			}
			Modify();
		}

	}

}


void UMetaLandRoadInteractiveTool::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	Super::DrawHUD(Canvas, RenderAPI);
	if (Canvas == nullptr|| !bIsDrawHud) return;

	FString HelpText = TEXT("Left Click + Ctrl: Draw Roads/Intersections\n")
		TEXT("Alt: Precise Angle Snap\n")
		TEXT("Ctrl + Z: Undo\n")
		TEXT("Shift+F1: Toggle Help");
	MoKuSplineEditorUtils::DrawHelpCanvasTileItem(Canvas, HelpText);
	
}





void UMetaLandRoadInteractiveTool::UpdateSplineRoad(USplineComponent* IntersectCurve, USplineComponent* InEditingCurve, const FVector& InIntersectionPos,bool NeedInvert)
{
	if (!IntersectCurve|| !InEditingCurve)return;
	AActor* EditParentActor = InEditingCurve->GetAttachmentRootActor();
	AMoKuEditSplineActor* EditingSplineActor = Cast<AMoKuEditSplineActor>(EditParentActor);
	if (!EditingSplineActor)return;
	float EditingCurveClosestKey = 0;
	float EditingCurveClosestDist = 0;
	float EditingCurveClosestRatio = 0;
	MoKuSplineEditorUtils::GetSplineInterpInfo(InEditingCurve, InIntersectionPos, EditingCurveClosestKey, EditingCurveClosestDist, EditingCurveClosestRatio);

	for (int32 i = 0; i<InEditingCurve->GetNumberOfSplinePoints(); i++)
	{
		float PointSplineKey = float(i) / float(InEditingCurve->GetNumberOfSplinePoints() - 1);
		float RegionalRange = EditingCurveClosestRatio;
		if (NeedInvert)
		{
			PointSplineKey = 1 - PointSplineKey;
			RegionalRange = 1 - RegionalRange;
		}
		if (PointSplineKey <= RegionalRange)
		{
			float Offset = 1500;
			if (EditingCurveClosestRatio > 0.5)Offset *= -1;
			FVector3d FinalLocation = InEditingCurve->GetLocationAtDistanceAlongSpline(EditingCurveClosestDist + Offset, ESplineCoordinateSpace::World);
			InEditingCurve->SetLocationAtSplinePoint(i, FinalLocation, ESplineCoordinateSpace::World);
			if (NeedInvert)
			{
				FVector Tangent = InEditingCurve->GetTangentAtSplineInputKey(EditingCurveClosestKey, ESplineCoordinateSpace::World);
				FVector Direction = FVector::CrossProduct(Tangent.GetSafeNormal(), FVector::UpVector);
				Direction.Z = 0.0;
				FVector PrveLocation = InEditingCurve->GetLocationAtSplinePoint(i - 1, ESplineCoordinateSpace::World);

				InEditingCurve->SetTangentsAtSplinePoint(i - 1,
					InEditingCurve->GetArriveTangentAtSplinePoint(i - 1, ESplineCoordinateSpace::Local),
					FinalLocation - PrveLocation,
					ESplineCoordinateSpace::Local);
			}
			InEditingCurve->UpdateSpline();

			EditingSplineActor->SetSplineComponent(Cast<UMoKuEditSplinesComponent>(InEditingCurve));
			EditingSplineActor->RefreshDynamicRoadMesh();
			ActiveActor = EditingSplineActor;
		}
	}
}

void UMetaLandRoadInteractiveTool::UpdateRoadJunctionInfo
(
	FCurvesRoadOp* InRoadOp,
	AMoKuEditIntersectionActor*& InJunctionActor, 
	AMoKuEditBaseActor* IntersectActor, 
	USplineComponent* InEditingCurve,
	const FVector& InIntersectionPos,
	EIntersectionState IntersectionState
)
{
	if (!InRoadOp||!InJunctionActor || !IntersectActor || !InEditingCurve) return;
	AActor* EditParentActor =  InEditingCurve->GetAttachmentRootActor();
	AMoKuEditSplineActor* EditingSplineActor = Cast<AMoKuEditSplineActor>(EditParentActor);
	if (!EditingSplineActor)return;

	AMoKuEditSplineActor* IntersectSplineActor = Cast<AMoKuEditSplineActor>(IntersectActor);
	float EditingCurveClosestKey = 0;
	float EditingCurveClosestDist = 0;
	float EditingCurveClosestRatio = 0;
	MoKuSplineEditorUtils::GetSplineInterpInfo(InEditingCurve, InIntersectionPos, EditingCurveClosestKey, EditingCurveClosestDist, EditingCurveClosestRatio);
	TArray<FCornerInfo> SideEdges;
	TArray<FCornerInfo> CornerEdges;
	TArray<FVector> IntersectPos;
	InRoadOp->GetIntersectionInfo( InJunctionActor, EditingSplineActor, EditingCurveClosestRatio, CornerEdges, SideEdges, IntersectPos);
	for (auto SideEdge : SideEdges)
	{
		InJunctionActor->CornerInfo.Add(SideEdge);
	}
	if (IntersectionState==EIntersectionState::Right|| IntersectionState == EIntersectionState::Left)
	{
		USplineComponent* BoundSpline = nullptr;

		if (IntersectSplineActor)
		{
			IntersectSplineActor->RefreshDynamicRoadMesh();
			FSideRoadCurvePaths SideCurvePaths = IntersectSplineActor->GetSideCurvePaths();
			IntersectSplineActor->BuildSideSpline(SideCurvePaths.LeftCurvePath);
			IntersectSplineActor->BuildSideSpline(SideCurvePaths.RightCurvePath);
			if (IntersectionState == EIntersectionState::Right)
			{
				BoundSpline = IntersectSplineActor->LeftCurve;
			}
			if (IntersectionState == EIntersectionState::Left)
			{
				BoundSpline = IntersectSplineActor->RightCurve;
			}

			FCornerInfo SideCornerInfo = UpdateJunctionSideInfo(BoundSpline, IntersectPos, IntersectionState);
			InJunctionActor->CornerInfo.Add(SideCornerInfo);
		}
	}
	FIntersectionRoadInfo RoadInfo;
	RoadInfo.RoadActor = EditingSplineActor;
	InJunctionActor->IntersectionRoadInfos.Add(RoadInfo);
	InJunctionActor->ConnectionActors.Add(EditingSplineActor);
}

FCornerInfo UMetaLandRoadInteractiveTool::UpdateJunctionSideInfo(USplineComponent* IntersectComp, TArray<FVector> IntersectionPosList, EIntersectionState IntersectionState)
{
	FCornerInfo TmpCornerInfo;

	if (IntersectionPosList.Num() == 2)
	{
		float InputKey0 = IntersectComp->FindInputKeyClosestToWorldLocation(IntersectionPosList[0]);
		float InputKey1 = IntersectComp->FindInputKeyClosestToWorldLocation(IntersectionPosList[1]);
		float Tmp0 = IntersectComp->GetDistanceAlongSplineAtSplineInputKey(InputKey0);
		float Tmp1 = IntersectComp->GetDistanceAlongSplineAtSplineInputKey(InputKey1);
		if (Tmp0 > Tmp1)
		{
			IntersectionPosList.Swap(0, 1);
		}
	}
	if (IntersectComp)
	{
		TmpCornerInfo = MoKuSplineEditorUtils::CutJunctionCornerInfo(IntersectComp, IntersectionPosList, 20);
	}
	return TmpCornerInfo;
}
FVector UMetaLandRoadInteractiveTool::UpdateSplineRoad(AMoKuEditBaseActor* InHitActor, AMoKuEditIntersectionActor* InJunctionActor,bool NeedInvert)
{

	AMoKuEditSplineActor* SplineActor = Cast<AMoKuEditSplineActor>(ActiveActor);
	UMoKuEditSplinesComponent* EditorSplineComp = SplineActor->GetSplineComponent();
	FVector IntersectPos;
	FEditSplineIntersectInfo IntersectedActorInfo = SplineActor->CheckIntersectionState(InHitActor);
	float FinalValue = 0;
	float CurveLength = EditorSplineComp->GetSplineLength();

	if (IntersectedActorInfo.State != EIntersectionState::None)
	{
		float LeftValue = CurveLength;
		float RightValue = CurveLength;
		if (!NeedInvert)
		{
			LeftValue = 0;
			RightValue = 0;
		}
		if (IntersectedActorInfo.LeftIntersectPositions.Num() > 0)
		{

			for (const FVector& Position : IntersectedActorInfo.LeftIntersectPositions)
			{
				float Ratio = SplineActor->LeftCurve->GetDistanceAlongSplineAtLocation(Position, ESplineCoordinateSpace::World);
				if (NeedInvert)
				{
					LeftValue = FMath::Min(LeftValue, Ratio);

				}
				else
				{
					LeftValue = FMath::Max(LeftValue, Ratio);
				}
			}
		}
		if (IntersectedActorInfo.RightIntersectPositions.Num() > 0)
		{

			for (const FVector& Position : IntersectedActorInfo.RightIntersectPositions)
			{
				float Ratio = SplineActor->RightCurve->GetDistanceAlongSplineAtLocation(Position, ESplineCoordinateSpace::World);
				if (NeedInvert)
				{
					RightValue = FMath::Min(RightValue, Ratio);
				}
				else
				{
					RightValue = FMath::Max(RightValue, Ratio);
				}

			}
		}
		if (NeedInvert)
		{
			FinalValue = FMath::Min(LeftValue, RightValue);
		}
		else
		{
			FinalValue = FMath::Max(LeftValue, RightValue);
		}

	}
	if (NeedInvert)
	{

		FinalValue -= 1000.0f;
		IntersectPos = EditorSplineComp->GetLocationAtDistanceAlongSpline(FinalValue, ESplineCoordinateSpace::World);
		int32 PointIndex = EditorSplineComp->GetNumberOfSplinePoints() - 1;
		EditorSplineComp->SetLocationAtSplinePoint(PointIndex, IntersectPos, ESplineCoordinateSpace::World);
	}
	else
	{
		FinalValue += 1000.0f;
		IntersectPos = EditorSplineComp->GetLocationAtDistanceAlongSpline(FinalValue, ESplineCoordinateSpace::World);
		EditorSplineComp->SetLocationAtSplinePoint(0, IntersectPos, ESplineCoordinateSpace::World);

	}

	EditorSplineComp->UpdateSpline();

	SplineActor->SetSplineComponent(Cast<UMoKuEditSplinesComponent>(EditorSplineComp));
	SplineActor->RefreshDynamicRoadMesh();
	if (NeedInvert)
	{
		SplineActor->EndOfJunction = InJunctionActor;
	}
	else
	{
		SplineActor->StartOfJunction = InJunctionActor;
	}
	return IntersectPos;
}
void UMetaLandRoadInteractiveTool::UpdateSplineAndJunction
(
	FCurvesRoadOp* InRoadOp, 
	AMoKuEditIntersectionActor*& InJunctionActor,
	AMoKuEditBaseActor* InHitActor,
	bool NeedInvert
)
{
	if (!InRoadOp || !InJunctionActor || !InHitActor||!ActiveActor)return;

	AMoKuEditSplineActor* CurrentHitActor = Cast<AMoKuEditSplineActor>(InHitActor);
	if (!CurrentHitActor)return;
	USplineComponent* LeftCurve = CurrentHitActor->LeftCurve;
	USplineComponent* RightCurve = CurrentHitActor->RightCurve;
	AMoKuEditSplineActor* SplineActor = Cast<AMoKuEditSplineActor>(ActiveActor);

	UMoKuEditSplinesComponent* EditorSplineComp = SplineActor->GetSplineComponent();
	EIntersectionState IntersectState = InRoadOp->CheckRoadIntersectionState(EditorSplineComp, CurrentHitActor);
	USplineComponent* IntersectCurve = LeftCurve;
	if (IntersectState == EIntersectionState::Right)
	{
		IntersectCurve = RightCurve;
	}
	FVector IntersectPos = UpdateSplineRoad(InHitActor, InJunctionActor, NeedInvert);
	UpdateRoadJunctionInfo(InRoadOp, InJunctionActor, CurrentHitActor, EditorSplineComp, InJunctionActor->GetActorLocation(), IntersectState);

}

void UMetaLandRoadInteractiveTool::AddSplinePoint(const FVector& InHitPos, const FRotator& InHitRot, bool InteractiveGizmo,const FVector& InTangent,UInteractiveGizmo* InGizmoInfo)
{
	FScopedTransaction Transaction(LOCTEXT("MoKuEdit_Add Point", "Add MoKu Spline Control Point"));
	UMoKuEditSplinesComponent* SplineComponent = nullptr;
	FVector Tangent = InTangent;
	if (!ActiveActor|| !GetEditState())
	{
		GEditor->SelectNone(true, true);
		FActorSpawnParameters SpawnParams;
		SpawnParams.OverrideLevel = this->TargetWorld->PersistentLevel;
		SpawnParams.bNoFail = true;
		SpawnParams.ObjectFlags |= RF_Transactional;
		ActiveActor = this->TargetWorld->SpawnActor<AMoKuEditSplineActor>(InHitPos, InHitRot, SpawnParams);
		if (ActiveActor)
		{
			ActiveActor->SetFolderPath(SCENE_ROAD_PATH);
		}
		AMoKuEditSplineActor* SplineActor = Cast<AMoKuEditSplineActor>(ActiveActor);
		SplineActor->RoadModeType = OutputProperties->RoadModeType;
		SplineComponent = SplineActor->GetSplineComponent();
		SplineComponent->SetVisibility(false);
		UpdateEditState(true);
		PreviewComponent = NewObject<UMoKuEditSplinesComponent>(ActiveActor, NAME_None, RF_Transactional);
		PreviewComponent->AttachToComponent(SplineActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		PreviewComponent->RegisterComponent();
		if (!InteractiveGizmo)
		{
			SplineComponent->RemoveSplinePoint(1);
		}
	}
	else
	{
		if (ActiveActor)
		{
			AMoKuEditSplineActor* SplineActor = Cast<AMoKuEditSplineActor>(ActiveActor);
			SplineComponent = SplineActor->GetSplineComponent();
			if (!InteractiveGizmo)
			{	
				if (StateOfEditor == EMoKuEditState::Forward)
				{
					SplineComponent->AddSplinePointLocation(InHitPos, 0);
				}
				if (StateOfEditor == EMoKuEditState::Defult)
				{
					SplineComponent->AddSplinePointLocation(InHitPos, SplineComponent->GetNumberOfSplinePoints());
				}
				SplineActor->SetSplineComponent(SplineComponent);
			}
			else
			{
				FVector InterPos = InHitPos + Tangent.GetSafeNormal()*50;
				SplineComponent->AddSplinePointLocation(InterPos, SplineComponent->GetNumberOfSplinePoints());
				//SplineComponent->SetTangentAtSplinePoint(SplineComponent->GetNumberOfSplinePoints(), -InTangent.GetSafeNormal(), ESplineCoordinateSpace::World);
				SplineComponent->AddSplinePointLocation(InHitPos, SplineComponent->GetNumberOfSplinePoints());
				//SplineComponent->SetTangentAtSplinePoint(SplineComponent->GetNumberOfSplinePoints(),-InTangent.GetSafeNormal(), ESplineCoordinateSpace::World);
				SplineActor->SetSplineComponent(SplineComponent);

			}
		}
		ActiveActor->IsInit = false;
		
	}
	RefreshGizmoInfo();
	GEditor->SelectActor(ActiveActor, true, true, false, true);
	//SplineComponent->DistributeSegmentToSpline();
	//ActiveActor->RefreshExtraMesh();
	ActiveActor->RefreshDynamicRoadMesh();
	GEditor->RedrawLevelEditingViewports();
	ActiveActor->Modify();
	Modify();

	FProperty* SplineCurvesProperty = FindFProperty<FProperty>(UMoKuEditSplinesComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(USplineComponent, SplineCurves));
	if (SplineCurvesProperty)
	{
		FPropertyChangedEvent PropertyChangedEvent(SplineCurvesProperty, EPropertyChangeType::ValueSet);

		// ֪ͨ�������
		SplineComponent->PostEditChangeProperty(PropertyChangedEvent);

		// ֪ͨӵ���������� Actor��Ҳ������� MoKuEditSplineActor��
		this->PostEditChangeProperty(PropertyChangedEvent);
	}




}
void UMetaLandRoadInteractiveTool::OnMouseWheelScrollUp(const FInputDeviceRay& CurrentPos)
{
	if (!CornerDataTable)return;
	TArray<FName> RowNames = CornerDataTable->GetRowNames();
	int32 TotalRows = RowNames.Num();
	ScrollIndex+=1;
	int SelectIndex = abs(ScrollIndex) % TotalRows;
	GetSelectedAsset(SelectIndex);
	PreviewActor->GetStaticMeshComponent()->SetStaticMesh(CrossRoad);
}
void UMetaLandRoadInteractiveTool::GetSelectedAsset(int Index)
{
	if (!CornerDataTable)return;
	TArray<FName> RowNames = CornerDataTable->GetRowNames();
	const UScriptStruct* DataTableStruct = CornerDataTable->GetRowStruct();
	uint8* Data = CornerDataTable->FindRowUnchecked(RowNames[Index]);
	if (FProperty* StaticMeshData = CornerDataTable->FindTableProperty(FName("RecommandAsset")))
	{
		uint8* AnimSlotData = StaticMeshData->ContainerPtrToValuePtr<uint8>(Data, 0);
		FObjectProperty* ObjectProp = CastField<FObjectProperty>(StaticMeshData);
		UObject* ObjectValue = ObjectProp->GetPropertyValue(AnimSlotData);
		if (ObjectValue)
		{
			UStaticMesh* Mesh = Cast<UStaticMesh>(ObjectValue);
			if (Mesh)
			{
				CrossRoad = Mesh;
			}


		}
	}

}
void UMetaLandRoadInteractiveTool::OnMouseWheelScrollDown(const FInputDeviceRay& CurrentPos)
{
	if (!CornerDataTable)return;
	TArray<FName> RowNames = CornerDataTable->GetRowNames();
	int32 TotalRows = RowNames.Num();
	ScrollIndex -= 1;
	int SelectIndex = abs(ScrollIndex) % TotalRows;
	GetSelectedAsset(SelectIndex);
	PreviewActor->GetStaticMeshComponent()->SetStaticMesh(CrossRoad);

}
void UMetaLandRoadInteractiveTool::GetSplitIntersectionData(AActor* InSplitActor,const FVector& InWorldPos)
{
	if (InSplitActor)
	{
		AMoKuEditSplineActor* SplineActor = Cast<AMoKuEditSplineActor>(InSplitActor);
		if (SplineActor)
		{
			UMoKuEditSplinesComponent* SplineComp = SplineActor->GetSplineComponent();

			check(SplineComp != nullptr);
			SplineComp->Modify();
			float SplineLength = SplineComp->GetSplineLength();
			if (AActor* Owner = SplineComp->GetOwner())
			{
				Owner->Modify();
			}

			float OutSquaredDistance = 0;
			float OutputKey = SplineComp->FindInputKeyClosestToWorldLocation(InWorldPos);
			float SampleDist = SplineComp->GetDistanceAlongSplineAtSplineInputKey(OutputKey);
			IntersectData.StartValue = SampleDist- IntersectData.EntryValue;
			IntersectData. EndValue = SampleDist + IntersectData.EntryValue;	
			IntersectData.StartValue = FMath::Clamp(IntersectData.StartValue,0, SplineLength);
			IntersectData.EndValue = FMath::Clamp(IntersectData.EndValue, 0, SplineLength);

			DrawOutSplinePos = SplineComp->GetLocationAtDistanceAlongSpline(SampleDist, ESplineCoordinateSpace::World);
			FVector InsertTangent = SplineComp->GetTangentAtDistanceAlongSpline(SampleDist, ESplineCoordinateSpace::World);
			OutSquaredDistance = FVector::Dist(DrawOutSplinePos,InWorldPos);
			if (OutSquaredDistance < 5000)
			{
				bIsDraw = true;
				IntersectData.TValue = SplineComp->GetDistanceAlongSplineAtLocation(DrawOutSplinePos, ESplineCoordinateSpace::World);
				//UE_LOG(LogTemp,Warning,TEXT("t==%f"),IntersectData.tvalue)
				//DrawOutSplinePos.Z += 10;
				//FRotator Rotator = InSertTangent.GetSafeNormal().Rotation();
				//FTransform IntersectionTransform;
				//UStaticMeshComponent* Comp = nullptr;
				//if (!PreviewActor)
				//{
				//	PreviewActor = SplineActor->GetWorld()->SpawnActor<AStaticMeshActor>(FVector(0,0,0),FRotator(0,0,0));
				//	Comp = PreviewActor->GetStaticMeshComponent();
				//	Comp->SetStaticMesh(CrossRoad);
				//	Comp->SetWorldLocation(DrawOutSplinePos);
				//	Comp->SetWorldRotation(Rotator);
				//}
				//else
				//{
				//	Comp = PreviewActor->GetStaticMeshComponent();
				//	Comp->SetWorldLocation(DrawOutSplinePos);
				//	Comp->SetWorldRotation(Rotator);
				//	if (bIsInvert)
				//	{
				//		Comp->SetWorldScale3D(FVector(1.0, -1.0, 1.0));
				//	}
				//	else
				//	{
				//		Comp->SetWorldScale3D(FVector(1.0, 1.0, 1.0));
				//	}
				//}
			}
			else
			{
				bIsDraw = false;
				IntersectData.TValue = 0;
				IntersectData.HitTransform.SetLocation(InWorldPos);
				//if (PreviewActor->IsValidLowLevel()|| PreviewActor!=nullptr)
				//{
				//	PreviewActor->Destroy();
				//	PreviewActor = nullptr;
				//	bIsInvert = false;
				//}
			}
			GEditor->RedrawLevelEditingViewports(true);
		}
		else
		{
			bIsDraw = false;
			IntersectData.TValue = 0;
			IntersectData.HitTransform.SetLocation(InWorldPos);
		}
	}
}
void UMetaLandRoadInteractiveTool::UpdateEditState(bool InEditing)
{
	bIsEditing = InEditing;
}
void UMetaLandRoadInteractiveTool::OnClickDrag(const FInputDeviceRay& DragPos)
{	
}
void UMetaLandRoadInteractiveTool::OnClickRelease(const FInputDeviceRay& ReleasePos)
{

	GEditor->EndTransaction();

}
void UMetaLandRoadInteractiveTool::OnTerminateDragSequence()
{

}
void UMetaLandRoadInteractiveTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	UE_LOG(LogTemp,Warning,TEXT("ParmChanged"))
}
FInputRayHit UMetaLandRoadInteractiveTool::FindRayHit(const FRay& WorldRay, FVector& HitPos)
{
	FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
	FHitResult Result;
	bool bHitWorld = TargetWorld->LineTraceSingleByObjectType(Result, WorldRay.Origin, WorldRay.PointAt(HALF_WORLD_MAX), QueryParams);
	if (bHitWorld)
	{
		HitPos = Result.ImpactPoint;
		return FInputRayHit(Result.Distance);
	}
	return FInputRayHit();
}
void UMetaLandRoadInteractiveTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	//if (bIsDraw&& ActiveActor)
	//{

	//		UMoKuEditSplinesComponent* Comp = RenderActor->GetSplineComponent();
	//		int32 NumberOfPoints = Comp->GetNumberOfSplinePoints();
	//		TArray<FVector>OutPosition;
	//		Comp->ConvertSplineToPolyLine(ESplineCoordinateSpace::World, 10, OutPosition);


	for (int32 i = 0; i < DebugOutIntersect.Num(); i++)
	{
		PDI->DrawPoint(
			DebugOutIntersect[i],
			FLinearColor(1.0f, 1.0f, 0.0f, 1.0f),
			15.0f,
			SDPG_World);
	}
	for (auto& Info : CornerInfo)
	{
		if (Info.CornerPoints.Num() > 1)
		{
			for (int32 i = 0; i < (Info.CornerPoints.Num()-1); i++)
			{
				PDI->DrawLine(Info.CornerPoints[i], Info.CornerPoints[i+1], FLinearColor(0.0f, 1.0f, 0.0f, 0.5f), SDPG_Foreground, 15.0f);
			}
			for (int32 i = 0; i < Info.CornerPoints.Num(); i++)
			{
				PDI->DrawPoint(
					Info.CornerPoints[i],
					FLinearColor(1.0f, 1.0f, 0.0f, 1.0f),
					5.0f,
					SDPG_World);
			}
		}

	}


	for (int32 i = 0; i < DebugCorner.Num(); ++i)
	{
		//FVector Location = Comp->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
		//FVector Tangent = Comp->GetTangentAtSplinePoint(i, ESplineCoordinateSpace::World);
		FVector S = DebugCorner[i];
		//UE_LOG(LogTemp,Warning,TEXT("S==%s"),*S.ToString())
		PDI->DrawPoint(
			S,
			FLinearColor(1.0f, 1.0f, 1.0f, 1.0f),
			15.0f,
			SDPG_World);
		//FVector F = DebugCorner[i + 1];
		//PDI->DrawLine(S, F, FLinearColor(0.0f, 1.0f, 0.0f, 0.5f), SDPG_Foreground, 35.0f);
	}
	for (int32 i = 0; i < DebugSideCorner.Num()/2; ++i)
	{
		//FVector Location = Comp->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
		//FVector Tangent = Comp->GetTangentAtSplinePoint(i, ESplineCoordinateSpace::World);
		FVector S = DebugSideCorner[i];
		PDI->DrawPoint(
			S,
			FLinearColor(1.0f, 0.0f, 0.0f, 1.0f),
			15.0f,
			SDPG_World);
		//FVector F = DebugCorner[i + 1];
		//PDI->DrawLine(S, F, FLinearColor(0.0f, 1.0f, 0.0f, 0.5f), SDPG_Foreground, 35.0f);
	}
	if (SceneManagement)
	{
		for (auto& Road : SceneManagement->EditRoadsList)
		{
			if (AMoKuEditSplineActor* DebugActiveActor = Cast<AMoKuEditSplineActor>(Road))
			{
				const TArray<FMoKuSplineInterpPoint>D = DebugActiveActor->LeftCurve->GetProceduralPoints();
				//	if (OutIntersectionPoints.Num() > 0)
				{
					for (auto& d : D)
					{

						FVector3d Diff = d.EndPos - d.StartPos;
						FVector3d Center = (d.StartPos + d.EndPos) / 2;
						//FVector3d Direction = Diff.GetSafeNormal();
						double Length = FVector::Distance(d.EndPos,d.StartPos) / 2;
						FVector HalfExtents(Length, 100.0f, 100);;
						FVector3d Direction = FMath::Lerp(d.StartTangent, d.EndTangent, 0.5);
						Direction = Direction.GetSafeNormal();
						FVector3d bTangent = FVector::CrossProduct(Direction, FVector::UpVector);

						//const  FRotator Rot = d.WorldTransform.Rotator();
						//FQuat Quat = Rot.Quaternion();
						//bTangent = Quat.RotateVector(bTangent);

						FVector S1 = d.StartPos - bTangent * 1000;
						//FVector S2 = d.EndPos + bTangent * 100;
						//FVector S3 = d.StartPos - bTangent * 100;
						FVector S4 = d.StartPos + Direction * 1000;
						FVector S3 = d.StartPos + FVector::UpVector * 1000;

						TArray<FVector> Vertices;
						const FVector Axes[3] = {
							Direction,  // ��ת���X��
							bTangent,  // ��ת���Y��
							FVector::UpVector  // ��ת���Z��
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
						FBox Bound = FBox(Vertices);

						//UE_LOG(LogTemp, Warning, TEXT("bTangent1111==%s"), *bTangent.ToString())
						//PDI->DrawLine(d.StartPos, S3, FLinearColor(1.0f, 0.0f, 0.0f, 0.5f), SDPG_Foreground, 15.0f);
						//PDI->DrawLine(d.StartPos, S4, FLinearColor(0.0f, 1.0f, 0.0f, 0.5f), SDPG_Foreground, 15.0f);
						//PDI->DrawLine(d.StartPos, S1, FLinearColor(0.0f, 0.0f, 1.0f, 0.5f), SDPG_Foreground, 15.0f);
						//PDI->DrawLine(d.EndPos, S2, FLinearColor(0.0f, 1.0f, 0.0f, 0.5f), SDPG_Foreground, 15.0f);
						//PDI->DrawLine(d.EndPos, S1, FLinearColor(0.0f, 1.0f, 0.0f, 0.5f), SDPG_Foreground, 15.0f);
						FMatrix M(Direction, bTangent,FVector::UpVector,FVector(0,0,0));
						//DrawWireBox(PDI, M, Bound, FColor(1, 0, 0), SDPG_World);
						//PDI->DrawPoint(
						//	d.StartPos,
						//	FLinearColor(1.0f, 0.0f, 0.0f, 1.0f),
						//	15.0f,
						//	SDPG_World);
					}
				}
			}
		}


	}


		if (HitActor)
		{
			if (!HitActor->IsA<AMoKuEditBaseActor>())
			{
				PDI->DrawPoint(
					ResultHitPos,
					FLinearColor(1.0f, 1.0f, 0.0f, 1.0f),
					15.0f,
					SDPG_World);
			}
			else
			{
				PDI->DrawPoint(
					ResultHitPos,
					FLinearColor(0.0f, 1.0f, 0.0f, 1.0f),
					15.0f,
					SDPG_World);
			}
		}
		//for (const auto& Bound : OutDebugBox)
		//{
		//	//DrawWireBox(PDI, Bound, FColor(1, 0, 0), SDPG_World);
		//}

		//for (auto Vertex : DebugCorner)
		//{
		//		PDI->DrawPoint(
		//			Vertex,
		//			FLinearColor(1.0f, 1.0f, 0.0f, 1.0f),
		//			5.0f,
		//			SDPG_Foreground);
		//}

			//}
			//AMoKuEditSplineActor* RenderActor = Cast<AMoKuEditSplineActor>(ActiveActor);
			//if (RenderActor)
			//{
			//	FSideRoadCurvePaths SideCurvePaths = RenderActor->GetSideCurvePaths();
			//	for (auto Vertex : RenderActor->EndConnection.ConnectEdge)
			//	{
			//		PDI->DrawPoint(
			//			Vertex,
			//			FLinearColor(1.0f, 0.0f, 0.0f, 1.0f),
			//			5.0f,
			//			SDPG_Foreground);

			//	}
			//	for (auto Vertex : RenderActor->StartConnection.ConnectEdge)
			//	{
			//		PDI->DrawPoint(
			//			Vertex,
			//			FLinearColor(1.0f, 0.0f, 0.0f, 1.0f),
			//			5.0f,
			//			SDPG_Foreground);

			//	}

			//}

	//}
}
void UMetaLandRoadInteractiveTool::InvertPreviewMesh()
{
	if (PreviewActor&& bIsInsertCross)
	{
		if (bIsInvert)
		{
			bIsInvert = false;
		}
		else
		{
			bIsInvert = true;
		}
	}

}
void UMetaLandRoadInteractiveTool::SwitchInsertCrossState()
{
	if (ActiveActor)
	{
		if (bIsInsertCross)
		{
			bIsInsertCross = false;
			bIsDraw = false;
			IntersectData.TValue = 0;
			//if (PreviewActor->IsValidLowLevel() || PreviewActor != nullptr)
			//{
			//	PreviewActor->Destroy();
			//	PreviewActor = nullptr;
			bIsInvert = false;
			//}
		}
		else
		{
			bIsInsertCross = true;
		}
	}

}
void UMetaLandRoadInteractiveTool::OnBeginHover(const FInputDeviceRay& DevicePos)
{

}
bool UMetaLandRoadInteractiveTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	const FName TraceTagName = FName("WorldCollision");
	FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
	FHitResult Result;
	FCollisionQueryParams QueryCollisionParams(TraceTagName, true);
	bool bHitWorld = GetWorld()->LineTraceSingleByObjectType(Result, DevicePos.WorldRay.Origin, DevicePos.WorldRay.PointAt(HALF_WORLD_MAX), QueryParams, QueryCollisionParams);
	AMoKuEditBaseActor* SplineActor = GetActiveActor();
	ResultHitPos = Result.ImpactPoint;

	if (!SplineActor)
	{
		HitActor = Result.GetActor();
		//return false;
	}
	else
	{
		AMoKuEditSplineActor* HoverActor = Cast<AMoKuEditSplineActor>(SplineActor);
		UMoKuEditSplinesComponent* SelecComponet = HoverActor->GetSplineComponent();
		if (HoverActor)
		{
			if (SelecComponet)
			{
				//QueryCollisionParams.AddIgnoredComponent(SelecComponet);
				if (PreviewActor)
				{
					QueryCollisionParams.AddIgnoredComponent(PreviewActor->GetStaticMeshComponent());
				}
			}
		}
		if (Result.GetActor() && bHitWorld)
		{
			HitActor = Result.GetActor();
			GetSplitIntersectionData(HitActor,ResultHitPos);
			if (GetEditState())
			{
				if (PreviewComponent)
				{
					FVector StartPos = SelecComponet->GetLocationAtSplinePoint(SelecComponet->GetNumberOfSplinePoints() - 1, ESplineCoordinateSpace::World);
					if (StateOfEditor == EMoKuEditState::Forward)StartPos = SelecComponet->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
					PreviewComponent->SetWorldLocation(StartPos);
					PreviewComponent->SetLocationAtSplinePoint(1, ResultHitPos, ESplineCoordinateSpace::World);
					PreviewComponent->UpdateSpline();
				}
			}
		}
	//	else
	//	{
	//		SplitSegment(HitPos);
	//	}
	//}	
	}
	return true;

}
void UMetaLandRoadInteractiveTool::OnFinishedSplineEdit()
{
	if(ActiveActor)
	{
		if (GetEditState())
		{
			GEditor->BeginTransaction(FText::FromString(TEXT("Finished The Curvel's Operation!")));
			AMoKuEditSplineActor* FinishActor = Cast<AMoKuEditSplineActor>(ActiveActor);
			if (FinishActor)
			{
				TArray<FVector> OutPoints;
				UMoKuEditSplinesComponent* Comp = FinishActor->GetSplineComponent();
				//Comp->ConvertSplineToPolyLine(ESplineCoordinateSpace::World, 1500, OutPoints);
				//Comp->SetSplinePoints(OutPoints, ESplineCoordinateSpace::World);

				if (PreviewComponent)
				{
					if (PreviewComponent->GetOwner() == ActiveActor)
					{
						PreviewComponent->UnregisterComponent();
						PreviewComponent->DestroyComponent();
					}

				}

				UpdateEditState(false);
				StateOfEditor = EMoKuEditState::Defult;
				this->Modify();
				GEditor->EndTransaction();
			}
		}	
	}
}
void UMetaLandRoadInteractiveTool::PostEditUndo()
{

	Super::PostEditUndo();
	bIsDraw = false;
	IntersectData.TValue = 0;
	if (PreviewActor->IsValidLowLevel() || PreviewActor != nullptr)
	{
		PreviewActor->Destroy();
		PreviewActor = nullptr;
		bIsInvert = false;
	}
	// ��ʱ���� ������Ҫ�Ż�
	for (const TPair<TObjectPtr<AMoKuEditBaseActor>, TArray<USPlineMarkGizmo*>>& KeyValuePair : GizmoListInfo)
	{
		for (auto Item : KeyValuePair.Value)
		{
			GetToolManager()->GetPairedGizmoManager()->DestroyGizmo(Item);
		}
	}

	GizmoListInfo.Empty();
	for (TActorIterator<AMoKuEditBaseActor>It(TargetWorld); It; ++It)
	{
		AMoKuEditBaseActor* SplineActor = *It;
		TArray<FSocketGizmoTransformInfo> TransformInfo;
		TArray<USPlineMarkGizmo*> GizmoInfos;
		TArray<FSocketGizmoTransformInfo>GizmoTransformInfos = SplineActor->GetGizmoInfo();
		for (const auto& Info : GizmoTransformInfos)
		{
			USPlineMarkGizmo* SPlineMarkGizmo = GetToolManager()->GetPairedGizmoManager()->CreateGizmo<USPlineMarkGizmo>(TEXT("SocketGizmo"), FString(), this);
			SPlineMarkGizmo->CreateGizmoHandle(Info);
			SPlineMarkGizmo->SetSelectedObject(SplineActor);
			GizmoInfos.Add(SPlineMarkGizmo);
		}
		GizmoListInfo.Add(SplineActor, GizmoInfos);
	}
	if (ActiveActor)
	{
		if (!GizmoListInfo.Find(ActiveActor))
		{
			ActiveActor->UnregisterAllComponents();
			ActiveActor = nullptr;
		}
	}
}
void UMetaLandRoadInteractiveTool::RefreshGizmoInfo()
{
	if (ActiveActor)
	{
		ActiveActor->OnInitGizmo().ExecuteIfBound(ActiveActor);
		if (!GetEditState())
		{
			UpdateEditState(true);
			AMoKuEditSplineActor* SplineActor = Cast<AMoKuEditSplineActor>(ActiveActor);
			UMoKuEditSplinesComponent* SplineComponent = SplineActor->GetSplineComponent();
			SplineComponent->bIsRenderPreviewMesh = true;
			SplineComponent->bIsAddPreviewMeshPoint = false;
		}
		ActiveActor->IsInit = false;
	}
	
}



void USplineFinishedBehavior::Initialize(UMetaLandRoadInteractiveTool* InSplineEditTool)
{
	SplineEditTool = InSplineEditTool;
}
FInputCaptureRequest USplineFinishedBehavior::WantsCapture(const FInputDeviceState& Input)
{
	FDeviceButtonState ActiveKey = Input.Keyboard.ActiveKey;
	
	if (ActiveKey.Button == EKeys::A && ActiveKey.bPressed == true)
	{

		SplineEditTool->OnFinishedSplineEdit();
		return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any, 0.0f);

	}
	if (ActiveKey.Button == EKeys::Y && ActiveKey.bPressed == true)
	{
		SplineEditTool->InvertPreviewMesh();
		return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any, 0.0f);

	}
	if (ActiveKey.Button == EKeys::B && ActiveKey.bPressed == true)
	{
		SplineEditTool->SwitchInsertCrossState();
		return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any, 0.0f);
	}

	if (ActiveKey.Button == EKeys::F1&&Input.bShiftKeyDown&& ActiveKey.bPressed == true)
	{
		SplineEditTool->SetHUDState();
		return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any, 0.0f);
	}

	return FInputCaptureRequest::Ignore();
}
FInputCaptureUpdate USplineFinishedBehavior::UpdateCapture(const FInputDeviceState& Input, const FInputCaptureData& data)
{
	return FInputCaptureUpdate::Ignore();
}
FInputCaptureUpdate USplineFinishedBehavior::BeginCapture(const FInputDeviceState& input, EInputCaptureSide eSide)
{
	return FInputCaptureUpdate::Ignore();
}
void USplineFinishedBehavior::ForceEndCapture(const FInputCaptureData& data)
{

}

#undef LOCTEXT_NAMESPACE
