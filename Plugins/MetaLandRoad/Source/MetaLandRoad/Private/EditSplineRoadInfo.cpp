#include "EditSplineRoadInfo.h"
#include "MoKuEditSplineActor.h"
#include "Components/DynamicMeshComponent.h"
#include "UDynamicMesh.h"
#include "Components/SceneCaptureComponent2D.h"
#include "FloodComponent2D.h"
#include "SplineRoadEditorSubsystem.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Materials/MaterialParameterCollection.h"
#include "WaterUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Canvas.h"
#include "WorldEditorSettings.h"
#include "Kismet/KismetRenderingLibrary.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(EditSplineRoadInfo)
#define LOCTEXT_NAMESPACE "EditPolyLine"

namespace MetaLandRoadBrushMaterials
{
	// Local copies (duplicate from Water/Landmass via CopyBrushMaterialsToMetaLandRoad.py).
	static const TCHAR* BrushAngleLocal = TEXT("/MetaLandRoad/Material/Landscape/Brush/MeshBrush_Angle.MeshBrush_Angle");
	static const TCHAR* BrushWidthLocal = TEXT("/MetaLandRoad/Material/Landscape/Brush/MeshBrush_Width.MeshBrush_Width");
	// Local forks still embed /TestMode/ → Landscape stuck on "Waiting for edit layer resources".
	// Always prefer official Water MeshBrush; CapShapeInterior is set from C++ either way.
	static const TCHAR* BrushWeightmapLocal = TEXT("/MetaLandRoad/Material/Landscape/Brush/MeshBrush_Weightmap.MeshBrush_Weightmap");
	static const TCHAR* CacheDistanceFieldLocal = TEXT("/MetaLandRoad/Material/Landscape/CacheDistanceField.CacheDistanceField");
	static const TCHAR* CompositeBodyLocal = TEXT("/MetaLandRoad/Material/Landscape/Brush/CompositeMeshBodyTexture.CompositeMeshBodyTexture");
	static const TCHAR* RenderSplineDepthsLocal = TEXT("/MetaLandRoad/Material/Landscape/RenderMeshDepths.RenderMeshDepths");
	static const TCHAR* JumpFloodStepLocal = TEXT("/MetaLandRoad/Material/Landscape/DistanceFields/JumpFloodStep.JumpFloodStep");
	static const TCHAR* BlurEdgesLocal = TEXT("/MetaLandRoad/Material/Landscape/DistanceFields/BlurEdges.BlurEdges");
	static const TCHAR* FindEdgesLocal = TEXT("/MetaLandRoad/Material/Landscape/DistanceFields/DetectEdges.DetectEdges");
	static const TCHAR* MPCLocal = TEXT("/MetaLandRoad/Landscape/BlueprintBrushes/MPC/MPC_Landscape.MPC_Landscape");

	// Official fallbacks until local copies exist.
	static const TCHAR* BrushAngleOfficial = TEXT("/Water/Materials/Brushes/MeshBrush_Angle.MeshBrush_Angle");
	static const TCHAR* BrushWidthOfficial = TEXT("/Water/Materials/Brushes/MeshBrush_Width.MeshBrush_Width");
	static const TCHAR* BrushWeightmapOfficial = TEXT("/Water/Materials/Brushes/MeshBrush_Weightmap.MeshBrush_Weightmap");
	static const TCHAR* CacheDistanceFieldOfficial = TEXT("/Landmass/Landscape/BlueprintBrushes/Materials/CacheDistanceField.CacheDistanceField");
	static const TCHAR* CompositeBodyOfficial = TEXT("/Water/Materials/Brushes/CompositeWaterBodyTexture.CompositeWaterBodyTexture");
	static const TCHAR* RenderSplineDepthsOfficial = TEXT("/Landmass/Landscape/BlueprintBrushes/Materials/RenderSplineDepths.RenderSplineDepths");
	static const TCHAR* JumpFloodStepOfficial = TEXT("/Landmass/DistanceFields/Materials/JumpFloodStep.JumpFloodStep");
	static const TCHAR* BlurEdgesOfficial = TEXT("/Landmass/DistanceFields/Materials/BlurEdges.BlurEdges");
	static const TCHAR* FindEdgesOfficial = TEXT("/Landmass/DistanceFields/Materials/DetectEdges.DetectEdges");
	static const TCHAR* MPCOfficial = TEXT("/Landmass/Landscape/BlueprintBrushes/MPC/MPC_Landscape.MPC_Landscape");

	// Prefer official Water/Landmass first. Local /MetaLandRoad/ Landscape fork assets still
	// embed /TestMode/ soft paths; loading them makes shaders never compile and Landscape
	// stuck on "Waiting for edit layer resources". Use local only after
	// CopyBrushMaterialsToMetaLandRoad.py has overwritten those forks with clean duplicates.
	static UMaterialInterface* LoadMaterial(const TCHAR* LocalPath, const TCHAR* OfficialPath, const TCHAR* Label)
	{
		UMaterialInterface* Material = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, OfficialPath));
		if (Material != nullptr)
		{
			UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] Brush material '%s' -> %s (official)"), Label, OfficialPath);
			return Material;
		}

		Material = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, LocalPath));
		if (Material != nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] Brush material '%s' -> %s (local fallback; run CopyBrushMaterialsToMetaLandRoad.py if shaders fail)"), Label, LocalPath);
			return Material;
		}

		UE_LOG(LogTemp, Error, TEXT("[RoadLandscape] Failed to load brush material '%s' (official=%s, local=%s)"), Label, OfficialPath, LocalPath);
		return nullptr;
	}

	static UMaterialParameterCollection* LoadMPC()
	{
		UMaterialParameterCollection* MPC = Cast<UMaterialParameterCollection>(StaticLoadObject(UMaterialParameterCollection::StaticClass(), nullptr, MPCOfficial));
		if (MPC != nullptr)
		{
			return MPC;
		}
		return Cast<UMaterialParameterCollection>(StaticLoadObject(UMaterialParameterCollection::StaticClass(), nullptr, MPCLocal));
	}
}

AWorldSceneManagement::AWorldSceneManagement(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
, LandscapeRTRes(0, 0)
, WorldSize(FVector::ZeroVector)
, LandscapeTransform(FTransform::Identity)
{

	TArray<FEngineShowFlagsSetting> ShowFlagSettings;

	JumpFloodComponent2D = CreateDefaultSubobject<UFloodComponent2D>(TEXT("JumpFloodComponent2D"));
	Octree = TOctree2<FSplineSegmentOctree, FSegmentsOctreeSemantics>(FVector::ZeroVector, HALF_WORLD_MAX);
	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCaptureComponent2D"));
	SceneCapture->CreationMethod = EComponentCreationMethod::Native;
	SceneCapture->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
	SceneCapture->ProjectionType = ECameraProjectionMode::Type::Orthographic;
	SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	SceneCapture->bCaptureEveryFrame = false;
	SceneCapture->bCaptureOnMovement = false;
	SceneCapture->bExcludeFromSceneTextureExtents = true;  // Don't let this influence the render target size used for other purposes.
	SceneCapture->SetRelativeRotation(FRotator(-90.0f, 0.0f, -90.0f));
	SceneCapture->SetRelativeScale3D(FVector(0.01f, 0.01f, 0.01f));

	ShowFlagSettings.Add(FEngineShowFlagsSetting{ TEXT("NaniteMeshes"), false });
	// These also need to be disabled to get a clean capture of just the water info material output
	ShowFlagSettings.Add(FEngineShowFlagsSetting{ TEXT("Atmosphere"), false });
	ShowFlagSettings.Add(FEngineShowFlagsSetting{ TEXT("Bloom"), false });
	ShowFlagSettings.Add(FEngineShowFlagsSetting{ TEXT("Lighting"), false });
	ShowFlagSettings.Add(FEngineShowFlagsSetting{ TEXT("Fog"), false });
	SceneCapture->SetShowFlagSettings(ShowFlagSettings);


}

void AWorldSceneManagement::Initialize_Native(FTransform const& InLandscapeTransform, FIntPoint const& InLandscapeSize, FIntPoint const& InLandscapeRenderTargetSize)
{
	//UE_LOG(LogWaterEditor, Verbose, TEXT("Updated Landscape Transform"));
	bool bNeedsFullUpdate = false;
	if (LandscapeQuads != InLandscapeSize)
	{
		LandscapeQuads = InLandscapeSize;
		bNeedsFullUpdate = true;
	}
	if (LandscapeRTRes != InLandscapeRenderTargetSize)
	{
		LandscapeRTRes = InLandscapeRenderTargetSize;
		bNeedsFullUpdate = true;
	}
	if (!InLandscapeTransform.Equals(LandscapeTransform))
	{
		LandscapeTransform = InLandscapeTransform;
		bNeedsFullUpdate = true;
	}

	if (bNeedsFullUpdate)
	{
		check(SceneCapture != nullptr);

		FVector Scale = LandscapeTransform.GetScale3D();
		WorldSize.Set(Scale.X * (float)LandscapeQuads.X, Scale.Y * (float)LandscapeQuads.Y, 0.512f);

		const FVector Temp(Scale.X * (float)LandscapeRTRes.X, Scale.Y * (float)LandscapeRTRes.Y, 0.512f);
		SceneCapture->OrthoWidth = FMath::Max(Temp.X, Temp.Y);

		FVector LocationVector(Temp - Scale);
		LocationVector *= 0.5f;
		LocationVector = LandscapeTransform.GetRotation().RotateVector(LocationVector);
		LocationVector += LandscapeTransform.GetLocation();
		LocationVector.Z = 50000.0f;
		SceneCapture->SetWorldLocation(LocationVector);

		// If the transform or resolution changes, the distance fields need to be recomputed entirely : 
		bKillCache = true;
	}
}


void AWorldSceneManagement::InitSceneCaptureAndParam()
{

		SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));

		if (RootComponent)
		{
			SceneCapture->SetupAttachment(RootComponent);
		}
		else
		{
			RootComponent = SceneCapture;
		}

		JumpFloodComponent2D = CreateDefaultSubobject<UFloodComponent2D>(TEXT("JumpFloodComponent2D"));
		SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCaptureComponent2D"));
		SceneCapture->CreationMethod = EComponentCreationMethod::Native;
		SceneCapture->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
		SceneCapture->ProjectionType = ECameraProjectionMode::Type::Orthographic;
		SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		SceneCapture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
		SceneCapture->bCaptureEveryFrame = false;
		SceneCapture->bCaptureOnMovement = false;
		SceneCapture->bExcludeFromSceneTextureExtents = true;
		SceneCapture->SetRelativeRotation(FRotator(-90.0f, 0.0f, -90.0f));
		SceneCapture->SetRelativeScale3D(FVector(0.01f, 0.01f, 0.01f));

		TArray<FEngineShowFlagsSetting> ShowFlagSettings;

		ShowFlagSettings.Add(FEngineShowFlagsSetting{ TEXT("NaniteMeshes"), false });

		ShowFlagSettings.Add(FEngineShowFlagsSetting{ TEXT("Atmosphere"), false });
		ShowFlagSettings.Add(FEngineShowFlagsSetting{ TEXT("Bloom"), false });
		ShowFlagSettings.Add(FEngineShowFlagsSetting{ TEXT("Lighting"), false });
		ShowFlagSettings.Add(FEngineShowFlagsSetting{ TEXT("Fog"), false });
		SceneCapture->SetShowFlagSettings(ShowFlagSettings);

		PrimaryActorTick.TickGroup = ETickingGroup::TG_PrePhysics;
		bIsEditorOnlyActor = false;

}

bool AWorldSceneManagement::AllocateRTs()
{
	// Live Coding / Blueprint reinstancer creates CDOs — never allocate RTs on templates.
	if (!IsValid(this)
		|| HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_BeginDestroyed)
		|| IsTemplate())
	{
		return false;
	}

	// Details shows None if we never painted (LandscapeRTRes still 0,0) — resolve size first.
	if (LandscapeRTRes.X <= 0 || LandscapeRTRes.Y <= 0)
	{
		FVector UnusedLoc, UnusedSize;
		if (!DeprecateWorldLandscapeInfo(UnusedLoc, UnusedSize))
		{
			LandscapeRTRes = FIntPoint(1024, 1024);
			UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] AllocateRTs: LandscapeRTRes was 0 — using fallback %dx%d (trigger Landscape WorldEditor paint for real size)"),
				LandscapeRTRes.X, LandscapeRTRes.Y);
		}
	}

	bool bSuccess = true;
	HeightmapRTA = FWaterUtils::GetOrCreateTransientRenderTarget2D(HeightmapRTA, TEXT("HeightmapRTA"), LandscapeRTRes, RTF_RGBA8);
	HeightmapRTB = FWaterUtils::GetOrCreateTransientRenderTarget2D(HeightmapRTB, TEXT("HeightmapRTB"), LandscapeRTRes, RTF_RGBA8);
	if ((HeightmapRTA == nullptr) || (HeightmapRTB == nullptr))
	{
		bSuccess = false;
	}

	JumpFloodRTA = FWaterUtils::GetOrCreateTransientRenderTarget2D(JumpFloodRTA, TEXT("JumpFloodRTA"), LandscapeRTRes, RTF_RGBA32f);
	JumpFloodRTB = FWaterUtils::GetOrCreateTransientRenderTarget2D(JumpFloodRTB, TEXT("JumpFloodRTB"), LandscapeRTRes, RTF_RGBA32f);
	if ((JumpFloodRTA != nullptr) && (JumpFloodRTB != nullptr) && JumpFloodComponent2D != nullptr)
	{
		JumpFloodRTA->AddressX = TextureAddress::TA_Clamp;
		JumpFloodRTA->AddressY = TextureAddress::TA_Clamp;

		JumpFloodRTB->AddressX = TextureAddress::TA_Clamp;
		JumpFloodRTB->AddressY = TextureAddress::TA_Clamp;

		JumpFloodComponent2D->AssignRenderTargets(JumpFloodRTA, JumpFloodRTB);
	}
	else
	{
		//UE_LOG(LogWaterEditor, Error, TEXT("Invalid JumpFlood Render Target for Water Brush. Aborting AllocateRTs."));
		bSuccess = false;
	}
	DepthAndShapeRT = FWaterUtils::GetOrCreateTransientRenderTarget2D(DepthAndShapeRT, TEXT("DepthAndShapeRT"), LandscapeRTRes, RTF_RG32f);
	if (DepthAndShapeRT != nullptr && SceneCapture != nullptr)
	{
		SceneCapture->TextureTarget = DepthAndShapeRT;
	}
	else if (DepthAndShapeRT == nullptr)
	{
		bSuccess = false;
	}

	RoadDepthRT = FWaterUtils::GetOrCreateTransientRenderTarget2D(RoadDepthRT, TEXT("RoadDepthRT"), LandscapeRTRes, RTF_RGBA32f);
	if (RoadDepthRT == nullptr)
	{	
		bSuccess = false;
	}

	// Must match Water MeshBrush_Weightmap + Landscape weight CombinedResult (R8). R16f can be ignored by merge.
	if (WeightmapRTA && WeightmapRTA->RenderTargetFormat != RTF_R8)
	{
		WeightmapRTA = nullptr;
	}
	if (WeightmapRTB && WeightmapRTB->RenderTargetFormat != RTF_R8)
	{
		WeightmapRTB = nullptr;
	}
	WeightmapRTA = FWaterUtils::GetOrCreateTransientRenderTarget2D(WeightmapRTA, TEXT("RoadWeightmapRTA"), LandscapeRTRes, RTF_R8);
	WeightmapRTB = FWaterUtils::GetOrCreateTransientRenderTarget2D(WeightmapRTB, TEXT("RoadWeightmapRTB"), LandscapeRTRes, RTF_R8);
	if ((WeightmapRTA == nullptr) || (WeightmapRTB == nullptr))
	{
		bSuccess = false;
	}

	CombinedShapeAndHeightRTA = FWaterUtils::GetOrCreateTransientRenderTarget2D(CombinedShapeAndHeightRTA, TEXT("CombinedShapeAndHeightRTA"), LandscapeRTRes, RTF_RGBA16f);
	CombinedShapeAndHeightRTB = FWaterUtils::GetOrCreateTransientRenderTarget2D(CombinedShapeAndHeightRTB, TEXT("CombinedShapeAndHeightRTB"), LandscapeRTRes, RTF_RGBA16f);
	if ((CombinedShapeAndHeightRTA == nullptr) || (CombinedShapeAndHeightRTB == nullptr))
	{
		bSuccess = false;
	}

	// Water always binds CurveRT; keep a linear 0→1 ramp so MeshBrush / CacheDistanceField never sample null.
	DefaultCurveRT = FWaterUtils::GetOrCreateTransientRenderTarget2D(DefaultCurveRT, TEXT("RoadDefaultCurveRT"), FIntPoint(256, 1), RTF_R16f);
	if (DefaultCurveRT != nullptr)
	{
		UCanvas* Canvas = nullptr;
		FVector2D CanvasSize;
		FDrawToRenderTargetContext Context;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, DefaultCurveRT, Canvas, CanvasSize, Context);
		if (Canvas)
		{
			for (int32 i = 0; i < 256; ++i)
			{
				const float V = static_cast<float>(i) / 255.0f;
				Canvas->K2_DrawBox(FVector2D(static_cast<float>(i), 0.0f), FVector2D(1.0f, 2.0f), 1.0f, FLinearColor(V, V, V));
			}
		}
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
	}
	else
	{
		bSuccess = false;
	}

	// Do NOT Rename/re-outer RTs onto this actor — breaks Live Coding Blueprint reinstancer / CDO.
	return bSuccess;
}

#if WITH_EDITOR
void AWorldSceneManagement::DebugForceAllocateRenderTargets()
{
	if (!IsValid(this)
		|| HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_BeginDestroyed)
		|| IsTemplate())
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] DebugForceAllocateRenderTargets skipped (CDO/template)"));
		return;
	}

	FVector UnusedLoc, UnusedSize;
	DeprecateWorldLandscapeInfo(UnusedLoc, UnusedSize);

	const bool bOk = AllocateRTs();
	UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] DebugForceAllocateRenderTargets success=%d LandscapeRTRes=%dx%d"),
		bOk ? 1 : 0, LandscapeRTRes.X, LandscapeRTRes.Y);

	auto LogRT = [](const TCHAR* Name, const UTextureRenderTarget2D* RT)
	{
		if (RT)
		{
			UE_LOG(LogTemp, Log, TEXT("[RoadLandscape]   %s = %s (%dx%d) outer=%s"),
				Name, *RT->GetName(), RT->SizeX, RT->SizeY, *GetNameSafe(RT->GetOuter()));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[RoadLandscape]   %s = None"), Name);
		}
	};
	LogRT(TEXT("HeightmapRTA"), HeightmapRTA);
	LogRT(TEXT("HeightmapRTB"), HeightmapRTB);
	LogRT(TEXT("JumpFloodRTA"), JumpFloodRTA);
	LogRT(TEXT("JumpFloodRTB"), JumpFloodRTB);
	LogRT(TEXT("DepthAndShapeRT"), DepthAndShapeRT);
	LogRT(TEXT("RoadDepthRT"), RoadDepthRT);
	LogRT(TEXT("CombinedShapeAndHeightRTA"), CombinedShapeAndHeightRTA);
	LogRT(TEXT("CombinedShapeAndHeightRTB"), CombinedShapeAndHeightRTB);
	LogRT(TEXT("WeightmapRTA"), WeightmapRTA);
	LogRT(TEXT("WeightmapRTB"), WeightmapRTB);

	// Log-only preview: opening Transient RTs in AssetEditor during Live Coding is unsafe.
	UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] RT slots may still show None in Details (Transient). Use Log sizes above; content fills after WorldEditor paint."));
}
#endif

void AWorldSceneManagement::ComputeWorldLandscapeInfo(FVector& OutRTWorldLocation, FVector& OutRTWorldSizeVector)
{

	FVector LandscapeScale = LandscapeTransform.GetScale3D();
	OutRTWorldSizeVector = FVector(LandscapeRTRes) * LandscapeScale;
	OutRTWorldSizeVector.Z = 1.0f;
	OutRTWorldLocation = LandscapeTransform.GetLocation();
	OutRTWorldLocation -= FVector(LandscapeScale.X, LandscapeScale.Y, 0.0f) * 0.5f;

}

bool AWorldSceneManagement::DeprecateWorldLandscapeInfo(FVector& OutRTWorldLocation, FVector& OutRTWorldSizeVector)
{
#if WITH_EDITOR
	if (ALandscape* Landscape = GetOwningLandscape())
	{
		FIntPoint LandscapeSize;
		if (Landscape->ComputeLandscapeLayerBrushInfo(LandscapeTransform, LandscapeSize, LandscapeRTRes))
		{
			ComputeWorldLandscapeInfo(OutRTWorldLocation, OutRTWorldSizeVector);
			return true;
		}
	}

	return false;
#endif // WITH_EDITOR
}


void AWorldSceneManagement::PostLoad()
{
	Super::PostLoad();
	if (GetClass()->ClassGeneratedBy == nullptr)
	{
		SetupDefaultMaterials();
		SyncJumpFloodMaterials();
	}
	if (UWorld* World = this->GetWorld())
	{
		ULevel* Level = this->GetLevel();
		if (Level)
		{
			TActorIterator<AWorldSceneManagement> It(World);
			if (AWorldSceneManagement* WorldRoadZone = It ? *It : nullptr)
			{
				FVector RTWorldLocation, RTWorldSizeVector;
				if (DeprecateWorldLandscapeInfo(RTWorldLocation, RTWorldSizeVector))
				{
					SetMPCParams();
				}
			}
		}
	}
	//OnLevelAddedToWorldHandle = FWorldDelegates::LevelAddedToWorld.AddLambda([this](ULevel* Level, UWorld* World)
	//	{

	//	});


}

void AWorldSceneManagement::GetRenderDependencies(TSet<UObject*>& OutDependencies)
{
	Super::GetRenderDependencies(OutDependencies);

	AddDependencyIfValid(BrushAngleFalloffMaterial, OutDependencies);
	AddDependencyIfValid(BrushWidthFalloffMaterial, OutDependencies);
	AddDependencyIfValid(DistanceFieldCacheMaterial, OutDependencies);
	AddDependencyIfValid(RenderRoadSplineDepthMaterial, OutDependencies);
	AddDependencyIfValid(DebugDistanceFieldMaterial, OutDependencies);
	AddDependencyIfValid(WeightmapMaterial, OutDependencies);
	AddDependencyIfValid(CompositeRoadBodyTextureMaterial, OutDependencies);
	AddDependencyIfValid(IslandFalloffMaterial, OutDependencies);
	AddDependencyIfValid(JumpStepMaterial, OutDependencies);
	AddDependencyIfValid(FindEdgesMaterial, OutDependencies);
	AddDependencyIfValid(BlurEdgesMaterial, OutDependencies);

	if (DrawCanvasMaterial != nullptr)
	{
		AddDependencyIfValid(DrawCanvasMaterial, OutDependencies);
	}

}
void AWorldSceneManagement::BeginDestroy()
{
	Super::BeginDestroy();

	FWorldDelegates::LevelAddedToWorld.Remove(OnLevelAddedToWorldHandle);
	OnLevelAddedToWorldHandle.Reset();
}


void AWorldSceneManagement::SetMPCParams()
{
	UWorld* World = GetWorld();
	USplineRoadEditorSubsystem* RoadEditorSubsystem = GEditor->GetEditorSubsystem<USplineRoadEditorSubsystem>();
	if (RoadEditorSubsystem&& World != nullptr)
	{

			FVector RTWorldLocation, RTWorldSizeVector;
			ComputeWorldLandscapeInfo(RTWorldLocation, RTWorldSizeVector);


			UMaterialParameterCollection* LandscapeCollection = RoadEditorSubsystem->GetLandscapeMaterialParameterCollection();
			if (!LandscapeCollection)return;

			UMaterialParameterCollectionInstance* LandscapeCollectionInstance = World->GetParameterCollectionInstance(LandscapeCollection);
			check(LandscapeCollectionInstance != nullptr);


			bool bIsFound = true;

			if (!LandscapeCollectionInstance->SetScalarParameterValue(FName(TEXT("RTResX")), (float)LandscapeRTRes.X))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"RTResX\" on Landscape MaterialParameterCollection"));
			}

			if (!LandscapeCollectionInstance->SetScalarParameterValue(FName(TEXT("RTResY")), (float)LandscapeRTRes.Y))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"RTResY\" on Landscape MaterialParameterCollection"));
			}

			if (!LandscapeCollectionInstance->SetScalarParameterValue(FName(TEXT("LSQuadsX")), (float)LandscapeQuads.X))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"LSQuadsX\" on Landscape MaterialParameterCollection"));
			}
			if (!LandscapeCollectionInstance->SetScalarParameterValue(FName(TEXT("LSQuadsY")), (float)LandscapeQuads.Y))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"LSQuadsY\" on Landscape MaterialParameterCollection"));
			}

			if (!LandscapeCollectionInstance->SetScalarParameterValue(FName(TEXT("WorldSizeX")), WorldSize.X))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"WorldSizeX\" on Landscape MaterialParameterCollection"));
			}
			if (!LandscapeCollectionInstance->SetScalarParameterValue(FName(TEXT("WorldSizeY")), WorldSize.Y))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"WorldSizeY\" on Landscape MaterialParameterCollection"));
			}

			if (!LandscapeCollectionInstance->SetVectorParameterValue(FName(TEXT("LandscapeLocation")), FLinearColor(LandscapeTransform.GetLocation())))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"LandscapeLocation\" on Landscape MaterialParameterCollection"));
			}
			if (!LandscapeCollectionInstance->SetScalarParameterValue(FName(TEXT("LandscapeZLocation")), LandscapeTransform.GetLocation().Z))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"LandscapeZLocation\" on Landscape MaterialParameterCollection"));
			}
			// TODO [jonathan.bard] : find out what this 128.0f corresponds to and put in a constant : ZSCALE in LandscapeLayersPS.usf maybe ??
			if (!LandscapeCollectionInstance->SetScalarParameterValue(FName(TEXT("LandscapeZScale")), LandscapeTransform.GetScale3D().Z/128))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"LandscapeZScale\" on Landscape MaterialParameterCollection"));
			}
			if (!LandscapeCollectionInstance->SetVectorParameterValue(FName(TEXT("RTWorldSize")), FLinearColor(RTWorldSizeVector)))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"RTWorldSize\" on Landscape MaterialParameterCollection"));
			}
			if (!LandscapeCollectionInstance->SetVectorParameterValue(FName(TEXT("RTWorldLocation")), FLinearColor(RTWorldLocation)))
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to set \"RTWorldLocation\" on Landscape MaterialParameterCollection"));
			}

	}
}

void AWorldSceneManagement::OctreeAddRoad(AMoKuEditSplineActor* InElement)
{
	if (!InElement)return;
	if (!EditRoadsList.Contains(InElement))
	{
		EditRoadsList.Add(InElement);
		//OctreeAddElement(InElement->LeftCurve);
		//OctreeAddElement(InElement->RightCurve);
		//OctreeAddElement(InElement->SplineComponent);

	}
}

void AWorldSceneManagement::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

}
bool AWorldSceneManagement::BrushRenderSetup()
{
	SetupDefaultMaterials();

	if (!AllocateRTs())
	{
		//UE_LOG(LogWaterEditor, Error, TEXT("Invalid Render Target for Water Brush. Aborting BrushRenderSetup."));
		return false;
	}

	SyncJumpFloodMaterials();
	if (::IsValid(DebugDistanceFieldMaterial))
	{
		//UStaticMeshComponent* StaticMeshComponent = CastChecked<UStaticMeshComponent>(AActor::AddComponent(FName(TEXT("NODE_AddStaticMeshComponent-0")), false, FTransform(FRotator::ZeroRotator, FVector::ZeroVector, WorldSize), this), ECastCheckedType::NullAllowed);
		if (DebugDistanceFieldMaterial->IsA<UMaterialInstanceDynamic>())
		{
			//UE_LOG(LogWaterEditor, Error, TEXT("Invalid DebugDistanceFieldMaterial Material : must be either a Material Instance Constant or a Material"));
		}
		else
		{
			// Transient MID : no outer, no name : 
			DebugDistanceFieldMID = UMaterialInstanceDynamic::Create(DebugDistanceFieldMaterial, nullptr);
			//check((DebugDistanceFieldMID != nullptr) && (DebugDistanceFieldMID->GetMaterial() == DebugDistanceFieldMaterial->GetMaterial()));
			//StaticMeshComponent->SetMaterial(0, DebugDistanceFieldMID);

			DebugDistanceFieldMID->SetScalarParameterValue(FName(TEXT("ShowGrid")), (float)ShowGrid);
			DebugDistanceFieldMID->SetScalarParameterValue(FName(TEXT("ShowDistance")), (float)ShowDistance);
			DebugDistanceFieldMID->SetScalarParameterValue(FName(TEXT("ShowGradient")), (float)ShowGradient);
			DebugDistanceFieldMID->SetScalarParameterValue(FName(TEXT("DistanceDivisor")), DistanceDivisor);
		}
	}

	UpdateBrushCacheKeys();
	//UpdateCurveCacheKeys();
	//UpdateCurves();
	SetMPCParams();

	if (!CreateMIDs())
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid material setup for Road Brush. Aborting BrushRenderSetup."));
		return false;
	}

	// Success at last!
	return true;

}

void AWorldSceneManagement::OnConstruction(const FTransform& Transform)
{

	Super::OnConstruction(Transform);
#if WITH_EDITOR
	if (ALandscape* Landscape = GetOwningLandscape())
	{
		if (Landscape->GetBrushLayer(this) == INDEX_NONE)
		{
			SetTargetLandscape(Landscape);
		}
	}
#endif 
	SetupDefaultMaterials();
	SyncJumpFloodMaterials();
	SetMPCParams();
};
bool AWorldSceneManagement::CreateMIDs()
{
	auto LogBrushMaterial = [](const TCHAR* Label, UMaterialInterface* Material)
	{
		if (Material == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("[RoadLandscape] Material MISSING: %s"), Label);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] Material OK: %s -> %s"), Label, *Material->GetPathName());
		}
	};

	LogBrushMaterial(TEXT("BrushAngleFalloff"), BrushAngleFalloffMaterial);
	LogBrushMaterial(TEXT("BrushWidthFalloff"), BrushWidthFalloffMaterial);
	LogBrushMaterial(TEXT("Weightmap"), WeightmapMaterial);
	LogBrushMaterial(TEXT("DistanceFieldCache"), DistanceFieldCacheMaterial);
	LogBrushMaterial(TEXT("CompositeRoadBody"), CompositeRoadBodyTextureMaterial);
	LogBrushMaterial(TEXT("RenderRoadDepth"), RenderRoadSplineDepthMaterial);
	LogBrushMaterial(TEXT("DrawCanvas"), DrawCanvasMaterial);

	BrushAngleFalloffMID = FWaterUtils::GetOrCreateTransientMID(BrushAngleFalloffMID, TEXT("BrushAngleFalloffMID"), BrushAngleFalloffMaterial);
	//IslandFalloffMID = FWaterUtils::GetOrCreateTransientMID(IslandFalloffMID, TEXT("IslandFalloffMID"), IslandFalloffMaterial);
	BrushWidthFalloffMID = FWaterUtils::GetOrCreateTransientMID(BrushWidthFalloffMID, TEXT("BrushWidthFalloffMID"), BrushWidthFalloffMaterial);
	WeightmapMID = FWaterUtils::GetOrCreateTransientMID(WeightmapMID, TEXT("WeightmapMID"), WeightmapMaterial);
	DistanceFieldCacheMID = FWaterUtils::GetOrCreateTransientMID(DistanceFieldCacheMID, TEXT("DistanceFieldCacheMID"), DistanceFieldCacheMaterial);
	CompositeRoadBodyTextureMID = FWaterUtils::GetOrCreateTransientMID(CompositeRoadBodyTextureMID, TEXT("CompositeRoadrBodyTextureMID"), CompositeRoadBodyTextureMaterial);
	DrawCanvasMID = FWaterUtils::GetOrCreateTransientMID(DrawCanvasMID, TEXT("DrawCanvasMID"), DrawCanvasMaterial);

	if ((BrushAngleFalloffMID == nullptr)
		//|| (IslandFalloffMID == nullptr)
		|| (BrushWidthFalloffMID == nullptr)
		|| (WeightmapMID == nullptr)
		|| (DistanceFieldCacheMID == nullptr)
		|| (CompositeRoadBodyTextureMID == nullptr))
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid Road brush materials."));
		return false;
	}

	if (DrawCanvasMID == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] DrawCanvas material unavailable (optional for mesh-based roads)."));
	}

	return JumpFloodComponent2D->CreateMIDs();
}
void AWorldSceneManagement::ModifyCaptureToTerrain(TObjectPtr<ALandscape> InOwningLandscape)
{
	if (InOwningLandscape)
	{
		 
		const FBox LandscapeBounds = InOwningLandscape->GetCompleteBounds();
		FVector Center = LandscapeBounds.GetCenter();
		FVector Extent =  LandscapeBounds.GetExtent();
		float SafeHeight = 50000.0f;
		if (SceneCapture)
		{
			SceneCapture->SetWorldLocation(FVector(Center.X, Center.Y, SafeHeight));
		}
		float MaxSide = FMath::Max(Extent.X, Extent.Y);
		float CalculatedWidth = MaxSide * 2.0f;
		SceneCapture->OrthoWidth = CalculatedWidth * 1.05f;
	}


}

void AWorldSceneManagement::AddDependencyIfValid(UObject* Dependency, TSet<UObject*>& OutDependencies)
{
	if (IsValid(Dependency))
	{
		OutDependencies.Add(Dependency);
	}
}

void AWorldSceneManagement::SyncJumpFloodMaterials()
{
	if (JumpFloodComponent2D == nullptr)
	{
		return;
	}

	JumpFloodComponent2D->JumpStepMaterial = JumpStepMaterial;
	JumpFloodComponent2D->FindEdgesMaterial = FindEdgesMaterial;
	JumpFloodComponent2D->BlurEdgesMaterial = BlurEdgesMaterial;
}

void AWorldSceneManagement::SetupDefaultMaterials()
{
	using namespace MetaLandRoadBrushMaterials;

	BrushAngleFalloffMaterial = LoadMaterial(BrushAngleLocal, BrushAngleOfficial, TEXT("BrushAngleFalloff"));
	BrushWidthFalloffMaterial = LoadMaterial(BrushWidthLocal, BrushWidthOfficial, TEXT("BrushWidthFalloff"));
	DistanceFieldCacheMaterial = LoadMaterial(CacheDistanceFieldLocal, CacheDistanceFieldOfficial, TEXT("CacheDistanceField"));
	RenderRoadSplineDepthMaterial = LoadMaterial(RenderSplineDepthsLocal, RenderSplineDepthsOfficial, TEXT("RenderSplineDepths"));
	WeightmapMaterial = LoadMaterial(BrushWeightmapLocal, BrushWeightmapOfficial, TEXT("BrushWeightmap"));
	DrawCanvasMaterial = nullptr;
	CompositeRoadBodyTextureMaterial = LoadMaterial(CompositeBodyLocal, CompositeBodyOfficial, TEXT("CompositeMeshBodyTexture"));
	JumpStepMaterial = LoadMaterial(JumpFloodStepLocal, JumpFloodStepOfficial, TEXT("JumpFloodStep"));
	BlurEdgesMaterial = LoadMaterial(BlurEdgesLocal, BlurEdgesOfficial, TEXT("BlurEdges"));
	FindEdgesMaterial = LoadMaterial(FindEdgesLocal, FindEdgesOfficial, TEXT("DetectEdges"));
	SyncJumpFloodMaterials();
}
void AWorldSceneManagement::OctreeRemoveRoad(AMoKuEditSplineActor* InElement)
{
	//if (!InElement) return;
	//if (EditRoadsList.Contains(InElement))
	//{
	//	OctreeRemoveElement(InElement->LeftCurve);
	//	OctreeRemoveElement(InElement->RightCurve);
	//	OctreeRemoveElement(InElement->SplineComponent);
	//	//EditRoadsList.Remove(InElement);
	//}

}
UTextureRenderTarget2D* AWorldSceneManagement::RenderLayer_Native(const FLandscapeBrushParameters& InParameters)
{

	TRACE_CPUPROFILER_EVENT_SCOPE(AWorldSceneManagement::RenderLayer_Native);

	const bool bHeightmapRender = InParameters.LayerType == ELandscapeToolTargetType::Heightmap;
	UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] RenderLayer BEGIN type=%s layer=%s"),
		bHeightmapRender ? TEXT("Height") : TEXT("Weight"),
		*InParameters.WeightmapLayerName.ToString());

	LandscapeRTRef = InParameters.CombinedResult;

	FEditorBrushRenderContext BrushRenderContext;
	BrushRenderContext.bHeightmapRender = InParameters.LayerType == ELandscapeToolTargetType::Heightmap;
	BrushRenderContext.WeightmapLayerName = InParameters.WeightmapLayerName;

	if (!BrushRenderSetup())
	{
		UE_LOG(LogTemp, Error, TEXT("[RoadLandscape] RenderLayer ABORT: BrushRenderSetup failed (check materials / RT allocation)"));
		return nullptr;
	}

	if (BrushRenderContext.bHeightmapRender)
	{
		const FLinearColor ClearColor(0.000000, 0.000000, RoadClearHeight, 0.000000);
		UKismetRenderingLibrary::ClearRenderTarget2D(this, CombinedShapeAndHeightRTA, ClearColor);
		UKismetRenderingLibrary::ClearRenderTarget2D(this, CombinedShapeAndHeightRTB, ClearColor);
	}
	else
	{
		// Clear weight ping-pong once per layer pass (not per-road — that wiped previous strokes).
		UKismetRenderingLibrary::ClearRenderTarget2D(this, WeightmapRTA, FLinearColor::Black);
		UKismetRenderingLibrary::ClearRenderTarget2D(this, WeightmapRTB, FLinearColor::Black);
	}

	// Refresh tracked roads before weight/height so newly edited actors are included.
	UpdateActors(/*bInTriggerEvents=*/false);
	UpdateAffectedWeightmaps();

	TArray<AMoKuEditBaseActor*> RoadList;
	GetRoadActors(AMoKuEditBaseActor::StaticClass(), RoadList);

	// Repair Influence/Curl/Blur; rebuild DF cache when soft-edge params change.
	bool bSoftEdgeRepaired = false;
	for (AMoKuEditBaseActor* SingleRoad : RoadList)
	{
		if (SingleRoad && SingleRoad->EnsureDefaultTerrainInfluenceSettings())
		{
			bSoftEdgeRepaired = true;
		}
	}
	if (bSoftEdgeRepaired)
	{
		InvalidateBrushDistanceFieldCache();
		UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] Soft-edge repair applied — distance-field cache invalidated"));
	}

	int32 WeightRoadsDrawn = 0;
	for (AMoKuEditBaseActor* SingleRoad : RoadList)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SingleRoad);
		if (!SingleRoad)
		{
			continue;
		}
		if (!BrushRenderContext.bHeightmapRender)
		{
			if (!SingleRoad->GetLayerWeightmapSettings().Contains(BrushRenderContext.WeightmapLayerName))
			{
				continue;
			}
		}
		// Height always stamps from road mesh (贴地画线 / Alt+W 挪路后地形跟随).
		FBrushActorRenderContext BrushActorRenderContext(SingleRoad);
		const int32 RTBefore = BrushRenderContext.RTIndex;
		RenderBrushActorContext(BrushRenderContext, BrushActorRenderContext);
		if (!BrushRenderContext.bHeightmapRender && BrushRenderContext.RTIndex > RTBefore)
		{
			++WeightRoadsDrawn;
		}
	}

	UTextureRenderTarget2D* ReturnRT = (InParameters.LayerType == ELandscapeToolTargetType::Heightmap) ? HeightPingPongRead(BrushRenderContext) : WeightPingPongRead(BrushRenderContext);
	
	bKillCache = false;
	UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] RenderLayer END type=%s layer=%s roads=%d drawn=%d return=%s"),
		bHeightmapRender ? TEXT("Height") : TEXT("Weight"),
		*InParameters.WeightmapLayerName.ToString(),
		RoadList.Num(),
		bHeightmapRender ? RoadList.Num() : WeightRoadsDrawn,
		ReturnRT ? *ReturnRT->GetName() : TEXT("null"));
	if (!bHeightmapRender && WeightRoadsDrawn == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] Weight layer '%s' drew 0 roads — Weightmap RT stays black. Check: actors in WorldEditor brush list, bAffectsLandscape, RoadBodyWeightmapSettings has key '%s'."),
			*InParameters.WeightmapLayerName.ToString(),
			*InParameters.WeightmapLayerName.ToString());
	}
	return ReturnRT;
}
//void AWorldSceneManagement::OctreeRemoveElement(UMoKuEditSplinesComponent* InElement)
//{
//	if (!InElement)return;
//	TArray<FOctreeElementId2> OctreeIds = InElement->OctreeIds;
//	for (int i = 0; i < OctreeIds.Num(); i++)
//	{
//		if (Octree.IsValidElementId(OctreeIds[i]))
//		{	
//			Octree.RemoveElement(OctreeIds[i]);
//		}
//	}
//	InElement->OctreeIds.Empty();
//
//}
//
//void AWorldSceneManagement::OctreeAddElement(UMoKuEditSplinesComponent* InElement)
//{
//	if (!InElement)return;
//	const TArray<FMoKuSplineInterpPoint>PointsData = InElement->GetProceduralPoints();
//	for (int32 i = 0; i < PointsData.Num(); i++)
//	{
//		FSplineSegmentOctree NewElement(InElement, i);
//		Octree.AddElement(NewElement);
//	}
//}
void AWorldSceneManagement::RenderBrushActorContext(FEditorBrushRenderContext& BrushRenderContext, FBrushActorRenderContext& BrushActorRenderContext)
{
	if (!BrushRenderContext.bHeightmapRender)
	{
		if (!(BrushActorRenderContext.RoadBrushActor->GetLayerWeightmapSettings().Find(BrushRenderContext.WeightmapLayerName)))
		{
			UE_LOG(LogTemp,Warning, TEXT("Actor does NOT affect this layer, Skipping"));
			return;
		}
	}

	URoadBodyBrushCacheContainer* CacheContainer = nullptr;
	FRoadBodyBrushCache RoadBrushCache;
	GetRoadCacheKey(BrushActorRenderContext.GetActor(), /*out*/ CacheContainer, /*out*/ RoadBrushCache);
	BrushActorRenderContext.CacheContainer = CacheContainer;
	SetBrushMIDParams(BrushRenderContext, BrushActorRenderContext);

	//UE_LOG(LogWaterEditor, Verbose, TEXT("===================================="));
	//UE_LOG(LogWaterEditor, Verbose, TEXT("Current Actor: %s"), *UKismetSystemLibrary::GetDisplayName(BrushActorRenderContext.WaterBrushActor.GetObject()));
	//UE_LOG(LogWaterEditor, Verbose, TEXT("Type: %s"), *BrushActorRenderContext.WaterBrushActor.GetObject()->GetClass()->GetName());
	//UE_LOG(LogWaterEditor, Verbose, TEXT("Cache is Valid: %s"), (BrushActorRenderContext.CacheContainer->Cache.CacheIsValid ? TEXT("true") : TEXT("false")));

	if (BrushActorRenderContext.CacheContainer->Cache.CacheIsValid)
	{
		if (bKillCache)
		{
			//UE_LOG(LogWaterEditor, Verbose, TEXT("Kill Cache Detected, running full render pass for Brush"));
		}
	}
	else
	{
		UseDynamicPreviewRT = true; 
	}

	AMoKuEditBaseActor* BasicBody = BrushActorRenderContext.TryGetActorAs<AMoKuEditBaseActor>();
	if (!BrushActorRenderContext.CacheContainer->Cache.CacheIsValid || bKillCache)
	{
		const FSplineRoadHeightmapSettings& HeightmapSettings = BrushActorRenderContext.RoadBrushActor->GetRoadHeightmapSettings();
		const FRoadBrushEffectCurlNoise& CurlNoise = HeightmapSettings.Effects.CurlNoise;
		// Water/Landmass pack: (Curl1Tiling, Curl1Amount, Curl2Tiling, Curl2Amount)
		// DisableBrushTextureEffects kills Curl — that was the speckled road edge noise.
		const FLinearColor CurlColor = DisableBrushTextureEffects
			? FLinearColor(CurlNoise.Curl1Tiling, 0.0f, CurlNoise.Curl2Tiling, 0.0f)
			: FLinearColor(CurlNoise.Curl1Tiling, CurlNoise.Curl1Amount, CurlNoise.Curl2Tiling, CurlNoise.Curl2Amount);
		if (BasicBody != nullptr)
		{
			CaptureRoadDepth(BrushActorRenderContext);
			JumpFloodComponent2D->JumpFlood(DepthAndShapeRT, 50000.0f, CurlColor, true, 0.0f);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Canvas shape render"));
			DrawCanvasShape(BrushActorRenderContext);
			UKismetRenderingLibrary::ClearRenderTarget2D(this, DepthAndShapeRT, FLinearColor(1.0f, 0.0f, 0.0f, 1.0f));
			JumpFloodComponent2D->JumpFlood(DepthAndShapeRT, 50000.0f, CurlColor, false, BrushActorRenderContext.GetActor()->GetActorLocation().Z);
		}
		CacheBrushDistanceField(BrushActorRenderContext);
	}
	DrawBrushMaterial(BrushRenderContext, BrushActorRenderContext);

	//if (BrushRenderContext.bHeightmapRender)
	//{
	//	// TODO [jonathan.bard] : along with MakePreviewRT?
	//	++HeightOnlyIndex;
	//}

	++BrushRenderContext.RTIndex;
	ApplyToCompositeRoadBodyTexture(BrushRenderContext, BrushActorRenderContext);
}



void AWorldSceneManagement::GetRoadCacheKey(AActor* RoadBrush, /*out*/ URoadBodyBrushCacheContainer*& ContainerObject, /*out*/ FRoadBodyBrushCache& Value)
{
	ContainerObject = CastChecked<URoadBodyBrushCacheContainer>(GetActorCache(RoadBrush, URoadBodyBrushCacheContainer::StaticClass()), ECastCheckedType::NullAllowed);
	if (IsValid(ContainerObject))
	{
		Value = ContainerObject->Cache;
	}
}
void AWorldSceneManagement::UpdateBrushCacheKeys()
{
	for (TWeakInterfacePtr<IRoadBrushActorInterface> BrushActor : GetActorsAffectingLandscape())
	{
		if (BrushActor.IsValid())
		{
			//��������
			ETextureRenderTargetFormat Format = ETextureRenderTargetFormat::RTF_RGBA32f;

			AActor* Actor = CastChecked<AActor>(BrushActor.GetObject());
			URoadBodyBrushCacheContainer* RoadBrushCacheContainer = Cast<URoadBodyBrushCacheContainer>(GetActorCache(Actor, URoadBodyBrushCacheContainer::StaticClass()));
			if (!::IsValid(RoadBrushCacheContainer))
			{
				RoadBrushCacheContainer = NewObject<URoadBodyBrushCacheContainer>(this, NAME_None, RF_Transactional);
				check(!RoadBrushCacheContainer->Cache.CacheIsValid);
				SetActorCache(Actor, RoadBrushCacheContainer);
			}
			// Make sure there's an appropriate render target in that cache : 
			RoadBrushCacheContainer->Cache.CacheRenderTarget = FWaterUtils::GetOrCreateTransientRenderTarget2D(RoadBrushCacheContainer->Cache.CacheRenderTarget, FName(*FString::Printf(TEXT("BrushCacheRT_%s"), *Actor->GetActorNameOrLabel())), LandscapeRTRes, Format);
			check(RoadBrushCacheContainer->Cache.CacheRenderTarget != nullptr);
		}
	}

}


void AWorldSceneManagement::DrawCanvasShape(const FBrushActorRenderContext& BrushActorRenderContext)
{

	TArray<FCanvasUVTri> CanvasUVTris;

	//UE_LOG(LogWaterEditor, Verbose, TEXT("Actor used for Spline Canvas Render: %s"), *UKismetSystemLibrary::GetDisplayName(BrushActorRenderContext.WaterBrushActor.GetObject()));

	UMoKuEditSplinesComponent* SplineComponent = CastChecked<UMoKuEditSplinesComponent>(BrushActorRenderContext.GetActor()->GetComponentByClass(UMoKuEditSplinesComponent::StaticClass()));
	ensure(SplineComponent);

	int32 TruncSegments = FMath::TruncToInt(SplineComponent->GetSplineLength() / CanvasSegmentSize);
	//UE_LOG(LogWaterEditor, Verbose, TEXT("Spline Segment Canvas Segments: %d"), TruncSegments);

	for (int32 ii = 0; ii < TruncSegments; ++ii)
	{
		FVector LocationA = SplineComponent->GetLocationAtDistanceAlongSpline(0.0f, ESplineCoordinateSpace::World);
		FVector LocationB = SplineComponent->GetLocationAtDistanceAlongSpline(float(ii + 1) * CanvasSegmentSize, ESplineCoordinateSpace::World);
		FVector LocationC = SplineComponent->GetLocationAtDistanceAlongSpline(float(ii + 2) * CanvasSegmentSize, ESplineCoordinateSpace::World);

		LocationA = LandscapeTransform.InverseTransformPosition(LocationA) + 0.5f;
		LocationB = LandscapeTransform.InverseTransformPosition(LocationB) + 0.5f;
		LocationC = LandscapeTransform.InverseTransformPosition(LocationC) + 0.5f;

		FCanvasUVTri& CanvasUVTri = CanvasUVTris.AddDefaulted_GetRef();
		CanvasUVTri.V0_Pos = FVector2D(LocationA);
		CanvasUVTri.V0_UV = FVector2D::ZeroVector;
		CanvasUVTri.V0_Color = FLinearColor::Red;
		CanvasUVTri.V1_Pos = FVector2D(LocationC);
		CanvasUVTri.V1_UV = FVector2D::ZeroVector;
		CanvasUVTri.V1_Color = FLinearColor::Green;
		CanvasUVTri.V2_Pos = FVector2D(LocationB);
		CanvasUVTri.V2_UV = FVector2D::ZeroVector;
		CanvasUVTri.V2_Color = FLinearColor::Blue;

	}


}


UTextureRenderTarget2D* AWorldSceneManagement::HeightPingPongRead(const FEditorBrushRenderContext& BrushRenderContext) const
{
	if (BrushRenderContext.RTIndex == 0)
	{
		return LandscapeRTRef;
	}
	else if (BrushRenderContext.RTIndex % 2)// Odd
	{
		return HeightmapRTA;
	}
	else // Even
	{
		return HeightmapRTB;
	}
}
UTextureRenderTarget2D* AWorldSceneManagement::HeightPingPongWrite(const FEditorBrushRenderContext& BrushRenderContext) const
{
	if (BrushRenderContext.RTIndex % 2)// Odd
	{
		return HeightmapRTB;
	}
	else // Even
	{
		return HeightmapRTA;
	}
}
UTextureRenderTarget2D* AWorldSceneManagement::WeightPingPongRead(const FEditorBrushRenderContext& BrushRenderContext) const
{
	if (BrushRenderContext.RTIndex == 0)
	{
		UE_LOG(LogTemp,Warning,TEXT("1234"))
		return LandscapeRTRef;
	}
	else if (BrushRenderContext.RTIndex % 2)// Odd
	{
		UE_LOG(LogTemp, Warning, TEXT("0"))
		return WeightmapRTA;
	}
	else // Even
	{
		UE_LOG(LogTemp, Warning, TEXT("1"))
		return WeightmapRTB;
	}
}
UTextureRenderTarget2D* AWorldSceneManagement::WeightPingPongWrite(const FEditorBrushRenderContext& BrushRenderContext) const
{
	if (BrushRenderContext.RTIndex % 2)// Odd
	{
		return WeightmapRTB;
	}
	else // Even
	{
		return WeightmapRTA;
	}
}
UTextureRenderTarget2D* AWorldSceneManagement:: RoadDepthPingPongRead(const FEditorBrushRenderContext& BrushRenderContext) const
{
	if (BrushRenderContext.RoadRTIndex % 2)
	{
		return CombinedShapeAndHeightRTA;
	}
	else
	{
		return CombinedShapeAndHeightRTB;
	}

}
UTextureRenderTarget2D* AWorldSceneManagement::RoadDepthPingPongWrite(const FEditorBrushRenderContext& BrushRenderContext) const
{
	if (BrushRenderContext.RoadRTIndex % 2)
	{
		return CombinedShapeAndHeightRTB;
	}
	else
	{
		return CombinedShapeAndHeightRTA;
	}

}

void AWorldSceneManagement::ApplyToCompositeRoadBodyTexture(FEditorBrushRenderContext& BrushRenderContext, const FBrushActorRenderContext& BrushActorRenderContext)
{

	AMoKuEditBaseActor* RoadBody = BrushActorRenderContext.TryGetActorAs<AMoKuEditBaseActor>();
	if (BrushRenderContext.bHeightmapRender && (RoadBody != nullptr))
	{
		check(::IsValid(BrushActorRenderContext.CacheContainer));
		const FSplineRoadHeightmapSettings& HeightmapSettings = BrushActorRenderContext.RoadBrushActor->GetRoadHeightmapSettings();

		CompositeRoadBodyTextureMID->SetTextureParameterValue(FName(TEXT("CachedDistanceFieldHeight")), BrushActorRenderContext.CacheContainer->Cache.CacheRenderTarget);
		CompositeRoadBodyTextureMID->SetTextureParameterValue(FName(TEXT("CombinedShapeAndHeight")), RoadDepthPingPongRead(BrushRenderContext));
		CompositeRoadBodyTextureMID->SetTextureParameterValue(FName(TEXT("LandscapeHeight")), HeightPingPongRead(BrushRenderContext));
		// Clamp: user/serialized ZOffset of tens of thousands breaks Min blend (terrain collapses → road looks floating).
		const float SafeZOffset = FMath::Clamp(HeightmapSettings.FalloffSettings.ZOffset, -2000.f, 2000.f);
		CompositeRoadBodyTextureMID->SetScalarParameterValue(FName(TEXT("ZOffset")), SafeZOffset);
		//CompositeWaterBodyTextureMID->SetScalarParameterValue(FName(TEXT("Shape Dilation")), RoadBody->GetWaterBodyComponent()->ShapeDilation);
		CompositeRoadBodyTextureMID->SetTextureParameterValue(FName(TEXT("RoadDepthRT")), RoadDepthRT);
		//UE_LOG(LogTemp, Warning, TEXT("Rendering Water Body Velocity/Height to Combined Texture: %s"), *UKismetSystemLibrary::GetDisplayName(VelocityPingPongWrite(BrushRenderContext)));

		UKismetRenderingLibrary::ClearRenderTarget2D(this, RoadDepthPingPongWrite(BrushRenderContext), FLinearColor::Black);
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, RoadDepthPingPongWrite(BrushRenderContext), CompositeRoadBodyTextureMID);

		++BrushRenderContext.RoadRTIndex;
	}








}

void AWorldSceneManagement::SetBrushMIDParams(const FEditorBrushRenderContext& BrushRenderContext, FBrushActorRenderContext& BrushActorRenderContext)
{
	const FSplineRoadHeightmapSettings&  HeightmapSettings = BrushActorRenderContext.RoadBrushActor->GetRoadHeightmapSettings();
	if (JumpFloodComponent2D != nullptr)
	{
		// Blur + Curl are the main "噪点" sources on weight/height RTs.
		if (DisableBrushTextureEffects)
		{
			JumpFloodComponent2D->UseBlur = false;
			JumpFloodComponent2D->BlurPasses = 0;
		}
		else
		{
			JumpFloodComponent2D->UseBlur = HeightmapSettings.Effects.Blurring.bBlurShape;
			JumpFloodComponent2D->BlurPasses = FMath::Clamp(HeightmapSettings.Effects.Blurring.Radius, 0, 16);
		}
	}
	if (BrushActorRenderContext.TryGetActorAs<AMoKuEditBaseActor>() != nullptr)
	{
		if (HeightmapSettings.FalloffSettings.FalloffMode == ERoadBrushFalloffMode::Angle)
		{
			BrushActorRenderContext.MID = BrushAngleFalloffMID;
		}
		else
		{
			BrushActorRenderContext.MID = BrushWidthFalloffMID;
		}
	}
	DistanceFieldCaching(BrushActorRenderContext);
	CurvesSmoothingAndTerracing(BrushActorRenderContext);

	if (BrushRenderContext.bHeightmapRender)
	{
		BrushActorRenderContext.MID->UMaterialInstanceDynamic::SetTextureParameterValue(FName(TEXT("HeightRT")), HeightPingPongRead(BrushRenderContext));
		BrushActorRenderContext.MID->UMaterialInstanceDynamic::SetTextureParameterValue(FName(TEXT("CombinedShapeAndHeightRT")), RoadDepthPingPongRead(BrushRenderContext));

		FalloffAndBlendMode(BrushActorRenderContext);
		DisplacementSettings(BrushActorRenderContext);
	}
	else
	{
		const FRoadBodyWeightmapSettings* WMSettings = BrushActorRenderContext.RoadBrushActor->GetLayerWeightmapSettings().Find(BrushRenderContext.WeightmapLayerName);
		if (WMSettings)
		{
			ApplyWeightmapSettings(BrushRenderContext, BrushActorRenderContext, *WMSettings);
			// Max blend: keep the hardest pavement core when multiple roads overlap (avoid diluted mix).
			if (WeightmapMID)
			{
				WeightmapMID->SetScalarParameterValue(FName(TEXT("BlendMode")), 2.0f);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] Weight layer '%s' not in road RoadBodyWeightmapSettings — skipped"),
				*BrushRenderContext.WeightmapLayerName.ToString());
		}
	}

}
void AWorldSceneManagement::DrawBrushMaterial(const FEditorBrushRenderContext& BrushRenderContext, const FBrushActorRenderContext& BrushActorRenderContext)
{
	if (BrushRenderContext.bHeightmapRender)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, HeightPingPongWrite(BrushRenderContext), FLinearColor::Black);
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, HeightPingPongWrite(BrushRenderContext), BrushActorRenderContext.MID);
	}
	else
	{
		// Do not Clear here — layer pass cleared once; each road composites via HeightRT=prev ping-pong.
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, WeightPingPongWrite(BrushRenderContext), WeightmapMID);
	}
}

void AWorldSceneManagement::DistanceFieldCaching(const FBrushActorRenderContext& BrushActorRenderContext)
{
	check(::IsValid(BrushActorRenderContext.MID));
	check(::IsValid(DistanceFieldCacheMID));
	check(::IsValid(BrushActorRenderContext.CacheContainer));

	BrushActorRenderContext.MID->SetTextureParameterValue(FName(TEXT("CachedDistanceFieldHeight")), BrushActorRenderContext.CacheContainer->Cache.CacheRenderTarget);
	DistanceFieldCacheMID->SetTextureParameterValue(FName(TEXT("MeshDepth")), DepthAndShapeRT);

	const FSplineRoadHeightmapSettings& HeightmapSettings = BrushActorRenderContext.RoadBrushActor->GetRoadHeightmapSettings();

	float JumpFloodRTValue = LandscapeRTRes.GetMax();
	JumpFloodRTValue = FMath::Log2(JumpFloodRTValue);

	// Must match the blur pass count actually used by JumpFlood (weight path may boost BlurPasses).
	const int32 EffectiveBlurPasses = (JumpFloodComponent2D != nullptr)
		? FMath::Clamp(JumpFloodComponent2D->BlurPasses, 0, 16)
		: FMath::Clamp(HeightmapSettings.Effects.Blurring.Radius, 0, 16);
	const bool bEffectiveUseBlur = (JumpFloodComponent2D != nullptr)
		? JumpFloodComponent2D->UseBlur
		: HeightmapSettings.Effects.Blurring.bBlurShape;

	int32 Ceiling = FMath::CeilToInt(JumpFloodRTValue);
	Ceiling += EffectiveBlurPasses;

	DistanceFieldCacheMID->SetTextureParameterValue(FName(TEXT("JumpFloodRT")), (Ceiling % 2 == 0) ? JumpFloodRTA : JumpFloodRTB);

	DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("UseBlur")), bEffectiveUseBlur ? 1.0f : 0.0f);
	DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("Bluroffset")), (float)EffectiveBlurPasses);

	AMoKuEditBaseActor* RoadBodyBasic = BrushActorRenderContext.TryGetActorAs<AMoKuEditBaseActor>();

	// Match Water: Invert only for ocean-like exterior shapes. Roads stay 0 (same as River).
	DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("Invert")), 0.0f);

	const FRoadBrushEffectCurlNoise& CurlNoise = HeightmapSettings.Effects.CurlNoise;
	const FLinearColor NoiseColor = DisableBrushTextureEffects
		? FLinearColor(CurlNoise.Curl1Tiling, 0.0f, CurlNoise.Curl2Tiling, 0.0f)
		: FLinearColor(CurlNoise.Curl1Tiling, CurlNoise.Curl1Amount, CurlNoise.Curl2Tiling, CurlNoise.Curl2Amount);
	DistanceFieldCacheMID->SetVectorParameterValue(FName(TEXT("Curl")), NoiseColor);

	const float SafeZOffset = FMath::Clamp(HeightmapSettings.FalloffSettings.ZOffset, -2000.f, 2000.f);
	DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("ZOffset")), SafeZOffset);
	DistanceFieldCacheMID->SetTextureParameterValue(FName(TEXT("ChannelDepth")), RoadDepthRT);

	// Weight/DF can use mesh silhouette; height raise only when |ZOffset| > 0 (see FalloffAndBlendMode).
	const bool bDoMeshDepth = (RoadBodyBasic != nullptr);
	DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("UseMeshDepth")), bDoMeshDepth ? 1.0f : 0.0f);
}

void AWorldSceneManagement::FalloffAndBlendMode(const FBrushActorRenderContext& BrushActorRenderContext)
{
	check(::IsValid(BrushActorRenderContext.MID));

	const FSplineRoadHeightmapSettings& HeightmapSettings = BrushActorRenderContext.RoadBrushActor->GetRoadHeightmapSettings();
	const FRoadFalloffSettings& FalloffSettings = HeightmapSettings.FalloffSettings;

	float FalloffTangentValue = FMath::Clamp(FalloffSettings.FalloffAngle, 1.0f, 89.0f);
	FalloffTangentValue = FMath::Tan(FMath::DegreesToRadians(FalloffTangentValue));

	const float EdgeWidth = FMath::Max(FalloffSettings.FalloffWidth, 0.1f);

	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("FalloffTangent")), FalloffTangentValue);
	// Always mesh-depth for height MID — toggling this off produced cliff/wall corruption.
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("UseMeshDepth")), 1.0f);
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("CapShapeInterior")), 1.0f);
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("EdgeWidth")), EdgeWidth);

	const float EdgeCenterOffsetValue = FalloffSettings.EdgeOffset - (SplineMeshExtension / 2.0f);
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("EdgeCenterOffset")), EdgeCenterOffsetValue);

	UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] Height FalloffMode=%s EdgeWidth=%.1f EdgeOffset=%.1f ZOffset=%.1f (distance falloff)"),
		FalloffSettings.FalloffMode == ERoadBrushFalloffMode::Angle ? TEXT("Angle") : TEXT("Width"),
		EdgeWidth, FalloffSettings.EdgeOffset, FalloffSettings.ZOffset);

	float ModeValue = 3.0f;
	switch (HeightmapSettings.BlendMode)
	{
	case ERoadBrushBlendType::AlphaBlend:
		ModeValue = 0.0f;
		break;
	case ERoadBrushBlendType::Min:
		ModeValue = 1.0f;
		break;
	case ERoadBrushBlendType::Max:
		ModeValue = 2.0f;
		break;
	default:
		ModeValue = 3.0f;
		break;
	}

	if (::IsValid(WeightmapMID))
	{
		WeightmapMID->SetScalarParameterValue(FName(TEXT("BlendMode")), ModeValue);
	}
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("BlendMode")), ModeValue);
}

void AWorldSceneManagement::CurvesSmoothingAndTerracing(const FBrushActorRenderContext& BrushActorRenderContext)
{
	// Match AWaterBrushManager::CurvesSmoothingAndTerracing — Smooth + Terrace + CurveRT pins.
	check(::IsValid(BrushActorRenderContext.MID));
	check(::IsValid(DistanceFieldCacheMID));

	const FSplineRoadHeightmapSettings& HeightmapSettings = BrushActorRenderContext.RoadBrushActor->GetRoadHeightmapSettings();
	const FRoadBrushEffectSmoothBlending& SmoothBlending = HeightmapSettings.Effects.SmoothBlending;
	const FRoadBrushEffectTerracing& Terracing = HeightmapSettings.Effects.Terracing;
	const FRoadFalloffSettings& FalloffSettings = HeightmapSettings.FalloffSettings;

	const FLinearColor SmoothingParamsColor(
		FMath::Max(SmoothBlending.InnerSmoothDistance, 0.01f),
		FMath::Max(SmoothBlending.OuterSmoothDistance, 0.01f),
		0.0f,
		0.0f);
	BrushActorRenderContext.MID->SetVectorParameterValue(FName(TEXT("SmoothingParams")), SmoothingParamsColor);

	// Full elevation-curve channels are Water/River-specific; roads keep channel depth off.
	const float CurveChannelDepthValue = 0.0f;
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("CurveChannelDepth")), CurveChannelDepthValue);
	DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("CurveChannelDepth")), CurveChannelDepthValue);

	const float ChannelEdgeOffsetValue = 0.0f - (SplineMeshExtension / 2.0f);
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("ChannelEdgeOffset")), ChannelEdgeOffsetValue);
	DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("ChannelEdgeOffset")), ChannelEdgeOffsetValue);

	const float CurveRampWidth = 512.0f;
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("CurveRampWidth")), CurveRampWidth);
	DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("CurveRampWidth")), CurveRampWidth);

	if (DefaultCurveRT)
	{
		BrushActorRenderContext.MID->SetTextureParameterValue(FName(TEXT("CurveRT")), DefaultCurveRT);
		DistanceFieldCacheMID->SetTextureParameterValue(FName(TEXT("CurveRT")), DefaultCurveRT);
	}

	const FLinearColor TerraceParamsColor(Terracing.TerraceAlpha, Terracing.TerraceSmoothness, Terracing.TerraceSpacing, Terracing.MaskLength);
	BrushActorRenderContext.MID->SetVectorParameterValue(FName(TEXT("TerraceParams")), TerraceParamsColor);
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("TerraceOffset")), Terracing.MaskStartOffset);

	// Keep ZOffset consistent on the height brush MID as well.
	const float SafeZOffset = FMath::Clamp(FalloffSettings.ZOffset, -2000.f, 2000.f);
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("ZOffset")), SafeZOffset);
}

void AWorldSceneManagement::DisplacementSettings(const FBrushActorRenderContext& BrushActorRenderContext)
{
	// Match AWaterBrushManager::DisplacementSettings (MeshBrush_Width / Angle).
	check(::IsValid(BrushActorRenderContext.MID));

	const FSplineRoadHeightmapSettings& HeightmapSettings = BrushActorRenderContext.RoadBrushActor->GetRoadHeightmapSettings();
	const FRoadBrushEffectDisplacement& Displacement = HeightmapSettings.Effects.Displacement;

	UTexture2D* DisplacementTexture = Displacement.Texture.LoadSynchronous();
	const float BrushTexHeightValue = (!DisableBrushTextureEffects && DisplacementTexture != nullptr)
		? Displacement.DisplacementHeight
		: 0.0f;

	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("BrushTexHeight")), BrushTexHeightValue);
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("T")), Displacement.DisplacementTiling);
	BrushActorRenderContext.MID->SetTextureParameterValue(FName(TEXT("BrushRT")), DisplacementTexture);
	BrushActorRenderContext.MID->SetScalarParameterValue(FName(TEXT("Displacement Midpoint")), Displacement.Midpoint);
	BrushActorRenderContext.MID->SetVectorParameterValue(FName(TEXT("DisplacementChannel")), Displacement.Channel);

	UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] Displacement Height=%.1f Tiling=%.2f Tex=%s Influence(weight)=%.2f"),
		BrushTexHeightValue,
		Displacement.DisplacementTiling,
		DisplacementTexture ? *DisplacementTexture->GetName() : TEXT("None"),
		Displacement.WeightmapInfluence);
}

void AWorldSceneManagement::BlueprintRoadBodyChanged_Native(AActor* Actor)
{
	if (::IsValid(Actor))
	{
		URoadBodyBrushCacheContainer* ContainerObject;
		FRoadBodyBrushCache WaterBrushCache;
		GetRoadCacheKey(Actor, /*out*/ ContainerObject, /*out*/ WaterBrushCache);

		if (::IsValid(ContainerObject))
		{
			ContainerObject->Cache.CacheIsValid = false;
		}
		RequestLandscapeUpdate();
	}
}

bool AWorldSceneManagement::SetupRoadSplineRenderMIDs(const FBrushActorRenderContext& BrushActorRenderContext, bool bRestoreMIDs, TArray<UMaterialInterface*>& InOutMIDs)
{
	AMoKuEditBaseActor* RoadBasicActor = BrushActorRenderContext.GetActorAs<AMoKuEditBaseActor>();
	if (!RoadBasicActor)
	{
		return false;
	}

	// Roads need a spline with >=2 points; junctions use cover DynamicMesh only (no spline).
	UPrimitiveComponent* RenderComponents = nullptr;
	if (AMoKuEditSplineActor* CurrentActor = Cast<AMoKuEditSplineActor>(RoadBasicActor))
	{
		if (CurrentActor->GetSplineComponent() && CurrentActor->GetSplineComponent()->GetNumberOfSplinePoints() > 1)
		{
			RenderComponents = CurrentActor->GetBrushRenderableComponents();
		}
	}
	else
	{
		// AMoKuEditIntersectionActor (and other mesh brush actors)
		RenderComponents = RoadBasicActor->GetBrushRenderableComponents();
		if (UDynamicMeshComponent* DynMesh = Cast<UDynamicMeshComponent>(RenderComponents))
		{
			const UDynamicMesh* MeshObj = DynMesh->GetDynamicMesh();
			if (!MeshObj || MeshObj->GetTriangleCount() <= 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] SetupRoadSplineRenderMIDs: junction/mesh actor %s has empty DynamicMesh — skip depth capture"),
					*RoadBasicActor->GetName());
				return false;
			}
		}
	}
	if (!RenderComponents)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] SetupRoadSplineRenderMIDs: no brush mesh on %s (%s)"),
			*RoadBasicActor->GetName(), *RoadBasicActor->GetClass()->GetName());
		return false;
	}

	if (bRestoreMIDs)
	{
		int32 NumMats = FMath::Max(1, RenderComponents->GetNumMaterials());
		for (int32 MatIdx = 0; MatIdx < NumMats; ++MatIdx)
		{
			//FSoftObjectPath SoftPath(InOutMIDs[MatIdx]->GetMaterial());
			//FString MySoftPathString = SoftPath.ToString();
			//UE_LOG(LogTemp, Warning, TEXT("SoftPathString==%s"), *MySoftPathString)
			// ȷ�������������ж�������ֹԽ�����
			if (InOutMIDs.IsValidIndex(MatIdx))
			{
				RenderComponents->SetMaterial(MatIdx, InOutMIDs[MatIdx]);
			}
		}
	}
	// 4. �滻���ʵ��߼������������ǰ����·�����ǿ�ƻ����Ǹ����߶�ͼ���ʣ�
	else
	{
		// ��̬������������߶�ͼ�Ĳ���ʵ��
		UMaterialInstanceDynamic* TempMID = FWaterUtils::GetOrCreateTransientMID(RoadSplineMID, TEXT("RoadSplineMID"), RenderRoadSplineDepthMaterial);

		// ���޸��߼���ת��������ʴ����ɹ�����ȥ�滻
		if (TempMID)
		{
			InOutMIDs.Empty();
			const int32 NumMats = FMath::Max(1, RenderComponents->GetNumMaterials());
			for (int32 MatIdx = 0; MatIdx < NumMats; ++MatIdx)
			{
				InOutMIDs.Add(RenderComponents->GetMaterial(MatIdx));
				RenderComponents->SetMaterial(MatIdx, TempMID);
			}
		}
		else
		{
			// �������û��ȡ������ӡ�����沢������Ⱦ
			UE_LOG(LogTemp, Error, TEXT("[RoadLandscape] SetupRoadSplineRenderMIDs: failed to create depth MID (check RenderRoadDepth material)"));
			return false;
		}
	}

	return true;
}
void AWorldSceneManagement::CacheBrushDistanceField(const FBrushActorRenderContext& BrushActorRenderContext)
{
	check(::IsValid(BrushActorRenderContext.CacheContainer));
	UKismetRenderingLibrary::ClearRenderTarget2D(this, BrushActorRenderContext.CacheContainer->Cache.CacheRenderTarget, FLinearColor::Black);
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, BrushActorRenderContext.CacheContainer->Cache.CacheRenderTarget, DistanceFieldCacheMID);
	BrushActorRenderContext.CacheContainer->Cache.CacheIsValid = true;
}


void AWorldSceneManagement::ApplyWeightmapSettings(const FEditorBrushRenderContext& BrushRenderContext ,const FBrushActorRenderContext& BrushActorRenderContext, const FRoadBodyWeightmapSettings& WMSettings)
{
	// Match AWaterBrushManager::ApplyWeightmapSettings (MeshBrush_Weightmap).
	check(::IsValid(WeightmapMID));
	check(::IsValid(BrushActorRenderContext.CacheContainer));

	SetMPCParams();

	const float LandscapeCmPerQuad = FMath::Max3(
		FMath::Abs((float)LandscapeTransform.GetScale3D().X),
		FMath::Abs((float)LandscapeTransform.GetScale3D().Y),
		1.0f);

	const UWorldEditorSettings* EditorSettings = GetDefault<UWorldEditorSettings>();
	const FName RoadLayerName = EditorSettings ? EditorSettings->GetDefaultRoadWeightmapLayerName() : FName(TEXT("Road"));
	const FName ShoulderLayerName = EditorSettings ? EditorSettings->GetDefaultShoulderWeightmapLayerName() : FName(TEXT("Dirty"));
	const bool bIsRoadPavement = BrushRenderContext.WeightmapLayerName == RoadLayerName;
	const bool bIsShoulder = BrushRenderContext.WeightmapLayerName == ShoulderLayerName;

	// Road: ~2 quads AA only. Dirty: wider soft ok for slopes (do not force 6 quads onto Road —
	// that turned a whole carriageway into a soft gray mix).
	const float MinSoftFalloffCm = LandscapeCmPerQuad * (bIsRoadPavement ? 2.0f : 4.0f);
	const float MaxFalloffCm = bIsRoadPavement ? 600.f : 2500.f;
	const float SafeFalloffWidth = FMath::Clamp(WMSettings.FalloffWidth, 0.1f, MaxFalloffCm);
	const float GradientWidth = FMath::Max(SafeFalloffWidth, MinSoftFalloffCm);

	// Shoulder may use EdgeOffset up to ±Falloff*2 (push Dirty onto side slopes).
	const float EdgeClamp = bIsShoulder ? (SafeFalloffWidth * 2.0f) : SafeFalloffWidth;
	float SafeEdgeOffset = FMath::Clamp(WMSettings.EdgeOffset, -EdgeClamp, EdgeClamp);
	if (bIsShoulder && FMath::IsNearlyZero(SafeEdgeOffset, 1.0f))
	{
		// Same silhouette as Road + CapShapeInterior → Dirty fills pavement (brown arms in screenshot).
		// Nudge shoulder outward so Dirt sits on the rim/slope; Road keeps the hard core.
		SafeEdgeOffset = SafeFalloffWidth * 0.5f;
	}

	const bool bInfluenceBroken = !FMath::IsFinite(WMSettings.TextureInfluence)
		|| WMSettings.TextureInfluence > 1.0f + KINDA_SMALL_NUMBER
		|| WMSettings.TextureInfluence < 0.0f;
	float SafeInfluence = 0.0f;
	if (bInfluenceBroken)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] Weightmap Influence=%.1f broken — using 0 (sawtooth / no material). Set 0..1 in Details."),
			WMSettings.TextureInfluence);
	}
	else
	{
		SafeInfluence = FMath::Clamp(WMSettings.TextureInfluence, 0.0f, 0.35f);
	}

	// Road = solid core + edge AA. Dirty = soft band (no Cap) so it cannot paint-over pavement.
	const float CapShapeInterior = bIsRoadPavement ? 1.0f : 0.0f;

	const float SoftQuads = GradientWidth / LandscapeCmPerQuad;
	UE_LOG(LogTemp, Log, TEXT("[RoadLandscape] Weightmap layer=%s role=%s GradientWidth=%.1f EdgeOffset=%.1f CapInterior=%.0f Opacity=%.2f (~%.1f quads soft)"),
		*BrushRenderContext.WeightmapLayerName.ToString(),
		bIsRoadPavement ? TEXT("Pavement") : (bIsShoulder ? TEXT("Shoulder") : TEXT("Other")),
		GradientWidth, SafeEdgeOffset, CapShapeInterior, WMSettings.FinalOpacity, SoftQuads);

	WeightmapMID->SetTextureParameterValue(FName(TEXT("CachedDistanceFieldHeight")), BrushActorRenderContext.CacheContainer->Cache.CacheRenderTarget);
	WeightmapMID->SetScalarParameterValue(FName(TEXT("GradientWidth")), GradientWidth);

	const float EdgeOffsetValue = SafeEdgeOffset - (SplineMeshExtension / 2.0f);
	WeightmapMID->SetScalarParameterValue(FName(TEXT("EdgeOffset")), EdgeOffsetValue);

	WeightmapMID->SetScalarParameterValue(FName(TEXT("CapShapeInterior")), CapShapeInterior);
	WeightmapMID->SetScalarParameterValue(FName(TEXT("UseMeshDepth")), 1.0f);

	// Prefer per-layer ModulationTexture; else Landmass Displacement texture for edge noise.
	const FRoadBrushEffectDisplacement& DisplacementFx = BrushActorRenderContext.RoadBrushActor->GetRoadHeightmapSettings().Effects.Displacement;
	UTexture2D* ModulationTexture = WMSettings.ModulationTexture.LoadSynchronous();
	if (ModulationTexture == nullptr)
	{
		ModulationTexture = DisplacementFx.Texture.LoadSynchronous();
	}
	const float TextureTiling = WMSettings.ModulationTexture.ToSoftObjectPath().IsValid()
		? WMSettings.TextureTiling
		: DisplacementFx.DisplacementTiling;
	WeightmapMID->SetTextureParameterValue(FName(TEXT("BrushRT")), ModulationTexture);
	WeightmapMID->SetScalarParameterValue(FName(TEXT("T")), TextureTiling);

	// None + Influence>0 samples black and kills the mask. Never fall back to Displacement when Influence was broken.
	float WeightmapInfluenceValue = 0.0f;
	if (!DisableBrushTextureEffects && ModulationTexture != nullptr && !bInfluenceBroken)
	{
		WeightmapInfluenceValue = SafeInfluence;
	}
	WeightmapMID->SetScalarParameterValue(FName(TEXT("WeightmapInfluence")), WeightmapInfluenceValue);

	const float MidpointValue = WMSettings.ModulationTexture.ToSoftObjectPath().IsValid()
		? WMSettings.Midpoint
		: DisplacementFx.Midpoint;
	WeightmapMID->SetScalarParameterValue(FName(TEXT("Displacement Midpoint")), MidpointValue);
	WeightmapMID->SetScalarParameterValue(FName(TEXT("Opacity")), WMSettings.FinalOpacity);

	// CRITICAL: Water MeshBrush_Weightmap reads previous weight as "HeightRT" (engine naming).
	UTextureRenderTarget2D* PrevWeight = WeightPingPongRead(BrushRenderContext);
	WeightmapMID->SetTextureParameterValue(FName(TEXT("HeightRT")), PrevWeight);
	// Some local forks renamed the pin — bind both.
	WeightmapMID->SetTextureParameterValue(FName(TEXT("WeightRT")), PrevWeight);
}


void AWorldSceneManagement::CaptureRoadDepth(const FBrushActorRenderContext& BrushActorRenderContext)
{
	TArray<UMaterialInterface*> MIDsToRestore;

	if (!SetupRoadSplineRenderMIDs(BrushActorRenderContext, /*bRestoreMIDs=*/false, MIDsToRestore))
	{
		UE_LOG(LogTemp, Error, TEXT("[RoadLandscape] SetupRoadSplineRenderMIDs failed before depth capture."));
		return;
	}
	AMoKuEditBaseActor* RoadBody = BrushActorRenderContext.GetActorAs<AMoKuEditBaseActor>();
	const bool Hidden = RoadBody->IsTemporarilyHiddenInEditor();
	RoadBody->SetIsTemporarilyHiddenInEditor(false);

	UPrimitiveComponent* BrushRenderableComponent = RoadBody->GetBrushRenderableComponents();
	if (BrushRenderableComponent && SceneCapture)
	{
		const bool bPreviousVisibility = BrushRenderableComponent->GetVisibleFlag();
		const bool bPreviousHiddenInGame = BrushRenderableComponent->bHiddenInGame;
		BrushRenderableComponent->SetVisibility(true);
		BrushRenderableComponent->SetHiddenInGame(false);

		SceneCapture->ClearShowOnlyComponents();
		SceneCapture->ShowOnlyComponent(BrushRenderableComponent);
		SceneCapture->TextureTarget = RoadDepthRT;
		SceneCapture->CaptureSource = ESceneCaptureSource::SCS_SceneColorSceneDepth;
		SceneCapture->CaptureScene();

		SceneCapture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
		SceneCapture->TextureTarget = DepthAndShapeRT;
		SceneCapture->CaptureScene();

		BrushRenderableComponent->SetVisibility(bPreviousVisibility);
		BrushRenderableComponent->SetHiddenInGame(bPreviousHiddenInGame);
		SceneCapture->ClearShowOnlyComponents();

		UE_LOG(LogTemp, Verbose, TEXT("[RoadLandscape] CaptureRoadDepth actor=%s comp=%s"),
			*RoadBody->GetName(), *BrushRenderableComponent->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoadLandscape] CaptureRoadDepth skipped actor=%s (comp=%s SceneCapture=%s) — weight/height stay empty/black"),
			RoadBody ? *RoadBody->GetName() : TEXT("null"),
			BrushRenderableComponent ? *BrushRenderableComponent->GetName() : TEXT("null"),
			SceneCapture ? TEXT("ok") : TEXT("null"));
	}

	RoadBody->SetIsTemporarilyHiddenInEditor(Hidden);
	SetupRoadSplineRenderMIDs(BrushActorRenderContext, /*bRestoreMIDs=*/true, MIDsToRestore);
}


#undef LOCTEXT_NAMESPACE