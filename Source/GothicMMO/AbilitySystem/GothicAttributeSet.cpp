// GothicAttributeSet.cpp

#include "AbilitySystem/GothicAttributeSet.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"
#include "GameFramework/Character.h"
#include "AI/GothicCombatStateComponent.h"
#include "AI/GothicVitalPointComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Character/GothicCharacterBase.h"

UGothicAttributeSet::UGothicAttributeSet()
{
    InitHealth(100.f);
    InitMaxHealth(100.f);
    InitStamina(100.f);
    InitMaxStamina(100.f);
    InitEther(50.f);
    InitMaxEther(50.f);
    InitAttackPower(10.f);
    InitDefense(5.f);
    InitIncomingDamage(0.f);

    // Secondary stats all start neutral — gear is strictly additive on top.
    InitMovementSpeed(0.f);
    InitEvasionChance(0.f);
    InitAbilityHaste(0.f);
    InitVitalPointRadius(0.f);
    InitSteadfastRate(0.f);
    InitHealingReceived(0.f);
    InitReloadSpeed(0.f);
    InitSuperMeter(0.f);
    InitMaxSuperMeter(100.f);
    InitSelah(0.f);
}

void UGothicAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME_CONDITION_NOTIFY(UGothicAttributeSet, Health,       COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UGothicAttributeSet, MaxHealth,    COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UGothicAttributeSet, Stamina,      COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UGothicAttributeSet, MaxStamina,   COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UGothicAttributeSet, Ether,        COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UGothicAttributeSet, MaxEther,     COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UGothicAttributeSet, AttackPower,  COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UGothicAttributeSet, Defense,      COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UGothicAttributeSet, MovementSpeed,    COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UGothicAttributeSet, EvasionChance,    COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UGothicAttributeSet, AbilityHaste,     COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UGothicAttributeSet, VitalPointRadius, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UGothicAttributeSet, SteadfastRate,    COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UGothicAttributeSet, HealingReceived,  COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UGothicAttributeSet, ReloadSpeed,      COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UGothicAttributeSet, SuperMeter,    COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UGothicAttributeSet, MaxSuperMeter, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UGothicAttributeSet, Selah, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME(UGothicAttributeSet, Steadfast);
    DOREPLIFETIME(UGothicAttributeSet, MaxSteadfast);
}

void UGothicAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
    Super::PreAttributeChange(Attribute, NewValue);

    if (Attribute == GetHealthAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
    }
    else if (Attribute == GetStaminaAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, GetMaxStamina());
    }
    else if (Attribute == GetEtherAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, GetMaxEther());
    }
    else if (Attribute == GetSuperMeterAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, GetMaxSuperMeter());
    }
    else if (Attribute == GetSelahAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
    }
}

void UGothicAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute,
                                                 float& NewValue) const
{
    Super::PreAttributeBaseChange(Attribute, NewValue);

    // Mirrors PreAttributeChange. See the header for why the init effect leans
    // on this: it overrides the pools with a sentinel so they land on the real
    // ceiling, gear included, instead of a hardcoded number that predates the
    // gear bonus.
    if (Attribute == GetHealthAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
    }
    else if (Attribute == GetStaminaAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, GetMaxStamina());
    }
    else if (Attribute == GetEtherAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, GetMaxEther());
    }
    else if (Attribute == GetSuperMeterAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, GetMaxSuperMeter());
    }
    else if (Attribute == GetSelahAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
    }
}

void UGothicAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute,
                                              float OldValue, float NewValue)
{
    Super::PostAttributeChange(Attribute, OldValue, NewValue);

    // Scale the current pool with its ceiling so the FRACTION is preserved.
    //
    // The ordering that makes this necessary: GE_InitStats_Player sets Health and
    // MaxHealth to 200, then GE_EquipmentStats adds gear MaxHealth on top. Health
    // was left behind, so the player spawned at 200/235.4 and every +MaxHealth
    // item made the wearer proportionally more wounded than before equipping it.
    //
    // Guarded on OldValue > 0 because the very first assignment comes from the
    // constructor defaults (0 -> 100), where there is no meaningful ratio to keep.
    // SetHealth re-enters PreAttributeChange, which clamps to the NEW MaxHealth.
    if (OldValue <= 0.f || FMath::IsNearlyEqual(OldValue, NewValue))
    {
        return;
    }

    const float Ratio = NewValue / OldValue;

    if (Attribute == GetMaxHealthAttribute())
    {
        SetHealth(GetHealth() * Ratio);
    }
    else if (Attribute == GetMaxStaminaAttribute())
    {
        SetStamina(GetStamina() * Ratio);
    }
    else if (Attribute == GetMaxEtherAttribute())
    {
        SetEther(GetEther() * Ratio);
    }
}

void UGothicAttributeSet::OnRep_Selah(const FGameplayAttributeData& OldSelah)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGothicAttributeSet, Selah, OldSelah);
}

void UGothicAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
    Super::PostGameplayEffectExecute(Data);

    FGameplayEffectContextHandle Context    = Data.EffectSpec.GetContext();
    UAbilitySystemComponent* SourceASC     = Context.GetOriginalInstigatorAbilitySystemComponent();

    AActor* TargetActor   = nullptr;
    ACharacter* TargetChar = nullptr;

    if (Data.Target.AbilityActorInfo.IsValid() && Data.Target.AbilityActorInfo->AvatarActor.IsValid())
    {
        TargetActor = Data.Target.AbilityActorInfo->AvatarActor.Get();
        TargetChar  = Cast<ACharacter>(TargetActor);
    }

if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute())
{
    const float RawDamage = GetIncomingDamage();
    SetIncomingDamage(0.f);

    if (RawDamage > 0.f)
    {
        // Evasion is rolled here rather than at the damage source: this runs on
        // the server only (IncomingDamage is never replicated), so the roll is
        // authoritative and a predicting client cannot influence it.
        const float Evasion = GetEvasionChance();
        if (Evasion > 0.f && FMath::FRandRange(0.f, 100.f) < Evasion)
        {
            // Evaded — no health change, and deliberately no combat-state
            // notification, since nothing landed.
            return;
        }

        // AttackPower is the attacker's flat contribution, mirroring Defense as
        // the defender's flat reduction. Applied here rather than in GA_Fire so
        // it covers EVERY damage source -- enemy melee, boss abilities and the
        // pillar collapse all route through IncomingDamage, and wiring it into
        // the player's fire path alone would have made it a player-only stat.
        //
        // Read from the instigator, so a source with no ASC (the Rotunda
        // collapse volume names the pillar as its source object) contributes
        // nothing rather than inheriting the victim's own AttackPower.
        //
        // Flat, not scaling: it is deliberately the same shape as Defense so the
        // pair stays predictable to tune. That also means both matter far more
        // to a 6-damage hit than a 250-damage one -- worth remembering when
        // balancing armour against late-game weapons.
        const float AttackBonus = SourceASC
            ? SourceASC->GetNumericAttribute(GetAttackPowerAttribute())
            : 0.f;

        const float FinalDamage = FMath::Max(1.f, RawDamage + AttackBonus - GetDefense());
        const float NewHealth = FMath::Clamp(GetHealth() - FinalDamage, 0.f, GetMaxHealth());
        SetHealth(NewHealth);

        // Notify combat state on both target (received damage) and source (dealt damage)
        if (TargetActor)
        {
            if (UGothicCombatStateComponent* TargetCombatState =
                TargetActor->FindComponentByClass<UGothicCombatStateComponent>())
            {
                TargetCombatState->NotifyCombatAction();
            }

            // Feed the vital point's damage accumulator so it shifts once enough
            // damage has landed — the other half of its shift trigger alongside
            // the timer. NotifyDamageTaken had no caller before this, so the
            // damage-based shift never fired.
            if (UGothicVitalPointComponent* TargetVital =
                TargetActor->FindComponentByClass<UGothicVitalPointComponent>())
            {
                TargetVital->NotifyDamageTaken(FinalDamage);
            }
        }

        AActor* SourceActor = SourceASC ? SourceASC->GetAvatarActor() : nullptr;
        if (SourceActor)
        {
            if (UGothicCombatStateComponent* SourceCombatState =
                SourceActor->FindComponentByClass<UGothicCombatStateComponent>())
            {
                SourceCombatState->NotifyCombatAction();
            }
        }

        if (NewHealth <= 0.f && TargetChar)
        {
            AGothicCharacterBase* GothicChar = Cast<AGothicCharacterBase>(TargetChar);
            if (GothicChar)
            {
                UAbilitySystemComponent* TargetASC = GothicChar->GetAbilitySystemComponent();
                if (TargetASC && !TargetASC->HasMatchingGameplayTag(
                    FGameplayTag::RequestGameplayTag(FName("State.Dead"))))
                {
                    // GothicAttributeSet.cpp — replace the direct call
                    IGothicCombatInterface::Execute_OnDeath(GothicChar,
                        SourceASC ? SourceASC->GetAvatarActor() : nullptr);

                    // Notify the killer for Not At All / any future on-kill systems
                    if (SourceActor)
                    {
                        FGameplayEventData KillEventPayload;
                        KillEventPayload.Target = TargetActor;
                        KillEventPayload.Instigator = SourceActor;

                        UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
                            SourceActor,
                            FGameplayTag::RequestGameplayTag(FName("Event.Kill.Confirmed")),
                            KillEventPayload);
                    }
                }
            }
        }
    }
}
}  // closes PostGameplayEffectExecute

void UGothicAttributeSet::OnRep_Health(const FGameplayAttributeData& OldHealth)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGothicAttributeSet, Health, OldHealth);
}

void UGothicAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGothicAttributeSet, MaxHealth, OldMaxHealth);
}

void UGothicAttributeSet::OnRep_Stamina(const FGameplayAttributeData& OldStamina)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGothicAttributeSet, Stamina, OldStamina);
}

void UGothicAttributeSet::OnRep_MaxStamina(const FGameplayAttributeData& OldMaxStamina)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGothicAttributeSet, MaxStamina, OldMaxStamina);
}

void UGothicAttributeSet::OnRep_Ether(const FGameplayAttributeData& OldEther)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGothicAttributeSet, Ether, OldEther);
}

void UGothicAttributeSet::OnRep_MaxEther(const FGameplayAttributeData& OldMaxEther)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGothicAttributeSet, MaxEther, OldMaxEther);
}

void UGothicAttributeSet::OnRep_AttackPower(const FGameplayAttributeData& OldAttackPower)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGothicAttributeSet, AttackPower, OldAttackPower);
}

void UGothicAttributeSet::OnRep_Defense(const FGameplayAttributeData& OldDefense)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGothicAttributeSet, Defense, OldDefense);
}

void UGothicAttributeSet::OnRep_MovementSpeed(const FGameplayAttributeData& OldMovementSpeed)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGothicAttributeSet, MovementSpeed, OldMovementSpeed);
}

void UGothicAttributeSet::OnRep_EvasionChance(const FGameplayAttributeData& OldEvasionChance)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGothicAttributeSet, EvasionChance, OldEvasionChance);
}

void UGothicAttributeSet::OnRep_AbilityHaste(const FGameplayAttributeData& OldAbilityHaste)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGothicAttributeSet, AbilityHaste, OldAbilityHaste);
}

void UGothicAttributeSet::OnRep_VitalPointRadius(const FGameplayAttributeData& OldVitalPointRadius)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGothicAttributeSet, VitalPointRadius, OldVitalPointRadius);
}

void UGothicAttributeSet::OnRep_SteadfastRate(const FGameplayAttributeData& OldSteadfastRate)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGothicAttributeSet, SteadfastRate, OldSteadfastRate);
}

void UGothicAttributeSet::OnRep_HealingReceived(const FGameplayAttributeData& OldHealingReceived)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGothicAttributeSet, HealingReceived, OldHealingReceived);
}

void UGothicAttributeSet::OnRep_ReloadSpeed(const FGameplayAttributeData& OldReloadSpeed)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGothicAttributeSet, ReloadSpeed, OldReloadSpeed);
}
void UGothicAttributeSet::OnRep_SuperMeter(const FGameplayAttributeData& OldSuperMeter)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGothicAttributeSet, SuperMeter, OldSuperMeter);
}

void UGothicAttributeSet::OnRep_MaxSuperMeter(const FGameplayAttributeData& OldMaxSuperMeter)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGothicAttributeSet, MaxSuperMeter, OldMaxSuperMeter);
}

void UGothicAttributeSet::OnRep_Steadfast(const FGameplayAttributeData& OldSteadfast)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGothicAttributeSet, Steadfast, OldSteadfast);
}

void UGothicAttributeSet::OnRep_MaxSteadfast(const FGameplayAttributeData& OldMaxSteadfast)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGothicAttributeSet, MaxSteadfast, OldMaxSteadfast);
}