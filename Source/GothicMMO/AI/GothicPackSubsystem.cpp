// GothicPackSubsystem.cpp

#include "AI/GothicPackSubsystem.h"
#include "AI/GothicEnemyBase.h"
#include "AI/GothicEnemyAIController.h"

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
    }

}