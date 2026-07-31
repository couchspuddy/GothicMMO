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

    /**
     * Force an immediate collapse regardless of remaining health.
     *
     * GA_BestialLucidWallPound finds the nearest WallPoundTarget-tagged actor
     * and calls this by name (FindFunction/ProcessEvent) — it is the scripted
     * Phase 2 opener that drops the pillar the boss just moved to. The pillars
     * carry that tag, so this is the seam that connects Wall Pound to the pillar
     * collapse system; before it existed Wall Pound activated and collapsed
     * nothing. Reuses the ordinary destruction path, so the warning fires, the
     * ceiling drops WarningDuration later taking 30% of the pool from anyone
     * still underneath, and the arena manager's OnPillarDestroyed bookkeeping
     * all fire exactly as a combat kill would.
     * No-op if already destroyed.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Arena")
    void TriggerWallCollapse();

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

    /**
     * The pillar's visual mesh, and the actor's ROOT.
     *
     * Being the root means the actor origin sits wherever this mesh's pivot is
     * -- for the engine Cylinder that is its CENTRE, i.e. half the pillar's
     * height above its base. Every system that locates a pillar
     * (GothicBTTask_FindNearestPillar, Wall Pound's target lookup) measures to
     * GetActorLocation, so a very tall pillar hoists its own origin out of
     * gameplay range: at ~2200uu up, the navmesh projection and Wall Pound both
     * stopped finding it, verified by A/B against a short one that collapsed
     * normally.
     *
     * Reparenting this under a bare scene root to move the origin to the foot
     * was tried and reverted -- it left the mesh with no physics body at all.
     * See the constructor. Until that is solved, keep pillars short enough that
     * their mid-height origin stays near the floor, and set the ceiling to
     * match the pillar rather than the other way round.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Arena")
    TObjectPtr<UStaticMeshComponent> PillarMesh;

    /** The ceiling section above this pillar. Animated downward on destruction. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    TObjectPtr<UStaticMeshComponent> CeilingMesh;

    /**
     * Damage volume active during ceiling collapse.
     *
     * Its placement is COMPUTED, not authored — see
     * PositionCollapseDamageVolumeFromMeshBase. The constructor's relative
     * offset is only a starting value; anything set here in the editor is
     * overwritten at BeginPlay.
     */
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

    /**
     * The telegraph. Time between the pillar breaking (OnPillarCollapseWarning)
     * and the ceiling actually landing (OnPillarCollapse + damage).
     *
     * Before this existed the collapse was undodgeable in the worst possible
     * way: damage was applied at pillar-death time and the slab visibly
     * teleported down 1.5s LATER, so the player took the hit from a ceiling
     * that was still overhead and saw the ceiling land on empty floor. The two
     * halves now happen together, at the end of this window, and this window is
     * the player's whole opportunity to leave.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    float WarningDuration = 1.75f;

    /**
     * Settle time AFTER impact before BlockingVolumeActor goes live.
     *
     * No longer gates the ceiling drop (WarningDuration does). Kept as the
     * debris-settling delay because the blocking volume must not switch on
     * inside a pawn — see EnableBlockingVolume.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    float CollapseDuration = 1.5f;

    /**
     * Collapse damage as a fraction of the VICTIM'S OWN MaxHealth. This is the
     * primary knob: 0.30 means the ceiling always costs 30% of your pool, at
     * any gear level, which is what makes it read as a fixed threat rather than
     * a number that quietly becomes irrelevant as MaxHealth grows.
     *
     * The SetByCaller magnitude is pre-compensated for the victim's Defense so
     * the POST-pipeline result lands on the fraction exactly — the pillar has no
     * ASC, so it contributes +0 AttackPower and only -Defense has to be undone.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    float CollapseDamageFraction = 0.30f;

    /** Flat fallback, used only for targets whose MaxHealth can't be read. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    float CollapseDamage = 80.f;

    /**
     * How far up from the pillar's mesh BASE the collapse damage volume
     * reaches (cm). Anything standing on the floor under the falling section
     * has to fall inside this, so it wants to comfortably clear a player
     * capsule rather than hug it.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    float CollapseVolumeHeight = 400.f;

    /** Half-width of the collapse damage volume's square footprint (cm). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    float CollapseVolumeHalfWidth = 200.f;

    /** GE applied to players caught in collapse. Uses Data.Damage SetByCaller. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    TSubclassOf<UGameplayEffect> CollapseDamageEffect;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Arena|Visuals")
    TObjectPtr<UMaterialInterface> CrackedMaterial;

    /**
     * How often EnableBlockingVolume re-checks whether a pawn has stepped out
     * of the blocking volume (seconds).
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    float BlockingVolumeRetryInterval = 0.5f;

    /**
     * Total time the blocking volume will wait for a pawn to clear it before
     * giving up for good (seconds).
     *
     * There has to be a ceiling. Switching the volume on under a standing pawn
     * ejects them, so waiting is right — but waiting forever is a retry loop
     * with no failure state, and that is what it looked like in the log: 2Hz
     * deferrals still going past attempt 14 with no stated bound. At the end of
     * this window the volume stays disabled and a single Warning explains that
     * the zone is now permanently walkable.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Arena")
    float BlockingVolumeMaxWaitSeconds = 10.f;

    // -------------------------------------------------------------------------
    // Blueprint events for VFX/SFX
    // -------------------------------------------------------------------------

    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Arena")
    void OnPillarCracked();

    /**
     * The tell. Fires the instant the pillar breaks, WarningDuration before the
     * ceiling lands. Implement the dust plume, the groan of stressed stone, the
     * decal on the floor under the falling section — anything that tells the
     * player which patch of ground is about to become lethal.
     *
     * Nothing damages anyone at this point. That is the entire purpose.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Arena")
    void OnPillarCollapseWarning();

    /** The impact cue. Fires WHEN the slab lands, together with the damage. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Gothic|Arena")
    void OnPillarCollapse();

private:
    float CurrentHealth = 0.f;
    EPillarState CurrentState = EPillarState::Healthy;

    /** Guards the once-per-collapse damage pass. */
    bool bCollapseDamageApplied = false;

    /** Retry counter for the deferred blocking-volume activation. */
    int32 BlockingVolumeAttempts = 0;

    FTimerHandle CollapseWarningTimer;
    FTimerHandle BlockingVolumeTimer;

    void TransitionToState(EPillarState NewState);

    /**
     * Puts the collapse damage volume on the floor, under the pillar.
     *
     * It cannot be authored as a fixed offset from the root. PillarMesh IS the
     * root and its pivot is the mesh's CENTRE (see the PillarMesh comment
     * above and the constructor), so the actor origin sits at half the pillar's
     * scaled height. The Rotunda's pillars are placed at Z=800 over a Z~-110
     * floor at (3,3,20) scale, which put the constructor's root-relative
     * +/-300Z box at Z 500-1100 — a slab of empty air roughly nine metres above
     * anybody's head. The collapse was unmissable and undodgeable at the same
     * time: it could never hit anyone.
     *
     * So derive it instead: take the mesh's world bounds, find the bottom, and
     * sit a CollapseVolumeHeight box on top of that. Correct at any pillar
     * height, any scale, any placement Z, with nothing to keep in sync.
     */
    void PositionCollapseDamageVolumeFromMeshBase();

    /** Break the pillar, fire the tell, and arm the impact timer. No damage. */
    void BeginCollapseWarning();

    /** Impact: slab drops, damage lands, OnPillarCollapse fires. All one frame. */
    void FinishCeilingCollapse();

    /** Deferred, and refuses to switch on while a pawn is standing inside it. */
    void EnableBlockingVolume();

    void ApplyCollapseDamageAtImpact();
};