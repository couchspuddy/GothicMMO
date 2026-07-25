// GothicAttributeSet.cpp

#include "AbilitySystem/GothicAttributeSet.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"
#include "GameFramework/Character.h"
#include "AI/GothicCombatStateComponent.h"
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
        UE_LOG(LogTemp, Log, TEXT("SuperMeter PreAttributeChange: NewValue=%.1f MaxSuperMeter=%.1f"), 
            NewValue, GetMaxSuperMeter());
        NewValue = FMath::Clamp(NewValue, 0.f, GetMaxSuperMeter());
    }
    else if (Attribute == GetSelahAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
    }
    else if (Attribute == GetSteadfastAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, GetMaxSteadfast());
    }
    else if (Attribute == GetMaxSteadfastAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
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
        const float FinalDamage = FMath::Max(1.f, RawDamage - GetDefense());
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