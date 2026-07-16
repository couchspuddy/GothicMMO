// GothicAbilitySet.cpp

#include "AbilitySystem/GothicAbilitySet.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "GameplayEffect.h"

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

        if (!Entry.bActivateOnGranted && !Entry.InputTag.IsValid())
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
        
        if (Entry.bActivateOnGranted)   // <-- new
        {
            const bool bActivated = ASC->TryActivateAbility(Handle);
            UE_LOG(LogTemp, Log, TEXT("GothicAbilitySet: %s activate-on-grant %s"),
                *Entry.AbilityClass->GetName(), bActivated ? TEXT("succeeded") : TEXT("FAILED"));
        }
    }

    // Apply granted effects
    for (const TSubclassOf<UGameplayEffect>& EffectClass : GrantedEffects)
    {
        if (!EffectClass) continue;

        const UGameplayEffect* EffectCDO = EffectClass->GetDefaultObject<UGameplayEffect>();
        if (!EffectCDO)
        {
            continue;
        }

        // ---- idempotency guard ----
        // The player's ASC lives on the PlayerState and survives death, but the
        // pawn does not — so a respawned pawn runs this grant path again against
        // an ASC that is already carrying everything from last life. Without this,
        // every death stacks another copy of every effect in the set.
        //
        // Ask the ASC what it is actually carrying rather than asking a pawn what
        // it remembers doing. A pawn-side bool cannot answer this correctly,
        // because the pawn is the thing being replaced.
        //
        // LIMITATION: this only works for Duration/Infinite effects. Instant
        // effects execute and are never added to ActiveGameplayEffects, so there
        // is nothing to query — see the warning below.
        if (EffectCDO->DurationPolicy == EGameplayEffectDurationType::Instant)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("GothicAbilitySet: %s in %s's GrantedEffects is Instant — CANNOT be guarded against ")
                TEXT("double-apply and WILL re-apply on every respawn. GrantedEffects is for persistent ")
                TEXT("passives (Infinite); an Instant effect belongs in an ability's activation path instead."),
                *EffectClass->GetName(), *GetName());
        }
        else
        {
            FGameplayEffectQuery Query;
            Query.EffectDefinition = EffectClass;

            if (ASC->GetActiveEffects(Query).Num() > 0)
            {
                UE_LOG(LogTemp, Log, TEXT("GothicAbilitySet: %s already active — skipping duplicate apply"),
                    *EffectClass->GetName());
                continue;
            }
        }
        // ---- END guard ----

        FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
        Context.AddSourceObject(SourceObject);

        FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(EffectClass, 1.f, Context);

        if (SpecHandle.IsValid())
        {
            ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
            UE_LOG(LogTemp, Log, TEXT("GothicAbilitySet: Applied effect %s"), *EffectClass->GetName());
        }
    }
}