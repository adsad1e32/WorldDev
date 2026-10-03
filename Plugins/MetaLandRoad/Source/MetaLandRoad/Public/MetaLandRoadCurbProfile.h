#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataAsset.h"
#include "MetaLandRoadCurbProfile.generated.h"

class UMetaLandRoadSplineComponent;
class UMaterialInterface;

namespace UE::Geometry
{
class FDynamicMesh3;
}

/** Legacy point array (migrated into CurbCurve on load). */
USTRUCT()
struct METALANDROAD_API FMetaLandRoadCurbSectionPoint
{
	GENERATED_BODY()

	UPROPERTY()
	FVector2D Point = FVector2D::ZeroVector;
};

/**
 * Project asset: right-click → Miscellaneous / MetaLandRoad Curb Profile.
 * Curve Time/Value are cm (0→Width, 0→Height). Changing Width/Height rewrites the keys.
 * Open n-curve → cover on the outer rim (Fig2). Closed + caps → solid box.
 * Sidewalk.CurbProfileAsset or CurbProfile name (ProfileId) selects it.
 */
UCLASS(BlueprintType)
class METALANDROAD_API UMetaLandRoadCurbProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UMetaLandRoadCurbProfile();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curb Profile")
	FName ProfileId = TEXT("Default");

	/** Time = lateral cm (0 → Width). Value = height cm (0 → Height). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curb Profile")
	FRuntimeFloatCurve CurbCurve;

	/** Lateral size of this n-curve (cm). Road / sidewalk width lives on the road, not here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curb Profile", meta = (ClampMin = "1.0"))
	float Width = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curb Profile", meta = (ClampMin = "1.0"))
	float Height = 15.0f;

	/**
	 * Extra lateral shift (cm) along the outward axis: road center → curb edge → grass.
	 * Positive pushes the cover further outside the road. Negative pulls it back onto the asphalt.
	 */
	/** Whole-number cm. Positive moves the cover into the grass. Negative moves it onto the asphalt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curb Profile", meta = (DisplayName = "Outward Offset"))
	int32 OutwardOffset = 0;

	/** Close the bottom (solid box). Off = open n-cover (Fig2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curb Profile")
	bool bClosedProfile = false;

	/** Cap the sweep ends. Off on roads — caps at CutS look like a floating rectangle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curb Profile")
	bool bCapEnds = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curb Profile", meta = (ClampMin = "10.0"))
	float UvTileCm = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curb Profile", DisplayName = "Default Material")
	TObjectPtr<UMaterialInterface> Material;

	UFUNCTION(CallInEditor, Category = "Curb Curve")
	void RecreateExternalCurve();

	void EnsureDefaultCurve();
	void GetProfilePoints(TArray<FVector2D>& OutPoints) const;
	virtual void PostLoad() override;

	static const UMetaLandRoadCurbProfile* Resolve(
		const TSoftObjectPtr<UMetaLandRoadCurbProfile>& Asset,
		FName ProfileId);

	static const UMetaLandRoadCurbProfile* FindById(FName ProfileId);

	static UMaterialInterface* FindFirstMaterial(UMetaLandRoadSplineComponent* Component);
	static void RecookRoadsUsingProfile(const UMetaLandRoadCurbProfile* Profile);

	void AppendSweptMesh(
		const TArray<FVector>& Path,
		const TArray<FVector>& Outwards,
		const TArray<float>& AlongS,
		float HeightOffset,
		bool bGenerateUVs,
		bool bInCapEnds,
		UE::Geometry::FDynamicMesh3& OutMesh) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

private:
	void ApplyDimensionsToCurve();
	void ReadDimensionsFromCurve();

	UPROPERTY()
	TArray<FMetaLandRoadCurbSectionPoint> CrossSection;
};

namespace MetaLandRoadCurb
{
	void AppendSidewalkLane(
		UMetaLandRoadSplineComponent* Component,
		int32 SectionIndex,
		int32 LaneIndex,
		double S0,
		double S1,
		int32 MinNumSteps,
		float RoadHeightOffset,
		bool bGenerateUVs,
		UE::Geometry::FDynamicMesh3& OutMesh);

	void AppendSidewalkCurbs(
		UMetaLandRoadSplineComponent* Component,
		int32 SectionIndex,
		double S0,
		double S1,
		int32 MinNumSteps,
		float RoadHeightOffset,
		bool bGenerateUVs,
		UE::Geometry::FDynamicMesh3& OutMesh,
		int32 LaneSign);
}
