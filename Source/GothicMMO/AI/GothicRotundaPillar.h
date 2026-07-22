// GothicRotundaPillar.h
// Individual destructible pillar in the City Hall Rotunda.
// Takes damage from boss charge impacts and passive Cry degradation.
// When destroyed, triggers ceiling collapse on its zone.
//
// Each pillar owns: a health pool, a visual state (healthy/cracked/destroyed),
// a ceiling mesh, a collapse damage volume, and a post-collapse blocking volume.
//
// Setup in editor:
//   1. Place four of these in the Rotunda at the pillar positions
//   2. Assign CeilingMesh to the ceiling section above each pillar
//   3. Assign BlockingVolumeActor to the nav-blocking collision for each zone
//   4. Register each pillar with the GothicBossArenaManager in the level

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GothicRotundaPillar.generated.h"

class UStaticMeshComponent;
class UBoxComponent;
class UGameplayEffect;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPillarDestroyed, AGothicRotundaPillar*, Pillar);

UENUM(BlueprintType)
enum class EPillarState : uint8
{
    Healthy   UMETA(DisplayName = "Healthy"),
    Cracked   UMETA(DisplayName = "Cracked"),
    Destroyed UMETA(DisplayName = "Destroyed"),
};

UCLASS()
class GOTHICMMO_API AGothicRotundaPillar : public AActor
{
    GENERATED_BODY()

public:
    AGothicRotundaPillar();

    /**
     * Called by the boss charge impact or Cry passive damage.
     * Returns true if this hit destroyed the pillar.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Arena")
    bool ApplyPillarDamage(float DamageAmount);

    UFUNCTION(BlueprintPure, Category = "Gothic|Arena")
    EPillarState GetPillarState() const { return CurrentState; }

    UFUNCTION(BlueprintPure, Category = "Gothic|Arena")
    bool IsDestroyed() const { return CurrentState == EPillarState::Destroyed; }

    UFUNCTION(BlueprintPure, Category = "Gothic|Arena")
    float GetHealthPercent() const { return MaxHealth > 0.f ? CurrentHealth / MaxHealth : 0.f; }

    /** Broadcast when this pillar is destroyed. Arena manager listens. */
    UPROPERTY(BlueprintAssignable, Category = "Gothic|Arena")
    FOnPillarDestroyed OnPillarDestroyed;

protected:
    virtual void BeginPlay() override;

    // -------------------------------------------------------------------------
    // Components
    // -------------------------------------------------------------------------

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Arena")
    TObjectPtr<UStaticMeshComponent> PillarMesh;

    /** The ceiling section above this pillar. Animated downward on destruction. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    TObjectPtr<UStaticMeshComponent> CeilingMesh;

    /** Damage volume active during ceiling collapse. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Arena")
    TObjectPtr<UBoxComponent> CollapseDamageVolume;

    /** Blocking volume activated after collapse settles. Blocks movement and nav. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    TObjectPtr<AActor> BlockingVolumeActor;

    // -------------------------------------------------------------------------
    // Tuning
    // -------------------------------------------------------------------------

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    float MaxHealth = 100.f;

    /** Fraction of MaxHealth below which pillar shows cracked visuals. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    float CrackedThreshold = 0.5f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    float CollapseDuration = 1.5f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    float CollapseDamage = 80.f;

    /** GE applied to players caught in collapse. Uses Data.Damage SetByCaller. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    TSubclassOf<UGameplayEffect> CollapseDamageEffect;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Arena|Visuals")
    TObjectPtr<UMaterialInterface> CrackedMaterial;

    // -------------------------------------------------------------------------
    // Blueprint events for VFX/SFX
    // -------------------------------------------------------------------------

    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Arena")
    void OnPillarCracked();

    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Arena")
    void OnPillarCollapse();

private:
    float CurrentHealth = 0.f;
    EPillarState CurrentState = EPillarState::Healthy;

    void TransitionToState(EPillarState NewState);
    void BeginCeilingCollapse();
    void FinishCeilingCollapse();
    void ApplyCollapseDamageToPlayersInZone();
};