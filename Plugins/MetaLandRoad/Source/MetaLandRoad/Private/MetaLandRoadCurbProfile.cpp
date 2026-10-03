#include "MetaLandRoadCurbProfile.h"
#include "MetaLandRoadSplineComponent.h"
#include "MetaLandRoadTypes.h"
#include "MoKuEditSplineActor.h"
#include "EngineUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Curves/RichCurve.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectIterator.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

using UE::Geometry::FDynamicMesh3;
using UE::Geometry::FIndex3i;
using UE::Geometry::FVertexInfo;

UMetaLandRoadCurbProfile::UMetaLandRoadCurbProfile()
{
	EnsureDefaultCurve();
}

void UMetaLandRoadCurbProfile::PostLoad()
{
	Super::PostLoad();
	EnsureDefaultCurve();
	if (const FRichCurve* Curve = CurbCurve.GetRichCurveConst();
		Curve && Curve->GetNumKeys() >= 2)
	{
		const float TSpan = Curve->Keys.Last().Time - Curve->Keys[0].Time;
		float VMax = 0.0f;
		for (const FRichCurveKey& Key : Curve->Keys)
		{
			VMax = FMath::Max(VMax, Key.Value);
		}
		if (TSpan <= 1.01f && VMax <= 1.01f)
		{
			ApplyDimensionsToCurve();
		}
		else
		{
			ReadDimensionsFromCurve();
		}
	}
}

void UMetaLandRoadCurbProfile::EnsureDefaultCurve()
{
	FRichCurve* Curve = CurbCurve.GetRichCurve();
	if (!Curve)
	{
		return;
	}

	if (Curve->GetNumKeys() < 2 && CrossSection.Num() >= 2)
	{
		TSet<float> UsedTimes;
		for (const FMetaLandRoadCurbSectionPoint& Pt : CrossSection)
		{
			float Time = static_cast<float>(Pt.Point.X);
			while (UsedTimes.Contains(Time))
			{
				Time += 0.5f;
			}
			UsedTimes.Add(Time);
			const FKeyHandle Handle = Curve->AddKey(Time, static_cast<float>(Pt.Point.Y));
			Curve->SetKeyInterpMode(Handle, ERichCurveInterpMode::RCIM_Linear);
		}
	}

	if (Curve->GetNumKeys() >= 2)
	{
		return;
	}

	Curve->Reset();
	const float W = FMath::Max(Width, 1.0f);
	const float H = FMath::Max(Height, 1.0f);
	const FKeyHandle H0 = Curve->AddKey(0.00f * W, 0.0f);
	const FKeyHandle H1 = Curve->AddKey(0.02f * W, H);
	const FKeyHandle H2 = Curve->AddKey(0.98f * W, H);
	const FKeyHandle H3 = Curve->AddKey(1.00f * W, 0.0f);
	Curve->SetKeyInterpMode(H0, ERichCurveInterpMode::RCIM_Linear);
	Curve->SetKeyInterpMode(H1, ERichCurveInterpMode::RCIM_Linear);
	Curve->SetKeyInterpMode(H2, ERichCurveInterpMode::RCIM_Linear);
	Curve->SetKeyInterpMode(H3, ERichCurveInterpMode::RCIM_Linear);
}

void UMetaLandRoadCurbProfile::ApplyDimensionsToCurve()
{
	FRichCurve* Curve = CurbCurve.GetRichCurve();
	if (!Curve || Curve->GetNumKeys() < 2)
	{
		EnsureDefaultCurve();
		return;
	}

	TArray<FRichCurveKey> OldKeys = Curve->Keys;
	float T0 = OldKeys[0].Time;
	float T1 = OldKeys.Last().Time;
	float VMax = 0.0f;
	for (const FRichCurveKey& Key : OldKeys)
	{
		VMax = FMath::Max(VMax, Key.Value);
	}
	const float TSpan = FMath::Max(T1 - T0, KINDA_SMALL_NUMBER);
	const float VScale = FMath::Max(VMax, KINDA_SMALL_NUMBER);
	const float W = FMath::Max(Width, 1.0f);
	const float H = FMath::Max(Height, 1.0f);

	Curve->Reset();
	for (const FRichCurveKey& Old : OldKeys)
	{
		const float U = (Old.Time - T0) / TSpan;
		const float V = Old.Value / VScale;
		const FKeyHandle Handle = Curve->AddKey(W * U, H * V);
		Curve->SetKeyInterpMode(Handle, Old.InterpMode);
		Curve->SetKeyTangentMode(Handle, Old.TangentMode);
	}
}

void UMetaLandRoadCurbProfile::ReadDimensionsFromCurve()
{
	const FRichCurve* Curve = CurbCurve.GetRichCurveConst();
	if (!Curve || Curve->GetNumKeys() < 2)
	{
		return;
	}

	const float T0 = Curve->Keys[0].Time;
	const float T1 = Curve->Keys.Last().Time;
	float VMax = 0.0f;
	for (const FRichCurveKey& Key : Curve->Keys)
	{
		VMax = FMath::Max(VMax, Key.Value);
	}
	Width = FMath::Max(T1 - T0, 1.0f);
	Height = FMath::Max(VMax, 1.0f);
}

void UMetaLandRoadCurbProfile::GetProfilePoints(TArray<FVector2D>& OutPoints) const
{
	OutPoints.Reset();
	TArray<FVector2D> Keys;
	if (const FRichCurve* Curve = CurbCurve.GetRichCurveConst())
	{
		for (const FRichCurveKey& Key : Curve->Keys)
		{
			Keys.Add(FVector2D(Key.Time, Key.Value));
		}
	}
	if (Keys.Num() < 2)
	{
		return;
	}

	OutPoints.Reserve(Keys.Num() + 1);
	for (const FVector2D& Key : Keys)
	{
		OutPoints.Add(Key);
	}

	if (bClosedProfile && OutPoints.Num() >= 2
		&& !OutPoints[0].Equals(OutPoints.Last(), 0.05))
	{
		const FVector2D First = OutPoints[0];
		OutPoints.Add(First);
	}
}

void UMetaLandRoadCurbProfile::RecreateExternalCurve()
{
#if WITH_EDITOR
	const FRichCurve* Source = CurbCurve.GetRichCurveConst();
	if (!Source)
	{
		return;
	}

	UCurveFloat* CurveAsset = CurbCurve.ExternalCurve;
	if (!CurveAsset)
	{
		const FName CurveName = MakeUniqueObjectName(
			GetOutermost(),
			UCurveFloat::StaticClass(),
			FName(*FString::Printf(TEXT("%s_Curve"), *GetName())));
		CurveAsset = NewObject<UCurveFloat>(
			GetOutermost(),
			CurveName,
			RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(CurveAsset);
	}

	CurveAsset->Modify();
	CurveAsset->FloatCurve = *Source;
	CurbCurve.ExternalCurve = CurveAsset;
	Modify();
	MarkPackageDirty();
	CurveAsset->MarkPackageDirty();
	CurveAsset->PostEditChange();
#endif
}

const UMetaLandRoadCurbProfile* UMetaLandRoadCurbProfile::Resolve(
	const TSoftObjectPtr<UMetaLandRoadCurbProfile>& Asset,
	FName ProfileId)
{
	if (!Asset.IsNull())
	{
		if (const UMetaLandRoadCurbProfile* Loaded = Asset.LoadSynchronous())
		{
			return Loaded;
		}
	}
	return FindById(ProfileId);
}

const UMetaLandRoadCurbProfile* UMetaLandRoadCurbProfile::FindById(FName ProfileId)
{
	if (ProfileId.IsNone())
	{
		return nullptr;
	}

	for (TObjectIterator<UMetaLandRoadCurbProfile> It; It; ++It)
	{
		const UMetaLandRoadCurbProfile* Profile = *It;
		if (Profile && !Profile->HasAnyFlags(RF_ClassDefaultObject) && Profile->ProfileId == ProfileId)
		{
			return Profile;
		}
	}

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
		TEXT("AssetRegistry")).Get();
	TArray<FAssetData> Assets;
	Registry.GetAssetsByClass(UMetaLandRoadCurbProfile::StaticClass()->GetClassPathName(), Assets);
	for (const FAssetData& Data : Assets)
	{
		if (const UMetaLandRoadCurbProfile* Profile = Cast<UMetaLandRoadCurbProfile>(Data.GetAsset()))
		{
			if (Profile->ProfileId == ProfileId)
			{
				return Profile;
			}
		}
	}
	return nullptr;
}

UMaterialInterface* UMetaLandRoadCurbProfile::FindFirstMaterial(UMetaLandRoadSplineComponent* Component)
{
	if (!Component)
	{
		return nullptr;
	}
	const int32 Num = Component->GetLaneSectionsNum();
	for (int32 SectionIndex = 0; SectionIndex < Num; ++SectionIndex)
	{
		const FMetaLandRoadLaneSection& Section = Component->GetLaneSection(SectionIndex);
		auto Search = [](const TArray<FMetaLandRoadLane>& Lanes) -> UMaterialInterface*
		{
			for (const FMetaLandRoadLane& Lane : Lanes)
			{
				const FMetaLandRoadLaneSidewalk* Sidewalk =
					Lane.LaneInstance.GetPtr<FMetaLandRoadLaneSidewalk>();
				if (!Sidewalk)
				{
					continue;
				}
				if (const UMetaLandRoadCurbProfile* Profile = Resolve(
					Sidewalk->CurbProfileAsset,
					Sidewalk->CurbProfile))
				{
					if (Profile->Material)
					{
						return Profile->Material;
					}
				}
			}
			return nullptr;
		};
		if (UMaterialInterface* Mat = Search(Section.Right))
		{
			return Mat;
		}
		if (UMaterialInterface* Mat = Search(Section.Left))
		{
			return Mat;
		}
	}
	return nullptr;
}

void UMetaLandRoadCurbProfile::RecookRoadsUsingProfile(const UMetaLandRoadCurbProfile* Profile)
{
#if WITH_EDITOR
	if (!Profile || !GEditor)
	{
		return;
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AMoKuEditSplineActor> It(World); It; ++It)
	{
		AMoKuEditSplineActor* Road = *It;
		UMetaLandRoadSplineComponent* Spline = Road ? Road->GetSplineComponent() : nullptr;
		if (!Spline)
		{
			continue;
		}
		bool bUses = false;
		const int32 Num = Spline->GetLaneSectionsNum();
		for (int32 SectionIndex = 0; SectionIndex < Num && !bUses; ++SectionIndex)
		{
			const FMetaLandRoadLaneSection& Section = Spline->GetLaneSection(SectionIndex);
			auto CheckFace = [&](const TArray<FMetaLandRoadLane>& Lanes)
			{
				for (const FMetaLandRoadLane& Lane : Lanes)
				{
					const FMetaLandRoadLaneSidewalk* Sidewalk =
						Lane.LaneInstance.GetPtr<FMetaLandRoadLaneSidewalk>();
					if (!Sidewalk)
					{
						continue;
					}
					if (Sidewalk->CurbProfileAsset.Get() == Profile)
					{
						bUses = true;
						return;
					}
					for (const FMetaLandRoadSidewalkCurbKey& Key : Sidewalk->CurbKeys)
					{
						if (Key.CurbProfileAsset.Get() == Profile)
						{
							bUses = true;
							return;
						}
					}
				}
			};
			CheckFace(Section.Left);
			CheckFace(Section.Right);
		}
		if (bUses)
		{
			Road->RefreshDynamicRoadMesh();
		}
	}
#endif
}

void UMetaLandRoadCurbProfile::AppendSweptMesh(
	const TArray<FVector>& Path,
	const TArray<FVector>& Outwards,
	const TArray<float>& AlongS,
	float HeightOffset,
	bool bGenerateUVs,
	bool bInCapEnds,
	FDynamicMesh3& OutMesh) const
{
	TArray<FVector2D> ProfilePts;
	GetProfilePoints(ProfilePts);
	const int32 NumPath = Path.Num();
	const int32 NumProf = ProfilePts.Num();
	if (NumPath < 2 || NumProf < 2 || Outwards.Num() != NumPath || AlongS.Num() != NumPath)
	{
		return;
	}

	if (!OutMesh.HasAttributes())
	{
		OutMesh.EnableAttributes();
	}
	if (OutMesh.Attributes() && !OutMesh.Attributes()->HasMaterialID())
	{
		OutMesh.Attributes()->EnableMaterialID();
	}
	if (bGenerateUVs && !OutMesh.HasVertexUVs())
	{
		OutMesh.EnableVertexUVs(FVector2f::Zero());
	}

	TArray<float> ProfU;
	ProfU.SetNum(NumProf);
	ProfU[0] = 0.0f;
	for (int32 j = 1; j < NumProf; ++j)
	{
		ProfU[j] = ProfU[j - 1]
			+ static_cast<float>(FVector2D::Distance(ProfilePts[j - 1], ProfilePts[j]));
	}
	const float Tile = FMath::Max(UvTileCm, 10.0f);
	const int32 Base = OutMesh.VertexCount();
	const FVector UpOffset(0.0, 0.0, HeightOffset);
	constexpr int32 CurbMatId = 0;
	// Lowest key sits on the road edge. The cross-section builds upward, never into the ground.
	double ProfileBaseY = ProfilePts[0].Y;
	for (const FVector2D& Pt : ProfilePts)
	{
		ProfileBaseY = FMath::Min(ProfileBaseY, static_cast<double>(Pt.Y));
	}

	for (int32 i = 0; i < NumPath; ++i)
	{
		const FVector Out = Outwards[i].GetSafeNormal();
		for (int32 j = 0; j < NumProf; ++j)
		{
			const FVector2D P = ProfilePts[j];
			FVertexInfo V;
			const double HeightCm = static_cast<double>(P.Y) - ProfileBaseY;
			V.Position = Path[i] + Out * static_cast<double>(P.X) + FVector(0.0, 0.0, HeightCm) + UpOffset;
			if (bGenerateUVs)
			{
				V.bHaveUV = true;
				V.UV = FVector2f(ProfU[j] / Tile, AlongS[i] / Tile);
			}
			const int32 Vid = OutMesh.AppendVertex(V);
			if (bGenerateUVs && OutMesh.HasVertexUVs())
			{
				OutMesh.SetVertexUV(Vid, V.UV);
			}
		}
	}

	auto Vid = [Base, NumProf](int32 i, int32 j) { return Base + i * NumProf + j; };
	auto AddTri = [&OutMesh](int32 A, int32 B, int32 C)
	{
		const int32 Tid = OutMesh.AppendTriangle(FIndex3i(A, B, C));
		if (Tid >= 0 && OutMesh.Attributes() && OutMesh.Attributes()->HasMaterialID())
		{
			OutMesh.Attributes()->GetMaterialID()->SetValue(Tid, CurbMatId);
		}
	};

	// One winding for the whole strip. Per-segment flips twist a curve into a triangle fan.
	// Cross(tangent, outward).Z < 0 means the unflipped horizontal cap would face down.
	bool bFlipWinding = false;
	for (int32 i = 0; i < NumPath - 1; ++i)
	{
		const FVector SegTangent = Path[i + 1] - Path[i];
		const double CrossZ = FVector::CrossProduct(SegTangent, Outwards[i]).Z;
		if (FMath::Abs(CrossZ) > 1.0e-4)
		{
			bFlipWinding = CrossZ < 0.0;
			break;
		}
	}
	for (int32 i = 0; i < NumPath - 1; ++i)
	{
		for (int32 j = 0; j < NumProf - 1; ++j)
		{
			if (bFlipWinding)
			{
				AddTri(Vid(i, j), Vid(i, j + 1), Vid(i + 1, j));
				AddTri(Vid(i + 1, j), Vid(i, j + 1), Vid(i + 1, j + 1));
			}
			else
			{
				AddTri(Vid(i, j), Vid(i + 1, j), Vid(i, j + 1));
				AddTri(Vid(i + 1, j), Vid(i + 1, j + 1), Vid(i, j + 1));
			}
		}
	}

	if (!bInCapEnds || NumProf < 3)
	{
		return;
	}

	const int32 NumLoop = (bClosedProfile && ProfilePts[0].Equals(ProfilePts.Last(), 0.05))
		? (NumProf - 1)
		: NumProf;
	if (NumLoop < 3)
	{
		return;
	}

	auto AddCap = [&](int32 PathIndex, const FVector& DesiredNormal)
	{
		TArray<int32> Loop;
		Loop.Reserve(NumLoop);
		for (int32 j = 0; j < NumLoop; ++j)
		{
			FVertexInfo CapV;
			CapV.Position = OutMesh.GetVertex(Vid(PathIndex, j));
			if (bGenerateUVs)
			{
				CapV.bHaveUV = true;
				CapV.UV = FVector2f(ProfilePts[j].X / Tile, ProfilePts[j].Y / Tile);
			}
			const int32 CapVid = OutMesh.AppendVertex(CapV);
			if (bGenerateUVs && OutMesh.HasVertexUVs())
			{
				OutMesh.SetVertexUV(CapVid, CapV.UV);
			}
			Loop.Add(CapVid);
		}
		const FVector A = OutMesh.GetVertex(Loop[0]);
		const FVector B = OutMesh.GetVertex(Loop[1]);
		const FVector C = OutMesh.GetVertex(Loop[2]);
		const FVector Cross = FVector::CrossProduct(B - A, C - A);
		const bool bFlip = FVector::DotProduct(Cross, DesiredNormal) < 0.0;
		for (int32 t = 1; t < NumLoop - 1; ++t)
		{
			if (bFlip)
			{
				AddTri(Loop[0], Loop[t + 1], Loop[t]);
			}
			else
			{
				AddTri(Loop[0], Loop[t], Loop[t + 1]);
			}
		}
	};

	FVector StartTan = Path[1] - Path[0];
	if (!StartTan.Normalize())
	{
		StartTan = FVector::XAxisVector;
	}
	FVector EndTan = Path.Last() - Path[Path.Num() - 2];
	if (!EndTan.Normalize())
	{
		EndTan = StartTan;
	}
	AddCap(0, -StartTan);
	AddCap(NumPath - 1, EndTan);
}

#if WITH_EDITOR
void UMetaLandRoadCurbProfile::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const FName Name = PropertyChangedEvent.GetMemberPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(UMetaLandRoadCurbProfile, Width)
		|| Name == GET_MEMBER_NAME_CHECKED(UMetaLandRoadCurbProfile, Height))
	{
		ApplyDimensionsToCurve();
	}
	else if (Name == GET_MEMBER_NAME_CHECKED(UMetaLandRoadCurbProfile, CurbCurve))
	{
		ReadDimensionsFromCurve();
	}
	EnsureDefaultCurve();
	RecookRoadsUsingProfile(this);
}

void UMetaLandRoadCurbProfile::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
	const FProperty* Member = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()
		? PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue()
		: nullptr;
	if (Member && Member->GetFName() == GET_MEMBER_NAME_CHECKED(UMetaLandRoadCurbProfile, CurbCurve))
	{
		ReadDimensionsFromCurve();
	}
	RecookRoadsUsingProfile(this);
}
#endif

namespace MetaLandRoadCurb
{
	void AppendOneCurb(
		UMetaLandRoadSplineComponent* Component,
		int32 SectionIndex,
		int32 LaneIndex,
		double Alpha,
		bool bOutwardPositive,
		int32 EdgeOutwardCm,
		const UMetaLandRoadCurbProfile* Profile,
		double S0,
		double S1,
		int32 MinNumSteps,
		float RoadHeightOffset,
		bool bGenerateUVs,
		FDynamicMesh3& OutMesh)
	{
		if (!Component || !Profile)
		{
			return;
		}

		TArray<FMetaLandRoadSplinePositionLinearApproximation> Samples;
		Component->BuildLinearApproximation(
			Samples,
			[Component, SectionIndex, LaneIndex, Alpha](double S)
			{
				return Component->GetLaneSection(SectionIndex).EvalLaneROffset(LaneIndex, S, Alpha)
					+ Component->EvalROffset(S);
			},
			S0,
			S1,
			MetaLandRoad::DefaultReparamStepsPerSegment,
			MinNumSteps,
			ESplineCoordinateSpace::Local);
		if (Samples.Num() < 2)
		{
			return;
		}

		TArray<FVector> Path;
		TArray<FVector> Outwards;
		TArray<float> AlongS;
		Path.Reserve(Samples.Num());
		Outwards.Reserve(Samples.Num());
		AlongS.Reserve(Samples.Num());
		for (const FMetaLandRoadSplinePositionLinearApproximation& Sample : Samples)
		{
			const double S = Component->GetDistanceAlongSplineAtSplineInputKey(Sample.SplineParam);
			FVector Right = Component->GetRightVectorAtDistanceAlongSpline(
				S, ESplineCoordinateSpace::Local);
			Right.Z = 0.0;
			if (!Right.Normalize())
			{
				Right = FVector::YAxisVector;
			}
			const FVector Center = Component->GetLocationAtDistanceAlongSpline(
				S, ESplineCoordinateSpace::Local);
			const FVector LaneOut = bOutwardPositive ? Right : -Right;
			FVector Outward = Sample.Position - Center;
			Outward.Z = 0.0;
			if (!Outward.Normalize())
			{
				Outward = LaneOut;
			}
			const double Shift = static_cast<double>(EdgeOutwardCm + Profile->OutwardOffset);
			Path.Add(Sample.Position + Outward * Shift);
			// +X is the grass side. The sweep uses one winding so the top faces up.
			Outwards.Add(Outward);
			AlongS.Add(static_cast<float>(S));
		}
		Profile->AppendSweptMesh(
			Path, Outwards, AlongS, RoadHeightOffset, bGenerateUVs, Profile->bCapEnds, OutMesh);
	}

	void AppendSidewalkLane(
		UMetaLandRoadSplineComponent* Component,
		int32 SectionIndex,
		int32 LaneIndex,
		double S0,
		double S1,
		int32 MinNumSteps,
		float RoadHeightOffset,
		bool bGenerateUVs,
		FDynamicMesh3& OutMesh)
	{
		if (!Component || SectionIndex < 0 || SectionIndex >= Component->GetLaneSectionsNum() || LaneIndex == 0)
		{
			return;
		}

		const FMetaLandRoadLaneSection& Section = Component->GetLaneSection(SectionIndex);
		const TArray<FMetaLandRoadLane>& Lanes = LaneIndex > 0 ? Section.Right : Section.Left;
		const int32 ArrayIndex = FMath::Abs(LaneIndex) - 1;
		if (!Lanes.IsValidIndex(ArrayIndex))
		{
			return;
		}
		const FMetaLandRoadLaneSidewalk* Sidewalk =
			Lanes[ArrayIndex].LaneInstance.GetPtr<FMetaLandRoadLaneSidewalk>();
		if (!Sidewalk)
		{
			return;
		}
		const UMetaLandRoadCurbProfile* Profile = Sidewalk->CurbProfileAsset.IsNull()
				? nullptr
				: UMetaLandRoadCurbProfile::Resolve(Sidewalk->CurbProfileAsset, NAME_None);
			bool bHasKeyProfile = false;
			for (const FMetaLandRoadSidewalkCurbKey& Key : Sidewalk->CurbKeys)
			{
				if (!Key.CurbProfileAsset.IsNull())
				{
					bHasKeyProfile = true;
					break;
				}
			}
		if (!Profile && !bHasKeyProfile)
		{
			return;
		}

			const bool bOutwardFromRoad = LaneIndex > 0;

			TArray<double> Splits;
			Splits.Add(S0);
			for (const FMetaLandRoadSidewalkCurbKey& Key : Sidewalk->CurbKeys)
			{
				if (Key.SOffset > S0 + 1.0 && Key.SOffset < S1 - 1.0)
				{
					Splits.Add(Key.SOffset);
				}
			}
			Splits.Add(S1);
			Splits.Sort();

			auto ProfileAtS = [&](double S) -> const UMetaLandRoadCurbProfile*
			{
				const UMetaLandRoadCurbProfile* SpanProfile = Profile;
				double BestS = -MAX_dbl;
				for (const FMetaLandRoadSidewalkCurbKey& Key : Sidewalk->CurbKeys)
				{
					if (Key.SOffset <= S + 0.5 && Key.SOffset >= BestS && !Key.CurbProfileAsset.IsNull())
					{
						if (const UMetaLandRoadCurbProfile* KeyProfile =
							UMetaLandRoadCurbProfile::Resolve(Key.CurbProfileAsset, NAME_None))
						{
							SpanProfile = KeyProfile;
							BestS = Key.SOffset;
						}
					}
				}
				return SpanProfile;
			};

			for (int32 Span = 0; Span < Splits.Num() - 1; ++Span)
			{
				const double A = Splits[Span];
				const double B = Splits[Span + 1];
				if (B - A < 2.0)
				{
					continue;
				}
				const UMetaLandRoadCurbProfile* SpanProfile = ProfileAtS(A);
				if (!SpanProfile)
				{
					continue;
				}
				int32 SpanOffset = Sidewalk->OutwardOffsetCm;
				double BestKeyS = -MAX_dbl;
				for (const FMetaLandRoadSidewalkCurbKey& Key : Sidewalk->CurbKeys)
				{
					if (Key.SOffset <= A + 0.5 && Key.SOffset >= BestKeyS)
					{
						SpanOffset = Key.OutwardOffsetCm;
						BestKeyS = Key.SOffset;
					}
				}
				AppendOneCurb(
					Component,
					SectionIndex,
					LaneIndex,
					1.0,
					bOutwardFromRoad,
					SpanOffset,
					SpanProfile,
					A,
					B,
					MinNumSteps,
					RoadHeightOffset,
					bGenerateUVs,
					OutMesh);
			}
	}

	void AppendSidewalkCurbs(
		UMetaLandRoadSplineComponent* Component,
		int32 SectionIndex,
		double S0,
		double S1,
		int32 MinNumSteps,
		float RoadHeightOffset,
		bool bGenerateUVs,
		FDynamicMesh3& OutMesh,
		int32 LaneSign)
	{
		if (!Component || SectionIndex < 0 || SectionIndex >= Component->GetLaneSectionsNum())
		{
			return;
		}
		const FMetaLandRoadLaneSection& Section = Component->GetLaneSection(SectionIndex);
		const TArray<FMetaLandRoadLane>& Lanes = LaneSign > 0 ? Section.Right : Section.Left;
		for (int32 i = 0; i < Lanes.Num(); ++i)
		{
			if (!Lanes[i].LaneInstance.GetPtr<FMetaLandRoadLaneSidewalk>())
			{
				continue;
			}
			const int32 LaneIndex = LaneSign > 0 ? (i + 1) : -(i + 1);
			AppendSidewalkLane(
				Component,
				SectionIndex,
				LaneIndex,
				S0,
				S1,
				MinNumSteps,
				RoadHeightOffset,
				bGenerateUVs,
				OutMesh);
		}
	}
}
