#pragma once
#include "CoreMinimal.h"
#include "MoKuEditSplinesComponent.h"
#include "MoKuEditSplineActor.h"
#include "MoKuEditIntersectionActor.h"
#include "LevelEditor.h"
#include "EditorModeManager.h"
#include "EditSplineRoadInfo.h"

#define SCENE_JUNCTION_PATH "/Procedural_Scene/Scene_Junction"
#define SCENE_ROAD_PATH "/Procedural_Scene/Scene_Road"
#define SCENE_LOT_PATH "/Procedural_Scene/Scene_Lots"
#define SCENE_ROOT_PATH "/Procedural_Scene"
#define SQUAREDISTANCE      400
enum EAssetDimensionAxis : uint8
{
	X,
	Y,
	Z,
};


struct FInteresectionPointInfo
{
	FVector IntersectPoint;
	TArray<TObjectPtr<UMoKuEditSplinesComponent>> Spline;
	TArray<double> Dist;
	TArray<AMoKuEditBaseActor*> IntersectRoadList;
	FString Desc;

};

struct FActorInteresectionPointInfo
{
	FVector IntersectPoint;
	TObjectPtr<UMoKuEditSplinesComponent> Spline;
	double Dist;
	FString Desc;

	bool operator==(const FActorInteresectionPointInfo& Other) const
	{

		return IntersectPoint == Other.IntersectPoint &&
			Spline == Other.Spline &&
			Desc == Other.Desc &&
			Dist == Dist;
	}

	bool operator!=(const FActorInteresectionPointInfo & Other) const
	{
		return !(*this == Other);
	}

};




class  MoKuSplineEditorUtils
{
public:
	static TArray<FMoKuSplineInterpPoint> SplitSegmentToSpline(UMoKuEditSplinesComponent* InSplineComponent, TArray<UStaticMesh*> InStaticMeshArray, const FSplineExtraMesh& InExtraMeshInfo = FSplineExtraMesh());

	static void UpdateSplineMesh(TArray<UStaticMeshComponent*>& InLocalMeshComponents, const TArray<FMoKuSplineInterpPoint>& InIterpPoints, USceneComponent* InComponent);

	static void CutSplineFromRange(USplineComponent* InSplineComponent, float StartValue, float EndValue, TArray<FTransform>& OutSplitSplinePostion);

	static AMoKuEditSplineActor* CreateNewMokuSplineActor(AActor* InSrcActor, const TArray<FTransform>& InTransforms,bool SetDefultPath = false);

	static float GetAssetDimensions(UStaticMesh* InStaticMesh, EAssetDimensionAxis Axis);

	static void SplitMoKuSplineActor(AMoKuEditBaseActor* InSplineActor, float SplitRatio, UStaticMesh* InCrossRoad = nullptr, bool bIsInvertCross = false);

	static AMoKuEditIntersectionActor* SplitSplineActorSegment(AMoKuEditBaseActor* InSplineActor, float SplitRatio, bool bIsInvertCross = false, FEditSplineIntersectInfo IntersectedActorInfo= FEditSplineIntersectInfo());

	static void  OctreeSegmentsIntersect(FSplineRoadActorOctree Octree, AMoKuEditSplineActor* InSplineActor, TArray<FInteresectionPointInfo>& OutIntersectionInfo,TArray<FBox>& OutDebugBox);

	static bool DoSegmentsIntersect(const FVector2D& Segment1Start, const FVector2D& Segment1End, const FVector2D& Segment2Start, const FVector2D& Segment2End, FVector2D& OutIntersectionPoint,double& OutSeg1Intersection, double& OutSeg2Intersection);

	static bool DoSplineIntersect(USplineComponent* Spline1, USplineComponent* Spline2, FVector3d& OutIntersectionPoint);

	static TArray<FVector3d> BezierLineConvert(const TArray<FVector3d>& InBaseCurve,uint32 Segment);

	static bool CreateSceneStructure();

	static TArray<AActor*> GetActorListByFolderPath(const FString& InFolderPath);

	static TArray<FVector> SortEdgePathByList(const TArray<TArray<FVector>>& InPostionList);

	static UDynamicMeshComponent* SplineMeshBuilder(const TArray<FVector>& InEdgePath);

	static  void GetSplineInterpInfo(USplineComponent* InCurve, const FVector& IntersectionPos, float& OutClosestKey, float& OutDist, float& OutRatio);

	static FCornerInfo CutJunctionCornerInfo(USplineComponent* InCurve, TArray<FVector> IntersectionPosList, uint32 Segments = 20);

	static void  CheckAndSetEditLayer(UEdMode* ModeIn,const FString& InLayerName);

	static void ConvertPointInfoData(const TArray<FInteresectionPointInfo>& IntersectionInfo, TMap<TObjectPtr<AMoKuEditBaseActor>, TMap<TObjectPtr<UMoKuEditSplinesComponent>, TArray<FActorInteresectionPointInfo>>>& OutActorInteresectionMapPointInfo);

	template<typename ModeType>
	static ModeType* GetCurrentEditMode(FEditorModeID Id)
	{
		if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
		{
			TSharedPtr<ILevelEditor> LevelEditorPtr = LevelEditorModule->GetLevelEditorInstance().Pin();
			if (LevelEditorPtr.IsValid())
			{
				ModeType* EditMode = Cast<ModeType>(LevelEditorPtr->GetEditorModeManager().GetActiveScriptableMode(Id));
				if (EditMode) return EditMode;
			}
		}
		return nullptr;
	}

	static void DrawHelpCanvasTileItem(FCanvas* Canvas, const FString& InText);

private:

	static void SplitSpline(USplineComponent* InSplineComponent, float TValue, TArray<FTransform>& InFirstHalfSplitPostion, TArray<FTransform>& InEndHalfSplitPostion);

	static void SplitSpline(USplineComponent* InSplineComponent,float StartValue,float EndValue, TArray<FTransform>& InFirstHalfSplitPosition, TArray<FTransform>& InEndHalfSplitPosition);



};

