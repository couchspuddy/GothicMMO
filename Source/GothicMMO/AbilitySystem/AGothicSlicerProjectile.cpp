// GothicSlicerProjectile.cpp

#include "AbilitySystem/AGothicSlicerProjectile.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "EngineUtils.h" // TActorIterator
#include "GothicMMO.h"    // LogVigilCombat
#include "AI/GothicEnemyBase.h"
#include "Character/GothicCharacterBase.h"

AGothicSlicerProjectile::AGothicSlicerProjectile()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;

    CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComponent"));
    CollisionComponent->InitSphereRadius(15.f);
    CollisionComponent->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    CollisionComponent->OnComponentHit.AddDynamic(this, &AGothicSlicerProjectile::OnHit);
    RootComponent = CollisionComponent;

    ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
    ProjectileMovement->UpdatedComponent = CollisionComponent;
    ProjectileMovement->InitialSpeed = 3000.f;
    ProjectileMovement->MaxSpeed = 3000.f;
    ProjectileMovement->bRotationFollowsVelocity = true;
    ProjectileMovement->bShouldBounce = false;
    ProjectileMovement->ProjectileGravityScale = 0.f;

    // Lifetime is NOT set here. MaxLifetime is the single source of truth and is
    // applied in BeginPlay — see the note there.
}

void AGothicSlicerProjectile::BeginPlay()
{
    Super::BeginPlay();

    if (ProjectileInstigator)
    {
        CollisionComponent->IgnoreActorWhenMoving(ProjectileInstigator, true);
    }

    // One destruction path, one tunable. This actor used to carry two independent
    // 3.0s timers — InitialLifeSpan in the constructor and a MaxLifetime timer
    // here — so editing MaxLifetime changed nothing: whichever fired first won,
    // and they were set to the same value. MaxLifetime is the property a designer
    // can see and edit, so it wins; the engine's lifespan is the mechanism.
    //
    // Either way expiry routes through Destroyed(), which broadcasts
    // OnSlicerExpired — GA_Slicer still gets told, so the ability cannot strand.
    SetLifeSpan(MaxLifetime);

    // Seed the ricochet run. CachedSpeed is the canonical flight speed we restore
    // on every redirect (the live Velocity magnitude is unreliable right after a
    // blocking hit — see ExecuteBounceRedirect).
    BouncesRemaining = MaxBounces;
    CurrentDamageMultiplier = 1.f;
    if (ProjectileMovement)
    {
        CachedSpeed = ProjectileMovement->InitialSpeed > 0.f
            ? ProjectileMovement->InitialSpeed
            : ProjectileMovement->MaxSpeed;
    }
    if (CachedSpeed <= 0.f)
    {
        CachedSpeed = 3000.f;
    }
}

void AGothicSlicerProjectile::InitializeProjectile(AActor* InInstigator)
{
    ProjectileInstigator = InInstigator;

    if (ProjectileInstigator)
    {
        CollisionComponent->IgnoreActorWhenMoving(ProjectileInstigator, true);
    }
}

void AGothicSlicerProjectile::OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
    FVector NormalImpulse, const FHitResult& Hit)
{
    if (!OtherActor || OtherActor == this || OtherActor == ProjectileInstigator
        || OtherActor->IsA(AGothicSlicerProjectile::StaticClass()))
    {
        return;
    }

    // No-re-hit guard: an already-touched actor never counts again, even if the
    // physics engine reports a second contact while we overlap it.
    if (HitTargets.Contains(OtherActor))
    {
        return;
    }

    HitTargets.Add(OtherActor);
    // Stop physically colliding with this actor for the rest of the run so the
    // redirect can graze past it toward the next target.
    if (CollisionComponent)
    {
        CollisionComponent->IgnoreActorWhenMoving(OtherActor, true);
    }

    // Broadcast the hit BEFORE advancing the falloff tier so GA_Slicer applies
    // THIS touch's damage at CurrentDamageMultiplier. Damage lives in the ability
    // (one choke point via MakeDamageContext -> PostGameplayEffectExecute); the
    // projectile never applies damage itself.
    OnSlicerHit.Broadcast(OtherActor, Hit.ImpactPoint);

    // Enemy-only chaining. A chain only propagates FROM an enemy contact — a
    // wall/floor clip ends the run (no surface ricochets this pass) exactly as
    // the single-target Slicer behaved before.
    if (BouncesRemaining > 0 && OtherActor->IsA(AGothicEnemyBase::StaticClass()))
    {
        if (AActor* NextTarget = FindNextBounceTarget())
        {
            PendingBounceTarget = NextTarget;
            --BouncesRemaining;
            CurrentDamageMultiplier *= DamageFalloffPerBounce;

            UE_LOG(LogVigilCombat, Verbose,
                TEXT("VigilTimeline|Slicer|Event=Ricochet|Target=%s|BouncesLeft=%d|NextMult=%.2f"),
                *NextTarget->GetName(), BouncesRemaining, CurrentDamageMultiplier);

            // Redirect one tick later — see ExecuteBounceRedirect for why.
            GetWorldTimerManager().SetTimerForNextTick(
                this, &AGothicSlicerProjectile::ExecuteBounceRedirect);
            return;
        }
    }

    // No bounces left or no valid neighbour — the run ends. Destroyed() fans out
    // OnSlicerExpired, which ends the ability.
    Destroy();
}

AActor* AGothicSlicerProjectile::FindNextBounceTarget() const
{
    const UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    const FVector Origin = GetActorLocation();
    const float RadiusSq = BounceSearchRadius * BounceSearchRadius;

    AActor* Best = nullptr;
    float BestDistSq = TNumericLimits<float>::Max();

    for (TActorIterator<AGothicEnemyBase> It(World); It; ++It)
    {
        AGothicEnemyBase* Enemy = *It;
        if (!Enemy || HitTargets.Contains(Enemy))
        {
            continue;
        }
        // Skip dead/downed/otherwise-unfightable enemies (shared filter).
        if (!AGothicCharacterBase::IsFightableActor(Enemy))
        {
            continue;
        }

        const float DistSq = FVector::DistSquared(Origin, Enemy->GetActorLocation());
        if (DistSq > RadiusSq)
        {
            continue;
        }

        // Pure distance; deterministic UID tie-break avoids any RNG dependency.
        if (DistSq < BestDistSq
            || (DistSq == BestDistSq && Best && Enemy->GetUniqueID() < Best->GetUniqueID()))
        {
            BestDistSq = DistSq;
            Best = Enemy;
        }
    }

    return Best;
}

void AGothicSlicerProjectile::ExecuteBounceRedirect()
{
    if (!ProjectileMovement || !CollisionComponent)
    {
        Destroy();
        return;
    }

    // The chosen target may have died in the intervening frame — try a fresh one
    // rather than dying outright, then give up if the pack is spent.
    if (!AGothicCharacterBase::IsFightableActor(PendingBounceTarget))
    {
        PendingBounceTarget = FindNextBounceTarget();
        if (!PendingBounceTarget)
        {
            Destroy();
            return;
        }
    }

    // Re-establish the updated component: StopSimulating() nulled it on the
    // blocking hit. Then set velocity toward the target at the cached speed;
    // bRotationFollowsVelocity whips the blade visibly to face the new heading.
    ProjectileMovement->SetUpdatedComponent(CollisionComponent);
    const FVector Dir =
        (PendingBounceTarget->GetActorLocation() - GetActorLocation()).GetSafeNormal();
    ProjectileMovement->Velocity = Dir * CachedSpeed;
    ProjectileMovement->Activate(true);

    PendingBounceTarget = nullptr;
}

void AGothicSlicerProjectile::Destroyed()
{
    OnSlicerExpired.Broadcast();
    Super::Destroyed();
}