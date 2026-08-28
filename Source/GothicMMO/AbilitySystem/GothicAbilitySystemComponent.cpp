// GothicAbilitySystemComponent.cpp

#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicGameplayAbility.h"
#include "AbilitySystem/GothicGameplayTags.h"   // reactive action-tag windows
#include "Character/GothicPlayerCharacter.h"  // CancelSprintForAbility — the sprint opportunity cost
#include "Game/GothicPlayerState.h"          // IsDowned — the input gate in AbilityInputTagPressed
#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayEffect.h"
#include "GothicMMO.h"                      // LogVigilCombat
#include "CoreGlobals.h"                    // GAllowActorScriptExecutionInEditor
#include "Engine/World.h"
#include "TimerManager.h"

UGothicAbilitySystemComponent::UGothicAbilitySystemComponent()
{
    SetIsReplicated(true);
    ReplicationMode = EGameplayEffectReplicationMode::Full;
}

void UGothicAbilitySystemComponent::ApplyTimedLooseTag(const FGameplayTag& Tag, float Duration)
{
    UWorld* World = GetWorld();
    if (!World || !Tag.IsValid() || Duration <= 0.f)
    {
        return;
    }

    FTimerManager& TimerManager = World->GetTimerManager();
    FTimerHandle& Handle = TimedLooseTagHandles.FindOrAdd(Tag);

    // Take the window's single count only when no window is already open. A re-apply
    // while the timer is still live falls through to the SetTimer below and simply
    // extends it — the count stays at 1 (plus any independent ActivationOwnedTags
    // count, which this never touches).
    if (!TimerManager.IsTimerActive(Handle))
    {
        AddLooseGameplayTag(Tag);
    }

    FTimerDelegate Del = FTimerDelegate::CreateUObject(
        this, &UGothicAbilitySystemComponent::HandleTimedLooseTagExpired, Tag);
    TimerManager.SetTimer(Handle, Del, Duration, /*bLoop=*/false);

    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|ASC=%s|Event=TimedTagOpen|tag=%s|dur=%.2f|count=%d"),
        *GetNameSafe(this), *Tag.ToString(), Duration, GetTagCount(Tag));
}

void UGothicAbilitySystemComponent::HandleTimedLooseTagExpired(FGameplayTag Tag)
{
    // Remove exactly the one count this window took. A montage path holding the same
    // tag through ActivationOwnedTags keeps its own count — this -1 pays off only the
    // window's +1.
    RemoveLooseGameplayTag(Tag);
    TimedLooseTagHandles.Remove(Tag);

    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|ASC=%s|Event=TimedTagExpire|tag=%s|count=%d"),
        *GetNameSafe(this), *Tag.ToString(), GetTagCount(Tag));
}

void UGothicAbilitySystemComponent::ClearTimedLooseTags()
{
    UWorld* World = GetWorld();
    for (TPair<FGameplayTag, FTimerHandle>& Pair : TimedLooseTagHandles)
    {
        if (World)
        {
            World->GetTimerManager().ClearTimer(Pair.Value);
        }
        // Pay off the window's count. Absolute zero (not a single Remove) so a tag
        // somehow left with a count >1 is fully cleared — belt and braces to match
        // the fresh-pawn sweep's State.Dead/State.Reloading idiom.
        SetLooseGameplayTagCount(Pair.Key, 0);
    }
    TimedLooseTagHandles.Reset();
}

void UGothicAbilitySystemComponent::GrantStartupAbilities(
    const TArray<TSubclassOf<UGothicGameplayAbility>>& AbilitiesToGrant,
    int32 Level)
{
    if (!GetOwnerActor() || !GetOwnerActor()->HasAuthority())
    {
        return;
    }


    for (TSubclassOf<UGothicGameplayAbility> AbilityClass : AbilitiesToGrant)
    {
        if (!AbilityClass)
        {
            continue;
        }

        FGameplayAbilitySpec Spec(AbilityClass, Level);
        const UGothicGameplayAbility* AbilityCDO = AbilityClass.GetDefaultObject();
        Spec.GetDynamicSpecSourceTags().AddTag(AbilityCDO->GetAbilityInputTag());

        FGameplayAbilitySpecHandle Handle = GiveAbility(Spec);
        SlotToAbilityMap.Add(AbilityCDO->GetAbilitySlot(), Handle);

    }

}

void UGothicAbilitySystemComponent::OnRep_ActivateAbilities()
{
    Super::OnRep_ActivateAbilities();

    // The client half of GrantStartupAbilities. See the header for why the map
    // is empty here in the first place.
    SlotToAbilityMap.Empty();

    for (const FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
    {
        const UGothicGameplayAbility* AbilityCDO = Cast<UGothicGameplayAbility>(Spec.Ability);
        if (!AbilityCDO)
        {
            continue;
        }

        SlotToAbilityMap.Add(AbilityCDO->GetAbilitySlot(), Spec.Handle);
    }
}

bool UGothicAbilitySystemComponent::TryActivateAbilityBySlot(EGothicAbilitySlot Slot)
{
    // Editor-script tripwire. Editor Python runs inside FEditorScriptExecutionGuard,
    // which sets GAllowActorScriptExecutionInEditor, and AActor::GetFunctionCallspace
    // returns Local for EVERY RPC while that flag is true -- before any net-role test.
    // So a LocalPredicted activation driven from a script against a non-authoritative
    // ASC calls ServerTryActivateAbility, that RPC executes IN PROCESS, re-enters
    // InternalTryActivateAbility still non-authoritative, and recurses unboundedly
    // inside one frame: a dependent prediction key per lap, ~200MB/min of log, then a
    // stack overflow that takes the editor with it.
    //
    // Only the harness can reach this. In a real client the flag is false and the RPC
    // routes over the wire as normal, so this refusal cannot fire in shipped play.
    // Drive the SERVER world's pawn for this player instead.
    if (!IsOwnerActorAuthoritative() && GAllowActorScriptExecutionInEditor)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("TryActivateAbilityBySlot: refused non-authoritative activation from "
                 "an editor-script context (would recurse; activate via the server-world "
                 "pawn instead)"));
        return false;
    }

    if (const FGameplayAbilitySpecHandle* Handle = SlotToAbilityMap.Find(Slot))
    {
        return TryActivateAbility(*Handle);
    }

    return false;
}

bool UGothicAbilitySystemComponent::IsSlotAbilityLocallyPredicted(EGothicAbilitySlot Slot) const
{
    if (const FGameplayAbilitySpecHandle* Handle = SlotToAbilityMap.Find(Slot))
    {
        if (const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(*Handle))
        {
            if (Spec->Ability)
            {
                const EGameplayAbilityNetExecutionPolicy::Type Policy =
                    Spec->Ability->GetNetExecutionPolicy();

                return Policy == EGameplayAbilityNetExecutionPolicy::LocalPredicted
                    || Policy == EGameplayAbilityNetExecutionPolicy::LocalOnly;
            }
        }
    }

    return false;
}

void UGothicAbilitySystemComponent::DevDeferredTryActivateAbilityBySlot(EGothicAbilitySlot Slot)
{
#if WITH_EDITOR
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("DevDeferredTryActivateAbilityBySlot: no world to schedule on"));
        return;
    }

    // Next tick, not now. By the time this runs the caller's
    // FEditorScriptExecutionGuard has left scope, GAllowActorScriptExecutionInEditor
    // is false again, and GetFunctionCallspace answers by net role instead of
    // forcing Local -- so a LocalPredicted activation on a client-world ASC sends a
    // real server RPC rather than re-entering in process. See the header.
    World->GetTimerManager().SetTimerForNextTick(
        FTimerDelegate::CreateWeakLambda(this, [this, Slot]()
        {
            const bool bActivated = TryActivateAbilityBySlot(Slot);

            // The caller is a frame gone, so the log line is the only result.
            UE_LOG(LogVigilCombat, Log,
                TEXT("DevDeferredTryActivateAbilityBySlot: slot %d activated=%s"),
                static_cast<int32>(Slot),
                bActivated ? TEXT("true") : TEXT("false"));
        }));
#else
    (void)Slot;
#endif
}

float UGothicAbilitySystemComponent::GetCooldownRemainingForSlot(EGothicAbilitySlot Slot) const
{
    if (const FGameplayAbilitySpecHandle* Handle = SlotToAbilityMap.Find(Slot))
    {
        if (const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(*Handle))
        {
            const UGothicGameplayAbility* AbilityCDO = Cast<UGothicGameplayAbility>(Spec->Ability);
            if (AbilityCDO)
            {
                // Get cooldown tags from the CDO safely — GetCooldownTags() is safe on CDO
                const FGameplayTagContainer* CooldownTags = AbilityCDO->GetCooldownTags();
                if (CooldownTags && CooldownTags->Num() > 0)
                {
                    // Query active effects on THIS ASC directly — no CDO instance methods
                    FGameplayEffectQuery Query = 
                        FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(*CooldownTags);
                    
                    TArray<float> Durations = GetActiveEffectsTimeRemaining(Query);
                    
                    if (Durations.Num() > 0)
                    {
                        float MaxDuration = 0.f;
                        for (float D : Durations)
                        {
                            MaxDuration = FMath::Max(MaxDuration, D);
                        }
                        return MaxDuration;
                    }
                }
            }
        }
    }
    return 0.f;
}

float UGothicAbilitySystemComponent::GetCooldownTotalForSlot(EGothicAbilitySlot Slot) const
{
    // Read the designed duration from the cooldown GE's CDO — not from the
    // active effect instance. The old implementation queried
    // GetActiveEffectsDuration, which only returns a value while the cooldown
    // is running. Every frame the ability is off cooldown, Total arrived as
    // 0.f, producing a divide-by-zero in the HUD's Remaining/Total math.
    //
    // The total duration is a property of the design, not the runtime state.
    // GetCooldownGameplayEffect() returns the CDO of whatever GE class is set
    // in the Blueprint's CooldownGameplayEffectClass field.
    //
    // That fix was only half of one. GetStaticMagnitudeIfPossible fails for a
    // SetByCaller duration, and the ability that matters most here — GA_Fire,
    // whose interval is the equipped weapon's fire rate, further modified by
    // AbilityHaste — is exactly that case. It fell through to the active-effect
    // query, which returns nothing while the ability is READY, so Total came
    // back 0.f every frame Fire was off cooldown: the same wrong answer the
    // comment above claims to have retired.
    //
    // So the query result is now cached. While a cooldown runs we learn its full
    // duration and remember it; once it ends we answer from the cache instead of
    // zero. First-ever call before the ability has been used still returns 0 —
    // unavoidable without asking the ability to compute its own duration, which
    // would mean a virtual on UGothicGameplayAbility and an override in GA_Fire.

    if (const FGameplayAbilitySpecHandle* Handle = SlotToAbilityMap.Find(Slot))
    {
        if (const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(*Handle))
        {
            const UGothicGameplayAbility* AbilityCDO = Cast<UGothicGameplayAbility>(Spec->Ability);
            if (AbilityCDO)
            {
                const UGameplayEffect* CooldownGE = AbilityCDO->GetCooldownGameplayEffect();
                if (CooldownGE)
                {
                    float DesignedDuration = 0.f;
                    if (CooldownGE->DurationMagnitude.GetStaticMagnitudeIfPossible(
                            1.f, DesignedDuration))
                    {
                        return DesignedDuration;
                    }

                    // If the magnitude isn't a static float (SetByCaller, curve,
                    // custom calc), fall back to querying the active instance —
                    // better than zero, and only abilities with non-static
                    // cooldowns would reach this path.
                    const FGameplayTagContainer* CooldownTags = AbilityCDO->GetCooldownTags();
                    if (CooldownTags && CooldownTags->Num() > 0)
                    {
                        FGameplayEffectQuery Query =
                            FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(*CooldownTags);
                        TArray<float> Durations = GetActiveEffectsDuration(Query);
                        if (Durations.Num() > 0)
                        {
                            float MaxDuration = 0.f;
                            for (float D : Durations)
                            {
                                MaxDuration = FMath::Max(MaxDuration, D);
                            }

                            // Learn it while we can see it.
                            LastObservedCooldownTotals.Add(Slot, MaxDuration);
                            return MaxDuration;
                        }

                        // Off cooldown: nothing to query. Answer with the last
                        // duration this slot actually ran, if we ever saw one.
                        if (const float* Cached = LastObservedCooldownTotals.Find(Slot))
                        {
                            return *Cached;
                        }
                    }
                }
            }
        }
    }
    return 0.f;
}

void UGothicAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
{
    // Sprinting is an opportunity cost, and this is where it is paid.
    //
    // Every non-gun ability — Slicer, Read, Lunge, Reckoning, melee, and anything
    // granted later — breaks the sprint on its way in and then activates normally.
    // Primary Fire is the one exception: it is BLOCKED by the sprint rather than
    // cancelling it, so letting it through here would turn a dead trigger into
    // cancel-and-shoot.
    //
    // Done at this choke point rather than per-ability, so a new kit inherits the
    // rule by existing. The slot comes off the ability CDO rather than the input
    // tag, because input tags are authored in a data asset and nothing in C++ can
    // spell-check one.
    //
    // Deliberately ahead of the scope lock: this ends up in
    // SetLooseGameplayTagCount, and a tag change can run delegates that touch the
    // ability list. Nothing here mutates the list itself, so there is nothing the
    // lock is protecting.
    CancelSprintForNonGunInput(InputTag);

    // A downed player presses nothing. EnterDownedState cancels what was running,
    // but nothing stopped the next press from starting it up again — and a downed
    // player who can still fire is not downed in any sense the design means.
    //
    // Gated here rather than by putting State.Downed in every ability's
    // ActivationBlockedTags for two reasons: the tag is a SERVER-SIDE loose tag
    // (loose tags never replicate), so a locally-predicted activation on the
    // client would sail straight past it, whereas the bool below is the
    // replicated flag and reads true on every machine; and this way a kit added
    // later inherits the rule by existing, the same argument the sprint cancel
    // above is placed here for.
    //
    // Input only. The passives are activated by the ability-set grant, not by
    // input, so ReviveFromDowned's re-grant is untouched by this.
    if (const AGothicPlayerState* GothicPS = Cast<AGothicPlayerState>(GetOwnerActor()))
    {
        if (GothicPS->IsDowned())
        {
            return;
        }
    }

    ABILITYLIST_SCOPE_LOCK();
    for (FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
    {
        // UE5.8: Use GetDynamicSpecSourceTags()
        if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
        {
            Spec.InputPressed = true;
            if (Spec.IsActive())
            {
                AbilitySpecInputPressed(Spec);
            }
            else
            {
                TryActivateAbility(Spec.Handle);
            }
        }
    }
}

void UGothicAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag& InputTag)
{
    ABILITYLIST_SCOPE_LOCK();
    for (FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
    {
        // UE5.8: Use GetDynamicSpecSourceTags()
        if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
        {
            Spec.InputPressed = false;
            if (Spec.IsActive())
            {
                AbilitySpecInputReleased(Spec);
            }
        }
    }
}

void UGothicAbilitySystemComponent::CancelSprintForNonGunInput(const FGameplayTag& InputTag)
{
    AGothicPlayerCharacter* PlayerChar = Cast<AGothicPlayerCharacter>(GetAvatarActor());
    if (!PlayerChar)
    {
        return; // enemies share this class and never route input through here
    }

    for (const FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
    {
        if (!Spec.Ability || !Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
        {
            continue;
        }

        const UGothicGameplayAbility* GothicAbility = Cast<UGothicGameplayAbility>(Spec.Ability);

        // An ability that is not a UGothicGameplayAbility has no slot to read, and
        // the safe reading of "not the gun" is that it cancels — the alternative
        // would silently exempt it from the whole rule.
        if (!GothicAbility || GothicAbility->GetAbilitySlot() != EGothicAbilitySlot::PrimaryFire)
        {
            PlayerChar->CancelSprintForAbility();
            return;
        }
    }
}

void UGothicAbilitySystemComponent::RegisterAbilitySlot(
    EGothicAbilitySlot Slot,
    FGameplayAbilitySpecHandle Handle)
{
    SlotToAbilityMap.Add(Slot, Handle);
}

FGameplayEffectContextHandle UGothicAbilitySystemComponent::MakeDamageContext(
    UAbilitySystemComponent* SourceASC,
    AActor* SourceAvatar)
{
    if (!SourceASC)
    {
        return FGameplayEffectContextHandle();
    }

    FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();

    if (SourceAvatar)
    {
        Context.AddSourceObject(SourceAvatar);

        // Both slots are the avatar: instigator drives AttackPower and the
        // Killer attribution, effect causer drives hit feedback. See the header.
        Context.AddInstigator(SourceAvatar, SourceAvatar);
    }

    return Context;
}

void UGothicAbilitySystemComponent::ApplyEffectToASC(
    UAbilitySystemComponent* TargetASC,
    TSubclassOf<UGameplayEffect> EffectClass,
    AActor* SourceActor)
{
    if (!TargetASC || !EffectClass) return;

    // The context has to be built from the SOURCE. This used to call
    // TargetASC->MakeEffectContext() and apply to self, which named the VICTIM as
    // its own instigator: any GE routed through here that reads source AttackPower
    // or attribution would have read the target's. Inert only because the one
    // caller's GE (GE_Stun_Shock) has no modifiers.
    UAbilitySystemComponent* SourceASC =
        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(SourceActor);

    if (SourceASC)
    {
        FGameplayEffectContextHandle Context = MakeDamageContext(SourceASC, SourceActor);
        FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(EffectClass, 1.f, Context);
        if (Spec.IsValid())
        {
            SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
        }
        return;
    }

    // No source ASC to name — every caller today passes a real pawn avatar
    // (GA_Fire, GA_BestialLucidRoar, GA_BestialLucidCry, GA_FeralBreakout), so
    // this is the "environment applied it" path rather than a normal one. Fall
    // back to the self-application shape, but say so rather than pretending the
    // target instigated it.
    UE_LOG(LogTemp, Warning,
        TEXT("ApplyEffectToASC: no ASC on source %s — applying %s with no instigator"),
        SourceActor ? *SourceActor->GetName() : TEXT("null"),
        *EffectClass->GetName());

    FGameplayEffectContextHandle Context = TargetASC->MakeEffectContext();
    Context.AddSourceObject(SourceActor);

    FGameplayEffectSpecHandle Spec = TargetASC->MakeOutgoingSpec(EffectClass, 1.f, Context);
    if (Spec.IsValid())
    {
        TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    }
}