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
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSlicerExpired);

UCLASS()
class GOTHICMMO_API AGothicSlicerProjectile : public AActor
{
    GENERATED_BODY()

public:
    AGothicSlicerProjectile();

    virtual void BeginPlay() override;
    virtual void Destroyed() override;

    UFUNCTION(BlueprintCallable, Category = "Gothic|Slicer")
    void InitializeProjectile(AActor* InInstigator);

    /** Fired when the projectile hits something valid. The spawning ability binds to this. */
    UPROPERTY(BlueprintAssignable, Category = "Gothic|Slicer")
    FOnSlicerHit OnSlicerHit;
    UPROPERTY(BlueprintAssignable, Category = "Gothic|Slicer")
    FOnSlicerExpired OnSlicerExpired; 


protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Slicer")
    TObjectPtr<USphereComponent> CollisionComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Slicer")
    TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

    /**
     * Seconds before the projectile despawns. The ONLY lifetime control on this
     * actor — applied via SetLifeSpan in BeginPlay, which also overrides anything
     * set in the Blueprint's Initial Life Span field.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|Slicer")
    float MaxLifetime = 3.f;

    UFUNCTION()
    void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
        FVector NormalImpulse, const FHitResult& Hit);

private:
    UPROPERTY()
    TObjectPtr<AActor> ProjectileInstigator;

};