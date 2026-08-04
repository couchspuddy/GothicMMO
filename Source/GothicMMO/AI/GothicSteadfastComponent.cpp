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

    // First-spawn pawns resolve here. Respawned and joining pawns don't have an
    // ASC yet at this point — ResolveASC() picks them up on first use.
    ResolveASC();
}

UAbilitySystemComponent* UGothicSteadfastComponent::ResolveASC()
{
    if (CachedASC)
    {
        return CachedASC;
    }

    if (GetOwner())
    {
        CachedASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
    }

    if (!CachedASC && !bWarnedNoASC)
    {
        bWarnedNoASC = true;
        UE_LOG(LogTemp, Warning, TEXT("GothicSteadfastComponent: No ASC found on %s"),
            GetOwner() ? *GetOwner()->GetName() : TEXT("NULL owner"));
    }

    return CachedASC;
}

void UGothicSteadfastComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    UAbilitySystemComponent* ASC = ResolveASC();
    if (!ASC || !InCombatTag.IsValid())
    {
        return;
    }

    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return; // server-authoritative accumulation only
    }

    if (!ASC->HasMatchingGameplayTag(InCombatTag))
    {
        return; // not in combat, no fill
    }

    const UGothicAttributeSet* AttributeSet = ASC->GetSet<UGothicAttributeSet>();
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

    // SteadfastRate is an additive per-second bonus from gear. Clamped at zero
    // so a negative roll can never invert the fill into a drain.
    const float EffectiveRate =
        FMath::Max(0.f, FillRatePerSecond + AttributeSet->GetSteadfastRate());

    const float NewSteadfast = FMath::Min(CurrentSteadfast + (EffectiveRate * DeltaTime), MaxSteadfast);

    // Direct attribute set — Steadfast is a simple accumulating resource,
    // not something that needs a GameplayEffect spec for a per-tick fill.
    UGothicAttributeSet* MutableAttributeSet = const_cast<UGothicAttributeSet*>(AttributeSet);
    MutableAttributeSet->SetSteadfast(NewSteadfast);
    UE_LOG(LogTemp, Verbose, TEXT("SteadfastTick: Set to %.2f, readback = %.2f"),
    NewSteadfast, MutableAttributeSet->GetSteadfast());
}

float UGothicSteadfastComponent::TryConvertSteadfast(float SteadfastCost, float AmmoGranted)
{
    UAbilitySystemComponent* ASC = ResolveASC();
    if (!ASC)
    {
        return 0.f;
    }

    UGothicAttributeSet* AttributeSet = const_cast<UGothicAttributeSet*>(ASC->GetSet<UGothicAttributeSet>());
    if (!AttributeSet)
    {
        return 0.f;
    }

    const float CurrentSteadfast = AttributeSet->GetSteadfast();

    if (CurrentSteadfast < SteadfastCost)
    {
        return 0.f;
    }

    AttributeSet->SetSteadfast(CurrentSteadfast - SteadfastCost);


    return AmmoGranted;
}

// The two readers are const to their Blueprint callers but still have to be able
// to fill a cache BeginPlay couldn't — const_cast here rather than a mutable
// UPROPERTY, which UHT won't take.
float UGothicSteadfastComponent::GetCurrentSteadfast() const
{
    if (UAbilitySystemComponent* ASC = const_cast<UGothicSteadfastComponent*>(this)->ResolveASC())
    {
        if (const UGothicAttributeSet* AttributeSet = ASC->GetSet<UGothicAttributeSet>())
        {
            return AttributeSet->GetSteadfast();
        }
    }
    return 0.f;
}

float UGothicSteadfastComponent::GetMaxSteadfast() const
{
    if (UAbilitySystemComponent* ASC = const_cast<UGothicSteadfastComponent*>(this)->ResolveASC())
    {
        if (const UGothicAttributeSet* AttributeSet = ASC->GetSet<UGothicAttributeSet>())
        {
            return AttributeSet->GetMaxSteadfast();
        }
    }
    return 0.f;
}