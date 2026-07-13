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

    InitialLifeSpan = 3.f;
}

void AGothicSlicerProjectile::BeginPlay()
{
    Super::BeginPlay();

    if (ProjectileInstigator)
    {
        CollisionComponent->IgnoreActorWhenMoving(ProjectileInstigator, true);
    }

    GetWorldTimerManager().SetTimer(
        LifetimeTimerHandle,
        [this]() { Destroy(); },
        MaxLifetime,
        false);
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

    UE_LOG(LogTemp, Log, TEXT("GA_Slicer Projectile: Hit %s"), *OtherActor->GetName());

    OnSlicerHit.Broadcast(OtherActor, Hit.ImpactPoint);

    Destroy();
}

void AGothicSlicerProjectile::Destroyed()
{
    OnSlicerExpired.Broadcast();
    Super::Destroyed();
}