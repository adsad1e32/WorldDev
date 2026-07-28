#include "MetaLandRoadEditorColors.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"

const FColor FMetaLandRoadEditorColors::EmptyColor(106, 145, 196);
const FColor FMetaLandRoadEditorColors::SelectedColor = FStyleColors::AccentOrange.GetSpecifiedColor().ToFColor(true);
const FColor FMetaLandRoadEditorColors::ReadOnlyColor(255, 0, 255, 255);
const FColor FMetaLandRoadEditorColors::AccentColorHi(129, 106, 196);
const FColor FMetaLandRoadEditorColors::AccentColorLow(64, 53, 98);
const FColor FMetaLandRoadEditorColors::CrossSplineColor = FColor::Yellow;
// Hardcoded — FStyleColors may be uninitialized during static ctor (pink becomes invisible).
const FColor FMetaLandRoadEditorColors::SplineColor(255, 64, 191);
const FColor FMetaLandRoadEditorColors::HoverColor(255, 196, 64);
