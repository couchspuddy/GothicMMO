// GothicSlicerProjectile.h
// The Slicer — Hunter weapon ability projectile.
// Detects collision only. All damage and stagger application happens in GA_Slicer,
// keeping damage logic centralized in one place for easier balance changes.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AGothicSlicerProjectile.generated.h"

class UProjectileMovementComponent;
class USphereComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSlicerHit, AActor*, HitActor, FVector, HitLocation);

UCLASS()
class GOTHICMMO_API AGothicSlicerProjectile : public AActor
{
    GENERATED_BODY()

public:
    AGothicSlicerProjectile();

    virtual void BeginPlay() override;

    UFUNCTION(BlueprintCallable, Category = "Gothic|Slicer")
    void InitializeProjectile(AActor* InInstigator);

    /** Fired when the projectile hits something valid. The spawning ability binds to this. */
    UPROPERTY(BlueprintAssignable, Category = "Gothic|Slicer")
    FOnSlicerHit OnSlicerHit;

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Slicer")
    TObjectPtr<USphereComponent> CollisionComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Slicer")
    TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

    UPROPERTY(EditDefaultsOnly, Category = "Gothic|Slicer")
    float MaxLifetime = 3.f;

    UFUNCTION()
    void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
        FVector NormalImpulse, const FHitResult& Hit);

private:
    UPROPERTY()
    TObjectPtr<AActor> ProjectileInstigator;

    FTimerHandle LifetimeTimerHandle;
};