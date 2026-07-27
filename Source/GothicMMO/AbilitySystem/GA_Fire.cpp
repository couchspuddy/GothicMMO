// GA_Fire.cpp

#include "AbilitySystem/GA_Fire.h"

#include "GothicMMO.h"                          // ECC_Weapon
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"   // AttackPower scalar
#include "AI/GothicEnemyBase.h"
#include "AI/GothicVitalPointComponent.h"
#include "Character/GothicPlayerCharacter.h"
#include "Camera/CameraComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

UGA_Fire::UGA_Fire()
{
    // One instance per shot. Fire holds no state between activations.
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;

    // The base class defaults to ServerInitiated, which is correct for Reckoning
    // (a little activation delay before a super is unnoticeable) and wrong for a
    // trigger pull. LocalPredicted lets the client run the cosmetic half instantly
    // while the server independently runs the trace and damage.
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

    // Slot is a default only — DA_HunterAbilitySet overrides it at grant time.
    AbilitySlot = EGothicAbilitySlot::PrimaryFire;

    // The gating OnFire() never had. A dead or stunned player could previously
    // still trace and deal damage.
    ActivationBlockedTags.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Dead")));
    ActivationBlockedTags.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Stunned")));
}

bool UGA_Fire::CanActivateAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayTagContainer* SourceTags,
    const FGameplayTagContainer* TargetTags,
    OUT FGameplayTagContainer* OptionalRelevantTags) const
{
    if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
    {
        return false;
    }

    const AGothicPlayerCharacter* Char =
        ActorInfo ? Cast<AGothicPlayerCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;

    return Char && Char->HasRoundChambered();
}

void UGA_Fire::ActivateAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    // Exactly one CommitAbility. Two pays the cooldown twice (Slicer, Read); zero
    // makes the ability free (Reckoning). Both have already happened in this project.
    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }

    AGothicPlayerCharacter* Char = Cast<AGothicPlayerCharacter>(GetAvatarActorFromActorInfo());
    if (!Char)
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }
    
    UE_LOG(LogTemp, Warning, TEXT("GA_Fire Activate: Auth=%d Local=%d | CooldownGE=%s | DamageGE=%s"),
    HasAuthority(&ActivationInfo) ? 1 : 0,
    ActorInfo->IsLocallyControlled() ? 1 : 0,
    *GetNameSafe(GetCooldownGameplayEffect()),
    *GetNameSafe(DamageEffectClass));

    // Runs on the predicting client and on the server. Both hold their own
    // unreplicated copy and run the same deterministic decrement, so they agree.
    // Not yet server-authoritative — see the AUTHORITY NOTE on FOnAmmoChanged.
    Char->ConsumeRound();

    // Cosmetic half — instant, local, never waits on the server.
    if (ActorInfo->IsLocallyControlled())
    {
        Char->ApplyRecoilKick();
        OnFireCosmetic();
    }

    // Authoritative half — the server alone decides what was hit and for how much.
    if (HasAuthority(&ActivationInfo))
    {
        PerformFireTrace(Char);
    }

    // Every path above reaches EndAbility. Fire has no projectile or async wait,
    // so there is no miss branch that can strand it (cf. the Slicer soft-lock).
    EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UGA_Fire::ApplyCooldown(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo) const
{
    const AGothicPlayerCharacter* Char =
        ActorInfo ? Cast<AGothicPlayerCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
    const UGothicWeaponData* WeaponData = Char ? Char->GetActiveWeaponData() : nullptr;

    // A weapon may bring its own cooldown GE; almost none will. Either way the
    // duration comes from the SetByCaller below, not from the asset.
    TSubclassOf<UGameplayEffect> CooldownGE = WeaponData && WeaponData->CooldownEffect
        ? WeaponData->CooldownEffect : CooldownGameplayEffectClass;

    if (!CooldownGE)
    {
        return;
    }

    FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(CooldownGE, GetAbilityLevel());
    if (!Spec.IsValid())
    {
        return;
    }

    float FireInterval = WeaponData
        ? WeaponData->GetFireInterval()
        : (FallbackRoundsPerMinute > 0.f ? 60.f / FallbackRoundsPerMinute : 0.f);

    // AbilityHaste is a percent cooldown reduction, clamped so gear can shorten
    // the interval but never reach zero. Note this only reaches cooldowns driven
    // by a Data.Cooldown SetByCaller — of the project's abilities that is Fire
    // alone; the rest carry fixed durations on their GE assets and are unaffected
    // until they are converted to the same pattern.
    if (const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
    {
        const float Haste = FMath::Clamp(
            ASC->GetNumericAttribute(UGothicAttributeSet::GetAbilityHasteAttribute()),
            0.f, MaxAbilityHastePercent);

        FireInterval *= (1.f - (Haste / 100.f));
    }

    Spec.Data->SetSetByCallerMagnitude(
        FGameplayTag::RequestGameplayTag(FName("Data.Cooldown")), FireInterval);

    ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, Spec);
}

void UGA_Fire::PerformFireTrace(AGothicPlayerCharacter* Char)
{
    UCameraComponent* Camera = Char->FindComponentByClass<UCameraComponent>();
    UWorld* World = Char->GetWorld();

    if (!Camera || !World)
    {
        return;
    }

    // Read stats from the active weapon data, falling back to GA_Fire's own defaults
    const UGothicWeaponData* WeaponData = Char->GetActiveWeaponData();
    const float EffectiveDamage     = WeaponData ? WeaponData->Damage             : Damage;
    const float EffectiveVitalMult  = WeaponData ? WeaponData->VitalDamageMultiplier : VitalDamageMultiplier;
    const float EffectiveRange      = WeaponData ? WeaponData->TraceRange         : TraceRange;
    TSubclassOf<UGameplayEffect> EffectiveDamageGE = WeaponData && WeaponData->DamageEffect
        ? WeaponData->DamageEffect : DamageEffectClass;

    const FVector Start = Camera->GetComponentLocation();
    const FVector End   = Start + (Camera->GetForwardVector() * EffectiveRange);

    FHitResult Hit;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(Char);

    const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Weapon, Params);

    if (!bHit || !Hit.GetActor())
    {
        // A miss breaks the Oversurge streak. Done here rather than on the
        // damage path so shooting a wall counts as a miss too -- the streak is
        // "hits without missing", not "hits since the last hit".
        Char->ResetConsecutiveHits();
        return;
    }

    UAbilitySystemComponent* TargetASC =
        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Hit.GetActor());
    UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();

    if (!TargetASC || !SourceASC || !EffectiveDamageGE)
    {
        return;
    }

    // Damage model — ITEMIZATION_AND_LOOT.md plus the Gear Power / Attack Power
    // split. Three factors, and no universal armor-damage anywhere:
    //
    //   Weapon base damage — EffectiveDamage. The weapon's own attack power, per
    //                        archetype. Armor never scales this universally.
    //   Gear Power floor   — the AGGREGATE power level across ALL equipped gear,
    //                        not just this weapon. It raises the floor of every
    //                        hit and is the intended activity gate. Relative to a
    //                        baseline so a starting loadout scales by ~1.0.
    //   Archetype bonus    — armor's per-archetype damage lines, and ONLY the one
    //                        matching the equipped weapon's archetype. A Revolver
    //                        line does nothing while a Rifle is out.
    const int32 AggregateGearPower = Char->GetAggregateGearPower();
    const float GearFloor = (BaselineGearPower > 0.f && AggregateGearPower > 0)
        ? static_cast<float>(AggregateGearPower) / BaselineGearPower
        : 1.f;

    const float ArchetypeBonusPct = WeaponData
        ? Char->GetArchetypeDamageBonusPct(WeaponData->Archetype)
        : 0.f;

    float FinalDamage = EffectiveDamage * GearFloor * (1.f + ArchetypeBonusPct / 100.f);
    bool bIsVitalHit = false;

    if (UGothicVitalPointComponent* VitalPoint =
            Hit.GetActor()->FindComponentByClass<UGothicVitalPointComponent>())
    {
        const bool bReckoning = SourceASC->HasMatchingGameplayTag(
            FGameplayTag::RequestGameplayTag(FName("State.Reckoning")));
        const float RadiusBonus = SourceASC->GetNumericAttribute(
            UGothicAttributeSet::GetVitalPointRadiusAttribute());

        bIsVitalHit = bReckoning || VitalPoint->IsVitalPointHit(Hit.ImpactPoint, RadiusBonus);
    }

    if (bIsVitalHit)
    {
        FinalDamage *= EffectiveVitalMult;

        // The Read: vital hits hurt more against a target you have READ, and
        // only that target. Checked on TargetASC, not SourceASC -- as a caster
        // tag it sharpened every shot at every enemy in the room for the whole
        // window, which made it a flat damage cooldown rather than an act of
        // reading one opponent.
        if (TargetASC->HasMatchingGameplayTag(
                FGameplayTag::RequestGameplayTag(FName("State.Read.Marked"))))
        {
            FinalDamage *= ReadVitalDamageMultiplier;
        }
    }

    FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
    Context.AddSourceObject(Char);
    Context.AddInstigator(Char, Char);

    FGameplayEffectSpecHandle Spec =
        SourceASC->MakeOutgoingSpec(EffectiveDamageGE, GetAbilityLevel(), Context);

    if (!Spec.IsValid())
    {
        return;
    }

    // ── Shock: streak, Oversurge, stun ───────────────────────────────────
    // Registered before the damage spec is finalised so an Oversurge can scale
    // THIS shot rather than the next one.
    Char->RegisterWeaponHit();

    bool bOversurged = false;
    if (WeaponData && WeaponData->OversurgeHitsRequired > 0 &&
        Char->GetConsecutiveHits() >= WeaponData->OversurgeHitsRequired &&
        FMath::FRand() < WeaponData->OversurgeChance)
    {
        FinalDamage *= WeaponData->OversurgeDamageMultiplier;
        bOversurged = true;

        // Spend the streak. Without this every subsequent hit keeps rolling at
        // full chance, which turns a payoff into a sustained damage multiplier.
        Char->ResetConsecutiveHits();
    }

    Spec.Data->SetSetByCallerMagnitude(
        FGameplayTag::RequestGameplayTag(FName("Data.Damage")), FinalDamage);

    SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);

    // Stun rolls independently of Oversurge -- they are separate hooks and a
    // single shot is allowed to do both.
    if (WeaponData && WeaponData->ShockStunEffect && WeaponData->StunChance > 0.f &&
        FMath::FRand() < WeaponData->StunChance)
    {
        UGothicAbilitySystemComponent::ApplyEffectToASC(
            TargetASC, WeaponData->ShockStunEffect, Char);
    }

    if (AGothicEnemyBase* HitEnemy = Cast<AGothicEnemyBase>(Hit.GetActor()))
    {
        // An Oversurge reads as a vital hit to the feedback layer so it gets the
        // heavier number and flash rather than passing as an ordinary tick.
        HitEnemy->MulticastOnHit(Hit.ImpactPoint, bIsVitalHit || bOversurged, FinalDamage);
    }

}