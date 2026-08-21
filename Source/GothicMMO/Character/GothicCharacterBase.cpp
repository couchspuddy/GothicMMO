// GothicCharacterBase.cpp

#include "Character/GothicCharacterBase.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "AbilitySystem/GothicGameplayTags.h"
#include "GameplayEffectTypes.h"
#include "Components/CapsuleComponent.h"
#include "AI/GothicCombatStateComponent.h"
#include "Game/GothicPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"

AGothicCharacterBase::AGothicCharacterBase()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    SetReplicatingMovement(true);
}

UAbilitySystemComponent* AGothicCharacterBase::GetAbilitySystemComponent() const
{
    return AbilitySystemComponent;
}

UGothicAbilitySystemComponent* AGothicCharacterBase::GetGothicASC() const
{
    return AbilitySystemComponent;
}

float AGothicCharacterBase::GetHealth() const
{
    return AttributeSet ? AttributeSet->GetHealth() : 0.f;
}

float AGothicCharacterBase::GetMaxHealth() const
{
    return AttributeSet ? AttributeSet->GetMaxHealth() : 0.f;
}

float AGothicCharacterBase::GetStamina() const
{
    return AttributeSet ? AttributeSet->GetStamina() : 0.f;
}

float AGothicCharacterBase::GetEther() const
{
    return AttributeSet ? AttributeSet->GetEther() : 0.f;
}

bool AGothicCharacterBase::IsAlive() const
{
    return GetHealth() > 0.f;
}

bool AGothicCharacterBase::IsDowned() const
{
    const AGothicPlayerState* GothicPS = GetPlayerState<AGothicPlayerState>();
    return GothicPS && GothicPS->IsDowned();
}

bool AGothicCharacterBase::IsFightableActor(const AActor* Actor)
{
    if (!IsValid(Actor))
    {
        return false;
    }

    if (const AGothicCharacterBase* Char = Cast<const AGothicCharacterBase>(Actor))
    {
        // Alive AND still in the fight. The downed half is the whole point: once a
        // player goes down the enemy standing over them stops counting them as a
        // target, releases the body on its normal drop cadence, and re-acquires
        // whoever is left through the perception and retaliation paths that already
        // exist. No new AI behaviour — the predicate simply tells the truth.
        //
        // ACCEPTED EDGE, DO NOT SPECIAL-CASE: when every player is downed there are
        // zero fightable targets, so the enemies disengage and walk home. That is
        // correct, not a bug. The party-wipe detector fires respawn inside that
        // window; adding a "stay engaged if everyone is down" clause here would put
        // enemies back on bodies they cannot kill.
        return Char->IsAlive() && !Char->IsDowned();
    }

    return true;
}

void AGothicCharacterBase::BeginPlay()
{
    Super::BeginPlay();
}

void AGothicCharacterBase::InitializeGAS()
{
    if (!AbilitySystemComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("%s: InitializeGAS called but AbilitySystemComponent is null!"), *GetName());
        return;
    }

    AbilitySystemComponent->InitAbilityActorInfo(GetOwner() ? GetOwner() : this, this);

    if (StartupTags.Num() > 0)
    {
        AbilitySystemComponent->AddLooseGameplayTags(StartupTags);
    }

    if (!HasAuthority())
    {
        return;
    }

    if (DefaultAttributeEffect)
    {
        FGameplayEffectContextHandle Context = AbilitySystemComponent->MakeEffectContext();
        Context.AddSourceObject(this);

        FGameplayEffectSpecHandle SpecHandle = AbilitySystemComponent->MakeOutgoingSpec(
            DefaultAttributeEffect, 1.f, Context);

        if (SpecHandle.IsValid())
        {
            AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
        }
    }

    AbilitySystemComponent->GrantStartupAbilities(StartupAbilities, 1);
}

void AGothicCharacterBase::OnDeath_Implementation(AActor* Killer)
{
    // The ASC is null on the player pawn until PossessedBy runs
    // InitGASFromPlayerState, and this is reachable from Blueprint and from the
    // fall-respawn path. Every other method in this file null-checks it; this one
    // did not, which is the same shape as the respawn crash already fixed.
    // The tag work is skipped without an ASC — the physical death is not, or the
    // corpse keeps its collision and keeps walking.
    if (AbilitySystemComponent)
    {
        if (AbilitySystemComponent->HasMatchingGameplayTag(
            GothicTags::State_Dead))
        {
            return;
        }
        AbilitySystemComponent->AddLooseGameplayTag(
            GothicTags::State_Dead);

        AbilitySystemComponent->CancelAllAbilities();
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("%s died with no AbilitySystemComponent — State.Dead not applied, so the ")
            TEXT("re-entry guard is inactive for this death."),
            *GetName());
    }

    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // UE5.8: include GameFramework/CharacterMovementComponent.h to use these
    GetCharacterMovement()->DisableMovement();
    GetCharacterMovement()->StopMovementImmediately();

}
