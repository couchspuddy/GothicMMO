// GothicHintTrigger.cpp

#include "Game/GothicHintTrigger.h"

#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "Character/GothicPlayerCharacter.h"
#include "GothicMMO.h"
#include "UI/GothicHintManagerComponent.h"

#include "Components/BoxComponent.h"
#include "GameFramework/PlayerState.h"

AGothicHintTrigger::AGothicHintTrigger()
{
    PrimaryActorTick.bCanEverTick = false;

    // Replicated so the server-side SuperMeter fill has an actor to run on in a
    // listen-server session. The trigger itself carries no replicated state —
    // LatchedPlayers is per-machine on purpose, because the hint half must latch
    // on the client that draws it and the fill half on the authority.
    bReplicates = true;

    TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
    SetRootComponent(TriggerBox);

    TriggerBox->SetBoxExtent(FVector(200.f, 200.f, 200.f));

    // Overlap only, and only against pawns. Deliberately NOT the ArenaBlock
    // pattern AGothicBleedGate uses — that channel exists to STOP the player,
    // and a tutorial hint that body-blocks the person it is trying to teach is
    // the opposite of the intent. Query-only, blocking nothing.
    TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    TriggerBox->SetCollisionObjectType(ECC_WorldStatic);
    TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
    TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    TriggerBox->SetGenerateOverlapEvents(true);
    TriggerBox->SetHiddenInGame(true);
    TriggerBox->ShapeColor = FColor(60, 160, 220);

    // Never an obstacle for pathfinding — the Accursed must not route around a
    // volume that exists only to show the player a line of text.
    TriggerBox->SetCanEverAffectNavigation(false);
}

void AGothicHintTrigger::BeginPlay()
{
    Super::BeginPlay();

    TriggerBox->OnComponentBeginOverlap.AddDynamic(
        this, &AGothicHintTrigger::OnTriggerBeginOverlap);
}

void AGothicHintTrigger::OnTriggerBeginOverlap(
    UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor,
    UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/,
    bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
    AGothicPlayerCharacter* Player = Cast<AGothicPlayerCharacter>(OtherActor);
    if (!Player)
    {
        return;
    }

    // Per-player latch. The key is resolved once and reused for the test and the
    // add, so a player whose PlayerState arrives between the two cannot latch
    // under one key and be tested under the other.
    AActor* LatchKey = LatchKeyFor(Player);
    if (bTriggerOnce)
    {
        if (LatchedPlayers.Contains(LatchKey))
        {
            return;
        }
        LatchedPlayers.Add(LatchKey);
    }

    // Server half. Runs before the hint so that on a listen-server host — where
    // both halves run on the same machine — the meter is already full when the
    // SuperMeter delegate raises Hint.Reckoning off the back of it.
    if (bFillSuperMeterOnOverlap && HasAuthority())
    {
        FillSuperMeter(Player);
    }

    // Client half. ShowHint is a no-op on a pawn this machine does not control,
    // so this is safe to call unconditionally.
    if (HintTag.IsValid())
    {
        if (UGothicHintManagerComponent* Hints = Player->GetHintManager())
        {
            Hints->ShowHint(HintTag);
        }
    }
}

AActor* AGothicHintTrigger::LatchKeyFor(AGothicPlayerCharacter* Player)
{
    // The PlayerState is the identity that outlives the pawn, so latching on it
    // is what makes a gate stay consumed across a death — the same reasoning that
    // put SeenTutorialHints on the PlayerState rather than on the hint manager.
    //
    // Falls back to the pawn because the PlayerState is not guaranteed resolved
    // at the moment of an overlap: these gates sit on the Palewood spawn line and
    // fire at t=0, before possession has necessarily replicated. A pawn-keyed
    // latch is still a per-PLAYER latch — it just does not survive that player's
    // respawn, and both paths behind it tolerate a second run (the hint manager's
    // seen-set swallows the repeat, and the fill writes the same ceiling twice).
    APlayerState* PS = Player->GetPlayerState();
    return PS ? static_cast<AActor*>(PS) : static_cast<AActor*>(Player);
}

void AGothicHintTrigger::FillSuperMeter(AGothicPlayerCharacter* Player)
{
    UGothicAbilitySystemComponent* ASC =
        Cast<UGothicAbilitySystemComponent>(Player->GetAbilitySystemComponent());

    UGothicAttributeSet* Attributes = ASC
        ? const_cast<UGothicAttributeSet*>(ASC->GetSet<UGothicAttributeSet>())
        : nullptr;

    if (!Attributes)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("HintTrigger|%s|SuperMeter fill refused — no attribute set on %s"),
            *GetName(), *GetNameSafe(Player));
        return;
    }

    const float MaxSuper = Attributes->GetMaxSuperMeter();
    if (MaxSuper <= 0.f)
    {
        // ASC not initialized yet. Refusing beats writing a zero ceiling, which
        // would read downstream as a completed fill to nothing — the same guard
        // UGothicSteadfastComponent::RefillToFull takes for the same reason.
        UE_LOG(LogVigilCombat, Warning,
            TEXT("HintTrigger|%s|SuperMeter fill refused — MaxSuperMeter is 0 (ASC not ready)"),
            *GetName());
        return;
    }

    // The direct setter, NOT ApplyGameplayEffectSpecToSelf. A raw ASC apply built
    // outside an ability carries a default FPredictionKey, and that combination
    // returns an invalid handle and applies NOTHING — silently, with no log line.
    // This actor has no ability to borrow a real key from, so the attribute is
    // written the way the Steadfast component writes its own.
    Attributes->SetSuperMeter(MaxSuper);

    UE_LOG(LogVigilCombat, Log, TEXT("HintTrigger|%s|SuperMeter filled to %.0f on %s"),
        *GetName(), MaxSuper, *GetNameSafe(Player));
}
