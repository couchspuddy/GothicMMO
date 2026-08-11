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

    /**
     * The falloff multiplier for the CURRENT contact, read by GA_Slicer when it
     * applies damage. Starts at 1.0 (the skill-shot first hit) and is multiplied
     * by DamageFalloffPerBounce AFTER each hit is broadcast, so by the time the
     * ability handler runs for a touch this still holds that touch's tier.
     * Damage itself is never applied here — the projectile only detects and
     * navigates; GA_Slicer owns the single damage path (see class comment).
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Slicer")
    float GetCurrentDamageMultiplier() const { return CurrentDamageMultiplier; }

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

    /**
     * Ricochet tuning — the Slicer is the swarm tool. The first contact is the
     * skill shot; each of up to MaxBounces further contacts is the pack tax.
     * Single-target TTK is unchanged: an isolated elite has no neighbour to
     * chain to, so none of the bounce value applies.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|Slicer|Ricochet")
    int32 MaxBounces = 3;

    /** Damage multiplier applied per bounce: 1.0 -> 0.7 -> 0.49 -> 0.34. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|Slicer|Ricochet")
    float DamageFalloffPerBounce = 0.7f;

    /** Search radius (cm) for the next chain target after a hit. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|Slicer|Ricochet")
    float BounceSearchRadius = 400.f;

    UFUNCTION()
    void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
        FVector NormalImpulse, const FHitResult& Hit);

private:
    UPROPERTY()
    TObjectPtr<AActor> ProjectileInstigator;

    /** Enemies already touched by THIS projectile instance — never re-hit. */
    UPROPERTY()
    TArray<TObjectPtr<AActor>> HitTargets;

    /** Bounces left before the run ends. Seeded to MaxBounces in BeginPlay. */
    int32 BouncesRemaining = 0;

    /** Falloff tier for the current contact — see GetCurrentDamageMultiplier. */
    float CurrentDamageMultiplier = 1.f;

    /** Canonical flight speed, cached at spawn so redirects preserve it. */
    float CachedSpeed = 0.f;

    /** Chain target chosen on hit, consumed one tick later by the redirect. */
    UPROPERTY()
    TObjectPtr<AActor> PendingBounceTarget;

    /** Nearest un-hit, fightable enemy within BounceSearchRadius, or null. */
    AActor* FindNextBounceTarget() const;

    /**
     * Applies the pending redirect one frame after the hit. Deferred on purpose:
     * with bShouldBounce=false the ProjectileMovementComponent calls
     * StopSimulating() immediately after OnComponentHit — nulling the updated
     * component and zeroing velocity — so a velocity write inside OnHit is
     * clobbered. Re-establishing the updated component next tick sidesteps it.
     */
    void ExecuteBounceRedirect();
};