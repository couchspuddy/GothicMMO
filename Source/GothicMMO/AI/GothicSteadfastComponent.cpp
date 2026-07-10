// GothicSteadfastComponent.cpp

#include "AI/GothicSteadfastComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/GothicAttributeSet.h"

UGothicSteadfastComponent::UGothicSteadfastComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UGothicSteadfastComponent::BeginPlay()
{
    Super::BeginPlay();

    if (GetOwner())
    {
        CachedASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
    }

    if (!CachedASC)
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicSteadfastComponent: No ASC found on %s"),
            *GetOwner()->GetName());
    }
}

void UGothicSteadfastComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!CachedASC || !InCombatTag.IsValid())
    {
        return;
    }

    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return; // server-authoritative accumulation only
    }

    if (!CachedASC->HasMatchingGameplayTag(InCombatTag))
    {
        return; // not in combat, no fill
    }

    const UGothicAttributeSet* AttributeSet = CachedASC->GetSet<UGothicAttributeSet>();
    if (!AttributeSet)
    {
        return;
    }

    const float CurrentSteadfast = AttributeSet->GetSteadfast();
    const float MaxSteadfast = AttributeSet->GetMaxSteadfast();

    if (CurrentSteadfast >= MaxSteadfast)
    {
        return; // already full
    }

    const float NewSteadfast = FMath::Min(CurrentSteadfast + (FillRatePerSecond * DeltaTime), MaxSteadfast);

    // Direct attribute set — Steadfast is a simple accumulating resource,
    // not something that needs a GameplayEffect spec for a per-tick fill.
    UGothicAttributeSet* MutableAttributeSet = const_cast<UGothicAttributeSet*>(AttributeSet);
    MutableAttributeSet->SetSteadfast(NewSteadfast);
    UE_LOG(LogTemp, Log, TEXT("SteadfastTick: Set to %.2f, readback = %.2f"),
    NewSteadfast, MutableAttributeSet->GetSteadfast());
}

float UGothicSteadfastComponent::TryConvertSteadfast(float SteadfastCost, float AmmoGranted)
{
    if (!CachedASC)
    {
        return 0.f;
    }

    UGothicAttributeSet* AttributeSet = const_cast<UGothicAttributeSet*>(CachedASC->GetSet<UGothicAttributeSet>());
    if (!AttributeSet)
    {
        return 0.f;
    }

    const float CurrentSteadfast = AttributeSet->GetSteadfast();

    if (CurrentSteadfast < SteadfastCost)
    {
        UE_LOG(LogTemp, Log, TEXT("GothicSteadfastComponent: Insufficient Steadfast (%.1f / %.1f needed)"),
            CurrentSteadfast, SteadfastCost);
        return 0.f;
    }

    AttributeSet->SetSteadfast(CurrentSteadfast - SteadfastCost);

    UE_LOG(LogTemp, Log, TEXT("GothicSteadfastComponent: Converted %.1f Steadfast for %.1f ammo"),
        SteadfastCost, AmmoGranted);

    return AmmoGranted;
}

float UGothicSteadfastComponent::GetCurrentSteadfast() const
{
    if (CachedASC)
    {
        if (const UGothicAttributeSet* AttributeSet = CachedASC->GetSet<UGothicAttributeSet>())
        {
            return AttributeSet->GetSteadfast();
        }
    }
    return 0.f;
}

float UGothicSteadfastComponent::GetMaxSteadfast() const
{
    if (CachedASC)
    {
        if (const UGothicAttributeSet* AttributeSet = CachedASC->GetSet<UGothicAttributeSet>())
        {
            return AttributeSet->GetMaxSteadfast();
        }
    }
    return 0.f;
}