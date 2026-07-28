#include "MetaLandRoadLaneAttribute.h"

void FMetaLandRoadLaneAttribute::Trim(double S0, double S1)
{
	int32 StartKey = INDEX_NONE;
	int32 EndKey = INDEX_NONE;
	for (int32 KeyIndex = 0; KeyIndex < Keys.Num(); ++KeyIndex)
	{
		const double KeyS = Keys[KeyIndex].SOffset;
		if (KeyS <= S0)
		{
			StartKey = KeyIndex;
		}
		if (KeyS >= S1)
		{
			EndKey = KeyIndex;
			break;
		}
	}

	if (EndKey != INDEX_NONE && (Keys.Num() - 1 != EndKey))
	{
		Keys.SetNum(EndKey + 1);
	}

	if (StartKey != INDEX_NONE && StartKey != 0)
	{
		Keys.RemoveAt(0, StartKey);
	}

	if (Keys.Num())
	{
		if (Keys[0].SOffset < S0)
		{
			Keys[0].SOffset = S0;
		}

		if (Keys[Keys.Num() - 1].SOffset > S1)
		{
			Keys[Keys.Num() - 1].SOffset = S1;
		}
	}
}
