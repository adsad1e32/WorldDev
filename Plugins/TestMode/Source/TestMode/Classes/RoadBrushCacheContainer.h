#pragma once

#include "RoadBrushCacheContainer.generated.h"

class UTextureRenderTarget2D;


USTRUCT(BlueprintType)
struct FRoadBodyBrushCache
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cache")
	TObjectPtr<UTextureRenderTarget2D> CacheRenderTarget = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cache")
	bool CacheIsValid = false;
};

UCLASS(config = Engine, Blueprintable, BlueprintType)
class URoadBodyBrushCacheContainer : public UObject
{
	GENERATED_BODY()

public:

	URoadBodyBrushCacheContainer(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, meta=(DisplayName="Cache", Category="Default"))
	FRoadBodyBrushCache Cache;
};

