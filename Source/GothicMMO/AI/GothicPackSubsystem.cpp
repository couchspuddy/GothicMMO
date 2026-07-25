// GothicPackSubsystem.cpp

#include "AI/GothicPackSubsystem.h"
#include "AI/GothicEnemyBase.h"
#include "AI/GothicEnemyAIController.h"
#include "AbilitySystemComponent.h"

namespace
{
    /** AssetTag on BP_GA_PackGuard — the guard pose played when a packmate falls.
     *  Declared in DefaultGameplayTags.ini, matched against the ability's
     *  AssetTags (NOT AbilityInputTag), same convention as every other
     *  tag-activated enemy ability in the project. */
    const FName PackGuardAbilityTagName(TEXT("Ability.Enemy.PackGuard"));
}

void UGothicPackSubsystem::RegisterMember(FName PackID, AGothicEnemyBase* Member)
{
    if (PackID.IsNone() || !Member)
    {
        return;
    }

    TArray<TWeakObjectPtr<AGothicEnemyBase>>& Members = Packs.FindOrAdd(PackID);

    // Redundant registration is a no-op, not a duplicate — SetPackID can be
    // called from both BeginPlay (serialized value) and a spawn-point stamp.
    if (!Members.Contains(Member))
    {
        Members.Add(Member);
    }
}

void UGothicPackSubsystem::UnregisterMember(FName PackID, AGothicEnemyBase* Member)
{
    if (PackID.IsNone() || !Member)
    {
        return;
    }

    if (TArray<TWeakObjectPtr<AGothicEnemyBase>>* Members = Packs.Find(PackID))
    {
        Members->Remove(Member);

        // Sweep stale weak ptrs while we're here — cheap, and keeps the
        // notify loop from iterating corpses that EndPlay never reached.
        Members->RemoveAll([](const TWeakObjectPtr<AGothicEnemyBase>& M)
        {
            return !M.IsValid();
        });

        if (Members->Num() == 0)
        {
            Packs.Remove(PackID);
            PackRegroupCooldownUntil.Remove(PackID);
        }
    }
}

void UGothicPackSubsystem::NotifyMemberDeath(FName PackID, AGothicEnemyBase* DeadMember)
{
    if (PackID.IsNone())
    {
        return;
    }

    UnregisterMember(PackID, DeadMember);

    TArray<TWeakObjectPtr<AGothicEnemyBase>>* Members = Packs.Find(PackID);
    if (!Members || Members->Num() == 0)
    {
        return;
    }

    // Retrigger lockout — see the design comment on PackRegroupCooldownUntil
    // in the header. Fires once per window; a second death inside the window
    // is absorbed silently.
    const UWorld* World = GetWorld();
    const float Now = World ? World->GetTimeSeconds() : 0.f;

    if (const float* LockedUntil = PackRegroupCooldownUntil.Find(PackID))
    {
        if (Now < *LockedUntil)
        {
            return;
        }
    }
    PackRegroupCooldownUntil.Add(PackID, Now + RegroupCooldown);

    int32 Notified = 0;
    for (const TWeakObjectPtr<AGothicEnemyBase>& WeakMember : *Members)
    {
        AGothicEnemyBase* Member = WeakMember.Get();
        if (!Member || !Member->IsAlive())
        {
            continue;
        }

        if (AGothicEnemyAIController* AIC =
            Cast<AGothicEnemyAIController>(Member->GetController()))
        {
            AIC->EnterRegroupPause(Member->GetPackRegroupDuration());
            Notified++;
        }

        // The visible half of the reaction. EnterRegroupPause only stops the
        // behaviour tree — on its own the pack freezes for PackRegroupDuration
        // with no animation, which reads as the AI hitching rather than
        // flinching. Activating the guard ability here is what makes the
        // synchronized reaction legible, which is the entire point of this
        // subsystem (see the header note).
        //
        // Failure is silent and harmless: an enemy without GA_PackGuard granted,
        // or one already playing it, simply doesn't start a new montage.
        if (UAbilitySystemComponent* ASC = Member->GetAbilitySystemComponent())
        {
            const FGameplayTag PackGuardTag =
                FGameplayTag::RequestGameplayTag(PackGuardAbilityTagName, /*ErrorIfNotFound=*/false);

            if (PackGuardTag.IsValid())
            {
                ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(PackGuardTag));
            }
        }
    }

    UE_LOG(LogTemp, Verbose, TEXT("PackSubsystem: %s fell — %d packmate(s) regrouped"),
        *GetNameSafe(DeadMember), Notified);
}