// GothicRotundaPillar.cpp

#include "AI/GothicRotundaPillar.h"
#include "GothicMMO.h"                      // LogVigilCombat
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Materials/MaterialInterface.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/GothicGameplayTags.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "Character/GothicPlayerCharacter.h"
#include "GameFramework/WorldSettings.h"
#include "TimerManager.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"

namespace
{
    /**
     * The `t=` stamp every VigilTimeline line carries. Same free function as
     * GothicBossArenaManager.cpp, deliberately duplicated rather than shared:
     * two four-line helpers in two files beat a header nobody else wants to
     * include, and the arena and the pillar are not otherwise coupled.
     */
    float PillarTimelineNow(const AActor* Actor)
    {
        const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
        return World ? World->GetTimeSeconds() : 0.f;
    }
}

AGothicRotundaPillar::AGothicRotundaPillar()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;

    // PillarMesh stays the root. Reparenting it under a bare scene component --
    // to put the actor origin at the pillar's foot rather than its mid-height --
    // was tried and reverted: every placed AND freshly spawned pillar came back
    // with no physics body at all. The mesh registered, reported
    // BlockAll/WorldStatic/QueryAndPhysics, and rendered, but a physics-filtered
    // overlap found nothing and traces passed straight through, so
    // FindNearestPillar's ECC_WorldStatic sphere overlap could never see it.
    // Recompiling and resaving the Blueprint did not restore it.
    //
    // The consequence to design around: this mesh's pivot is at its CENTRE, so
    // the actor origin sits at half the pillar's height, and everything locating
    // a pillar uses GetActorLocation. Keep pillars short enough that their
    // origin stays near the floor -- an origin ~2200uu up put them out of range
    // of both the navmesh projection and Wall Pound's target lookup.
    PillarMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PillarMesh"));
    RootComponent = PillarMesh;

    CollapseDamageVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("CollapseDamageVolume"));
    CollapseDamageVolume->SetupAttachment(RootComponent);
    CollapseDamageVolume->SetBoxExtent(FVector(200.f, 200.f, 300.f));
    CollapseDamageVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CollapseDamageVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
    CollapseDamageVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void AGothicRotundaPillar::BeginPlay()
{
    Super::BeginPlay();
    CurrentHealth = MaxHealth;
    CurrentState = EPillarState::Healthy;

    // Drop the damage volume onto the floor under the pillar. Done once here so
    // it is correct (and debug-drawable) from the start of the level rather
    // than only at the moment of collapse.
    PositionCollapseDamageVolumeFromMeshBase();

    // Force blocking volume to start hidden and disabled
    // regardless of how it was placed in the editor
    if (BlockingVolumeActor)
    {
        BlockingVolumeActor->SetActorHiddenInGame(true);
        BlockingVolumeActor->SetActorEnableCollision(false);
    }
}

void AGothicRotundaPillar::PositionCollapseDamageVolumeFromMeshBase()
{
    if (!CollapseDamageVolume || !PillarMesh)
    {
        return;
    }

    // World-space bounds of the mesh as it actually sits in the level —
    // placement Z, actor scale and mesh pivot all already folded in. Nothing
    // here is allowed to assume where the origin is relative to the geometry;
    // that assumption is precisely what broke.
    const FBoxSphereBounds MeshBounds = PillarMesh->Bounds;

    if (MeshBounds.BoxExtent.IsNearlyZero())
    {
        // No static mesh assigned. Fall back to the actor origin so the volume
        // is at least somewhere defensible, and say so — a silent guess here is
        // how the original bug stayed invisible for so long.
        UE_LOG(LogTemp, Warning,
            TEXT("RotundaPillar[%s]: PillarMesh has no bounds — collapse volume left at the actor origin (Z %.0f)"),
            *GetName(), GetActorLocation().Z);
        return;
    }

    const float BaseZ      = MeshBounds.Origin.Z - MeshBounds.BoxExtent.Z;
    const float HalfHeight = FMath::Max(1.f, CollapseVolumeHeight * 0.5f);

    // Scale is neutralised deliberately. The volume is attached to PillarMesh,
    // so it inherits the root's scale — and the Rotunda pillars are scaled
    // (3,3,20), which would silently turn a 400uu-tall box into an 8000uu one.
    // Working in world units means the numbers on the tuning properties are the
    // numbers in the level.
    CollapseDamageVolume->SetWorldScale3D(FVector::OneVector);
    CollapseDamageVolume->SetBoxExtent(
        FVector(CollapseVolumeHalfWidth, CollapseVolumeHalfWidth, HalfHeight), false);
    CollapseDamageVolume->SetWorldLocation(
        FVector(MeshBounds.Origin.X, MeshBounds.Origin.Y, BaseZ + HalfHeight));

    UE_LOG(LogTemp, Verbose,
        TEXT("RotundaPillar[%s]: collapse volume spans Z %.0f-%.0f (mesh base %.0f, actor origin %.0f)"),
        *GetName(), BaseZ, BaseZ + CollapseVolumeHeight, BaseZ, GetActorLocation().Z);
}

bool AGothicRotundaPillar::ApplyPillarDamage(float DamageAmount)
{
    if (CurrentState == EPillarState::Destroyed) return false;

    CurrentHealth = FMath::Max(0.f, CurrentHealth - DamageAmount);


    if (CurrentHealth <= 0.f)
    {
        TransitionToState(EPillarState::Destroyed);
        return true;
    }
    else if (CurrentHealth <= MaxHealth * CrackedThreshold
             && CurrentState == EPillarState::Healthy)
    {
        TransitionToState(EPillarState::Cracked);
    }

    return false;
}

void AGothicRotundaPillar::TriggerWallCollapse()
{
    if (CurrentState == EPillarState::Destroyed)
    {
        return;
    }

    // Drive health to zero through the normal path rather than jumping straight
    // to BeginCeilingCollapse — that keeps the Destroyed transition, the
    // OnPillarCollapse BIE, and the OnPillarDestroyed broadcast identical to a
    // combat kill, so the arena manager and any listeners see one consistent
    // collapse event whatever caused it.
    ApplyPillarDamage(CurrentHealth);
}

void AGothicRotundaPillar::TransitionToState(EPillarState NewState)
{
    if (CurrentState == NewState) return;
    CurrentState = NewState;

    switch (NewState)
    {
    case EPillarState::Cracked:
        if (CrackedMaterial && PillarMesh)
        {
            PillarMesh->SetMaterial(0, CrackedMaterial);
        }
        OnPillarCracked();
        break;

    case EPillarState::Destroyed:
        // The arena manager's bookkeeping fires now, at the moment the pillar
        // dies — phase progression should not wait on debris. The player-facing
        // half of the collapse (cue, slab, damage) is deferred to impact.
        BeginCollapseWarning();
        OnPillarDestroyed.Broadcast(this);
        break;

    default:
        break;
    }
}

void AGothicRotundaPillar::BeginCollapseWarning()
{
    bCollapseDamageApplied = false;

    // ── The collapse sequence, on the timeline ───────────────────────────────
    //
    // PILLAR-fell (the arena manager's line) was the only mark the whole
    // sequence left, and it fires on the state change — so a verification log
    // could prove a pillar died and prove nothing at all about the telegraph the
    // player is supposed to react to or the slab that is supposed to hurt them.
    // Three lines, one per beat: warning here, slab at impact, sealed when the
    // nav blocker goes live. The WarningDuration gap between the first two is
    // the telegraph, readable directly off the t= stamps.
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|Pillar|COLLAPSE-warning|pillar=%s|telegraph=%.2f"),
        PillarTimelineNow(this), *GetName(), *GetName(), WarningDuration);

    // The mesh STAYS. It used to be hidden right here, one line before the
    // warning event fired — so BP_RotundaPillar's OnPillarCollapseWarning
    // dutifully swapped in the ember-red cracked material and painted it onto
    // an invisible mesh. The telegraph existed and was literally impossible to
    // see; the first thing the player knew about a collapse was the damage.
    //
    // The stressed, glowing pillar IS the tell, and it has to be on screen for
    // the whole of WarningDuration. It goes away at impact, in
    // FinishCeilingCollapse, together with the slab and the damage.
    //
    // The swap itself lives in BP_RotundaPillar's OnPillarCollapseWarning, which
    // makes it invisible to a headless verification pass — the last PIE run could
    // confirm the timing of the warning and the damage but not that the ember-red
    // tell was ever applied. So read slot 0 back across the event and say what
    // actually changed. Cheap, once per collapse, and it turns "the telegraph
    // looked fine to me" into something the log can settle.
    UMaterialInterface* MaterialBefore = PillarMesh ? PillarMesh->GetMaterial(0) : nullptr;

    OnPillarCollapseWarning();

    UMaterialInterface* MaterialAfter = PillarMesh ? PillarMesh->GetMaterial(0) : nullptr;

    if (MaterialBefore != MaterialAfter)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("RotundaPillar[%s]: collapse warning — mesh material 0 swapped %s -> %s, visible for %.2fs"),
            *GetName(), *GetNameSafe(MaterialBefore), *GetNameSafe(MaterialAfter), WarningDuration);
    }
    else
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("RotundaPillar[%s]: collapse warning — mesh material 0 UNCHANGED (%s). "
                 "The %.2fs telegraph is running with no visual tell unless OnPillarCollapseWarning "
                 "sells it some other way (VFX, decal, cue)."),
            *GetName(), *GetNameSafe(MaterialAfter), WarningDuration);
    }

    // Re-derive the damage footprint now, while the mesh is still standing and
    // its bounds are meaningful. BeginPlay already did this, but a pillar that
    // was moved or rescaled at runtime would otherwise damage its old floor.
    PositionCollapseDamageVolumeFromMeshBase();

    if (WarningDuration <= 0.f)
    {
        // Degenerate config — still go through the impact path so the drop and
        // the damage stay welded together, just with no window to react in.
        FinishCeilingCollapse();
        return;
    }

    GetWorldTimerManager().SetTimer(
        CollapseWarningTimer, this,
        &AGothicRotundaPillar::FinishCeilingCollapse,
        WarningDuration, false);
}

void AGothicRotundaPillar::FinishCeilingCollapse()
{
    // ── The pillar goes ──────────────────────────────────────────────────
    // Moved here from BeginCollapseWarning. It stood, cracked and glowing,
    // for the whole warning window; it disappears on the same frame the slab
    // lands, so the column giving way and the ceiling arriving read as one
    // event instead of two unrelated ones.
    if (PillarMesh)
    {
        PillarMesh->SetVisibility(false);
        PillarMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // ── The slab lands ───────────────────────────────────────────────────
    if (CeilingMesh)
    {
        // No ejection. A 400uu single-frame teleport of a blocking body through
        // a standing pawn is a depenetration event: the character movement
        // component resolves it by shoving the pawn out along the shortest
        // axis, which from under a ceiling means sideways at speed or, worse,
        // through the floor. The collapse is meant to cost health, not
        // position, so the slab stops interacting with pawns entirely before
        // it moves and never gets the chance.
        CeilingMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

        FVector Loc = CeilingMesh->GetComponentLocation();
        Loc.Z -= 400.f;
        // bSweep deliberately false: a swept move would re-introduce exactly the
        // push-out this is avoiding.
        CeilingMesh->SetWorldLocation(Loc);
    }

    // ── and the damage lands with it, same frame ─────────────────────────
    const int32 Victims = ApplyCollapseDamageAtImpact();

    // victims= is the number of PLAYERS the impact actually damaged, counted by
    // the overlap rather than assumed from the volume's position. Zero is a
    // legitimate and common outcome — the telegraph exists to produce it — so
    // this line is what separates "the slab landed and they dodged" from "the
    // slab never landed", which the old log could not distinguish at all.
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|Pillar|COLLAPSE-slab|pillar=%s|victims=%d"),
        PillarTimelineNow(this), *GetName(), *GetName(), Victims);

    OnPillarCollapse();

    // Debris settles, THEN the nav blocker goes live.
    BlockingVolumeAttempts = 0;
    GetWorldTimerManager().SetTimer(
        BlockingVolumeTimer, this,
        &AGothicRotundaPillar::EnableBlockingVolume,
        FMath::Max(0.01f, CollapseDuration), false);
}

void AGothicRotundaPillar::EnableBlockingVolume()
{
    if (!BlockingVolumeActor)
    {
        return;
    }

    // The CDO has this as None, but placed pillars can point it at a real
    // blocking volume. Switching collision on underneath a pawn does the same
    // violence the slab would have: the pawn is inside a solid now, and the
    // movement component's next depenetration pass fires it out. So we ask
    // first, and wait if the answer is "someone is standing there."
    FVector Origin, Extent;
    BlockingVolumeActor->GetActorBounds(false, Origin, Extent);

    TArray<FOverlapResult> Overlaps;
    FCollisionObjectQueryParams ObjParams;
    ObjParams.AddObjectTypesToQuery(ECC_Pawn);
    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PillarBlockingVolumeClearance), false, this);
    QueryParams.AddIgnoredActor(BlockingVolumeActor);

    const bool bOccupied = GetWorld() && GetWorld()->OverlapMultiByObjectType(
        Overlaps, Origin, FQuat::Identity, ObjParams,
        FCollisionShape::MakeBox(Extent), QueryParams) && Overlaps.Num() > 0;

    // The retry budget, spelled out rather than left as a bare "< 20". The last
    // PIE pass watched this tick past attempt 14 at 2Hz with no visible end to
    // it, because nothing in the log said what the ceiling was or how close it
    // was getting. A deferral loop should always be able to answer "and then
    // what?" — this one gives up after BlockingVolumeMaxWaitSeconds and says so
    // at Warning, once.
    const int32 MaxAttempts = FMath::Max(1,
        FMath::CeilToInt(BlockingVolumeMaxWaitSeconds / BlockingVolumeRetryInterval));

    if (bOccupied && BlockingVolumeAttempts < MaxAttempts)
    {
        ++BlockingVolumeAttempts;
        UE_LOG(LogTemp, Verbose,
            TEXT("RotundaPillar[%s]: blocking volume still occupied by a pawn — deferring activation "
                 "(attempt %d of %d, %.1fs of %.1fs elapsed)"),
            *GetName(), BlockingVolumeAttempts, MaxAttempts,
            BlockingVolumeAttempts * BlockingVolumeRetryInterval, BlockingVolumeMaxWaitSeconds);

        GetWorldTimerManager().SetTimer(
            BlockingVolumeTimer, this,
            &AGothicRotundaPillar::EnableBlockingVolume,
            BlockingVolumeRetryInterval, false);
        return;
    }

    if (bOccupied)
    {
        // The full wait, and the pawn is still standing in it. Enabling anyway
        // would risk ejecting them, so the nav blocker stays off permanently for
        // this pillar; a walkable zone is a much cheaper failure than a player
        // launched out of the arena. No further timer is scheduled — this is the
        // end of the line, not another deferral.
        UE_LOG(LogTemp, Warning,
            TEXT("RotundaPillar[%s]: pawn never cleared the blocking volume after %.1fs (%d attempts) — "
                 "leaving it disabled rather than ejecting them. That zone stays walkable for the rest of the fight."),
            *GetName(), BlockingVolumeMaxWaitSeconds, MaxAttempts);
        return;
    }

    BlockingVolumeActor->SetActorHiddenInGame(false);
    BlockingVolumeActor->SetActorEnableCollision(true);

    // The third beat: the zone is now geometry. Only on the path that actually
    // enables it — the give-up branch above returns, and it already says at
    // Warning that this pillar's zone stays walkable, so a missing
    // COLLAPSE-sealed is meaningful rather than ambiguous.
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|Pillar|COLLAPSE-sealed|pillar=%s|volume=%s|attempts=%d"),
        PillarTimelineNow(this), *GetName(), *GetName(),
        *GetNameSafe(BlockingVolumeActor), BlockingVolumeAttempts);

    if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
    {
        NavSys->Build();
    }
}

// Returns the number of players actually damaged, so FinishCeilingCollapse can
// put it on the COLLAPSE-slab line. The count is the overlap's own answer, not
// a guess reconstructed from the volume's position afterwards.
int32 AGothicRotundaPillar::ApplyCollapseDamageAtImpact()
{
    if (!HasAuthority() || bCollapseDamageApplied)
    {
        return 0;
    }
    bCollapseDamageApplied = true;

    if (!CollapseDamageEffect)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("RotundaPillar[%s]: ceiling landed but CollapseDamageEffect is unassigned — collapse is cosmetic"),
            *GetName());
        return 0;
    }

    UWorld* World = GetWorld();
    if (!World || !CollapseDamageVolume)
    {
        return 0;
    }

    // Explicit overlap query rather than enabling the volume's collision and
    // reading GetOverlappingActors on the same frame. That pattern is a race:
    // overlaps are populated by the physics scene's next update, so a volume
    // switched on this frame reports nothing, and whether the collapse hurt
    // anyone depended on how recently the player had moved. A direct
    // OverlapMultiByObjectType against the volume's world box answers now.
    const FVector BoxOrigin = CollapseDamageVolume->GetComponentLocation();
    const FVector BoxExtent = CollapseDamageVolume->GetScaledBoxExtent();
    const FQuat   BoxRot    = CollapseDamageVolume->GetComponentQuat();

    TArray<FOverlapResult> Overlaps;
    FCollisionObjectQueryParams ObjParams;
    ObjParams.AddObjectTypesToQuery(ECC_Pawn);
    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PillarCollapseDamage), false, this);

    World->OverlapMultiByObjectType(
        Overlaps, BoxOrigin, BoxRot, ObjParams,
        FCollisionShape::MakeBox(BoxExtent), QueryParams);

    const float KillZ = World->GetWorldSettings() ? World->GetWorldSettings()->KillZ : -HALF_WORLD_MAX;

    int32 VictimsDamaged = 0;

    TSet<AActor*> AlreadyDamaged;
    for (const FOverlapResult& Overlap : Overlaps)
    {
        AActor* Victim = Overlap.GetActor();
        if (!Victim || !Victim->IsA(AGothicPlayerCharacter::StaticClass()))
        {
            continue;
        }

        // One pawn can present several overlapping shapes (capsule, mesh,
        // hitboxes). Once per victim per collapse, whatever they bring.
        bool bAlreadyInSet = false;
        AlreadyDamaged.Add(Victim, &bAlreadyInSet);
        if (bAlreadyInSet)
        {
            continue;
        }

        UAbilitySystemComponent* TargetASC =
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Victim);
        if (!TargetASC)
        {
            continue;
        }

        // Percentage of the VICTIM'S pool, not a flat number.
        //
        // The damage pipeline is FinalDamage = max(1, Raw + SourceAttackPower -
        // TargetDefense), where SourceAttackPower is read off the context's
        // ORIGINAL INSTIGATOR. The pillar has no ASC, so naming the pillar as
        // instigator contributes 0 — which is what the +TargetDefense
        // compensation below assumes, and it is only true because the
        // AddInstigator call is made explicitly.
        //
        // It was NOT true before: the context was built from TargetASC and left
        // to default, which stamps that ASC's OwnerActor as instigator. For a
        // player that resolves through the PlayerState to the victim's OWN ASC,
        // so the ceiling would have hit them for their own AttackPower on top —
        // and once the other damage sites started resolving instigators
        // properly, this site's compensation would have been double-counting.
        // The pillar is the source object AND the instigator; it always was in
        // intent, and now it is in fact.
        const float TargetMaxHealth =
            TargetASC->GetNumericAttribute(UGothicAttributeSet::GetMaxHealthAttribute());
        const float TargetDefense =
            TargetASC->GetNumericAttribute(UGothicAttributeSet::GetDefenseAttribute());

        const float Magnitude = TargetMaxHealth > 0.f
            ? (TargetMaxHealth * CollapseDamageFraction) + TargetDefense
            : CollapseDamage;

        FGameplayEffectContextHandle Context = TargetASC->MakeEffectContext();
        Context.AddSourceObject(this);
        Context.AddInstigator(this, this);

        FGameplayEffectSpecHandle Spec = TargetASC->MakeOutgoingSpec(
            CollapseDamageEffect, 1.f, Context);

        if (Spec.IsValid())
        {
            Spec.Data->SetSetByCallerMagnitude(
                GothicTags::Data_Damage,
                Magnitude);
            TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());

            ++VictimsDamaged;

            UE_LOG(LogTemp, Log,
                TEXT("RotundaPillar[%s]: ceiling hit %s for %.1f%% of %.1f MaxHealth "
                     "(sent %.1f raw, Defense %.1f) | victim Z %.0f vs KillZ %.0f"),
                *GetName(), *Victim->GetName(), CollapseDamageFraction * 100.f,
                TargetMaxHealth, Magnitude, TargetDefense,
                Victim->GetActorLocation().Z, KillZ);
        }
    }

    return VictimsDamaged;
}