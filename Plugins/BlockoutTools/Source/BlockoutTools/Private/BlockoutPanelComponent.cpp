#include "BlockoutPanelComponent.h"
#include "BlockoutPanelGeometry.h"

UBlockoutPanelComponent::UBlockoutPanelComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bIsEditorOnly = true;
}

double UBlockoutPanelComponent::GetThickness() const
{
	const int32 Axis = FMath::Clamp(WAxis, 0, 2);
	return FMath::Abs(PanelSize[Axis]);
}

TArray<FVector2D> UBlockoutPanelComponent::ResolveOuterContour() const
{
	if (Outer.Num() >= 3)
	{
		return Outer;
	}
	return BlockoutPanelGeometry::RectangleContour(PanelSize, FMath::Clamp(UAxis, 0, 2), FMath::Clamp(VAxis, 0, 2));
}

TArray<TArray<FVector2D>> UBlockoutPanelComponent::ResolveHoleContours() const
{
	TArray<TArray<FVector2D>> Result;
	Result.Reserve(Holes.Num());
	for (const FBlockoutPanelHole& Hole : Holes)
	{
		if (Hole.Points.Num() >= 3)
		{
			Result.Add(Hole.Points);
		}
	}
	return Result;
}
