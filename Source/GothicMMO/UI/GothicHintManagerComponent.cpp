// GothicHintManagerComponent.cpp

#include "UI/GothicHintManagerComponent.h"

#include "AbilitySystem/GothicGameplayTags.h"
#include "Character/GothicPlayerCharacter.h"
#include "Game/GothicPlayerState.h"
#include "GothicMMO.h"
#include "Items/GothicInventoryComponent.h"
#include "Items/GothicItemDefinition.h"
#include "Items/GothicItemTypes.h"
#include "UI/GothicHUD.h"

#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

UGothicHintManagerComponent::UGothicHintManagerComponent()
{
    // Nothing here ticks. Everything is delegate- or timer-driven, and a hint
    // system that costs a tick per frame per player to show nothing is a tick
    // per frame per player wasted.
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(false);

    // ── DRAFT COPY — FOR REDLINE ─────────────────────────────────────────────
    // Every line below is a placeholder written to a house rule, not final text:
    // action first, key named inline, one clause, no second sentence, and never
    // the word "press" where the key alone reads as an instruction.
    //
    // These are DEFAULTS. The property is EditDefaultsOnly and FText, so the
    // authored pass happens in BP_GothicPlayerCharacter with no recompile and no
    // engineer in the loop — which is the whole reason the strings are not
    // literals at the twelve call sites that raise them.
    //
    // Keys are hardcoded here rather than resolved through KeyDisplayName,
    // because the defaults have to exist before any mapping context is loaded.
    // KeyDisplayName is for copy that is composed at runtime from a live binding;
    // a rebinding pass over these lines is a known follow-up, not an oversight.
    HintCopy = {
        { GothicTags::Hint_Move,             NSLOCTEXT("GothicHints", "Move",        "W A S D — move") },
        { GothicTags::Hint_Look,             NSLOCTEXT("GothicHints", "Look",        "Mouse — look around") },
        { GothicTags::Hint_Jump,             NSLOCTEXT("GothicHints", "Jump",        "Space — jump") },
        { GothicTags::Hint_Sprint,           NSLOCTEXT("GothicHints", "Sprint",      "Shift — sprint") },
        { GothicTags::Hint_SprintLowersGun,  NSLOCTEXT("GothicHints", "SprintCost",  "Sprinting lowers your gun — you cannot fire while running") },
        { GothicTags::Hint_Fire,             NSLOCTEXT("GothicHints", "Fire",        "LMB — fire") },
        { GothicTags::Hint_Aim,              NSLOCTEXT("GothicHints", "Aim",         "Hold RMB — steady your aim") },
        { GothicTags::Hint_Melee,            NSLOCTEXT("GothicHints", "Melee",       "F — strike what is already on you") },
        { GothicTags::Hint_Reload,           NSLOCTEXT("GothicHints", "Reload",      "R — reload") },
        { GothicTags::Hint_SteadfastConvert, NSLOCTEXT("GothicHints", "Steadfast",   "Hold R — convert Steadfast to ammo") },
        { GothicTags::Hint_Interact,         NSLOCTEXT("GothicHints", "Interact",    "E — interact") },
        { GothicTags::Hint_Inventory,        NSLOCTEXT("GothicHints", "Inventory",   "I — open your inventory") },
        { GothicTags::Hint_Equip,            NSLOCTEXT("GothicHints", "Equip",       "Equip it from the inventory to carry it") },
        { GothicTags::Hint_WeaponSwap,       NSLOCTEXT("GothicHints", "WeaponSwap",  "1 2 3 — change weapons") },
        { GothicTags::Hint_AbilitySlicer,    NSLOCTEXT("GothicHints", "Slicer",      "Q — throw the Slicer") },
        { GothicTags::Hint_AbilityRead,      NSLOCTEXT("GothicHints", "Read",        "C — Read the Accursed for its vital point") },
        { GothicTags::Hint_AbilityLunge,     NSLOCTEXT("GothicHints", "Lunge",       "V — lunge") },
        { GothicTags::Hint_Reckoning,        NSLOCTEXT("GothicHints", "Reckoning",   "G — unleash your Reckoning") },
        { GothicTags::Hint_Collect,          NSLOCTEXT("GothicHints", "Collect",     "E — take the Selah the dead left behind") },
    };
}

void UGothicHintManagerComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!IsLocalPlayerPawn())
    {
        // A remote player's pawn exists on this machine but draws no HUD here.
        return;
    }

    // Restore the session set. On a first life this is empty; after a respawn it
    // is everything the player has already been taught.
    if (const AGothicPlayerState* PS = GetOwner<APawn>()
        ? GetOwner<APawn>()->GetPlayerState<AGothicPlayerState>() : nullptr)
    {
        SeenHints = PS->GetSeenTutorialHints();
    }

    // The zone gate. GetCurrentLevelName strips the PIE "UEDPIE_0_" prefix, so
    // the same comparison holds in PIE, standalone and a cooked build.
    const FString CurrentMap = UGameplayStatics::GetCurrentLevelName(this);
    bHintsEnabled = HintEnabledMaps.ContainsByPredicate(
        [&CurrentMap](const FString& Allowed) { return Allowed.Equals(CurrentMap, ESearchCase::IgnoreCase); });

    UE_LOG(LogVigilCombat, Log, TEXT("Hint|map=%s|hints %s"),
        *CurrentMap, bHintsEnabled ? TEXT("enabled") : TEXT("disabled"));

    // Bound regardless of the gate. The bind is a retry loop that costs nothing
    // when it succeeds, and the delegates land on ShowHint, which is where the
    // gate is actually enforced — keeping one chokepoint rather than two.
    BindInventoryDelegates();
}

void UGothicHintManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(HintTimerHandle);
        World->GetTimerManager().ClearTimer(InventoryBindRetryHandle);
    }

    if (UGothicInventoryComponent* Inventory = BoundInventory.Get())
    {
        Inventory->OnItemAdded.RemoveDynamic(this, &UGothicHintManagerComponent::HandleItemAdded);
        Inventory->OnItemEquipped.RemoveDynamic(this, &UGothicHintManagerComponent::HandleItemEquipped);
    }
    BoundInventory = nullptr;

    // Leave the HUD slot clean. The HUD outlives the pawn, so a hint left on
    // screen at death would still be there through the respawn.
    if (AGothicHUD* HUD = GetOwningHUD())
    {
        HUD->ClearTutorialHint();
    }

    Super::EndPlay(EndPlayReason);
}

bool UGothicHintManagerComponent::IsLocalPlayerPawn() const
{
    const APawn* Pawn = GetOwner<APawn>();
    return Pawn && Pawn->IsLocallyControlled();
}

AGothicHUD* UGothicHintManagerComponent::GetOwningHUD() const
{
    const APawn* Pawn = GetOwner<APawn>();
    APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
    return PC ? Cast<AGothicHUD>(PC->GetHUD()) : nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
// Latch and queue
// ═══════════════════════════════════════════════════════════════════════════

bool UGothicHintManagerComponent::HasSeenHint(FGameplayTag HintTag) const
{
    return SeenHints.Contains(HintTag);
}

void UGothicHintManagerComponent::MarkHintSeen(FGameplayTag HintTag)
{
    SeenHints.Add(HintTag);

    if (APawn* Pawn = GetOwner<APawn>())
    {
        if (AGothicPlayerState* PS = Pawn->GetPlayerState<AGothicPlayerState>())
        {
            PS->MarkTutorialHintSeen(HintTag);
        }
    }
}

void UGothicHintManagerComponent::ResetSeenHints()
{
    SeenHints.Reset();

    if (APawn* Pawn = GetOwner<APawn>())
    {
        if (AGothicPlayerState* PS = Pawn->GetPlayerState<AGothicPlayerState>())
        {
            PS->ClearSeenTutorialHints();
        }
    }
}

void UGothicHintManagerComponent::ShowHint(FGameplayTag HintTag)
{
    if (!HintTag.IsValid() || !IsLocalPlayerPawn())
    {
        return;
    }

    // The zone gate lives here and nowhere else. Every raise in the project —
    // the pawn's Reckoning/Steadfast/Sprint/Reload beats, the HUD's, the
    // inventory delegates below, and AGothicHintTrigger's ShowHintByTag — funnels
    // through this one function, so the gate costs no caller a line.
    //
    // Returns WITHOUT marking the tag seen: outside Palewood the hint was never
    // shown, and latching it here would burn it for a later visit.
    if (!bHintsEnabled)
    {
        return;
    }

    if (SeenHints.Contains(HintTag))
    {
        return;
    }

    // No copy authored means the hint is muted on purpose (see HintCopy). Latch
    // it anyway: a muted hint that keeps re-entering the queue every time its
    // trigger fires is a muted hint doing work.
    const FText* Copy = HintCopy.Find(HintTag);
    if (!Copy || Copy->IsEmpty())
    {
        MarkHintSeen(HintTag);
        UE_LOG(LogVigilCombat, Verbose,
            TEXT("Hint|%s|no copy authored — latched and skipped"), *HintTag.ToString());
        return;
    }

    // Marked seen at ENQUEUE, not at display. Two triggers firing in the same
    // frame for the same tag must produce one hint, not two.
    MarkHintSeen(HintTag);

    FGothicQueuedHint& Queued = PendingHints.AddDefaulted_GetRef();
    Queued.Tag  = HintTag;
    Queued.Copy = *Copy;

    UE_LOG(LogVigilCombat, Verbose, TEXT("Hint|%s|queued (depth=%d)"),
        *HintTag.ToString(), PendingHints.Num());

    // Only start the pump if nothing is on screen and nothing is mid-gap.
    if (!CurrentHintTag.IsValid()
        && !GetWorld()->GetTimerManager().IsTimerActive(HintTimerHandle))
    {
        DisplayNextHint();
    }
}

void UGothicHintManagerComponent::DisplayNextHint()
{
    if (PendingHints.Num() == 0)
    {
        CurrentHintTag = FGameplayTag();
        return;
    }

    const FGothicQueuedHint Next = PendingHints[0];
    PendingHints.RemoveAt(0);

    CurrentHintTag = Next.Tag;

    if (AGothicHUD* HUD = GetOwningHUD())
    {
        HUD->SetTutorialHint(Next.Copy);
    }

    GetWorld()->GetTimerManager().SetTimer(
        HintTimerHandle,
        this,
        &UGothicHintManagerComponent::OnHintExpired,
        HintDuration,
        false);

    UE_LOG(LogVigilCombat, Verbose, TEXT("Hint|%s|shown for %.1fs"),
        *CurrentHintTag.ToString(), HintDuration);
}

void UGothicHintManagerComponent::OnHintExpired()
{
    DismissCurrentHint();
}

void UGothicHintManagerComponent::DismissCurrentHint()
{
    CurrentHintTag = FGameplayTag();

    if (AGothicHUD* HUD = GetOwningHUD())
    {
        HUD->ClearTutorialHint();
    }

    GetWorld()->GetTimerManager().ClearTimer(HintTimerHandle);

    if (PendingHints.Num() > 0)
    {
        // A short blank gap, so a queue of three does not read as one line of
        // text mutating twice.
        GetWorld()->GetTimerManager().SetTimer(
            HintTimerHandle,
            this,
            &UGothicHintManagerComponent::DisplayNextHint,
            HintGap,
            false);
    }
}

void UGothicHintManagerComponent::NotifyHintActionPerformed(FGameplayTag HintTag)
{
    // Safe to call outside a hint-enabled map — the queue is empty and nothing
    // is on screen there, so this is a no-op either way. Stated rather than
    // relied on, so the callers in the pawn need no gate of their own.
    if (!HintTag.IsValid() || !bHintsEnabled)
    {
        return;
    }

    if (CurrentHintTag == HintTag)
    {
        UE_LOG(LogVigilCombat, Verbose, TEXT("Hint|%s|dismissed early — action performed"),
            *HintTag.ToString());
        DismissCurrentHint();
        return;
    }

    // Still waiting its turn: the player has already worked it out, so it never
    // needs to be drawn. It is already in SeenHints from the enqueue.
    PendingHints.RemoveAll([HintTag](const FGothicQueuedHint& Q) { return Q.Tag == HintTag; });
}

// ═══════════════════════════════════════════════════════════════════════════
// First-input dismissal
//
// Move/Look/Jump/Sprint were once raised by a timer chain that ran on BeginPlay
// in every level. They now come from AGothicHintTrigger volumes placed along the
// Palewood opening, which teach in the order the ground actually presents the
// problem. What survives here is the dismissal half: once the player moves or
// looks, the corresponding hint is answered and comes down early.
// ═══════════════════════════════════════════════════════════════════════════

void UGothicHintManagerComponent::NotifyMoveInput()
{
    if (!bHasMoved)
    {
        bHasMoved = true;
        NotifyHintActionPerformed(GothicTags::Hint_Move);
    }
}

void UGothicHintManagerComponent::NotifyLookInput()
{
    if (!bHasLooked)
    {
        bHasLooked = true;
        NotifyHintActionPerformed(GothicTags::Hint_Look);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Inventory triggers
// ═══════════════════════════════════════════════════════════════════════════

void UGothicHintManagerComponent::BindInventoryDelegates()
{
    if (BoundInventory.IsValid())
    {
        return;
    }

    APawn* Pawn = GetOwner<APawn>();
    AGothicPlayerState* PS = Pawn ? Pawn->GetPlayerState<AGothicPlayerState>() : nullptr;
    UGothicInventoryComponent* Inventory = PS ? PS->GetInventory() : nullptr;

    if (!Inventory)
    {
        // The PlayerState is not guaranteed to be resolved at BeginPlay — the
        // pawn's own GAS init has the same problem and solves it the same way.
        // Retried rather than abandoned, because an unbound inventory means the
        // first-pickup and first-equip hints never fire at all.
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().SetTimer(
                InventoryBindRetryHandle,
                this,
                &UGothicHintManagerComponent::BindInventoryDelegates,
                0.5f,
                false);
        }
        return;
    }

    Inventory->OnItemAdded.AddDynamic(this, &UGothicHintManagerComponent::HandleItemAdded);
    Inventory->OnItemEquipped.AddDynamic(this, &UGothicHintManagerComponent::HandleItemEquipped);
    BoundInventory = Inventory;
}

void UGothicHintManagerComponent::HandleItemAdded(const FGothicItemInstance& Item)
{
    ShowHint(GothicTags::Hint_Inventory);
}

void UGothicHintManagerComponent::HandleItemEquipped(EGothicEquipSlot Slot, const FGothicItemInstance& Item)
{
    ShowHint(GothicTags::Hint_Equip);

    // Swap is only teachable once there is somewhere to swap TO. Counting armed
    // slots rather than watching a specific index, because the starting kit is
    // sidearm-only and which slot fills second depends on whether the player
    // finds the Piece or the Rig first.
    const AGothicPlayerCharacter* Player = GetOwner<AGothicPlayerCharacter>();
    if (!Player)
    {
        return;
    }

    int32 ArmedSlots = 0;
    for (const FGothicWeaponSlot& WeaponSlot : Player->WeaponSlots)
    {
        if (WeaponSlot.WeaponData)
        {
            ++ArmedSlots;
        }
    }

    if (ArmedSlots >= 2)
    {
        ShowHint(GothicTags::Hint_WeaponSwap);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Key display
// ═══════════════════════════════════════════════════════════════════════════

FString UGothicHintManagerComponent::KeyDisplayName(const FKey& Key)
{
    // Only the keys whose UE identifier is wrong to show a player. Letters and
    // digits fall through to GetDisplayName, which already reads as "R" or "1".
    static const TMap<FName, FString> Overrides = {
        { EKeys::LeftMouseButton.GetFName(),   TEXT("LMB")    },
        { EKeys::RightMouseButton.GetFName(),  TEXT("RMB")    },
        { EKeys::MiddleMouseButton.GetFName(), TEXT("MMB")    },
        { EKeys::LeftShift.GetFName(),         TEXT("Shift")  },
        { EKeys::RightShift.GetFName(),        TEXT("Shift")  },
        { EKeys::LeftControl.GetFName(),       TEXT("Ctrl")   },
        { EKeys::RightControl.GetFName(),      TEXT("Ctrl")   },
        { EKeys::LeftAlt.GetFName(),           TEXT("Alt")    },
        { EKeys::RightAlt.GetFName(),          TEXT("Alt")    },
        { EKeys::SpaceBar.GetFName(),          TEXT("Space")  },
        { EKeys::Escape.GetFName(),            TEXT("Esc")    },
        { EKeys::Tab.GetFName(),               TEXT("Tab")    },
        { EKeys::MouseWheelAxis.GetFName(),    TEXT("Wheel")  },
    };

    if (!Key.IsValid())
    {
        return FString();
    }

    if (const FString* Override = Overrides.Find(Key.GetFName()))
    {
        return *Override;
    }

    return Key.GetDisplayName(/*bLongDisplayName=*/false).ToString();
}
