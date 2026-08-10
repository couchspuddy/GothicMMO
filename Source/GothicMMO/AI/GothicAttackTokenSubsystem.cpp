// GothicAttackTokenSubsystem.cpp

#include "AI/GothicAttackTokenSubsystem.h"
#include "GothicMMO.h"                       // LogVigilCombat
#include "Character/GothicCharacterBase.h"

int32 UGothicAttackTokenSubsystem::SweepStaleHolders()
{
    const int32 Before = Holders.Num();

    // A holder is stale if its weak pointer went invalid (destroyed) OR it is a
    // character that has died. Dead-but-not-yet-destroyed is the common case:
    // the pawn lingers on its CorpseLifetime timer still occupying a slot, and
    // the whole point of reclaiming here is that a corpse must not hold a permit
    // the survivors are queuing for.
    Holders.RemoveAll([](const TWeakObjectPtr<AActor>& Holder)
    {
        const AActor* Actor = Holder.Get();
        if (!Actor)
        {
            return true;
        }
        if (const AGothicCharacterBase* Character = Cast<AGothicCharacterBase>(Actor))
        {
            return !Character->IsAlive();
        }
        return false;
    });

    return Before - Holders.Num();
}

bool UGothicAttackTokenSubsystem::RequestToken(AActor* Requester)
{
    if (!Requester)
    {
        return false;
    }

    const int32 Reclaimed = SweepStaleHolders();

    // Already holding one: idempotent grant, no second slot consumed.
    if (Holders.Contains(Requester))
    {
        return true;
    }

    if (Holders.Num() >= MaxTokens)
    {
        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VigilTimeline|t=%.3f|%s|AttackToken|DENY|held=%d|max=%d|reclaimed=%d"),
            GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f,
            *GetNameSafe(Requester), Holders.Num(), MaxTokens, Reclaimed);
        return false;
    }

    Holders.Add(Requester);

    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|AttackToken|GRANT|held=%d|max=%d|reclaimed=%d"),
        GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f,
        *GetNameSafe(Requester), Holders.Num(), MaxTokens, Reclaimed);
    return true;
}

void UGothicAttackTokenSubsystem::ReleaseToken(AActor* Requester)
{
    if (!Requester)
    {
        return;
    }

    // Remove-by-key is inherently idempotent: releasing a permit the caller never
    // held removes nothing and logs nothing. Also drop any invalid entries the
    // Requester's slot sat next to while we hold the array.
    const int32 Removed = Holders.RemoveAll(
        [Requester](const TWeakObjectPtr<AActor>& Holder)
        {
            return !Holder.IsValid() || Holder.Get() == Requester;
        });

    if (Removed > 0)
    {
        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VigilTimeline|t=%.3f|%s|AttackToken|RELEASE|held=%d|max=%d"),
            GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f,
            *GetNameSafe(Requester), Holders.Num(), MaxTokens);
    }
}

bool UGothicAttackTokenSubsystem::HasToken(const AActor* Requester) const
{
    if (!Requester)
    {
        return false;
    }

    for (const TWeakObjectPtr<AActor>& Holder : Holders)
    {
        if (Holder.Get() == Requester)
        {
            return true;
        }
    }
    return false;
}
