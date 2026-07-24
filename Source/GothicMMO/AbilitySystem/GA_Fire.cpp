// GA_Fire.cpp

#include "AbilitySystem/GA_Fire.h"

#include "GothicMMO.h"                          // ECC_Weapon
#include "AbilitySystem/GothicAbilitySystemComponent.h"
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

    const float FireInterval = WeaponData
        ? WeaponData->GetFireInterval()
        : (FallbackRoundsPerMinute > 0.f ? 60.f / FallbackRoundsPerMinute : 0.f);

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
        return;
    }

    UAbilitySystemComponent* TargetASC =
        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Hit.GetActor());
    UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();

    if (!TargetASC || !SourceASC || !EffectiveDamageGE)
    {
        return;
    }

    float FinalDamage = EffectiveDamage;
    bool bIsVitalHit = false;

    if (UGothicVitalPointComponent* VitalPoint =
            Hit.GetActor()->FindComponentByClass<UGothicVitalPointComponent>())
    {
        const bool bReckoning = SourceASC->HasMatchingGameplayTag(
            FGameplayTag::RequestGameplayTag(FName("State.Reckoning")));
        bIsVitalHit = bReckoning || VitalPoint->IsVitalPointHit(Hit.ImpactPoint);
    }

    if (bIsVitalHit)
    {
        FinalDamage *= EffectiveVitalMult;
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

    Spec.Data->SetSetByCallerMagnitude(
        FGameplayTag::RequestGameplayTag(FName("Data.Damage")), FinalDamage);

    SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);

    if (AGothicEnemyBase* HitEnemy = Cast<AGothicEnemyBase>(Hit.GetActor()))
    {
        HitEnemy->MulticastOnHit(Hit.ImpactPoint, bIsVitalHit, FinalDamage);
    }

}