// GothicHintManagerComponent.h
// The tutorial hint latch and queue. One hint on screen at a time, each shown
// at most once per session, raised by whatever the player is about to need
// rather than by a menu they have to read first.
//
// A COMPONENT ON THE PLAYER PAWN, not a world subsystem. The choice is forced
// by two things and only looks arbitrary from outside:
//   - Every trigger source is pawn-local. The ASC attribute delegates, the
//     inventory on the PlayerState, the weapon slots, the HUD reached through
//     the owning PlayerController — a UWorldSubsystem is one per world and would
//     have to be handed a pawn on every single call to know whose Steadfast just
//     filled. In listen-server PIE that pawn is ambiguous by construction.
//   - The hint surface is per-player. Two players in one world must not share a
//     seen-set; a world subsystem shares one by definition.
//
// The seen-set survives respawn because it is stashed on the PlayerState, which
// outlives the pawn. "Per session" means per play session, not per life — dying
// should not re-teach you how to walk, and the downed/revive loop makes that a
// real path rather than a hypothetical one.
//
// Runs on the OWNING CLIENT only. A hint is a screen element; there is nothing
// here for the server to be authoritative about, and the component early-outs
// on anything that is not locally controlled.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
// Not a forward declaration: FGothicItemInstance and EGothicEquipSlot are
// parameters of UFUNCTION-marked delegate handlers, and UHT needs the whole
// type to generate their thunks.
#include "Items/GothicItemTypes.h"
#include "GothicHintManagerComponent.generated.h"

class AGothicHUD;
class UGothicInventoryComponent;

/** One hint waiting its turn, or the one currently on screen. */
USTRUCT()
struct FGothicQueuedHint
{
    GENERATED_BODY()

    UPROPERTY()
    FGameplayTag Tag;

    UPROPERTY()
    FText Copy;
};

UCLASS(ClassGroup = (Gothic), meta = (BlueprintSpawnableComponent))
class GOTHICMMO_API UGothicHintManagerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGothicHintManagerComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /**
     * Raise a hint. No-ops if the tag has already been seen this session, if the
     * tag has no copy authored, or if this is not the locally controlled pawn.
     *
     * Marks the tag seen at ENQUEUE time, not at display time. A hint that is
     * queued behind two others has already been earned; re-raising it while it
     * waits must not put a second copy in the queue.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Hints")
    void ShowHint(FGameplayTag HintTag);

    /**
     * Blueprint/level-facing alias for ShowHint. Exists so AGothicHintTrigger and
     * any Blueprint beat can drive the hints that have no natural C++ call site
     * (Fire, Aim, Melee, the three abilities, Collect) without those triggers
     * needing to know about the component's internals.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Hints")
    void ShowHintByTag(FGameplayTag HintTag) { ShowHint(HintTag); }

    /**
     * The player did the thing. Dismisses the hint early if it is the one on
     * screen, and drops it from the queue if it is still waiting.
     *
     * Deliberately separate from ShowHint's seen-set: a hint the player answered
     * and a hint that merely timed out are both "seen", but only the first is
     * worth cutting short.
     */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Hints")
    void NotifyHintActionPerformed(FGameplayTag HintTag);

    /** True if this tag has already had its turn on screen this session. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Hints")
    bool HasSeenHint(FGameplayTag HintTag) const;

    /** Clears the seen-set. Test affordance — nothing in the game calls it. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Hints")
    void ResetSeenHints();

    /**
     * Human-readable name for a bound key, for hint copy that has to name a
     * button. "LeftMouseButton" is a UE identifier, not something to put in
     * front of a player.
     *
     * A function and a lookup table rather than a data table: nineteen hints
     * reference maybe a dozen keys, and a DataTable would add an asset, a row
     * struct and a load-time dependency to save nothing. Anything not in the
     * table falls through to FKey::GetDisplayName(), which is already correct
     * for letter and number keys.
     */
    UFUNCTION(BlueprintPure, Category = "Gothic|Hints")
    static FString KeyDisplayName(const FKey& Key);

    // -------------------------------------------------------------------------
    // Opener input flags — set from the pawn's input handlers so the opener
    // timer can tell "hasn't been taught" from "already worked it out".
    // -------------------------------------------------------------------------
    void NotifyMoveInput();
    void NotifyLookInput();

protected:
    /**
     * Hint copy, keyed by tag. EditDefaultsOnly and FText so the user can redline
     * every line in BP_GothicPlayerCharacter without a recompile — which is the
     * whole reason the strings are not literals at the call sites.
     *
     * A tag with no entry here shows nothing. That is the intended way to mute a
     * hint: clear its copy, leave the trigger alone.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Gothic|Hints")
    TMap<FGameplayTag, FText> HintCopy;

    /** Seconds a hint stays up if the player never performs the taught action. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|Hints")
    float HintDuration = 5.f;

    /** Gap between one hint clearing and the next being drawn. Stops a flicker-swap. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|Hints")
    float HintGap = 0.35f;

    /** Delay after BeginPlay before the first movement opener is considered. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|Hints|Openers")
    float OpenerFirstDelay = 2.f;

    /** Gap between consecutive movement openers. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|Hints|Openers")
    float OpenerInterval = 6.f;

    /** Set false to silence the Move/Look/Jump/Sprint opener sequence entirely. */
    UPROPERTY(EditDefaultsOnly, Category = "Gothic|Hints|Openers")
    bool bRunMovementOpeners = true;

private:
    /** Pulls the next hint off the queue and puts it on the HUD. */
    void DisplayNextHint();

    /** Timer target — the current hint outstayed its welcome. */
    void OnHintExpired();

    /** Clears the HUD slot and schedules the next hint after HintGap. */
    void DismissCurrentHint();

    /** One step of the Move/Look/Jump/Sprint opener sequence. */
    void TickMovementOpeners();

    AGothicHUD* GetOwningHUD() const;

    /** True only on the pawn this machine's player is driving. */
    bool IsLocalPlayerPawn() const;

    /** Marks a tag seen, in the PlayerState if there is one and locally regardless. */
    void MarkHintSeen(FGameplayTag HintTag);

    /**
     * Local mirror of the seen-set. The authority for it is
     * AGothicPlayerState::SeenTutorialHints, which survives the respawn that
     * destroys this component; this copy exists so a hint raised before the
     * PlayerState is resolved still latches instead of repeating.
     */
    UPROPERTY(Transient)
    TSet<FGameplayTag> SeenHints;

    UPROPERTY(Transient)
    TArray<FGothicQueuedHint> PendingHints;

    UPROPERTY(Transient)
    FGameplayTag CurrentHintTag;

    FTimerHandle HintTimerHandle;
    FTimerHandle OpenerTimerHandle;

    /** How many openers have fired. Indexes the Move/Look/Jump/Sprint sequence. */
    int32 OpenerStep = 0;

    bool bHasMoved = false;
    bool bHasLooked = false;

    // -------------------------------------------------------------------------
    // Inventory triggers. Bound here rather than in the pawn's own equip path so
    // the hint system stays one file — the pawn already carries enough.
    // -------------------------------------------------------------------------
    void BindInventoryDelegates();

    UFUNCTION()
    void HandleItemAdded(const FGothicItemInstance& Item);

    UFUNCTION()
    void HandleItemEquipped(EGothicEquipSlot Slot, const FGothicItemInstance& Item);

    UPROPERTY(Transient)
    TWeakObjectPtr<UGothicInventoryComponent> BoundInventory;

    /** Retry handle for the inventory bind — the PlayerState arrives after BeginPlay. */
    FTimerHandle InventoryBindRetryHandle;
};
