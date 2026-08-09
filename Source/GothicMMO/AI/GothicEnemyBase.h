// GothicEnemyBase.h
// Base class for all enemy types (Thrall, Retained, Feral, etc.).
// Unlike players, enemies host their own ASC directly — they don't persist.
// The AI Controller drives behavior; the ASC handles damage/buffs/debuffs.
//
// Attack model:
//   Enemies carry a GothicMeleeHitboxComponent attached to a weapon/hand bone.
//   Attack montages use AnimNotifyState_MeleeHitbox to open/close the damage
//   window. The hitbox applies damage via the standard GAS pipeline on overlap.
//   Binary range-check damage is gone — the player can dodge.
//
// Blueprint children: BP_Enemy_Thrall, BP_Enemy_Retained, etc.
// Each sets its own DefaultAttributeEffect, StartupAbilities, and hitbox tuning.

#pragma once

#include "CoreMinimal.h"
#include "Character/GothicCharacterBase.h"
#include "Perception/AIPerceptionTypes.h"
#include "GothicEnemyBase.generated.h"

class UAIPerceptionComponent;
class UWidgetComponent;
class UGothicLootTable;
class UGothicMeleeHitboxComponent;
class UGothicVitalPointComponent;
class AGothicEncounterVolume;

// Forward-declared here so both GothicEnemyBase and GothicEncounterVolume
// resolve the same delegate type from a single declaration.
class AGothicEnemyBase;  // needed before the delegate macro expands

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnEnemyDied,
    AGothicEnemyBase*, DeadEnemy);

// EEnemyTier, EnemyTier and ExperienceReward were removed here. The enum
// claimed to affect "loot tables and XP reward" and did neither: loot is driven
// entirely by the per-Blueprint LootTable asset below, and no code path in the
// project ever awarded experience — GetExperienceReward and GetEnemyTier both
// had zero callers in C++ and zero references in any Blueprint graph. All three
// BPs authored values (Minion/50, Elite/50, Boss/300) that nothing read.
//
// Nothing is stubbed in their place on purpose. When XP exists it will want a
// real progression design, and a dead enum with four unread tiers is worse than
// an empty space to design into.

UCLASS(Abstract)
class GOTHICMMO_API AGothicEnemyBase : public AGothicCharacterBase
{
    GENERATED_BODY()

public:
    AGothicEnemyBase();

    virtual void BeginPlay() override;

    // IGothicCombatInterface override — enemies ragdoll and drop loot on death.
    virtual void OnDeath_Implementation(AActor* Killer) override;

    /**
     * Called by the AI Controller when combat is entered/exited.
     *
     * Ignored while the controller is leashing home — see
     * AGothicEnemyAIController::IsAcceptingCombatTargets. This is the single
     * choke point every aggro source shares (perception, encounter volumes,
     * pack propagation), so gating it here is what makes a disengage stick.
     *
     * It is also where fightability is enforced: an unfightable target (dead, or
     * a DOWNED player) is refused outright rather than latched, through the
     * shared AGothicCharacterBase::IsFightableActor. Callers do not need to
     * pre-filter — and must not rely on being able to skip this by calling in
     * with a target they know is down. A null passes through as a clear.
     *
     * This one-argument form reports itself as "unspecified" in the TargetAcquire
     * line; prefer the overload below wherever the route has a name.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Enemy")
    void SetCombatTarget(AActor* NewTarget);

    /**
     * As above, plus the name of the acquisition route for the timeline.
     *
     * TargetAcquire used to be logged from exactly one site (the leash tick's
     * rescan), so six other routes into this function — perception, retaliation,
     * the encounter volume's overlap aggro and its wave hand-off, the pack rally,
     * and the possess-time flush — set a target without leaving a trace, and a log
     * read as "this enemy acquired once" when it had acquired seven times. Logging
     * at the choke point instead means a route can only be invisible by being
     * genuinely absent.
     *
     * C++-only overload: a UFUNCTION cannot take a const TCHAR*, and the string is
     * a compile-time literal at every call site rather than data. Blueprint keeps
     * the one-argument form.
     */
    void SetCombatTarget(AActor* NewTarget, const TCHAR* Via);

    /**
     * Drops the pawn-side combat latch.
     *
     * Nothing cleared CombatTarget before this existed. Because
     * GothicBTService_CombatSync reconciles the pawn against the Blackboard at
     * 5Hz, a latch left set re-created the Blackboard target within 0.2s of any
     * disengage — which is why the leash in AGothicEnemyAIController never once
     * held. Call the controller's ClearCombatTarget rather than this directly
     * unless you specifically want only the pawn half.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Enemy")
    void ClearCombatTarget() { CombatTarget = nullptr; }

    UFUNCTION(BlueprintPure, Category = "Gothic|Enemy")
    AActor* GetCombatTarget() const { return CombatTarget; }

    /**
     * Damage retaliation — the acquisition source that does not depend on
     * perception or on an encounter volume's trigger box.
     *
     * Called from UGothicAttributeSet::PostGameplayEffectExecute once damage has
     * actually landed (post-evasion, post-mitigation), with the effect context's
     * ORIGINAL INSTIGATOR AVATAR — the shooter's pawn, not its PlayerState, since
     * UGothicAbilitySystemComponent::MakeDamageContext resolves the avatar.
     *
     * Nothing in any damage path set a combat target before this existed: an
     * enemy could be shot to death without ever noticing it was being shot. That
     * was masked while GothicBTService_CombatSync restored targets from the
     * never-cleared pawn latch; once the leash made disengage real, an enemy that
     * never perceived the player was inert no matter how much damage it took.
     *
     * Routes through SetCombatTarget deliberately, so retaliation is still
     * refused while the controller is leashing home. Being shot in the back on
     * the way to the anchor must not cancel a disengage — the leash is what makes
     * the boss reach her anchor at all.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Enemy")
    void NotifyDamagedBy(AActor* DamageInstigator);

    /** Returns the hitbox component for direct access from BT tasks if needed. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Combat")
    UGothicMeleeHitboxComponent* GetMeleeHitbox() const { return MeleeHitbox; }

    // -----------------------------------------------------------------
    // Encounter integration — the encounter volume binds to OnEnemyDied
    // to track roster deaths without polling.
    // -----------------------------------------------------------------

    /** Broadcast from OnDeath — the encounter volume listens to this. */
    UPROPERTY(BlueprintAssignable, Category = "Gothic|Enemy")
    FOnEnemyDied OnEnemyDied;

    /** Set by the encounter volume in BeginPlay. Null for free-roaming enemies. */
    UPROPERTY()
    TObjectPtr<AGothicEncounterVolume> OwningEncounter;

    // -----------------------------------------------------------------
    // Accursed identity — revealed during the Selah moment, not combat.
    // -----------------------------------------------------------------

    /** The human name this Accursed had before the Bleed took them.
     *  Set per-instance in the level or per-Blueprint default. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gothic|Lore")
    FText AccursedName;

    UFUNCTION(BlueprintPure, Category = "Gothic|Lore")
    FText GetAccursedName() const { return AccursedName; }

    /**
     * Fill AccursedName with a generated period name when it has been left empty.
     *
     * On by default because the Thralls are the many, and hand-naming a plaza of
     * 45 of them is not work anyone should do — a measured Encounter 1 revealed
     * 12 names out of 25 kills, and the blanks were simply unnamed spawns. Wave
     * enemies especially cannot be named by hand: they do not exist until the
     * encounter spawns them.
     *
     * CLEAR THIS on the named Accursed. The Feral Retained and the Bestial Lucid
     * are written characters, not generated ones, and a random name would
     * overwrite the authored beat the Selah moment is built around.
     *
     * An authored AccursedName always wins regardless of this flag — generation
     * only ever fills a blank, so it can never clobber a name someone typed.
     *
     * Read at runtime from the CLASS DEFAULT rather than from the placed actor, so
     * clearing it on a Blueprint takes effect on actors already in the level. See
     * the note in BeginPlay.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Lore")
    bool bGenerateAccursedName = true;

    /**
     * Builds one name from the period tables. Deterministic in Seed, so the same
     * Accursed is the same person on every machine and across every run — the
     * name is identity, not decoration, and a corpse that renamed itself between
     * a client and the server would be worse than an unnamed one.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Lore")
    static FText MakeAccursedName(int32 Seed);

    // -----------------------------------------------------------------
    // Pack coordination — optional stamp for Thrall pack grouping.
    // -----------------------------------------------------------------

    /** Moves this enemy between packs, keeping GothicPackSubsystem's registry in
     *  step. Was an inline setter that only wrote the field — so the subsystem's
     *  Packs map stayed permanently empty and no pack ever regrouped. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|AI")
    void SetPackID(FName NewPackID);

    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    FName GetPackID() const { return PackID; }

    /** Public read access for encounter volume Selah accumulation. */
    float GetSelahAwardAmount() const { return SelahAwardAmount; }
    TSubclassOf<UGameplayEffect> GetSelahGainEffect() const { return SelahGainEffect; }

    /**
     * Spawner hook — wave-spawned enemies can't be hand-edited in the level, so
     * the spawn point's loot suppression is stamped here post-spawn. The flag is
     * only read at death, so post-BeginPlay assignment is safe.
     */
    void SetSuppressLootDrop(bool bSuppress) { bSuppressLootDrop = bSuppress; }
    
    /**
     * Multicast RPC — server broadcasts hit feedback to all clients.
     *
     * DamageInstigator is the actor that dealt the hit (the ability's avatar).
     * It exists so the receiving machine can tell a shooter-scoped effect (the
     * floating damage number, which belongs only to whoever fired) from a
     * viewer-scoped one (the health bar, which every local viewer earns). Pass
     * null for damage with no player behind it — nobody is credited and no
     * number is drawn.
     */
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastOnHit(FVector HitLocation, bool bVitalHit, float DamageAmount, AActor* DamageInstigator);
    
    /** Per-enemy regroup pause duration after a pack member dies. Set in Blueprint. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|AI")
    float PackRegroupDuration = 1.5f;

    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    float GetPackRegroupDuration() const { return PackRegroupDuration; }

    /**
     * The pawn's perception component, so the controller can ask what is being
     * perceived RIGHT NOW rather than waiting for the next perception edge.
     *
     * Perception lives on the pawn in this project, not on the controller, so
     * AGothicEnemyAIController::FindBestPerceivedTarget has no other route to it.
     * Read-only by intent — the controller queries it and never reconfigures it.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|AI")
    UAIPerceptionComponent* GetPerceptionComponent() const { return PerceptionComponent; }

protected:
    // -------------------------------------------------------------------------
    // Components
    // -------------------------------------------------------------------------

    /** For perceiving players (sight + hearing). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|AI")
    TObjectPtr<UAIPerceptionComponent> PerceptionComponent;

    /** World-space health bar widget, visible to all nearby players. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|UI")
    TObjectPtr<UWidgetComponent> HealthBarWidget;

    /**
     * Melee attack hitbox — attached to weapon/hand bone in Blueprint.
     * Anim notifies control the damage window.
     * Set DamageEffect, BaseDamage, and box extent per-enemy in Blueprint.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Combat")
    TObjectPtr<UGothicMeleeHitboxComponent> MeleeHitbox;

    /** Moving vital point + amber overlay. Configure BoneName locations and
     *  VitalOverlayMaterial per enemy Blueprint (rig-agnostic). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Combat")
    TObjectPtr<UGothicVitalPointComponent> VitalPointComponent;

    // -------------------------------------------------------------------------
    // Data — set in Blueprint
    // -------------------------------------------------------------------------

    /**
     * Delay before the corpse is destroyed (seconds).
     * Gives time for death animations and loot interaction.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Enemy")
    float CorpseLifetime = 10.f;

    /**
     * Loot table for this enemy type. Assign per Blueprint child.
     * On death, RollDrop() picks a weighted random item and spawns a world pickup.
     *
     * EditANYWHERE, changed from EditDefaultsOnly. Per-instance override is now
     * the point: the Palewood progression designates ONE placed Thrall as the
     * First Blood kill that guarantees the Piece, and it carries
     * DA_LootTable_PalewoodPiece on the instance rather than in a Blueprint
     * subclass that would exist solely to hold one asset reference.
     *
     * Consequence, and it is a real one: placed instances freeze their overrides
     * at placement time, so an instance that has ever had this set no longer
     * inherits changes to its Blueprint's default table. That is the intended
     * trade for the designated enemy and a hazard for every other placed enemy —
     * leave it untouched on instances that should follow their class.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gothic|Enemy")
    TObjectPtr<UGothicLootTable> LootTable;

    /**
     * When true this enemy never drops loot, regardless of LootTable.
     *
     * EditAnywhere on purpose: set it per placed instance. Palewood's tutorial
     * enemies are meant to teach without paying out — the Piece comes from one
     * designated kill and nowhere else — and suppressing at the instance keeps
     * the shared Thrall loot table untouched for the rest of the game.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gothic|Enemy")
    bool bSuppressLootDrop = false;

    /**
     * Halts or resumes this enemy when State.Stunned is gained or lost.
     *
     * State.Stunned previously did nothing to an enemy. It drove an animation
     * flag on GothicEnemyAnimInstance and appeared in a couple of PLAYER
     * abilities' ActivationBlockedTags, so an enemy that "got stunned" played a
     * stun pose while continuing to path and swing. This stops the behaviour
     * tree and the movement so the tag means what it looks like.
     */
    UFUNCTION()
    void HandleStunTagChanged(const FGameplayTag CallbackTag, int32 NewCount);

    /**
     * Lights or darkens this enemy's Read mark as State.Read.Marked comes and goes.
     *
     * Cosmetic only, and deliberately driven off the TAG rather than a timer of
     * its own: GE_ReadMark (target) and GE_ReadState (caster's HUD proc icon) are
     * both 4.0s duration today, and a third clock would be one more thing that
     * can disagree with them. Binding to the tag's lifetime keeps the glow, the
     * mark and the icon in sync by construction — including early removal.
     *
     * Runs on every machine, unlike HandleStunTagChanged: this changes nothing
     * authoritative, and a mark only the server can see is not a tell. The enemy
     * ASC replicates in Minimal mode, which still carries granted tags to clients.
     */
    UFUNCTION()
    void HandleReadMarkTagChanged(const FGameplayTag CallbackTag, int32 NewCount);

    // SelahAwardRadius (500uu) and AwardSelahToNearbyEmbers() lived here. The
    // per-kill award was replaced by encounter-based collection; the function had
    // no callers and was not BlueprintCallable, so nothing could reach it. Both
    // removed so the remaining Selah surface is the one that actually runs.

    /** Amount of Selah awarded to each nearby player on death. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Selah")
    float SelahAwardAmount = 1.f;

    /** The GameplayEffect that awards Selah. Assign GE_SelahGain in BP_Enemy_Draugr. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Selah")
    TSubclassOf<UGameplayEffect> SelahGainEffect;

    /**
     * Bone name to attach the hitbox to.
     * Override in Blueprint per-enemy type.
     * Common values: "weapon_r", "hand_r", "RightHand"
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Combat")
    FName HitboxAttachBone = FName("hand_r");

    /**
     * How fast (deg/sec) the enemy turns to keep facing its focus target.
     * Higher tracks harder; lower is more flankable. A big boss wants this
     * lowish (~180) so getting behind her is real counterplay; trash can be
     * snappier. Applied to RotationRate.Yaw in BeginPlay.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Combat")
    float TurnRateDegrees = 300.f;

    /**
     * Whether taking damage from a NEW attacker switches this enemy off the
     * target it is already fighting.
     *
     * Default false, derived from the acquisition problem this fix is for rather
     * than from a combat-feel preference: the failure being repaired is an enemy
     * with NO target, and false is the value that repairs exactly that and
     * nothing else. True re-targets on every hit, which in co-op makes an enemy
     * flip between two players at the rate they fire — at the player's measured
     * ~39 DPS against Thralls that is several switches per second, and a pawn
     * that re-picks its focus that often never closes on anyone.
     *
     * Retaliation always fires when the current target is null or already dead,
     * regardless of this flag.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gothic|Combat")
    bool bRetaliationOverridesCurrentTarget = false;

    /**
     * Edge-trigger for the RetaliationReject timeline line, and nothing else — no
     * behaviour reads it.
     *
     * Retaliation is evaluated per damage event, and a damage event is per ROUND at
     * the player's fire rate, so an unlatched log here writes several identical
     * lines a second while a burst lands. Holding the last rejected attacker means
     * the refusal prints on the transition and stays quiet afterwards; it clears
     * when a target is actually taken, so the next real refusal still shows up.
     *
     * Weak, not raw: this outlives the attacker's pawn across a respawn, and it must
     * never be the thing keeping one alive.
     */
    TWeakObjectPtr<AActor> LastRetaliationRejectAttacker;

    /**
     * Edge-trigger for the AcquireReject timeline line, and nothing else — no
     * behaviour reads it.
     *
     * Same shape and same reason as LastRetaliationRejectAttacker above, against a
     * faster source: perception re-senses a downed body every stimulus refresh, so
     * an unlatched reject line writes continuously for as long as the body lies in
     * view. Holding the last refused actor prints the refusal once. It releases
     * only when that actor is ACCEPTED — not on any accepted target — so an enemy
     * alternating between the body and the survivor stays quiet.
     *
     * Weak, not raw: a refused actor is by definition one on its way out (downed or
     * dead), and this must never be the thing keeping it alive.
     */
    TWeakObjectPtr<AActor> LastAcquireRejectTarget;

    /**
     * Whether losing sight of the combat target for TargetForgetSeconds drops it.
     *
     * DEFAULT FALSE, and that is a design decision waiting on a playtest rather
     * than a value derived from anything measurable — see the note below.
     *
     * The mechanism exists because OnPerceptionUpdated had no lost-perception path
     * at all: it only ever SET targets, which left the leash timer as the single
     * route out of combat in the entire AI stack. But turning it on is not
     * obviously correct. The perception config is LoseSightRadius 2000 with
     * SetMaxAge 5, and the fights this would run in are pillared arenas — the
     * Rotunda above all. Every time the player breaks line of sight behind a
     * pillar the stimulus expires, so an eager forget would have the Bestial Lucid
     * disengage from a player standing ten metres away with a column between them.
     *
     * The two disengage routes that ARE unambiguously correct — the target dying,
     * and this enemy dying — run unconditionally and do not depend on this flag.
     * The leash already covers "the player left the arena".
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gothic|Combat")
    bool bForgetTargetOnPerceptionLoss = false;

    /**
     * How long the target may go unsensed before it is dropped (seconds).
     *
     * 8 is a floor, not a tuning result: the sight config's own SetMaxAge is 5, so
     * anything at or below that would fire on the stimulus expiry itself and make
     * the delay meaningless. It wants a playtest behind a pillar before it is
     * trusted — measure how long a real LOS break lasts in the Rotunda and set
     * this above the worst case.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gothic|Combat")
    float TargetForgetSeconds = 8.f;

private:
    UPROPERTY()
    TObjectPtr<AActor> CombatTarget;

    /** Pack group identifier — enemies with the same PackID coordinate. */
    UPROPERTY(EditAnywhere, Category = "Gothic|AI")
    FName PackID = NAME_None;

    UFUNCTION()
    void OnPerceptionUpdated(const TArray<AActor*>& UpdatedActors);

    /** Arms the forget countdown when the current target stops being sensed. */
    void HandleTargetPerceptionLost();

    /** Countdown expiry — drops the target if perception still cannot see it. */
    void ForgetTargetIfStillUnseen();

    /** Handle for the lost-perception forget countdown. */
    FTimerHandle ForgetTargetTimer;

    /** Delayed destruction after death animation plays out. */
    void DestroyCorpse();
    void SpawnLootDrop();
};