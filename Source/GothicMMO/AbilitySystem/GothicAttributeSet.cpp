// GothicAttributeSet.cpp

#include "AbilitySystem/GothicAttributeSet.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"
#include "GameFramework/Character.h"
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
            
            GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Cyan,
    FString::Printf(TEXT("%s Health: %.1f / %.1f"),
    *GetOwningActor()->GetName(),
    NewHealth,
    GetMaxHealth()));

            if (NewHealth <= 0.f && TargetChar)
            {
                // Cast to our base class and call OnDeath directly
                AGothicCharacterBase* GothicChar = Cast<AGothicCharacterBase>(TargetChar);
                if (GothicChar)
                {
                    GothicChar->OnDeath_Implementation(SourceASC ? SourceASC->GetAvatarActor() : nullptr);
                }
            }
        }
    }
}

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
