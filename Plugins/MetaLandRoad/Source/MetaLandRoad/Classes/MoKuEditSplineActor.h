#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Components/SplineMeshComponent.h"
#include "Components/SplineComponent.h"
#include "ToolContextInterfaces.h"
#include "Serialization/Archive.h"
#include "Tools/LegacyEdModeWidgetHelpers.h"
#include "MoKuEditBaseActor.h"
#include "SplineBasicElement.h"
#include "ModelingOperators.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SceneComponent.h"
#include "MoKuEditIntersectionActor.h"
#include "MoKuEditSplinesComponent.h"
#include "Materials/MaterialInterface.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshAttributeUtil.h"
#include "DynamicMesh/MeshNormals.h"
#if WITH_EDITOR
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Factories/MaterialFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Engine/Texture2D.h"
#endif

#include "MoKuEditSplineActor.generated.h"


class UMoKuEditSplinesComponent;
struct FMoKuSplineInterpPoint;


UENUM(BlueprintType)
namespace ESplineMeshAlign
{
	enum Type : int
	{
		X,
		Y,
	};
}




USTRUCT(BlueprintType)
struct FSplineConnectAssetMesh : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly,meta = (DisplayName = "转角资产输入"))
	UStaticMesh* Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,meta = (DisplayName = "转角排布朝向"))
	TEnumAsByte<ESplineMeshAlign::Type> ForwardAxis;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "启动翻转模型"))
	bool EnableFlipY;

	UPROPERTY(EditAnywhere,meta = (UIMin = "0", UIMax = "270", SliderExponent = "1", Delta = "180"))
	float RotateValue;



	FSplineConnectAssetMesh():
	Mesh(nullptr),
	ForwardAxis(ESplineMeshAlign::Type::X),
	EnableFlipY(false),
	RotateValue(0.0f)
	{

	}
};

USTRUCT(BlueprintType)
struct FSplineMeshInfoDetails : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly,meta = (DisplayName = "资产输入"))
	UStaticMesh* Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Ratio;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,meta = (DisplayName = "资产布局对齐方向"))
	TEnumAsByte<ESplineMeshAxis::Type> ForwardAxis;

	FSplineMeshInfoDetails():
	Ratio(1),
	ForwardAxis(ESplineMeshAxis::Type::X)
	{
		//Mesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, TEXT("/Game/AssetTest/NewTest/SM_X_Road_H_01.SM_X_Road_H_01")));
	}
};

USTRUCT(BlueprintType)
struct FOffsetInfos
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly,meta = (DisplayName = "X朝向偏移"))
	float OffsetX = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,meta = (DisplayName = "Y朝向偏移"))
	float OffsetY = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,meta = (DisplayName = "Z朝向偏移"))
	float OffsetZ = 0;
};

USTRUCT(BlueprintType)
struct FSplineExtraMesh : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly,meta = (DisplayName = "资产排布方式"))
	TEnumAsByte<ESplineMeshLayoutMethod::Type> LayoutMethod = ESplineMeshLayoutMethod::Type::SplineMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "附件资产模型输入"))
	TArray<UStaticMesh*> ExtraMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (UIMin = "0", DisplayName = "资产随机值"))
	float AssetSeed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "资产间隔范围"))
	FVector2D GapSize;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (UIMin = "0",DisplayName = "资产间隔范围随机"))
	float  GapSeed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (UIMin = "0",DisplayName = "资产排布偏移"))
	FVector2D Offset;


	//UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (EditCondition = "EnableMirror == false"))
	//FRotator MinRotate;

	//UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (EditCondition = "EnableMirror == false"))
	//FRotator MaxRotate;


	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (EditCondition = "LayoutMethod ==  ESplineMeshLayoutMethod::InstancedMesh", DisplayName = "填充结尾资产"))
	bool EnableEndCaps;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (EditCondition = "EnableMirror == false", DisplayName = "翻转模型"))
	bool InvertModel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (EditCondition = "InvertModel == false",DisplayName = "启动镜像"))
	bool EnableMirror;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "对齐世界Z轴"))
	bool AlwaysFaceZ;

	UPROPERTY(EditAnywhere, BlueprintReadOnly,meta = (DisplayName = "资产位置偏移"))
	FOffsetInfos  PointOffset;

	FSplineExtraMesh():
		AssetSeed(0),
		GapSize(FVector2D::ZeroVector),
		GapSeed(0),
		Offset(FVector2D::ZeroVector),
		InvertModel(false),
		EnableMirror(false),
		AlwaysFaceZ(false)
	{
		ExtraMesh.SetNumUninitialized(1);
		ExtraMesh[0] = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, TEXT("/Game/SnappyRoads/Meshes/Props/SM_GuardRail001.SM_GuardRail001")));

	}

};


USTRUCT()
struct FRoadCurveInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FVector3d Location;

	UPROPERTY()
	FVector3d Direction;
};

USTRUCT()
struct FSideRoadCurvePaths
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVector> RightCurvePath;

	UPROPERTY()
	TArray<FVector> LeftCurvePath;

	void Empty()
	{
		LeftCurvePath.Empty();
		RightCurvePath.Empty();
	}

};

USTRUCT()
struct FConnectionInfo
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVector3d> ConnectEdge;

	UPROPERTY()
	TArray<FVector3d> ConnectDirection;

	void Empty()
	{
		ConnectEdge.Empty();
		ConnectDirection.Empty();
	}
};



/** One junction gap on a road spline (supports multiple junctions per road). */
USTRUCT(BlueprintType)
struct FMetaLandRoadJunctionTrimZone
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	TWeakObjectPtr<AMoKuEditIntersectionActor> Junction;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	double ApproachCutS = -1.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	double DepartCutS = -1.0;

	/** T through: Left/Right = half-notch only (far asphalt stays). Both = full gap (Cross). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	EMetaLandRoadJunctionConnectSide ConnectSide = EMetaLandRoadJunctionConnectSide::Both;
};



UCLASS(Blueprintable, hidecategories = (Replication, Input, LOD, Actor, Cooking, Rendering))
class AMoKuEditSplineActor : public AMoKuEditBaseActor
{
	GENERATED_BODY()

public:
	AMoKuEditSplineActor(const FObjectInitializer& ObjectInitializer);
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo();
	virtual void PostLoadSubobjects(FObjectInstancingGraph* OuterInstanceGraph) override;
	virtual void Tick(float DeltaTime)override;
	virtual void PostLoad() override;
	virtual void Serialize(FArchive& Ar);
	virtual void Destroyed()override;
	virtual TObjectPtr<UPrimitiveComponent> GetBrushRenderableComponents() const override { return DynamicMeshComponent; }
	//virtual void RenderActorBrush() override;

	
	//临时方案注意修改 虚函数
	virtual void InitGizmoSetting(TArray<FSocketGizmoTransformInfo>& OutGizmoTransform);
	virtual TArray<FSocketGizmoTransformInfo> GetGizmoInfo() override;
	//临时方案 虚函数注意修改
	virtual void RefreshExtraMesh();

	// 假如当前道路为DynamicMesh 则需要刷新
	virtual void RefreshDynamicRoadMesh();

	/** Copy the selected (or Begin) landscape key falloff onto RoadHeightmapSettings. */
	void SyncHeightmapFalloffFromLandscapeKey(int32 SectionIndex, int32 KeyIndex);

	/** Write RoadHeightmapSettings.FalloffSettings into the selected key, or Begin+End if none. */
	void PushHeightmapFalloffToLandscapeKeys();

	/**
	 * Debug: recook Start/End/TrimZone junctions + refresh this road (no redraw).
	 * Details panel button under MetaLandRoad|Debug.
	 */
	UFUNCTION(CallInEditor, Category = "MetaLandRoad|Debug", meta = (DisplayName = "Recook Linked Junctions"))
	void DebugRecookLinkedJunctions();

	/** Debug: full-world junction scan + cook (all road pairs). Prefer local cook during draw; use sparingly. */
	UFUNCTION(CallInEditor, Category = "MetaLandRoad|Debug", meta = (DisplayName = "Full World Junction Recook"))
	void DebugFullWorldJunctionRecook();

	/** Guard: prevents road refresh → junction recook recursion during batch junction cook. */
	static bool bSuppressJunctionRecookFromRoadRefresh;

	void EnsureRoadLayoutInitialized();
	//virtual void UpdateSceneManagementData()override;
	virtual FEditSplineIntersectInfo CheckIntersectionState(AMoKuEditBaseActor* InEditActor);

#if WITH_EDITOR

	virtual void PostActorCreated() override;
#endif

	UPROPERTY(VisibleAnywhere, Category = "Spline")
	TObjectPtr<USceneComponent> SceneRoot = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Spline")
	TObjectPtr<UMoKuEditSplinesComponent> SplineComponent = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline", meta = (DisplayName = "道路类型"))
	ERoadType RoadModeType;

	/** Material applied to DynamicMesh after lane mesh generation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Material", meta = (DisplayName = "道路材质"))
	TObjectPtr<UMaterialInterface> RoadMaterial = nullptr;

	/** Soft default when RoadMaterial is unset. Temporary test: asphalt (T_Asphalt_D pack). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road|Material", meta = (DisplayName = "默认道路材质"))
	TSoftObjectPtr<UMaterialInterface> DefaultRoadMaterial;

	/**
	 * Temporary DynamicMesh test material: simple UV0 lit material from T_Asphalt_D.
	 * Do not use MI_Asphalt01 here — its parent (MainShader) is not UV0/TexCoord friendly
	 * (preview sphere looks like a checker / wrong scale).
	 */
	static const TCHAR* GetAsphaltTestMaterialPath()
	{
		return TEXT("/Game/Textures/Road/M_MetaLandRoadAsphaltUV.M_MetaLandRoadAsphaltUV");
	}

	/** Create or load M_MetaLandRoadAsphaltUV (TexCoord0 → T_Asphalt_D / N / SRM). */
	static UMaterialInterface* EnsureAsphaltTestMaterial()
	{
		if (UMaterialInterface* Existing = LoadObject<UMaterialInterface>(nullptr, GetAsphaltTestMaterialPath()))
		{
			return Existing;
		}

#if WITH_EDITOR
		UTexture2D* Diffuse = LoadObject<UTexture2D>(
			nullptr, TEXT("/Game/Textures/Road/T_Asphalt_D.T_Asphalt_D"));
		if (!Diffuse)
		{
			return nullptr;
		}

		UTexture2D* NormalMap = LoadObject<UTexture2D>(
			nullptr, TEXT("/Game/Textures/Road/T_Asphalt_N.T_Asphalt_N"));
		UTexture2D* SrmMap = LoadObject<UTexture2D>(
			nullptr, TEXT("/Game/Textures/Road/T_Asphalt_SRM.T_Asphalt_SRM"));

		const FString PackageName = TEXT("/Game/Textures/Road/M_MetaLandRoadAsphaltUV");
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();

		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		UMaterial* Material = Cast<UMaterial>(Factory->FactoryCreateNew(
			UMaterial::StaticClass(),
			Package,
			FName(TEXT("M_MetaLandRoadAsphaltUV")),
			RF_Public | RF_Standalone | RF_Transactional,
			nullptr,
			GWarn));
		if (!Material)
		{
			return nullptr;
		}

		// Explicit UV0 so mesh TexCoords drive tiling (matches asphalt test UVs).
		auto* TexCoord = Cast<UMaterialExpressionTextureCoordinate>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionTextureCoordinate::StaticClass(), -550, -80));
		if (TexCoord)
		{
			TexCoord->CoordinateIndex = 0;
			TexCoord->UTiling = 1.0f;
			TexCoord->VTiling = 1.0f;
		}

		auto* SampleD = Cast<UMaterialExpressionTextureSample>(
			UMaterialEditingLibrary::CreateMaterialExpressionEx(
				Material, nullptr, UMaterialExpressionTextureSample::StaticClass(), Diffuse, -350, -80));
		if (SampleD)
		{
			if (TexCoord)
			{
				UMaterialEditingLibrary::ConnectMaterialExpressions(TexCoord, TEXT(""), SampleD, TEXT("UVs"));
			}
			UMaterialEditingLibrary::ConnectMaterialProperty(SampleD, TEXT("RGB"), MP_BaseColor);
		}

		if (NormalMap)
		{
			if (auto* SampleN = Cast<UMaterialExpressionTextureSample>(
					UMaterialEditingLibrary::CreateMaterialExpressionEx(
						Material, nullptr, UMaterialExpressionTextureSample::StaticClass(), NormalMap, -350, 120)))
			{
				SampleN->SamplerType = SAMPLERTYPE_Normal;
				if (TexCoord)
				{
					UMaterialEditingLibrary::ConnectMaterialExpressions(TexCoord, TEXT(""), SampleN, TEXT("UVs"));
				}
				UMaterialEditingLibrary::ConnectMaterialProperty(SampleN, TEXT("RGB"), MP_Normal);
			}
		}

		if (SrmMap)
		{
			if (auto* SampleSrm = Cast<UMaterialExpressionTextureSample>(
					UMaterialEditingLibrary::CreateMaterialExpressionEx(
						Material, nullptr, UMaterialExpressionTextureSample::StaticClass(), SrmMap, -350, 320)))
			{
				if (TexCoord)
				{
					UMaterialEditingLibrary::ConnectMaterialExpressions(TexCoord, TEXT(""), SampleSrm, TEXT("UVs"));
				}
				UMaterialEditingLibrary::ConnectMaterialProperty(SampleSrm, TEXT("R"), MP_Specular);
				UMaterialEditingLibrary::ConnectMaterialProperty(SampleSrm, TEXT("G"), MP_Roughness);
				UMaterialEditingLibrary::ConnectMaterialProperty(SampleSrm, TEXT("B"), MP_Metallic);
			}
		}

		UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
		UMaterialEditingLibrary::RecompileMaterial(Material);

		FAssetRegistryModule::AssetCreated(Material);
		Material->MarkPackageDirty();
		Package->MarkPackageDirty();

		const FString Filename = FPackageName::LongPackageNameToFilename(
			PackageName, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, Material, *Filename, SaveArgs);

		return Material;
#else
		return nullptr;
#endif
	}

	/** Same asphalt UV as lane mesh: U = lateral cm, V = spline S (tile = DefaultAsphaltUvTileCm). */
	static FVector2f MakeAsphaltUVAtDistance(
		const UMetaLandRoadSplineComponent* Spline,
		double DistanceS,
		const FVector& WorldPos);

	/**
	 * Promote per-vertex UVs → PrimaryUV overlay (required for UDynamicMeshComponent render).
	 * Must use CopyVertexUVsToOverlay (element ID == vertex ID); hand-rolled AppendElement
	 * wedges often leave the scene proxy sampling (0,0) → solid smear / "UV broken" look.
	 */
	static void FinalizeRoadMeshUVs(UE::Geometry::FDynamicMesh3& Mesh)
	{
		using namespace UE::Geometry;

		if (Mesh.TriangleCount() == 0)
		{
			return;
		}

		Mesh.EnableAttributes();
		if (!Mesh.HasVertexUVs())
		{
			Mesh.EnableVertexUVs(FVector2f::Zero());
		}

		FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes()->PrimaryUV();
		if (!UVOverlay)
		{
			return;
		}

		// Do not CompactInPlace — CompactMaps::GetVertexMapping asserts on non-compact meshes.
		CopyVertexUVsToOverlay(Mesh, *UVOverlay, /*bCompactElements*/ false);
	}

	/** Vertex normals + PrimaryNormals overlay (needed for lit asphalt + auto tangents). */
	static void FinalizeRoadMeshNormalsAndOverlays(UE::Geometry::FDynamicMesh3& Mesh)
	{
		using namespace UE::Geometry;

		if (Mesh.TriangleCount() == 0)
		{
			return;
		}

		if (!Mesh.HasVertexNormals())
		{
			Mesh.EnableVertexNormals(FVector3f::UnitZ());
		}
		FMeshNormals::QuickComputeVertexNormals(Mesh, false);

		Mesh.EnableAttributes();
		if (FDynamicMeshNormalOverlay* NormalOverlay = Mesh.Attributes()->PrimaryNormals())
		{
			CopyVertexNormalsToOverlay(Mesh, *NormalOverlay, /*bCompactElements*/ false);
		}
	}

	/** Resolve + assign road material on DynamicMeshComponent (asphalt test default). */
	void ApplyRoadMeshMaterial()
	{
		if (!DynamicMeshComponent)
		{
			return;
		}

		UMaterialInterface* Mat = RoadMaterial;
		if (!Mat)
		{
			// Temporary: all DynamicMesh road tests use asphalt from T_Asphalt_D.
			Mat = EnsureAsphaltTestMaterial();
		}
		if (!Mat && !DefaultRoadMaterial.IsNull())
		{
			Mat = DefaultRoadMaterial.LoadSynchronous();
		}
		if (!Mat)
		{
			Mat = LoadObject<UMaterialInterface>(
				nullptr,
				TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
		}
		if (Mat)
		{
			DynamicMeshComponent->SetMaterial(0, Mat);
		}
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline", meta = (DisplayName = "启用起始模型"))
	bool  EnableStartAssetMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline",meta=(DisplayName = "启用尾端模型"))
	bool  EnableEndAssetMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline",meta=(DisplayName = "曲线模型参数列表"))
	TArray<FSplineMeshInfoDetails> SplineMeshInfoList;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline",meta = (DisplayName = "模型随机值"))
	int32 SplineMeshSeed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline", meta = (EditCondition = "EnableStartAssetMesh", EditConditionHides,DisplayName = "起始模型输入"))
	FSplineConnectAssetMesh StartAssetMesh;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline", meta = (EditCondition = "EnableEndAssetMesh", EditConditionHides, DisplayName = "尾部模型输入"))
	FSplineConnectAssetMesh EndAssetMesh;

	UPROPERTY(TextExportTransient)
	TArray<FSocketGizmoTransformInfo> GizmoTransformInfos;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExtraMesh", meta = (DisplayName = "启用附件资产模型"))
	bool  EnableExtraMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExtraMesh", meta = (EditCondition = "EnableExtraMesh", EditConditionHides, DisplayName = "附件资产模型参数列表"))
	TArray<FSplineExtraMesh> ExtraSplineInfo;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (DisplayPriority = 19))
	FLandscapeBrushParameters BrushRenderParameters;




	UPROPERTY()
	bool IsInit = false;

	UPROPERTY()
	TMap<FString, FConnectionInfo> JunctionConnections =
	{
		{TEXT("Start"),FConnectionInfo()},
		{TEXT("End"),FConnectionInfo()}
	};

	UPROPERTY()
	TObjectPtr<UMoKuEditSplinesComponent> LeftCurve;

	UPROPERTY()
	TObjectPtr<UMoKuEditSplinesComponent> RightCurve;

	UPROPERTY()
	TWeakObjectPtr<AMoKuEditIntersectionActor> StartOfJunction;

	UPROPERTY()
	TWeakObjectPtr<AMoKuEditIntersectionActor> EndOfJunction;

	/** Mesh generation starts here when StartOfJunction is set (-1 = full spline start). Spline geometry unchanged. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	double StartJunctionMeshTrimS = -1.0;

	/** Mesh generation ends here when EndOfJunction is set (-1 = full spline end). Spline geometry unchanged. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	double EndJunctionMeshTrimS = -1.0;

	/** Multiple junction gaps along one road (preferred over Start/End trim fields). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Junction)
	TArray<FMetaLandRoadJunctionTrimZone> JunctionTrimZones;

	/** Active [SMin, SMax] for lane mesh generation (respects junction trim, not spline length). */
	void GetJunctionMeshGenerationRange(double& OutSMin, double& OutSMax) const;

	/** One or two spans when a through road has a middle gap at a junction. */
	void GetJunctionMeshGenerationSpans(TArray<TPair<double, double>>& OutSpans) const;

	//UPROPERTY()
	//TWeakObjectPtr<AWorldSceneManagement> SceneManagement;

	//TArray<TArray<TObjectPtr<UStaticMeshComponent>>> ExtraLocalMeshComponents;
	TArray<TArray<UStaticMeshComponent*>> ExtraLocalMeshComponents;
	TArray<TArray<FMoKuSplineInterpPoint>> ExtraLocalMeshInterPointList;


	/** Legacy angle/arc spline subdivision — disabled; kept for API compatibility. */
	TArray<FVector> UpdateInterpolateType(int32 Index, int32 SegmentNum=16,float Radius = 3000);
	void SetSplineComponent(UMoKuEditSplinesComponent* InSplineComponent);
	FORCEINLINE UMoKuEditSplinesComponent* GetSplineComponent() const { return SplineComponent;}
	FORCEINLINE UStaticMeshComponent* GetStartAsset() const { return StartAsset.Get(); }
	FORCEINLINE UStaticMeshComponent* GetEndAsset() const { return EndAsset.Get(); }
	UMoKuEditSplinesComponent* BuildSideSpline(const TArray<FVector>& InPosition);
	FORCEINLINE FSideRoadCurvePaths GetSideCurvePaths() { return SideCurvePaths; }
	void AddGizmoInfoToSocket(const TObjectPtr<UStaticMeshComponent>& InAsset, TArray<FSocketGizmoTransformInfo>& OutGizmoTransform,bool EndMesh = false);
	TSharedRef<FLegacyEdModeWidgetHelper> CreateWidgetHelper();
	void AddCapAssetToSpline();
	void UpdateOctreeInfo();


protected:

	UPROPERTY(TextExportTransient)
	TObjectPtr<UStaticMeshComponent> StartAsset;

	UPROPERTY(TextExportTransient)
	TObjectPtr<UStaticMeshComponent> EndAsset;




private:

	/** Spline + road mesh as siblings under SceneRoot (mesh must not hang under spline for ComponentVisualizer selection). */
	void EnsureRoadComponentHierarchy();

	void RemoveAllExtraMesh();
	void SerializeLocalMeshArray(FArchive& Ar, TArray<UStaticMeshComponent*>&InExtraLocalMeshComponents);
	void SerializeInterPointArray(FArchive& Ar, TArray<FMoKuSplineInterpPoint>& InExtraLocalMeshInterPointList);
	void UpdateExtraMesh(UStaticMeshComponent*& InLocalMeshComponents, const FMoKuSplineInterpPoint& InSplineInterpPoint,const FSplineExtraMesh& InExtraMeshInfo = FSplineExtraMesh());
	void RefreshExtraMeshViaIndex(int Index);
	//void UpdateTrunkPathCache();
#if 0 // Legacy: fixed-width ribbon from ConvertSplineToPolyLine + Cross(Tangent,Up). Use GenerateRoadFromLaneLayoutAsDynamicMesh.
	FDynamicMesh3* GenerateRoadFromSplineAsDynamicMesh(
		USplineComponent* SplineComponent,
		float RoadWidth,
		float RoadHeightOffset = 10.0f,
		bool bGenerateUVs = true);
#endif

	/** MetaRoad-style mesh: per-lane BuildLinearApproximation strips via GenerateCombinedLaneDynamicMesh. */
	FDynamicMesh3* GenerateRoadFromLaneLayoutAsDynamicMesh(
		UMetaLandRoadSplineComponent* RoadSplineComponent,
		float RoadHeightOffset = 0.0f,
		bool bGenerateUVs = true);

private:

	bool bIsDragging;
	bool bSkipConstruction;
	bool bSyncingLandscapeHeightFalloff = false;
	TSharedPtr<FLegacyEdModeWidgetHelper> WidgetHelper;

	struct FTrunkPathCache
	{
		TArray<FVector3d> Vertices;
		bool bClosed;
		FTransform ComponentTransform;
	};

	/** Legacy polyline cache for arc/angle mesh path — no longer used for road mesh generation. */
	FTrunkPathCache SplinesCache;
	UPROPERTY()
	FSideRoadCurvePaths SideCurvePaths;



};


#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#endif