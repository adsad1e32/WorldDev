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
#include "MoKuEditIntersectionActor.h"
#include "MoKuEditSplinesComponent.h"

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
	//virtual void UpdateSceneManagementData()override;
	virtual FEditSplineIntersectInfo CheckIntersectionState(AMoKuEditBaseActor* InEditActor);

#if WITH_EDITOR

	virtual void PostActorCreated() override;
#endif

	UPROPERTY(VisibleAnywhere, Category = "Spline")
	TObjectPtr<UMoKuEditSplinesComponent> SplineComponent = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline", meta = (DisplayName = "道路类型"))
	ERoadType RoadModeType;

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

	//UPROPERTY()
	//TWeakObjectPtr<AWorldSceneManagement> SceneManagement;

	//TArray<TArray<TObjectPtr<UStaticMeshComponent>>> ExtraLocalMeshComponents;
	TArray<TArray<UStaticMeshComponent*>> ExtraLocalMeshComponents;
	TArray<TArray<FMoKuSplineInterpPoint>> ExtraLocalMeshInterPointList;


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

	void RemoveAllExtraMesh();
	void SerializeLocalMeshArray(FArchive& Ar, TArray<UStaticMeshComponent*>&InExtraLocalMeshComponents);
	void SerializeInterPointArray(FArchive& Ar, TArray<FMoKuSplineInterpPoint>& InExtraLocalMeshInterPointList);
	void UpdateExtraMesh(UStaticMeshComponent*& InLocalMeshComponents, const FMoKuSplineInterpPoint& InSplineInterpPoint,const FSplineExtraMesh& InExtraMeshInfo = FSplineExtraMesh());
	void RefreshExtraMeshViaIndex(int Index);
	//void UpdateTrunkPathCache();
	FDynamicMesh3* GenerateRoadFromSplineAsDynamicMesh(
		USplineComponent* SplineComponent,
		float RoadWidth,
		float RoadHeightOffset = 10.0f,
		bool bGenerateUVs = true);

private:

	bool bIsDragging;
	bool bSkipConstruction;
	TSharedPtr<FLegacyEdModeWidgetHelper> WidgetHelper;

	struct FTrunkPathCache
	{
		TArray<FVector3d> Vertices;
		bool bClosed;
		FTransform ComponentTransform;
	};

	FTrunkPathCache SplinesCache;
	UPROPERTY()
	FSideRoadCurvePaths SideCurvePaths;



};


#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#endif