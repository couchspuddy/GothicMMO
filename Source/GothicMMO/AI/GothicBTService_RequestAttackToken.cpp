// GothicBTService_RequestAttackToken.cpp

#include "AI/GothicBTService_RequestAttackToken.h"
#include "GothicMMO.h"                       // LogVigilCombat
#include "AI/GothicAttackTokenSubsystem.h"
#include "Character/GothicCharacterBase.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "GameFramework/Pawn.h"

UGothicBTService_RequestAttackToken::UGothicBTService_RequestAttackToken()
{
    NodeName = TEXT("Gothic Request Attack Token");

    // ~0.2s retry cadence: a denied member re-asks 5x/second so a freed slot is
    // claimed promptly, and RandomDeviation staggers packmates off a shared
    // frame so they don't all race the same Request tick. Same knobs as
    // GothicBTService_CombatSync.
    Interval        = 0.2f;
    RandomDeviation = 0.05f;

    bNotifyCeaseRelevant = true;

    // Default the key to the name the wiring session will author, and constrain
    // the editor dropdown to Bool keys. Required (no "None") — a token gate with
    // no output key is a misconfiguration, not an opt-out.
    HasTokenKey.AddBoolFilter(this,
        GET_MEMBER_NAME_CHECKED(UGothicBTService_RequestAttackToken, HasTokenKey));
    HasTokenKey.SelectedKeyName = TEXT("HasAttackToken");
}

void UGothicBTService_RequestAttackToken::InitializeFromAsset(UBehaviorTree& Asset)
{
    Super::InitializeFromAsset(Asset);

    if (UBlackboardData* BBAsset = GetBlackboardAsset())
    {
        HasTokenKey.ResolveSelectedKey(*BBAsset);
    }
}

void UGothicBTService_RequestAttackToken::TickNode(UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory, float DeltaSeconds)
{
    Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

    AAIController* AIC = OwnerComp.GetAIOwner();
    APawn* Pawn       = AIC ? AIC->GetPawn() : nullptr;
    UGothicAttackTokenSubsystem* Tokens =
        OwnerComp.GetWorld() ? OwnerComp.GetWorld()->GetSubsystem<UGothicAttackTokenSubsystem>() : nullptr;

    if (!Pawn || !Tokens)
    {
        return;
    }

    // Owner death releases the permit immediately — the subsystem would reclaim
    // it on the next Request from a packmate anyway, but releasing here returns
    // the slot the same tick the enemy dies rather than waiting on someone else
    // to ask. Also mirror false into the key so any observer aborts cleanly.
    if (const AGothicCharacterBase* Character = Cast<AGothicCharacterBase>(Pawn))
    {
        if (!Character->IsAlive())
        {
            Tokens->ReleaseToken(Pawn);
            WriteToken(OwnerComp, false);
            return;
        }
    }

    // The request self-heals a denied member: Interval re-asks until a slot frees.
    const bool bHasToken = Tokens->RequestToken(Pawn);
    WriteToken(OwnerComp, bHasToken);
}

void UGothicBTService_RequestAttackToken::OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory)
{
    Super::OnCeaseRelevant(OwnerComp, NodeMemory);

    // Left the attack branch (out of range, staggered, aborted, tree stopped) —
    // give the slot back so a waiting packmate can take it. This is the primary
    // release path; the dead-owner branch in TickNode covers the case where the
    // tree keeps ticking a corpse.
    AAIController* AIC = OwnerComp.GetAIOwner();
    APawn* Pawn       = AIC ? AIC->GetPawn() : nullptr;
    UGothicAttackTokenSubsystem* Tokens =
        OwnerComp.GetWorld() ? OwnerComp.GetWorld()->GetSubsystem<UGothicAttackTokenSubsystem>() : nullptr;

    if (Pawn && Tokens)
    {
        Tokens->ReleaseToken(Pawn);
    }
    WriteToken(OwnerComp, false);
}

void UGothicBTService_RequestAttackToken::WriteToken(UBehaviorTreeComponent& OwnerComp,
    bool bHasToken) const
{
    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    if (!BB || HasTokenKey.IsNone())
    {
        return;
    }

    const bool bPrev = BB->GetValue<UBlackboardKeyType_Bool>(HasTokenKey.GetSelectedKeyID());
    BB->SetValue<UBlackboardKeyType_Bool>(HasTokenKey.GetSelectedKeyID(), bHasToken);

    if (bPrev != bHasToken)
    {
        const APawn* Pawn = OwnerComp.GetAIOwner() ? OwnerComp.GetAIOwner()->GetPawn() : nullptr;
        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VigilTimeline|t=%.3f|%s|AttackToken|KEY|%s|%d->%d"),
            OwnerComp.GetWorld() ? OwnerComp.GetWorld()->GetTimeSeconds() : 0.f,
            *GetNameSafe(Pawn), *HasTokenKey.SelectedKeyName.ToString(),
            bPrev ? 1 : 0, bHasToken ? 1 : 0);
    }
}

FString UGothicBTService_RequestAttackToken::GetStaticDescription() const
{
    return FString::Printf(TEXT("%s\nRequests an attack token → %s"),
        *Super::GetStaticDescription(),
        HasTokenKey.IsNone() ? TEXT("(no key set)") : *HasTokenKey.SelectedKeyName.ToString());
}
