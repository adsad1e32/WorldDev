#include "MetaLandRoadCurbEditor.h"
#include "MetaLandRoadCurbProfile.h"
#include "MetaLandRoadEditorColors.h"
#include "MetaLandRoadEditorModule.h"
#include "MetaLandRoadLaneAttribute.h"
#include "MetaLandRoadSplineComponent.h"
#include "MetaLandRoadTypes.h"
#include "MoKuEditSplineActor.h"
#include "MoKuEditIntersectionActor.h"
#include "Curves/RichCurve.h"
#include "Editor.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "ScopedTransaction.h"
#include "Selection.h"

namespace MetaLandRoadCurbEditor
{
	struct FPreviewSite
	{
		UMetaLandRoadSplineComponent* Spline = nullptr;
		UMetaLandRoadCurbProfile* Profile = nullptr;
		int32 SectionIndex = 0;
		int32 LaneIndex = 1;
		double Alpha = 0.0;
		bool bOutwardPositive = true;
	};

	static bool TryMakeSite(
		UMetaLandRoadSplineComponent* Spline,
		int32 SectionIndex,
		int32 LaneSign,
		int32 LaneArrayIndex,
		const FMetaLandRoadLaneSidewalk& Sidewalk,
		FPreviewSite& OutSite)
	{
		if (Sidewalk.CurbProfileAsset.IsNull())
		{
			return false;
		}
		UMetaLandRoadCurbProfile* Profile = const_cast<UMetaLandRoadCurbProfile*>(
			UMetaLandRoadCurbProfile::Resolve(Sidewalk.CurbProfileAsset, NAME_None));
		if (!Profile)
		{
			return false;
		}
		OutSite.Spline = Spline;
		OutSite.Profile = Profile;
		OutSite.SectionIndex = SectionIndex;
		OutSite.LaneIndex = LaneSign > 0 ? (LaneArrayIndex + 1) : -(LaneArrayIndex + 1);
		OutSite.Alpha = 1.0;
		OutSite.bOutwardPositive = LaneSign > 0;
		return true;
	}

	static bool FindPreviewSite(UMetaLandRoadSplineComponent* Spline, FPreviewSite& OutSite)
	{
		if (!Spline)
		{
			return false;
		}
		const int32 Num = Spline->GetLaneSectionsNum();
		for (int32 SectionIndex = 0; SectionIndex < Num; ++SectionIndex)
		{
			const FMetaLandRoadLaneSection& Section = Spline->GetLaneSection(SectionIndex);
			for (int32 i = 0; i < Section.Right.Num(); ++i)
			{
				if (const FMetaLandRoadLaneSidewalk* Sidewalk =
					Section.Right[i].LaneInstance.GetPtr<FMetaLandRoadLaneSidewalk>())
				{
					if (TryMakeSite(Spline, SectionIndex, 1, i, *Sidewalk, OutSite))
					{
						return true;
					}
				}
			}
			for (int32 i = 0; i < Section.Left.Num(); ++i)
			{
				if (const FMetaLandRoadLaneSidewalk* Sidewalk =
					Section.Left[i].LaneInstance.GetPtr<FMetaLandRoadLaneSidewalk>())
				{
					if (TryMakeSite(Spline, SectionIndex, -1, i, *Sidewalk, OutSite))
					{
						return true;
					}
				}
			}
		}
		return false;
	}

	UMetaLandRoadCurbProfile* FindProfileForSpline(UMetaLandRoadSplineComponent* Spline)
	{
		FPreviewSite Site;
		return FindPreviewSite(Spline, Site) ? Site.Profile : nullptr;
	}

	static void CollectRoadVisibleSpans(
		UMetaLandRoadSplineComponent* Spline,
		TArray<TPair<double, double>>& OutSpans)
	{
		OutSpans.Reset();
		if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(Spline ? Spline->GetOwner() : nullptr))
		{
			Road->GetJunctionMeshGenerationSpans(OutSpans);
		}
		if (OutSpans.Num() > 0)
		{
			return;
		}
		const double Length = Spline ? Spline->GetSplineLength() : 0.0;
		if (Length > KINDA_SMALL_NUMBER)
		{
			OutSpans.Emplace(0.0, Length);
		}
	}

	static bool IsRoadVisibleS(UMetaLandRoadSplineComponent* Spline, double S)
	{
		TArray<TPair<double, double>> Spans;
		CollectRoadVisibleSpans(Spline, Spans);
		for (const TPair<double, double>& Span : Spans)
		{
			if (S >= Span.Key - 1.0 && S <= Span.Value + 1.0)
			{
				return true;
			}
		}
		return false;
	}

	static void DrawJunctionOuterEdges(FPrimitiveDrawInterface* PDI, AMoKuEditIntersectionActor* Junction)
	{
		if (!PDI || !Junction)
		{
			return;
		}
		const FMetaLandRoadEditorModule& Module = FMetaLandRoadEditorModule::Get();
		const int32 HoverCorner = Module.GetSidewalkHoverCornerIndex();
		const int32 SelectedCorner = Module.GetSidewalkSelectedCornerIndex();
		const bool bHoverJunction = Module.IsSidewalkHoverJunction(Junction);
		const bool bSelectedJunction = Module.IsSidewalkSelectedJunction(Junction);
		for (int32 CornerIndex = 0; CornerIndex < Junction->CornerInfo.Num(); ++CornerIndex)
		{
			const FCornerInfo& Info = Junction->CornerInfo[CornerIndex];
			if (Info.Tag != ECornerTag::Corner || Info.CornerPoints.Num() < 2)
			{
				continue;
			}
			const bool bSelected = bSelectedJunction && CornerIndex == SelectedCorner;
			const bool bHover = bHoverJunction && CornerIndex == HoverCorner && !bSelected;
			const FLinearColor Color = bSelected
				? FLinearColor(1.0f, 0.85f, 0.15f)
				: (bHover
					? FLinearColor(FMetaLandRoadEditorColors::HoverColor)
					: FLinearColor(1.0f, 0.55f, 0.1f));
			const float Thickness = bSelected ? 7.0f : (bHover ? 5.0f : 2.5f);
			for (int32 PointIndex = 1; PointIndex < Info.CornerPoints.Num(); ++PointIndex)
			{
				FVector A(Info.CornerPoints[PointIndex - 1]);
				FVector B(Info.CornerPoints[PointIndex]);
				A.Z += 8.0;
				B.Z += 8.0;
				PDI->DrawLine(A, B, Color, SDPG_Foreground, Thickness);
			}
		}
	}

	void DrawSelectedRoadPreview(FPrimitiveDrawInterface* PDI)
	{
		if (!PDI || !GEditor || !FMetaLandRoadEditorModule::Get().ShouldDrawSidewalkOverlay())
		{
			return;
		}
		auto DrawSpline = [PDI](UMetaLandRoadSplineComponent* Spline)
		{
			if (Spline)
			{
				DrawSidewalkEdges(PDI, Spline);
			}
		};
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(*It))
			{
				DrawSpline(Road->GetSplineComponent());
			}
			else if (AMoKuEditIntersectionActor* Junction = Cast<AMoKuEditIntersectionActor>(*It))
			{
				if (Junction->JunctionData)
				{
					for (AMoKuEditSplineActor* Linked : Junction->JunctionData->LinkedRoadActors)
					{
						DrawSpline(Linked ? Linked->GetSplineComponent() : nullptr);
					}
				}
				DrawJunctionOuterEdges(PDI, Junction);
			}
		}
	}

	static bool FaceHasOuterSidewalk(const TArray<FMetaLandRoadLane>& Face)
	{
		return Face.Num() > 0
			&& Face.Last().LaneInstance.GetPtr<FMetaLandRoadLaneSidewalk>() != nullptr;
	}

	static FMetaLandRoadLane MakeSidewalkLane(float WidthCm, UMetaLandRoadCurbProfile* Profile)
	{
		FMetaLandRoadLane Lane;
		FMetaLandRoadLaneSidewalk Sidewalk;
		if (Profile)
		{
			Sidewalk.CurbProfileAsset = Profile;
			if (!Profile->ProfileId.IsNone())
			{
				Sidewalk.CurbProfile = Profile->ProfileId;
			}
		}
		Sidewalk.bInsideCurb = false;
		Sidewalk.bOutsideCurb = true;
		Sidewalk.WidthCm = FMath::Clamp(WidthCm, 20.0f, 800.0f);
		FMetaLandRoadSidewalkCurbKey StartKey;
		StartKey.SOffset = 0.0;
		StartKey.CurbProfileAsset = Sidewalk.CurbProfileAsset;
		StartKey.WidthCm = Sidewalk.WidthCm;
		Sidewalk.CurbKeys.Add(StartKey);
		Lane.LaneInstance.InitializeAs<FMetaLandRoadLaneSidewalk>(Sidewalk);
		Lane.Width.Reset();
		Lane.Width.SetDefaultValue(Sidewalk.WidthCm);
		const FKeyHandle Handle = Lane.Width.AddKey(0.0f, Sidewalk.WidthCm);
		Lane.Width.SetKeyInterpMode(Handle, ERichCurveInterpMode::RCIM_Linear);
		Lane.PinAttribute(FMetaLandRoadAttributeRegistry::TypeSidewalkHeight);
		return Lane;
	}

	static void ForEachLane(
		UMetaLandRoadSplineComponent* Spline,
		TFunctionRef<void(FMetaLandRoadLane&)> Fn)
	{
		if (!Spline)
		{
			return;
		}
		const int32 Num = Spline->GetLaneSectionsNum();
		for (int32 SectionIndex = 0; SectionIndex < Num; ++SectionIndex)
		{
			FMetaLandRoadLaneSection& Section = Spline->GetLaneSection(SectionIndex);
			for (FMetaLandRoadLane& Lane : Section.Left)
			{
				Fn(Lane);
			}
			for (FMetaLandRoadLane& Lane : Section.Right)
			{
				Fn(Lane);
			}
		}
	}

	static void ForEachSidewalkLane(
		UMetaLandRoadSplineComponent* Spline,
		TFunctionRef<void(FMetaLandRoadLane&)> Fn)
	{
		ForEachLane(Spline, [&](FMetaLandRoadLane& Lane)
		{
			if (Lane.LaneInstance.GetPtr<FMetaLandRoadLaneSidewalk>())
			{
				Fn(Lane);
			}
		});
	}

	bool EnableSidewalksOnSpline(UMetaLandRoadSplineComponent* Spline)
	{
		if (!Spline)
		{
			return false;
		}

		UMetaLandRoadCurbProfile* Profile = FindProfileForSpline(Spline);

		bool bAdded = false;
		const int32 Num = Spline->GetLaneSectionsNum();
		for (int32 SectionIndex = 0; SectionIndex < Num; ++SectionIndex)
		{
			FMetaLandRoadLaneSection& Section = Spline->GetLaneSection(SectionIndex);
			if (MetaLandRoad::HasLeftSide(Section.Side) && !FaceHasOuterSidewalk(Section.Left))
			{
				Section.Left.Add(MakeSidewalkLane(150.0f, Profile));
				bAdded = true;
			}
			if (MetaLandRoad::HasRightSide(Section.Side) && !FaceHasOuterSidewalk(Section.Right))
			{
				Section.Right.Add(MakeSidewalkLane(150.0f, Profile));
				bAdded = true;
			}
		}

		if (!bAdded)
		{
			return FindProfileForSpline(Spline) != nullptr;
		}

		Spline->Modify();
		if (AActor* Owner = Spline->GetOwner())
		{
			Owner->Modify();
		}
		Spline->UpdateRoadLayout();
		if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(Spline->GetOwner()))
		{
			Road->RefreshDynamicRoadMesh();
		}
		return true;
	}

	bool GetSidewalkDetails(UMetaLandRoadSplineComponent* Spline, FMetaLandRoadSidewalkEditDetails& OutDetails)
	{
		OutDetails = FMetaLandRoadSidewalkEditDetails();
		if (!Spline)
		{
			return false;
		}
		const int32 EdgeSign = FMetaLandRoadEditorModule::Get().GetSidewalkEdgeSign();
		if (EdgeSign == 0)
		{
			return false;
		}
		bool bFound = false;
		const int32 Num = Spline->GetLaneSectionsNum();
		for (int32 SectionIndex = 0; SectionIndex < Num && !bFound; ++SectionIndex)
		{
			FMetaLandRoadLaneSection& Section = Spline->GetLaneSection(SectionIndex);
			TArray<FMetaLandRoadLane>& Face = EdgeSign > 0 ? Section.Right : Section.Left;
			for (FMetaLandRoadLane& Lane : Face)
			{
				const FMetaLandRoadLaneSidewalk* Sidewalk =
					Lane.LaneInstance.GetPtr<FMetaLandRoadLaneSidewalk>();
				if (!Sidewalk)
				{
					continue;
				}
				const int32 KeyIndex = FMath::Clamp(
					FMetaLandRoadEditorModule::Get().GetSidewalkKeyIndex(),
					0,
					FMath::Max(Sidewalk->CurbKeys.Num() - 1, 0));
				if (Sidewalk->CurbKeys.IsValidIndex(KeyIndex))
				{
					OutDetails.CurbProfileAsset = Sidewalk->CurbKeys[KeyIndex].CurbProfileAsset;
					OutDetails.KeyS = Sidewalk->CurbKeys[KeyIndex].SOffset;
					OutDetails.OutwardOffset = Sidewalk->CurbKeys[KeyIndex].OutwardOffsetCm;
				}
				else
				{
					OutDetails.CurbProfileAsset = Sidewalk->CurbProfileAsset;
					OutDetails.OutwardOffset = Sidewalk->OutwardOffsetCm;
				}
				bFound = true;
				break;
			}
		}
		return bFound;
	}

	bool SetSidewalkDetails(UMetaLandRoadSplineComponent* Spline, const FMetaLandRoadSidewalkEditDetails& InDetails)
	{
		if (!Spline)
		{
			return false;
		}

		const int32 EdgeSign = FMetaLandRoadEditorModule::Get().GetSidewalkEdgeSign();
		if (EdgeSign == 0)
		{
			return false;
		}

		UMetaLandRoadCurbProfile* Profile = InDetails.CurbProfileAsset.LoadSynchronous();
		bool bChanged = false;
		const int32 Num = Spline->GetLaneSectionsNum();
		for (int32 SectionIndex = 0; SectionIndex < Num; ++SectionIndex)
		{
			FMetaLandRoadLaneSection& Section = Spline->GetLaneSection(SectionIndex);
			TArray<FMetaLandRoadLane>& Face = EdgeSign > 0 ? Section.Right : Section.Left;
			if (Face.Num() == 0 || !Face.Last().LaneInstance.GetPtr<FMetaLandRoadLaneSidewalk>())
			{
				if (EdgeSign < 0 && !MetaLandRoad::HasLeftSide(Section.Side))
				{
					continue;
				}
				if (EdgeSign > 0 && !MetaLandRoad::HasRightSide(Section.Side))
				{
					continue;
				}
				Face.Add(MakeSidewalkLane(150.0f, Profile));
				bChanged = true;
			}
			for (FMetaLandRoadLane& Lane : Face)
			{
				FMetaLandRoadLaneSidewalk* Sidewalk = Lane.LaneInstance.GetMutablePtr<FMetaLandRoadLaneSidewalk>();
				if (!Sidewalk)
				{
					continue;
				}
				Sidewalk->bInsideCurb = false;
				Sidewalk->bOutsideCurb = true;
				Sidewalk->CurbProfileAsset = InDetails.CurbProfileAsset;
				if (Profile && !Profile->ProfileId.IsNone())
				{
					Sidewalk->CurbProfile = Profile->ProfileId;
				}
				if (Sidewalk->CurbKeys.IsEmpty())
				{
					FMetaLandRoadSidewalkCurbKey StartKey;
					StartKey.SOffset = 0.0;
					StartKey.CurbProfileAsset = InDetails.CurbProfileAsset;
					StartKey.WidthCm = Sidewalk->WidthCm;
					Sidewalk->CurbKeys.Add(StartKey);
				}
				const int32 KeyIndex = FMath::Clamp(
					FMetaLandRoadEditorModule::Get().GetSidewalkKeyIndex(),
					0,
					Sidewalk->CurbKeys.Num() - 1);
				Sidewalk->CurbKeys[KeyIndex].CurbProfileAsset = InDetails.CurbProfileAsset;
				Sidewalk->CurbKeys[KeyIndex].OutwardOffsetCm = InDetails.OutwardOffset;
				if (KeyIndex == 0)
				{
					Sidewalk->OutwardOffsetCm = InDetails.OutwardOffset;
				}
				bChanged = true;
			}
		}

		if (!bChanged)
		{
			return false;
		}

		Spline->Modify();
		Spline->UpdateRoadLayout();
		if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(Spline->GetOwner()))
		{
			Road->RefreshDynamicRoadMesh();
		}
		return true;
	}

	static void RefreshSidewalkSpline(UMetaLandRoadSplineComponent* Spline)
	{
		if (!Spline)
		{
			return;
		}
		Spline->Modify();
		Spline->UpdateRoadLayout();
		if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(Spline->GetOwner()))
		{
			Road->RefreshDynamicRoadMesh();
		}
	}

	bool AddCurbKeyAtS(UMetaLandRoadSplineComponent* Spline, double SOffset)
	{
		if (!Spline)
		{
			return false;
		}
		bool bAdded = false;
		const int32 HoverSign = FMetaLandRoadEditorModule::Get().GetSidewalkHoverEdgeSign();
		const int32 EdgeSign = HoverSign != 0
			? HoverSign
			: FMetaLandRoadEditorModule::Get().GetSidewalkEdgeSign();
		const int32 Num = Spline->GetLaneSectionsNum();
		for (int32 SectionIndex = 0; SectionIndex < Num; ++SectionIndex)
		{
			FMetaLandRoadLaneSection& Section = Spline->GetLaneSection(SectionIndex);
			TArray<FMetaLandRoadLane>& Face = EdgeSign < 0 ? Section.Left : Section.Right;
			for (FMetaLandRoadLane& Lane : Face)
		{
			FMetaLandRoadLaneSidewalk* Sidewalk = Lane.LaneInstance.GetMutablePtr<FMetaLandRoadLaneSidewalk>();
			if (!Sidewalk)
			{
				continue;
			}
			bool bTooClose = false;
			for (const FMetaLandRoadSidewalkCurbKey& Key : Sidewalk->CurbKeys)
			{
				if (FMath::Abs(Key.SOffset - SOffset) < 50.0)
				{
					bTooClose = true;
					break;
				}
			}
			if (bTooClose)
			{
				continue;
			}
			FMetaLandRoadSidewalkCurbKey NewKey;
			NewKey.SOffset = SOffset;
			NewKey.CurbProfileAsset = Sidewalk->CurbProfileAsset;
			NewKey.WidthCm = Sidewalk->WidthCm;
			if (!Sidewalk->CurbKeys.IsEmpty())
			{
				const FMetaLandRoadSidewalkCurbKey* Prev = &Sidewalk->CurbKeys.Last();
				for (const FMetaLandRoadSidewalkCurbKey& Key : Sidewalk->CurbKeys)
				{
					if (Key.SOffset <= SOffset)
					{
						Prev = &Key;
					}
				}
				NewKey.CurbProfileAsset = Prev->CurbProfileAsset;
				NewKey.WidthCm = Prev->WidthCm;
				NewKey.OutwardOffsetCm = Prev->OutwardOffsetCm;
			}
			Sidewalk->CurbKeys.Add(NewKey);
			Sidewalk->CurbKeys.Sort([](const FMetaLandRoadSidewalkCurbKey& A, const FMetaLandRoadSidewalkCurbKey& B)
			{
				return A.SOffset < B.SOffset;
			});
			FMetaLandRoadEditorModule::Get().SetSidewalkKeyIndex(Sidewalk->CurbKeys.IndexOfByPredicate(
				[&](const FMetaLandRoadSidewalkCurbKey& Key) { return FMath::IsNearlyEqual(Key.SOffset, SOffset); }));
			bAdded = true;
			}
		}
		if (bAdded)
		{
			RefreshSidewalkSpline(Spline);
		}
		return bAdded;
	}

	bool RemoveSelectedCurbKey(UMetaLandRoadSplineComponent* Spline)
	{
		if (!Spline)
		{
			return false;
		}
		const int32 KeyIndex = FMetaLandRoadEditorModule::Get().GetSidewalkKeyIndex();
		if (KeyIndex <= 0)
		{
			return false;
		}
		bool bRemoved = false;
		ForEachSidewalkLane(Spline, [&](FMetaLandRoadLane& Lane)
		{
			FMetaLandRoadLaneSidewalk* Sidewalk = Lane.LaneInstance.GetMutablePtr<FMetaLandRoadLaneSidewalk>();
			if (!Sidewalk || !Sidewalk->CurbKeys.IsValidIndex(KeyIndex) || Sidewalk->CurbKeys.Num() <= 1)
			{
				return;
			}
			Sidewalk->CurbKeys.RemoveAt(KeyIndex);
			bRemoved = true;
		});
		if (bRemoved)
		{
			FMetaLandRoadEditorModule::Get().SetSidewalkKeyIndex(0);
			RefreshSidewalkSpline(Spline);
		}
		return bRemoved;
	}

	static void SelectSidewalkKeyAtS(UMetaLandRoadSplineComponent* Spline, int32 EdgeSign, double S)
	{
		int32 BestKey = 0;
		bool bFound = false;
		const int32 Num = Spline ? Spline->GetLaneSectionsNum() : 0;
		for (int32 SectionIndex = 0; SectionIndex < Num; ++SectionIndex)
		{
			const FMetaLandRoadLaneSection& Section = Spline->GetLaneSection(SectionIndex);
			const TArray<FMetaLandRoadLane>& Face = EdgeSign < 0 ? Section.Left : Section.Right;
			for (const FMetaLandRoadLane& Lane : Face)
			{
				const FMetaLandRoadLaneSidewalk* Sidewalk = Lane.LaneInstance.GetPtr<FMetaLandRoadLaneSidewalk>();
				if (!Sidewalk)
				{
					continue;
				}
				for (int32 i = 0; i < Sidewalk->CurbKeys.Num(); ++i)
				{
					if (Sidewalk->CurbKeys[i].SOffset <= S + 1.0)
					{
						BestKey = i;
						bFound = true;
					}
				}
				break;
			}
		}
		if (bFound)
		{
			FMetaLandRoadEditorModule::Get().SetSidewalkKeyIndex(BestKey);
		}
	}

	bool ApplySidewalkPick(const FSidewalkEdgePick& Pick)
	{
		if (!Pick.IsHit())
		{
			return false;
		}
		FMetaLandRoadEditorModule& Module = FMetaLandRoadEditorModule::Get();
		if (Pick.HitCorner())
		{
			Module.SetSidewalkSelectedCorner(Pick.Junction, Pick.CornerIndex);
			const int32 Count = Pick.Junction->CornerInfo.Num();
			const int32 PrevIndex = (Pick.CornerIndex - 1 + Count) % Count;
			const FCornerInfo& Side = Pick.Junction->CornerInfo[PrevIndex];
			UMetaLandRoadSplineComponent* Spline = Side.SampleSpline.Get();
			const FCornerInfo& Corner = Pick.Junction->CornerInfo[Pick.CornerIndex];
			if (Spline && Side.SampleDistanceS >= 0.0 && Corner.CornerPoints.Num() > 0)
			{
				FVector EdgeL = FVector::ZeroVector;
				FVector EdgeR = FVector::ZeroVector;
				FVector EdgeC = FVector::ZeroVector;
				int32 Sign = 1;
				if (Spline->GetRoadEdgePointsAtS(
					Side.SampleDistanceS, EdgeL, EdgeR, EdgeC, ESplineCoordinateSpace::World))
				{
					const FVector Edge(Corner.CornerPoints[0]);
					Sign = FVector::DistSquared2D(Edge, EdgeR) <= FVector::DistSquared2D(Edge, EdgeL) ? 1 : -1;
				}
				Module.SetSidewalkFocusSpline(Spline);
				Module.SetSidewalkEdgeSign(Sign);
				SelectSidewalkKeyAtS(Spline, Sign, Side.SampleDistanceS);
			}
			Module.BroadcastSidewalkEditorRefresh();
			return true;
		}

		Module.SetSidewalkSelectedCorner(nullptr, INDEX_NONE);
		Module.SetSidewalkFocusSpline(Pick.Spline);
		Module.SetSidewalkEdgeSign(Pick.Sign);
		SelectSidewalkKeyAtS(Pick.Spline, Pick.Sign, Pick.S);
		Module.BroadcastSidewalkEditorRefresh();
		return true;
	}

	void DrawSidewalkKeys(FPrimitiveDrawInterface* PDI, UMetaLandRoadSplineComponent* Spline)
	{
		if (!PDI || !Spline)
		{
			return;
		}
		const int32 Selected = FMetaLandRoadEditorModule::Get().GetSidewalkKeyIndex();
		const int32 Num = Spline->GetLaneSectionsNum();
		for (int32 SectionIndex = 0; SectionIndex < Num; ++SectionIndex)
		{
			const FMetaLandRoadLaneSection& Section = Spline->GetLaneSection(SectionIndex);
			auto DrawFace = [&](const TArray<FMetaLandRoadLane>& Face, int32 LaneSign)
			{
				for (int32 i = 0; i < Face.Num(); ++i)
				{
					const FMetaLandRoadLaneSidewalk* Sidewalk =
						Face[i].LaneInstance.GetPtr<FMetaLandRoadLaneSidewalk>();
					if (!Sidewalk)
					{
						continue;
					}
					const int32 LaneIndex = LaneSign > 0 ? (i + 1) : -(i + 1);
					for (int32 k = 0; k < Sidewalk->CurbKeys.Num(); ++k)
					{
						if (!IsRoadVisibleS(Spline, Sidewalk->CurbKeys[k].SOffset))
						{
							continue;
						}
						const FVector World = Spline->EvalLanePosition(
							SectionIndex,
							LaneIndex,
							Sidewalk->CurbKeys[k].SOffset,
							1.0,
							ESplineCoordinateSpace::World);
						const FLinearColor Color = (k == Selected)
							? FLinearColor(1.0f, 0.85f, 0.15f)
							: FLinearColor(0.2f, 0.7f, 1.0f);
						PDI->DrawPoint(World, Color, k == Selected ? 16.0f : 11.0f, SDPG_Foreground);
					}
				}
			};
			DrawFace(Section.Right, 1);
			DrawFace(Section.Left, -1);
		}
	}

	void DrawSidewalkEdges(FPrimitiveDrawInterface* PDI, UMetaLandRoadSplineComponent* Spline)
	{
		if (!PDI || !Spline || !FMetaLandRoadEditorModule::Get().ShouldDrawSidewalkOverlay())
		{
			return;
		}
		const FMetaLandRoadEditorModule& Module = FMetaLandRoadEditorModule::Get();
		const int32 Selected = Module.GetSidewalkEdgeSign();
		const int32 Hover = Module.GetSidewalkHoverEdgeSign();
		const double HoverS = Module.GetSidewalkHoverS();
		const int32 SelectedKey = Module.GetSidewalkKeyIndex();
		const bool bHoverSpline = Module.GetSidewalkHoverSpline() == Spline;
		const bool bFocusSpline = Module.GetSidewalkFocusSpline() == Spline;
		const bool bCornerSelected = Module.GetSidewalkSelectedCornerIndex() != INDEX_NONE;
		TArray<TPair<double, double>> VisibleSpans;
		CollectRoadVisibleSpans(Spline, VisibleSpans);
		const float Length = Spline->GetSplineLength();
		const int32 EdgeSigns[2] = { 1, -1 };
		for (const int32 EdgeSign : EdgeSigns)
		{
			TArray<double> Spans;
			Spans.Add(0.0);
			double SelectedKeyS = 0.0;
			const int32 Num = Spline->GetLaneSectionsNum();
			for (int32 SectionIndex = 0; SectionIndex < Num; ++SectionIndex)
			{
				const FMetaLandRoadLaneSection& Section = Spline->GetLaneSection(SectionIndex);
				const TArray<FMetaLandRoadLane>& Face = EdgeSign > 0 ? Section.Right : Section.Left;
				for (const FMetaLandRoadLane& Lane : Face)
				{
					const FMetaLandRoadLaneSidewalk* Sidewalk =
						Lane.LaneInstance.GetPtr<FMetaLandRoadLaneSidewalk>();
					if (!Sidewalk)
					{
						continue;
					}
					for (int32 KeyIndex = 0; KeyIndex < Sidewalk->CurbKeys.Num(); ++KeyIndex)
					{
						Spans.Add(Sidewalk->CurbKeys[KeyIndex].SOffset);
						if (KeyIndex == SelectedKey)
						{
							SelectedKeyS = Sidewalk->CurbKeys[KeyIndex].SOffset;
						}
					}
					break;
				}
			}
			Spans.Add(Length);
			Spans.Sort();
			for (int32 SpanIndex = 0; SpanIndex + 1 < Spans.Num(); ++SpanIndex)
			{
				const double KeyS0 = Spans[SpanIndex];
				const double KeyS1 = Spans[SpanIndex + 1];
				for (const TPair<double, double>& Visible : VisibleSpans)
				{
					const double S0 = FMath::Max(KeyS0, Visible.Key);
					const double S1 = FMath::Min(KeyS1, Visible.Value);
					if (S1 - S0 < 2.0)
					{
						continue;
					}
					const bool bHoverSpan = bHoverSpline
						&& EdgeSign == Hover
						&& HoverS >= S0 - 1.0
						&& HoverS <= S1 + 1.0;
					const bool bSelectedSpan = !bCornerSelected
						&& bFocusSpline
						&& EdgeSign == Selected
						&& FMath::IsNearlyEqual(KeyS0, SelectedKeyS, 1.0);
					const FLinearColor Color = bSelectedSpan
						? FLinearColor(1.0f, 0.85f, 0.15f)
						: (bHoverSpan
							? FLinearColor(FMetaLandRoadEditorColors::HoverColor)
							: FLinearColor(1.0f, 0.55f, 0.1f));
					const float Thickness = bSelectedSpan ? 7.0f : (bHoverSpan ? 5.0f : 2.5f);
					const int32 Steps = FMath::Clamp(FMath::CeilToInt(static_cast<float>(S1 - S0) / 80.0f), 2, 64);
					FVector Prev = FVector::ZeroVector;
					bool bHavePrev = false;
					for (int32 Step = 0; Step <= Steps; ++Step)
					{
						const double S = FMath::Lerp(S0, S1, static_cast<double>(Step) / static_cast<double>(Steps));
						FVector Left = FVector::ZeroVector;
						FVector Right = FVector::ZeroVector;
						FVector Center = FVector::ZeroVector;
						if (!Spline->GetRoadEdgePointsAtS(S, Left, Right, Center, ESplineCoordinateSpace::World))
						{
							continue;
						}
						FVector World = EdgeSign > 0 ? Right : Left;
						World.Z += 8.0;
						if (bHavePrev)
						{
							PDI->DrawLine(Prev, World, Color, SDPG_Foreground, Thickness);
						}
						Prev = World;
						bHavePrev = true;
					}
				}
			}
		}
	}

	bool PickSidewalkEdge(
		UMetaLandRoadSplineComponent* Spline,
		const FSceneView* View,
		int32 MouseX,
		int32 MouseY,
		int32& OutSign,
		double& OutS)
	{
		OutSign = 0;
		OutS = -1.0;
		if (!Spline || !View)
		{
			return false;
		}
		const FVector2D Mouse(static_cast<float>(MouseX), static_cast<float>(MouseY));
		float BestDistSq = 14.0f * 14.0f;
		const float Length = Spline->GetSplineLength();
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(Length / 80.0f), 8, 160);
		const int32 EdgeSigns[2] = { 1, -1 };
		for (const int32 EdgeSign : EdgeSigns)
		{
			for (int32 Step = 0; Step <= Steps; ++Step)
			{
				const double S = Length * (static_cast<double>(Step) / static_cast<double>(Steps));
				if (!IsRoadVisibleS(Spline, S))
				{
					continue;
				}
				FVector Left = FVector::ZeroVector;
				FVector Right = FVector::ZeroVector;
				FVector Center = FVector::ZeroVector;
				if (!Spline->GetRoadEdgePointsAtS(S, Left, Right, Center, ESplineCoordinateSpace::World))
				{
					continue;
				}
				FVector World = EdgeSign > 0 ? Right : Left;
				World.Z += 8.0;
				FVector2D Pixel;
				if (!View->WorldToPixel(World, Pixel))
				{
					continue;
				}
				const float DistSq = FVector2D::DistSquared(Pixel, Mouse);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					OutSign = EdgeSign;
					OutS = S;
				}
			}
		}
		return OutSign != 0;
	}

	static float DistSqToPolylinePx(
		const FSceneView* View,
		const FVector2D& Mouse,
		const TArray<FVector3d>& Points)
	{
		float Best = MAX_flt;
		FVector2D PrevPixel;
		bool bHavePrev = false;
		for (const FVector3d& Point : Points)
		{
			FVector World(Point);
			World.Z += 8.0;
			FVector2D Pixel;
			if (!View->WorldToPixel(World, Pixel))
			{
				bHavePrev = false;
				continue;
			}
			Best = FMath::Min(Best, FVector2D::DistSquared(Pixel, Mouse));
			if (bHavePrev)
			{
				const FVector2D Seg = Pixel - PrevPixel;
				const float LenSq = Seg.SizeSquared();
				if (LenSq > KINDA_SMALL_NUMBER)
				{
					const float T = FMath::Clamp(
						FVector2D::DotProduct(Mouse - PrevPixel, Seg) / LenSq, 0.0f, 1.0f);
					Best = FMath::Min(Best, FVector2D::DistSquared(PrevPixel + Seg * T, Mouse));
				}
			}
			PrevPixel = Pixel;
			bHavePrev = true;
		}
		return Best;
	}

	bool PickSidewalkOverlay(const FSceneView* View, int32 MouseX, int32 MouseY, FSidewalkEdgePick& OutPick)
	{
		OutPick = FSidewalkEdgePick();
		if (!View || !GEditor)
		{
			return false;
		}
		float BestDistSq = 14.0f * 14.0f;
		const FVector2D Mouse(static_cast<float>(MouseX), static_cast<float>(MouseY));
		auto ConsiderSpline = [&](UMetaLandRoadSplineComponent* Spline)
		{
			int32 Sign = 0;
			double S = -1.0;
			if (!PickSidewalkEdge(Spline, View, MouseX, MouseY, Sign, S))
			{
				return;
			}
			FVector Left = FVector::ZeroVector;
			FVector Right = FVector::ZeroVector;
			FVector Center = FVector::ZeroVector;
			if (!Spline->GetRoadEdgePointsAtS(S, Left, Right, Center, ESplineCoordinateSpace::World))
			{
				return;
			}
			FVector World = Sign > 0 ? Right : Left;
			World.Z += 8.0;
			FVector2D Pixel;
			if (!View->WorldToPixel(World, Pixel))
			{
				return;
			}
			const float DistSq = FVector2D::DistSquared(Pixel, Mouse);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				OutPick.Spline = Spline;
				OutPick.Sign = Sign;
				OutPick.S = S;
				OutPick.Junction = nullptr;
				OutPick.CornerIndex = INDEX_NONE;
			}
		};
		auto ConsiderJunction = [&](AMoKuEditIntersectionActor* Junction)
		{
			if (!Junction)
			{
				return;
			}
			for (int32 CornerIndex = 0; CornerIndex < Junction->CornerInfo.Num(); ++CornerIndex)
			{
				const FCornerInfo& Info = Junction->CornerInfo[CornerIndex];
				if (Info.Tag != ECornerTag::Corner || Info.CornerPoints.Num() < 2)
				{
					continue;
				}
				const float DistSq = DistSqToPolylinePx(View, Mouse, Info.CornerPoints);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					OutPick = FSidewalkEdgePick();
					OutPick.Junction = Junction;
					OutPick.CornerIndex = CornerIndex;
				}
			}
		};
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if (AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(*It))
			{
				ConsiderSpline(Road->GetSplineComponent());
			}
			else if (AMoKuEditIntersectionActor* Junction = Cast<AMoKuEditIntersectionActor>(*It))
			{
				if (Junction->JunctionData)
				{
					for (AMoKuEditSplineActor* Linked : Junction->JunctionData->LinkedRoadActors)
					{
						ConsiderSpline(Linked ? Linked->GetSplineComponent() : nullptr);
					}
				}
				ConsiderJunction(Junction);
			}
		}
		return OutPick.IsHit();
	}
}
