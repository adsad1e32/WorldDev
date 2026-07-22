#include "EditSplineRoadInfo.h"
#include "MoKuEditSplineActor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "FloodComponent2D.h"
#include "SplineRoadEditorSubsystem.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Materials/MaterialParameterCollection.h"
#include "WaterUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "WorldEditorSettings.h"
#include "Kismet/KismetRenderingLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EditSplineRoadInfo)
#define LOCTEXT_NAMESPACE "EditPolyLine"

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
	bool bSuccess = true;
	HeightmapRTA = FWaterUtils::GetOrCreateTransientRenderTarget2D(HeightmapRTA, TEXT("HeightmapRTA"), LandscapeRTRes, RTF_RGBA8);
	HeightmapRTB = FWaterUtils::GetOrCreateTransientRenderTarget2D(HeightmapRTB, TEXT("HeightmapRTB"), LandscapeRTRes, RTF_RGBA8);
	if ((HeightmapRTA == nullptr) || (HeightmapRTB == nullptr))
	{
		bSuccess = false;
	}

	JumpFloodRTA = FWaterUtils::GetOrCreateTransientRenderTarget2D(JumpFloodRTA, TEXT("JumpFloodRTA"), LandscapeRTRes, RTF_RGBA32f);
	JumpFloodRTB = FWaterUtils::GetOrCreateTransientRenderTarget2D(JumpFloodRTB, TEXT("JumpFloodRTB"), LandscapeRTRes, RTF_RGBA32f);
	if ((JumpFloodRTA != nullptr) && (JumpFloodRTB != nullptr))
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
	if (DepthAndShapeRT != nullptr)
	{
		SceneCapture->TextureTarget = DepthAndShapeRT;
	}
	else
	{
		bSuccess = false;
	}

	RoadDepthRT = FWaterUtils::GetOrCreateTransientRenderTarget2D(RoadDepthRT, TEXT("RoadDepthRT"), LandscapeRTRes, RTF_RGBA32f);
	if (RoadDepthRT == nullptr)
	{	
		bSuccess = false;
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


	return bSuccess;
}
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
	}
	if (JumpFloodComponent2D != nullptr)
	{
		if (JumpFloodComponent2D->BlurEdgesMaterial != nullptr)
		{
			BlurEdgesMaterial = JumpFloodComponent2D->BlurEdgesMaterial;
		}
		if (JumpFloodComponent2D->FindEdgesMaterial != nullptr)
		{
			FindEdgesMaterial = JumpFloodComponent2D->FindEdgesMaterial;
		}
		if (JumpFloodComponent2D->JumpStepMaterial != nullptr)
		{
			JumpStepMaterial = JumpFloodComponent2D->JumpStepMaterial;
		}
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
	AddDependencyIfValid(DrawCanvasMaterial, OutDependencies);
	AddDependencyIfValid(CompositeRoadBodyTextureMaterial, OutDependencies);
	AddDependencyIfValid(IslandFalloffMaterial, OutDependencies);
	AddDependencyIfValid(JumpStepMaterial, OutDependencies);
	AddDependencyIfValid(FindEdgesMaterial, OutDependencies);
	AddDependencyIfValid(BlurEdgesMaterial, OutDependencies);

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

			UMaterialParameterCollectionInstance* LandscapeCollectionInstance = World->GetParameterCollectionInstance(CastChecked<UMaterialParameterCollection>(LandscapeCollection));
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
	if (!AllocateRTs())
	{
		//UE_LOG(LogWaterEditor, Error, TEXT("Invalid Render Target for Water Brush. Aborting BrushRenderSetup."));
		return false;
	}

	JumpFloodComponent2D->BlurEdgesMaterial = BlurEdgesMaterial;
	JumpFloodComponent2D->FindEdgesMaterial = FindEdgesMaterial;
	JumpFloodComponent2D->JumpStepMaterial = JumpStepMaterial;
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
		ULandscapeEditLayerBase* EditLayerBase = Landscape->GetEditLayer(FName("WorldEditor"));
		int32 LayerIndex = -1;
		if (!EditLayerBase)
		{
			LayerIndex = Landscape->CreateLayer(FName("WorldEditor"));
		}
		else
		{
			LayerIndex = Landscape->GetLayerIndex(FName("WorldEditor"));

		}
		Landscape->AddBrushToLayer(LayerIndex, this);
	}
#endif 
	SetMPCParams();
};
bool AWorldSceneManagement::CreateMIDs()
{
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
		|| (CompositeRoadBodyTextureMID == nullptr)
		|| (DrawCanvasMID == nullptr))
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid Road brush materials."));
		return false;
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

void AWorldSceneManagement::SetupDefaultMaterials()
{
	const UWorldEditorSettings* WorldEditorSettings = GetDefault<UWorldEditorSettings>();
	check(WorldEditorSettings != nullptr);


	BrushAngleFalloffMaterial = WorldEditorSettings->GetDefaultBrushAngleFalloffMaterial();
	BrushWidthFalloffMaterial = WorldEditorSettings->GetDefaultBrushWidthFalloffMaterial();
	DistanceFieldCacheMaterial = WorldEditorSettings->GetDefaultCacheDistanceFieldCacheMaterial();
	RenderRoadSplineDepthMaterial = WorldEditorSettings->GetDefaultRenderRoadSplineDepthsMaterial();
	WeightmapMaterial = WorldEditorSettings->GetDefaultBrushWeightmapMaterial();
	DrawCanvasMaterial = WorldEditorSettings->GetDefaultDrawCanvasMaterial();
	CompositeRoadBodyTextureMaterial = WorldEditorSettings->GetDefaultCompositeRoadBodyTextureMaterial();

	JumpStepMaterial = WorldEditorSettings->GetDefaultJumpFloodStepMaterial();
	BlurEdgesMaterial = WorldEditorSettings->GetDefaultBlurEdgesMaterial();
	FindEdgesMaterial = WorldEditorSettings->GetDefaultFindEdgesMaterial();



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

	LandscapeRTRef = InParameters.CombinedResult;

	FEditorBrushRenderContext BrushRenderContext;
	BrushRenderContext.bHeightmapRender = InParameters.LayerType == ELandscapeToolTargetType::Heightmap;
	BrushRenderContext.WeightmapLayerName = InParameters.WeightmapLayerName;
	//BrushRenderContext.RTIndex = 0;
	//BrushRenderContext.RoadRTIndex = 0; 

	if (!BrushRenderSetup())
	{
		//UE_LOG(LogWaterEditor, Error, TEXT("Invalid setup for water brush. Aborting Render."));
		return nullptr;
	}

	if (BrushRenderContext.bHeightmapRender)
	{
		const FLinearColor ClearColor(0.000000, 0.000000, RoadClearHeight, 0.000000);
		UKismetRenderingLibrary::ClearRenderTarget2D(this, CombinedShapeAndHeightRTA, ClearColor);
		UKismetRenderingLibrary::ClearRenderTarget2D(this, CombinedShapeAndHeightRTB, ClearColor);
	}

	TArray<AMoKuEditBaseActor*> RoadList;
	AWorldLandscapeBlueprintBrush::GetRoadActors(AMoKuEditBaseActor::StaticClass(), RoadList);


	for (AMoKuEditBaseActor* SingleRoad : RoadList)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SingleRoad);
		FBrushActorRenderContext BrushActorRenderContext(SingleRoad);
		RenderBrushActorContext(BrushRenderContext, BrushActorRenderContext);
	}

	UTextureRenderTarget2D* ReturnRT = (InParameters.LayerType == ELandscapeToolTargetType::Heightmap) ? HeightPingPongRead(BrushRenderContext) : WeightPingPongRead(BrushRenderContext);
	
	bKillCache = false;
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
		FLinearColor CurlColor(0, 0, 0, 0);
		if (BasicBody != nullptr)
		{
			CaptureRoadDepth(BrushActorRenderContext);
			//UKismetRenderingLibrary::ClearRenderTarget2D(this, DepthAndShapeRT, FLinearColor(1.0f, 0.0f, 0.0f, 1.0f));
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
			//问题所在
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
		CompositeRoadBodyTextureMID->SetScalarParameterValue(FName(TEXT("ZOffset")), HeightmapSettings.FalloffSettings.ZOffset);
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
	//JumpFloodComponent2D->UseBlur = HeightmapSettings.Effects.Blurring.bBlurShape;
	//JumpFloodComponent2D->BlurPasses = HeightmapSettings.Effects.Blurring.Radius;
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



	if (BrushRenderContext.bHeightmapRender)
	{
		BrushActorRenderContext.MID->UMaterialInstanceDynamic::SetTextureParameterValue(FName(TEXT("HeightRT")), HeightPingPongRead(BrushRenderContext));
		BrushActorRenderContext.MID->UMaterialInstanceDynamic::SetTextureParameterValue(FName(TEXT("CombinedShapeAndHeightRT")), RoadDepthPingPongRead(BrushRenderContext));

		//FalloffAndBlendMode(BrushActorRenderContext);
		//DisplacementSettings(BrushActorRenderContext);
	}
	else
	{
		const FRoadBodyWeightmapSettings* WMSettings = BrushActorRenderContext.RoadBrushActor->GetLayerWeightmapSettings().Find(BrushRenderContext.WeightmapLayerName);
		if (WMSettings)
		{
			ApplyWeightmapSettings(BrushRenderContext, BrushActorRenderContext, *WMSettings);
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
		UKismetRenderingLibrary::ClearRenderTarget2D(this, WeightPingPongWrite(BrushRenderContext), FLinearColor::Black);
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

	int32 Ceiling = FMath::CeilToInt(JumpFloodRTValue);
	Ceiling += HeightmapSettings.Effects.Blurring.Radius;

	DistanceFieldCacheMID->SetTextureParameterValue(FName(TEXT("JumpFloodRT")), (Ceiling % 2 == 0) ? JumpFloodRTA : JumpFloodRTB);

	//DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("UseBlur")), HeightmapSettings.Effects.Blurring.bBlurShape ? 1.0f : 0.0f);
	//DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("Bluroffset")), (float)HeightmapSettings.Effects.Blurring.Radius);

	AMoKuEditBaseActor* RoadBodyBasic = BrushActorRenderContext.TryGetActorAs<AMoKuEditBaseActor>();

	bool bDoInvert = (RoadBodyBasic != nullptr);
	DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("Invert")), bDoInvert?0.0f:1.0f);

	//const FWaterBrushEffectCurlNoise& CurlNoise = HeightmapSettings.Effects.CurlNoise;
	//FLinearColor NoiseColor(CurlNoise.Curl1Tiling, CurlNoise.Curl1Amount, CurlNoise.Curl2Tiling, CurlNoise.Curl2Amount);
	//DistanceFieldCacheMID->SetVectorParameterValue(FName(TEXT("Curl")), NoiseColor);

	//DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("ZOffset")), HeightmapSettings.FalloffSettings.ZOffset);
	DistanceFieldCacheMID->SetTextureParameterValue(FName(TEXT("ChannelDepth")),RoadDepthRT);

	bool bDoMeshDepth = (RoadBodyBasic != nullptr);
	DistanceFieldCacheMID->SetScalarParameterValue(FName(TEXT("UseMeshDepth")), bDoMeshDepth ? 0.0f : 1.0f);
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
	// 1. 获取基础 Actor
	AMoKuEditBaseActor* RoadBasicActor = BrushActorRenderContext.GetActorAs<AMoKuEditBaseActor>();
	if (!RoadBasicActor)
	{
		return false;
	}

	// 2. 安全地获取需要渲染的组件 (也就是你的 DynamicMeshComponent)
	UPrimitiveComponent* RenderComponents = nullptr;
	if (AMoKuEditSplineActor* CurrentActor = Cast<AMoKuEditSplineActor>(RoadBasicActor))
	{
		if (CurrentActor->GetSplineComponent() && CurrentActor->GetSplineComponent()->GetNumberOfSplinePoints() > 1)
		{
			RenderComponents = CurrentActor->GetBrushRenderableComponents();
		}
	}
	if (!RenderComponents)
	{
		return false;
	}

	// 3. 恢复材质的逻辑（当相机拍完照片后，把原本酷炫的路面材质还给它）
	if (bRestoreMIDs)
	{
		int32 NumMats = FMath::Max(1, RenderComponents->GetNumMaterials());
		for (int32 MatIdx = 0; MatIdx < NumMats; ++MatIdx)
		{
			//FSoftObjectPath SoftPath(InOutMIDs[MatIdx]->GetMaterial());
			//FString MySoftPathString = SoftPath.ToString();
			//UE_LOG(LogTemp, Warning, TEXT("SoftPathString==%s"), *MySoftPathString)
			// 确保备份数组里有东西，防止越界崩溃
			if (InOutMIDs.IsValidIndex(MatIdx))
			{
				RenderComponents->SetMaterial(MatIdx, InOutMIDs[MatIdx]);
			}
		}
	}
	// 4. 替换材质的逻辑（在相机拍照前，把路面材质强制换成那个纯高度图材质）
	else
	{
		// 动态创建用来输出高度图的材质实例
		UMaterialInstanceDynamic* TempMID = FWaterUtils::GetOrCreateTransientMID(RoadSplineMID, TEXT("RoadSplineMID"), RenderRoadSplineDepthMaterial);

		// 【修复逻辑反转】如果材质创建成功，才去替换
		if (TempMID)
		{
			// 先清空用来备份的数组
			InOutMIDs.Empty();

			// 遍历网格体上的所有材质槽（DynamicMesh 通常只有1个槽，但写循环更严谨）
			for (int32 MatIdx = 0; MatIdx < RenderComponents->GetNumMaterials(); ++MatIdx)
			{
				// 把原来的材质存进数组备份起来
				InOutMIDs.Add(RenderComponents->GetMaterial(MatIdx));

				// 给网格体换上我们要拍高度的材质
				RenderComponents->SetMaterial(MatIdx, TempMID);
			}
		}
		else
		{
			// 如果材质没获取到，打印个警告并放弃渲染
			UE_LOG(LogTemp, Error, TEXT("SetupRoadSplineRenderMIDs: 无法创建高度图材质实例！"));
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

	check(::IsValid(WeightmapMID));
	check(::IsValid(BrushActorRenderContext.CacheContainer));

	const float GradientWidth = FMath::Max(WMSettings.FalloffWidth, 0.1f);
	UE_LOG(LogTemp,Warning,TEXT("GradientWidth==%f"), GradientWidth)
	WeightmapMID->SetTextureParameterValue(FName(TEXT("CachedDistanceFieldHeight")), BrushActorRenderContext.CacheContainer->Cache.CacheRenderTarget);
	WeightmapMID->SetScalarParameterValue(FName(TEXT("GradientWidth")), GradientWidth);

	float EdgeOffsetValue = WMSettings.EdgeOffset - (SplineMeshExtension / 2.0f);
	WeightmapMID->SetScalarParameterValue(FName(TEXT("EdgeOffset")), EdgeOffsetValue);
	UE_LOG(LogTemp, Warning, TEXT("EdgeOffset==%f"), EdgeOffsetValue)
	WeightmapMID->SetTextureParameterValue(FName(TEXT("BrushRT")), WMSettings.ModulationTexture.Get());
	WeightmapMID->SetScalarParameterValue(FName(TEXT("T")), WMSettings.TextureTiling);

	float WeightmapInfluenceValue = WMSettings.TextureInfluence * (float)!DisableBrushTextureEffects;
	WeightmapMID->SetScalarParameterValue(FName(TEXT("WeightmapInfluence")), WeightmapInfluenceValue);

	WeightmapMID->SetScalarParameterValue(FName(TEXT("Displacement Midpoint")), WMSettings.Midpoint);
	WeightmapMID->SetScalarParameterValue(FName(TEXT("Opacity")), WMSettings.FinalOpacity);

	WeightmapMID->SetTextureParameterValue(FName(TEXT("WeightRT")), WeightPingPongRead(BrushRenderContext));



}


void AWorldSceneManagement::CaptureRoadDepth(const FBrushActorRenderContext& BrushActorRenderContext)
{
	TArray<UMaterialInterface*> MIDsToRestore;

	if (!SetupRoadSplineRenderMIDs(BrushActorRenderContext, /*bRestoreMIDs  = */true, MIDsToRestore))
	{
		//UE_LOG(LogWaterEditor, Error, TEXT("Error in setup River spline render material for Water Brush. Aborting CaptureRiverDepthAndVelocity."));
		return;
	}
	AMoKuEditBaseActor* RoadBody = BrushActorRenderContext.GetActorAs<AMoKuEditBaseActor>();
	const bool Hidden = RoadBody->IsTemporarilyHiddenInEditor();
	RoadBody->SetIsTemporarilyHiddenInEditor(false);

	UPrimitiveComponent* BrushRenderableComponent = RoadBody->GetBrushRenderableComponents();
	if (BrushRenderableComponent && SceneCapture)
	{
		SceneCapture->ClearShowOnlyComponents(); 
		SceneCapture->ShowOnlyComponent(BrushRenderableComponent);
		SceneCapture->TextureTarget = RoadDepthRT;
		SceneCapture->CaptureSource = ESceneCaptureSource::SCS_SceneColorSceneDepth;
		SceneCapture->CaptureScene();


		SceneCapture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
		SceneCapture->TextureTarget = DepthAndShapeRT;
		SceneCapture->CaptureScene();
		BrushRenderableComponent->SetVisibility(BrushRenderableComponent->GetVisibleFlag());
		BrushRenderableComponent->SetHiddenInGame(BrushRenderableComponent->bHiddenInGame);
		SceneCapture->ClearShowOnlyComponents();
	}

	RoadBody->SetIsTemporarilyHiddenInEditor(Hidden);
	SetupRoadSplineRenderMIDs(BrushActorRenderContext, true, MIDsToRestore);
}


#undef LOCTEXT_NAMESPACE