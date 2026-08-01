// GothicSlicerProjectile.cpp

#include "AbilitySystem/AGothicSlicerProjectile.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"

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


    OnSlicerHit.Broadcast(OtherActor, Hit.ImpactPoint);

    Destroy();
}

void AGothicSlicerProjectile::Destroyed()
{
    OnSlicerExpired.Broadcast();
    Super::Destroyed();
}