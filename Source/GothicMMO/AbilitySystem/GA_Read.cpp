// GA_Read.cpp

#include "AbilitySystem/GA_Read.h"
#include "GothicMMO.h"                          // ECC_Weapon
#include "AbilitySystem/GothicGameplayTags.h"   // State_Read, State_Read_Marked
#include "Character/GothicPlayerCharacter.h"    // SetReadMark — single-prey bookkeeping
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"

UGA_Read::UGA_Read()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
}

void UGA_Read::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
    AActor* Avatar = GetAvatarActorFromActorInfo();

    if (!ASC || !Avatar || !ReadMarkEffect)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GA_Read: missing ASC, avatar or ReadMarkEffect — cancelling"));
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }

    // ── Find who is being read ───────────────────────────────────────────
    // Traced from the camera, matching GA_Fire, so what you are looking at is
    // what you read. Deliberately BEFORE CommitAbility: a Read that finds
    // nothing must not burn the cooldown, or the ability punishes the player
    // for a moving target slipping the trace.
    UWorld* World = Avatar->GetWorld();
    ACharacter* Char = Cast<ACharacter>(Avatar);
    UCameraComponent* Camera = Char ? Char->FindComponentByClass<UCameraComponent>() : nullptr;

    if (!World || !Camera)
    {
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }

    const FVector Start = Camera->GetComponentLocation();
    const FVector End = Start + Camera->GetForwardVector() * ReadRange;

    FCollisionQueryParams Params;
    Params.AddIgnoredActor(Avatar);

    // A LINE on ECC_Weapon, character-for-character GA_Fire's trace (GA_Fire.cpp
    // PerformFireTrace). This was a 40uu swept sphere on ECC_Pawn, and it read a
    // different enemy than the one under the crosshair — measured as marking
    // BP_Enemy_Draugr_C_9 while aimed squarely at BP_Enemy_Thrall_Feral_C_2.
    //
    // Two faults, both fixed by matching Fire:
    //   - Wrong channel. ECC_Weapon is the channel enemy meshes are authored to
    //     block (the Custom profile on BP_Enemy_Thrall_Feral). Enemy geometry that
    //     stops a bullet does NOT necessarily block ECC_Pawn, so the query passed
    //     straight through the near target and ran on until something further away
    //     happened to block Pawn.
    //   - Wrong shape. A fat sweep and a line disagree about what you are aiming
    //     at, and Read MUST agree with Fire — the whole point of the mark is that
    //     the next shot lands on the thing you read. Whatever Fire can hit is
    //     exactly what Read can mark, by construction.
    //
    // ReadTraceRadius is kept on the header but no longer used by the trace; see
    // the note there before reintroducing any tolerance.
    FHitResult Hit;
    const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Weapon, Params);

    UAbilitySystemComponent* TargetASC = (bHit && Hit.GetActor())
        ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Hit.GetActor())
        : nullptr;

    if (!TargetASC)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("GA_Read: nothing readable under the crosshair — no cost spent."));
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }

    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }

    // ── Apply the two state effects ──────────────────────────────────────
    //
    // Both of these used to be built with ASC->MakeOutgoingSpec and pushed through
    // the RAW ASC entry points with no prediction key:
    //
    //     ASC->ApplyGameplayEffectSpecToTarget(*MarkSpec.Data.Get(), TargetASC);
    //     ASC->ApplyGameplayEffectSpecToSelf(*SelfSpec.Data.Get());
    //
    // That is why neither State.Read nor State.Read.Marked ever appeared on any
    // actor, while GE_Cooldown_Read from the very same activation applied
    // perfectly. UAbilitySystemComponent::ApplyGameplayEffectSpecToSelf opens with
    // HasNetworkAuthorityToApplyGameplayEffect(PredictionKey), which is
    // (IsOwnerActorAuthoritative() || PredictionKey.IsValidForMorePrediction()).
    // With a default-constructed key that reduces to the authority test alone, and
    // when it fails the function returns an INVALID handle and applies nothing —
    // no log, no warning, no ensure. The old code discarded the return value, so
    // the failure was completely silent.
    //
    // The cooldown survived because it does not take this path: CommitAbility ->
    // ApplyCooldown -> UGameplayAbility::ApplyGameplayEffectToOwner passes
    // ActivationInfo.GetActivationPredictionKey(), which satisfies the check.
    //
    // So: go through the ability's own helpers, exactly as the cooldown does, and
    // carry the activation prediction key on both applications. MakeOutgoing-
    // GameplayEffectSpec also builds a proper ability-aware context (ability,
    // instigator, effect causer) rather than the bare MakeEffectContext the old
    // code hand-rolled.
    const float Level = GetAbilityLevel(Handle, ActorInfo);

    // The bonus rides on the enemy, not the shooter, so reading one enemy no
    // longer sharpens every shot you take at everything else in the room.
    FGameplayEffectSpecHandle MarkSpec =
        MakeOutgoingGameplayEffectSpec(Handle, ActorInfo, ActivationInfo, ReadMarkEffect, Level);

    FActiveGameplayEffectHandle MarkHandle;
    if (MarkSpec.IsValid())
    {
        MarkHandle = ASC->ApplyGameplayEffectSpecToTarget(
            *MarkSpec.Data.Get(), TargetASC, ActivationInfo.GetActivationPredictionKey());
    }

    // Single prey: record this mark and lift the one on whoever we last read. The
    // Read holds no cross-activation state itself (InstancedPerExecution), so the
    // bookkeeping lives on the character; SetReadMark no-ops when re-reading the
    // same enemy and is authority-gated inside. Without this, reading a second
    // enemy inside GE_ReadMark's window left BOTH marked.
    if (AGothicPlayerCharacter* PlayerChar = Cast<AGothicPlayerCharacter>(Avatar))
    {
        PlayerChar->SetReadMark(TargetASC, MarkHandle);
    }

    // Caster-side window tag, HUD only. Kept so IsReadActive() and its proc icon
    // work unchanged; on its own it now grants no damage.
    FActiveGameplayEffectHandle SelfHandle;
    if (ReadStateEffect)
    {
        FGameplayEffectSpecHandle SelfSpec =
            MakeOutgoingGameplayEffectSpec(Handle, ActorInfo, ActivationInfo, ReadStateEffect, Level);

        if (SelfSpec.IsValid())
        {
            SelfHandle = ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, SelfSpec);
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GA_Read: ReadStateEffect is unset — no State.Read, the HUD proc icon cannot light."));
    }

    // ── Prove it landed ──────────────────────────────────────────────────
    // This ability has already shipped once in a state where it committed, logged
    // a successful mark, and applied nothing at all. Assert the observable
    // outcome — the TAG, not the handle — so that failure can never be silent
    // again. Warnings, not ensures: a missed mark must not take the session down.
    const FGameplayTag ReadTag = GothicTags::State_Read;
    const FGameplayTag MarkedTag = GothicTags::State_Read_Marked;

    if (!MarkHandle.IsValid() || !TargetASC->HasMatchingGameplayTag(MarkedTag))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GA_Read: GE_ReadMark did NOT take on %s (handle %s, tag %s) — ")
            TEXT("vital damage will not be amplified. Check GE_ReadMark's duration and ")
            TEXT("that the target's ASC owner is authoritative."),
            *GetNameSafe(Hit.GetActor()),
            MarkHandle.IsValid() ? TEXT("valid") : TEXT("INVALID"),
            TargetASC->HasMatchingGameplayTag(MarkedTag) ? TEXT("present") : TEXT("ABSENT"));
    }

    if (ReadStateEffect && (!SelfHandle.IsValid() || !ASC->HasMatchingGameplayTag(ReadTag)))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GA_Read: GE_ReadState did NOT take on the caster (handle %s, tag %s) — ")
            TEXT("the HUD proc icon will stay dark."),
            SelfHandle.IsValid() ? TEXT("valid") : TEXT("INVALID"),
            ASC->HasMatchingGameplayTag(ReadTag) ? TEXT("present") : TEXT("ABSENT"));
    }

    UE_LOG(LogTemp, Log, TEXT("GA_Read: marked %s (mark %s, self %s)"),
        *GetNameSafe(Hit.GetActor()),
        TargetASC->HasMatchingGameplayTag(MarkedTag) ? TEXT("up") : TEXT("MISSING"),
        ASC->HasMatchingGameplayTag(ReadTag) ? TEXT("up") : TEXT("MISSING"));

    EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
