// GothicAbilitySet.cpp

#include "AbilitySystem/GothicAbilitySet.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicGameplayAbility.h"

void UGothicAbilitySet::GiveToAbilitySystem(
    UGothicAbilitySystemComponent* ASC,
    UObject* SourceObject) const
{
    if (!ASC)
    {
        UE_LOG(LogTemp, Error, TEXT("GothicAbilitySet: GiveToAbilitySystem called with null ASC"));
        return;
    }

    if (!ASC->IsOwnerActorAuthoritative())
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicAbilitySet: GiveToAbilitySystem called without authority — skipping"));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("GothicAbilitySet: Granting %d abilities to %s"),
        GrantedAbilities.Num(),
        ASC->GetOwnerActor() ? *ASC->GetOwnerActor()->GetName() : TEXT("Unknown"));
    
    

    // Grant abilities
    for (const FGothicAbilitySetEntry& Entry : GrantedAbilities)
    {
        if (!Entry.AbilityClass)
        {
            UE_LOG(LogTemp, Warning, TEXT("GothicAbilitySet: Null ability class in set — skipping entry"));
            continue;
        }

        if (!Entry.InputTag.IsValid())
        {
            UE_LOG(LogTemp, Warning, TEXT("GothicAbilitySet: Invalid input tag for ability %s — skipping"),
                *Entry.AbilityClass->GetName());
            continue;
        }

        // ---- ADD: idempotency guard ----
        // If this ASC already has a spec of this ability class, don't grant a
        // second one. The player init path runs twice (PossessedBy +
        // OnRep_PlayerState), so without this a single input activates two
        // identical specs.
        bool bAlreadyGranted = false;
        for (const FGameplayAbilitySpec& Existing : ASC->GetActivatableAbilities())
        {
            if (Existing.Ability && Existing.Ability->GetClass() == Entry.AbilityClass)
            {
                bAlreadyGranted = true;
                break;
            }
        }

        if (bAlreadyGranted)
        {
            UE_LOG(LogTemp, Log, TEXT("GothicAbilitySet: %s already granted — skipping duplicate"),
                *Entry.AbilityClass->GetName());
            continue;
        }
        // ---- END guard ----

        // Build the spec — level comes from the data asset entry
        FGameplayAbilitySpec Spec(Entry.AbilityClass, Entry.AbilityLevel);

        // Store the input tag on the spec's dynamic tags
        // This is what AbilityInputTagPressed searches for
        Spec.GetDynamicSpecSourceTags().AddTag(Entry.InputTag);

        // Grant to the ASC
        FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(Spec);

        // Register in the slot map for cooldown polling and HUD
        ASC->RegisterAbilitySlot(Entry.AbilitySlot, Handle);

        UE_LOG(LogTemp, Log, TEXT("GothicAbilitySet: Granted %s | Tag: %s | Slot: %d"),
            *Entry.AbilityClass->GetName(),
            *Entry.InputTag.ToString(),
            (int32)Entry.AbilitySlot);
    }

    // Apply granted effects
    for (const TSubclassOf<UGameplayEffect>& EffectClass : GrantedEffects)
    {
        if (!EffectClass) continue;

        FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
        Context.AddSourceObject(SourceObject);

        FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(
            EffectClass, 1.f, Context);

        if (SpecHandle.IsValid())
        {
            ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
            UE_LOG(LogTemp, Log, TEXT("GothicAbilitySet: Applied effect %s"),
                *EffectClass->GetName());
        }
    }

    UE_LOG(LogTemp, Log, TEXT("GothicAbilitySet: Grant complete"));
}