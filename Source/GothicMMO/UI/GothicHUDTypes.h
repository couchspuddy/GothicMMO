// GothicHUDTypes.h
// Shared enums for the HUD system.
// Included by HUD, widget, and crosshair classes.

#pragma once

#include "CoreMinimal.h"
#include "GothicHUDTypes.generated.h"

/** Which HUD layout is currently active. */
UENUM(BlueprintType)
enum class EGothicHUDLayout : uint8
{
    LayoutA  UMETA(DisplayName = "Layout A - Destiny Corners"),
    LayoutC  UMETA(DisplayName = "Layout C - Gothic Asymmetric"),
    LayoutF  UMETA(DisplayName = "Layout F - Immersive"),
};

/** Crosshair style driven by equipped weapon type. */
UENUM(BlueprintType)
enum class EGothicCrosshairType : uint8
{
    Melee      UMETA(DisplayName = "Melee - Dot only"),
    Pistol     UMETA(DisplayName = "Pistol - Four line gap"),
    Rifle      UMETA(DisplayName = "Rifle - Tight with dot"),
    Throwable  UMETA(DisplayName = "Throwable - Circle arc"),
};
