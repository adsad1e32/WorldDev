#include "MetaLandRoadPropInstances.h"
#include "MetaLandRoadLaneAttribute.h"
#include "MetaLandRoadPropLibrary.h"
#include "MetaLandRoadPropSetting.h"
#include "MetaLandRoadSplineComponent.h"
#include "MoKuEditSplineActor.h"
#include "Misc/CoreMisc.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"

namespace
{
	UStaticMesh* ResolveFenceMesh(const TSoftObjectPtr<UStaticMesh>& Authored)
	{
		if (UStaticMesh* Loaded = Authored.LoadSynchronous())
		{
			return Loaded;
		}
		if (const FMetaLandRoadPropAssetRow* Row = FMetaLandRoadPropLibrary::FindDefaultFenceRow())
		{
			return Row->Mesh.LoadSynchronous();
		}
		return nullptr;
	}

	float MeshAlongX(const UStaticMesh* Mesh)
	{
		if (!Mesh)
		{
			return 200.0f;
		}
		return FMath::Max(Mesh->GetBoundingBox().GetSize().X, 1.0f);
	}

	float DeterministicScaleZ(const FGuid& RailId, int32 SampleIndex)
	{
		const uint32 Hash = GetTypeHash(RailId) ^ (static_cast<uint32>(SampleIndex) * 0x9e3779b9u);
		const float Unit = static_cast<float>(Hash % 1001) / 1000.0f;
		return 1.0f + (Unit * 2.0f - 1.0f) * UMetaLandRoadPropInstanceComponent::ScaleZRange;
	}

	UHierarchicalInstancedStaticMeshComponent* FindOrAddHism(
		UMetaLandRoadPropInstanceComponent* Host,
		UStaticMesh* Mesh)
	{
		if (!Host || !Mesh)
		{
			return nullptr;
		}

		const FName CompName(*FString::Printf(TEXT("PropHISM_%s"), *Mesh->GetName()));
		TArray<USceneComponent*> Children;
		Host->GetChildrenComponents(false, Children);
		for (USceneComponent* Child : Children)
		{
			UHierarchicalInstancedStaticMeshComponent* Existing =
				Cast<UHierarchicalInstancedStaticMeshComponent>(Child);
			if (Existing && Existing->GetFName() == CompName)
			{
				Existing->SetStaticMesh(Mesh);
				return Existing;
			}
		}

		AActor* Owner = Host->GetOwner();
		UHierarchicalInstancedStaticMeshComponent* Hism = NewObject<UHierarchicalInstancedStaticMeshComponent>(
			Owner ? static_cast<UObject*>(Owner) : Host,
			UHierarchicalInstancedStaticMeshComponent::StaticClass(),
			CompName,
			RF_Transactional);
		Hism->SetupAttachment(Host);
		Hism->SetStaticMesh(Mesh);
		Hism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Hism->SetCanEverAffectNavigation(false);
		Hism->SetMobility(EComponentMobility::Movable);
		Hism->SetVisibility(true);
		Hism->SetHiddenInGame(false);
		Hism->SetCastShadow(true);
		Hism->bSelectable = false;
		Hism->CreationMethod = EComponentCreationMethod::Instance;
		if (Owner && Owner->GetWorld())
		{
			Owner->Modify();
			Hism->RegisterComponent();
			Owner->AddInstanceComponent(Hism);
		}
		return Hism;
	}
}

void UMetaLandRoadPropInstanceComponent::RefreshForRoad(AMoKuEditSplineActor* Road)
{
	if (!Road || !Road->GetWorld() || UE::GetIsEditorLoadingPackage())
	{
		return;
	}

	UMetaLandRoadPropInstanceComponent* Host = Road->PropInstanceHost;
	if (!Host)
	{
		Host = Road->FindComponentByClass<UMetaLandRoadPropInstanceComponent>();
	}
	if (!Host)
	{
		return;
	}
	Host->Rebuild();
}

void UMetaLandRoadPropInstanceComponent::Rebuild()
{
	AMoKuEditSplineActor* Road = Cast<AMoKuEditSplineActor>(GetOwner());
	UMetaLandRoadSplineComponent* Spline = Road ? Road->GetSplineComponent() : nullptr;
	if (!Road || !Spline)
	{
		return;
	}

	TArray<TPair<double, double>> VisibleSpans;
	Road->GetJunctionMeshGenerationSpans(VisibleSpans);

	TMap<UStaticMesh*, TArray<FTransform>> Batches;
	const TArray<FMetaLandRoadLaneSection>& Sections = Spline->GetLaneSections();
	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		const FMetaLandRoadLaneAttribute* Track =
			Sections[SectionIndex].FindAttribute(FMetaLandRoadAttributeRegistry::TypePropSetting);
		if (!Track)
		{
			continue;
		}

		const double SectionS0 = Sections[SectionIndex].SOffset;
		for (const FMetaLandRoadPropRail& Rail : Track->Rails)
		{
			const int32 KeyCount = Rail.Keys.Num();
			if (KeyCount < 2)
			{
				continue;
			}

			for (int32 KeySpan = 0; KeySpan < KeyCount - 1; ++KeySpan)
			{
				const FMetaLandRoadPropRailKey& Key = Rail.Keys[KeySpan];
				if (Key.Layout == EMetaLandRoadPropLayout::Off)
				{
					continue;
				}
				UStaticMesh* Mesh = ResolveFenceMesh(Key.Mesh);
				if (!Mesh)
				{
					continue;
				}

				const double KeyS0 = SectionS0 + Key.SOffset;
				const double KeyS1 = SectionS0 + Rail.Keys[KeySpan + 1].SOffset;
				const float MeshX = MeshAlongX(Mesh);
				const float Gap = (Key.Layout == EMetaLandRoadPropLayout::EvenSpacing)
					? FMath::Max(Key.SpacingCm, 0.0f)
					: 0.0f;
				const float Pitch = FMath::Max(MeshX + Gap, 1.0f);

				int32 SampleIndex = 0;
				for (const TPair<double, double>& Span : VisibleSpans)
				{
					const double DrawS0 = FMath::Max(KeyS0, Span.Key);
					const double DrawS1 = FMath::Min(KeyS1, Span.Value);
					const float SpanLen = static_cast<float>(DrawS1 - DrawS0);
					if (SpanLen <= KINDA_SMALL_NUMBER)
					{
						continue;
					}

					int32 Count = FMath::FloorToInt(SpanLen / Pitch);
					if (Count < 1 && SpanLen >= MeshX * 0.5f)
					{
						Count = 1;
					}
					Count = FMath::Clamp(Count, 0, 400);
					if (Count <= 0)
					{
						continue;
					}

					const float SumMesh = static_cast<float>(Count) * MeshX;
					const float SumGaps = static_cast<float>(FMath::Max(Count - 1, 0)) * Gap;
					const float AlongScale = (SpanLen - SumGaps) / SumMesh;
					if (AlongScale <= KINDA_SMALL_NUMBER)
					{
						continue;
					}
					const float UsedLen = MeshX * AlongScale;
					const float Step = UsedLen + Gap;
					for (int32 Index = 0; Index < Count; ++Index)
					{
						const double WorldS = DrawS0 + static_cast<double>(Index) * static_cast<double>(Step);
						if (WorldS + UsedLen > DrawS1 + 1.0)
						{
							break;
						}
						FVector World;
						if (!FMetaLandRoadPropSettingUtil::EvalRailWorld(Spline, WorldS, Rail, World)
							&& !FMetaLandRoadPropSettingUtil::EvalRailWorld(
								Spline, WorldS + 0.5, Rail, World))
						{
							continue;
						}
						World.Z += Key.ZOffsetCm;

						FVector Tangent = Spline->GetTangentAtDistanceAlongSpline(
							WorldS, ESplineCoordinateSpace::World).GetSafeNormal();
						if (Tangent.IsNearlyZero())
						{
							FVector Ahead;
							if (FMetaLandRoadPropSettingUtil::EvalRailWorld(Spline, WorldS + 10.0, Rail, Ahead))
							{
								Tangent = (Ahead - World).GetSafeNormal();
							}
						}
						if (Tangent.IsNearlyZero())
						{
							Tangent = FVector::ForwardVector;
						}

						const FRotator Rot = FRotationMatrix::MakeFromXZ(Tangent, FVector::UpVector).Rotator();
						const FVector Scale(
							AlongScale,
							1.0f,
							DeterministicScaleZ(Rail.RailId, SampleIndex++));
						Batches.FindOrAdd(Mesh).Add(FTransform(Rot, World, Scale));
					}
				}
			}
		}
	}

	TSet<FName> KeepNames;
	for (TPair<UStaticMesh*, TArray<FTransform>>& Batch : Batches)
	{
		UHierarchicalInstancedStaticMeshComponent* Hism = FindOrAddHism(this, Batch.Key);
		if (!Hism)
		{
			continue;
		}
		KeepNames.Add(Hism->GetFName());
		Hism->ClearInstances();
		const FTransform CompToWorld = Hism->GetComponentTransform();
		for (const FTransform& WorldXform : Batch.Value)
		{
			Hism->AddInstance(WorldXform.GetRelativeTransform(CompToWorld), false);
		}
		Hism->MarkRenderStateDirty();
	}

	TArray<USceneComponent*> Children;
	GetChildrenComponents(false, Children);
	for (USceneComponent* Child : Children)
	{
		UHierarchicalInstancedStaticMeshComponent* Hism =
			Cast<UHierarchicalInstancedStaticMeshComponent>(Child);
		if (Hism && !KeepNames.Contains(Hism->GetFName()))
		{
			Hism->DestroyComponent();
		}
	}
}
