// GothicRotundaPillar.cpp

#include "AI/GothicRotundaPillar.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "Character/GothicPlayerCharacter.h"
#include "GameFramework/WorldSettings.h"
#include "TimerManager.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"

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

    // The mesh STAYS. It used to be hidden right here, one line before the
    // warning event fired — so BP_RotundaPillar's OnPillarCollapseWarning
    // dutifully swapped in the ember-red cracked material and painted it onto
    // an invisible mesh. The telegraph existed and was literally impossible to
    // see; the first thing the player knew about a collapse was the damage.
    //
    // The stressed, glowing pillar IS the tell, and it has to be on screen for
    // the whole of WarningDuration. It goes away at impact, in
    // FinishCeilingCollapse, together with the slab and the damage.
    OnPillarCollapseWarning();

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
    ApplyCollapseDamageAtImpact();

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

    if (bOccupied && BlockingVolumeAttempts < 20)
    {
        ++BlockingVolumeAttempts;
        UE_LOG(LogTemp, Verbose,
            TEXT("RotundaPillar[%s]: blocking volume still occupied by a pawn — deferring activation (attempt %d)"),
            *GetName(), BlockingVolumeAttempts);

        GetWorldTimerManager().SetTimer(
            BlockingVolumeTimer, this,
            &AGothicRotundaPillar::EnableBlockingVolume,
            0.5f, false);
        return;
    }

    if (bOccupied)
    {
        // Four seconds of a pawn refusing to leave. Enabling anyway would risk
        // ejecting it, so the nav blocker stays off; a walkable zone is a much
        // cheaper failure than a player launched out of the arena.
        UE_LOG(LogTemp, Warning,
            TEXT("RotundaPillar[%s]: pawn never cleared the blocking volume — leaving it disabled rather than ejecting them"),
            *GetName());
        return;
    }

    BlockingVolumeActor->SetActorHiddenInGame(false);
    BlockingVolumeActor->SetActorEnableCollision(true);

    if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
    {
        NavSys->Build();
    }
}

void AGothicRotundaPillar::ApplyCollapseDamageAtImpact()
{
    if (!HasAuthority() || bCollapseDamageApplied)
    {
        return;
    }
    bCollapseDamageApplied = true;

    if (!CollapseDamageEffect)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("RotundaPillar[%s]: ceiling landed but CollapseDamageEffect is unassigned — collapse is cosmetic"),
            *GetName());
        return;
    }

    UWorld* World = GetWorld();
    if (!World || !CollapseDamageVolume)
    {
        return;
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
        // TargetDefense). The pillar has no ASC, so SourceAttackPower is 0 (see
        // GothicAttributeSet::PostGameplayEffectExecute — the bonus is read off
        // the instigator precisely so ASC-less sources don't inherit the
        // victim's own AttackPower). That leaves Defense as the only distortion,
        // and adding it back here is what makes the post-pipeline result land on
        // the fraction exactly instead of the fraction minus armour.
        const float TargetMaxHealth =
            TargetASC->GetNumericAttribute(UGothicAttributeSet::GetMaxHealthAttribute());
        const float TargetDefense =
            TargetASC->GetNumericAttribute(UGothicAttributeSet::GetDefenseAttribute());

        const float Magnitude = TargetMaxHealth > 0.f
            ? (TargetMaxHealth * CollapseDamageFraction) + TargetDefense
            : CollapseDamage;

        FGameplayEffectContextHandle Context = TargetASC->MakeEffectContext();
        Context.AddSourceObject(this);

        FGameplayEffectSpecHandle Spec = TargetASC->MakeOutgoingSpec(
            CollapseDamageEffect, 1.f, Context);

        if (Spec.IsValid())
        {
            Spec.Data->SetSetByCallerMagnitude(
                FGameplayTag::RequestGameplayTag(FName("Data.Damage")),
                Magnitude);
            TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());

            UE_LOG(LogTemp, Log,
                TEXT("RotundaPillar[%s]: ceiling hit %s for %.1f%% of %.1f MaxHealth "
                     "(sent %.1f raw, Defense %.1f) | victim Z %.0f vs KillZ %.0f"),
                *GetName(), *Victim->GetName(), CollapseDamageFraction * 100.f,
                TargetMaxHealth, Magnitude, TargetDefense,
                Victim->GetActorLocation().Z, KillZ);
        }
    }
}