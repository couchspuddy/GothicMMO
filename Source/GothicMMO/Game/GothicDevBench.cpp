// GothicDevBench.cpp

#include "Game/GothicDevBench.h"
#include "Character/GothicPlayerCharacter.h"
#include "GothicMMO.h"                 // LogVigilCombat
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace GothicDevBench
{
    AGothicPlayerCharacter* ResolveLocalBenchPawn(UWorld* World, const TCHAR* OptionName)
    {
        if (!World)
        {
            return nullptr;
        }

        // The console is a local surface, so a bench option acts on the local
        // player's pawn — the bench is single-player measurement by construction.
        APlayerController* PC = World->GetFirstPlayerController();
        AGothicPlayerCharacter* Pawn = PC ? Cast<AGothicPlayerCharacter>(PC->GetPawn()) : nullptr;
        if (!Pawn)
        {
            UE_LOG(LogVigilCombat, Warning,
                TEXT("Bench|%s|INERT|reason=no-local-pawn"), OptionName);
            return nullptr;
        }

        // THE gate. Off the bench every option stops here — the allow-list lives
        // on the pawn and is shared with the pinned canonical loadout.
        if (!Pawn->IsDevBenchLevel())
        {
            UE_LOG(LogVigilCombat, Log,
                TEXT("Bench|%s|INERT|reason=not-a-bench-level|map=%s"),
                OptionName, *UGameplayStatics::GetCurrentLevelName(World));
            return nullptr;
        }

        return Pawn;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Item catalog
//
// The names the grant/cycle options offer, as DA_ItemDef_<Name> suffixes under
// /Game/Data/Loot (the pawn resolves the full path). These are REAL named
// definitions — the whole point over a transient mint: a named def carries its
// authored slot, archetype, weapon data and perk catalog. Curated static lists
// rather than an AssetRegistry sweep because that is the least code and the most
// robust (no load-order dependency), and this is a dev bench, not shipping
// content — if a new DA_ItemDef ships, add its name here.
// ═══════════════════════════════════════════════════════════════════════════

namespace
{
    // The twelve weapon definitions, in cycle order.
    const TArray<FString>& BenchWeapons()
    {
        static const TArray<FString> Weapons = {
            TEXT("Revolver"), TEXT("Derringer"), TEXT("RepeatingPistol"),
            TEXT("LeverActionRepeater"), TEXT("Carbine"), TEXT("BoltActionRifle"),
            TEXT("Scattergun"), TEXT("Breacher"), TEXT("ShockRepeater"),
            TEXT("GatlingRig"), TEXT("CenserLauncher"), TEXT("HeavyMeleeRig"),
        };
        return Weapons;
    }

    // Two full ten-piece armor sets. Cycling between them is a measurement
    // affordance in itself: the Salvage set is Gear Score 0, the Kept set is not,
    // so a press flips the AP/Def core the damage math reads.
    const TArray<TArray<FString>>& BenchArmorSets()
    {
        static const TArray<TArray<FString>> Sets = {
            { TEXT("Boots"), TEXT("BracerL"), TEXT("BracerR"), TEXT("Cloak"),
              TEXT("Coat"), TEXT("Cord"), TEXT("GreaveL"), TEXT("GreaveR"),
              TEXT("Hood"), TEXT("Wrist") },
            { TEXT("Kept_Boots"), TEXT("Kept_BracerL"), TEXT("Kept_BracerR"),
              TEXT("Kept_Cloak"), TEXT("Kept_Coat"), TEXT("Kept_Cord"),
              TEXT("Kept_GreaveL"), TEXT("Kept_GreaveR"), TEXT("Kept_Hood"),
              TEXT("Kept_Wrist") },
        };
        return Sets;
    }

    // Process-global cycle cursors. Dev-only state; a bench is single-player, and
    // a stray value between sessions costs nothing but which item comes up first.
    int32 GBenchWeaponCursor = 0;
    int32 GBenchArmorSetCursor = 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// Options
//
// Each option is a small stub: gate through ResolveLocalBenchPawn, then act on
// the pawn it returns. Adding an option is adding one of these plus its pawn-side
// body — the gate is never re-implemented, so a new option cannot accidentally
// escape it.
// ═══════════════════════════════════════════════════════════════════════════

/** Gothic.Bench.DumpLoadout — log the equipped kit + the base→pre-vital scalar. */
static void GothicBenchDumpLoadout(UWorld* World)
{
    if (AGothicPlayerCharacter* Pawn = GothicDevBench::ResolveLocalBenchPawn(World, TEXT("DumpLoadout")))
    {
        Pawn->DumpBenchLoadout();
    }
}

static FAutoConsoleCommandWithWorld GGothicBenchDumpLoadoutCmd(
    TEXT("Gothic.Bench.DumpLoadout"),
    TEXT("Dump the local player's equipped loadout, gear score and archetype-damage ")
    TEXT("scalar to the log. Dev measurement bench only (L_DEV_FeelBox); no-ops elsewhere."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&GothicBenchDumpLoadout));

/** Gothic.Bench.ListItems — log the grantable weapon and armor-set names. */
static void GothicBenchListItems(UWorld* World)
{
    // Gated like every option even though it only reads static lists — off the
    // bench a dev tool should be uniformly silent, not selectively chatty.
    if (!GothicDevBench::ResolveLocalBenchPawn(World, TEXT("ListItems")))
    {
        return;
    }

    FString Weapons;
    for (const FString& Name : BenchWeapons())
    {
        Weapons += Name + TEXT(" ");
    }
    UE_LOG(LogVigilCombat, Log, TEXT("Bench|ListItems|weapons=%s"), *Weapons);

    for (int32 i = 0; i < BenchArmorSets().Num(); ++i)
    {
        FString Set;
        for (const FString& Name : BenchArmorSets()[i])
        {
            Set += Name + TEXT(" ");
        }
        UE_LOG(LogVigilCombat, Log, TEXT("Bench|ListItems|armorSet[%d]=%s"), i, *Set);
    }
    UE_LOG(LogVigilCombat, Log,
        TEXT("Bench|ListItems|usage=Gothic.Bench.GrantItem <Name> (any DA_ItemDef_<Name> under /Game/Data/Loot)"));
}

static FAutoConsoleCommandWithWorld GGothicBenchListItemsCmd(
    TEXT("Gothic.Bench.ListItems"),
    TEXT("List the grantable bench weapon and armor-set names. Dev bench only; no-ops elsewhere."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&GothicBenchListItems));

/** Gothic.Bench.GrantItem <Name> — grant + equip one named DA_ItemDef by name. */
static void GothicBenchGrantItem(const TArray<FString>& Args, UWorld* World)
{
    AGothicPlayerCharacter* Pawn = GothicDevBench::ResolveLocalBenchPawn(World, TEXT("GrantItem"));
    if (!Pawn)
    {
        return;
    }
    if (Args.Num() < 1)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("Bench|GrantItem|usage=Gothic.Bench.GrantItem <Name> — see Gothic.Bench.ListItems."));
        return;
    }
    Pawn->GrantBenchItem(Args[0]);
}

static FAutoConsoleCommandWithWorldAndArgs GGothicBenchGrantItemCmd(
    TEXT("Gothic.Bench.GrantItem"),
    TEXT("Gothic.Bench.GrantItem <Name> — grant + equip a named DA_ItemDef (weapon or armor) ")
    TEXT("with a canonical roll. Dev bench only; no-ops elsewhere."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GothicBenchGrantItem));

/** Gothic.Bench.CycleWeapon — grant + arm the next weapon in the catalog. */
static void GothicBenchCycleWeapon(UWorld* World)
{
    AGothicPlayerCharacter* Pawn = GothicDevBench::ResolveLocalBenchPawn(World, TEXT("CycleWeapon"));
    if (!Pawn)
    {
        return;
    }
    const TArray<FString>& Weapons = BenchWeapons();
    const FString& Name = Weapons[GBenchWeaponCursor % Weapons.Num()];
    ++GBenchWeaponCursor;
    UE_LOG(LogVigilCombat, Log, TEXT("Bench|CycleWeapon|next=%s"), *Name);
    Pawn->GrantBenchItem(Name);
}

static FAutoConsoleCommandWithWorld GGothicBenchCycleWeaponCmd(
    TEXT("Gothic.Bench.CycleWeapon"),
    TEXT("Grant + arm the next weapon in the bench catalog. Dev bench only; no-ops elsewhere."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&GothicBenchCycleWeapon));

/** Gothic.Bench.CycleArmorSet — grant + equip every piece of the next armor set. */
static void GothicBenchCycleArmorSet(UWorld* World)
{
    AGothicPlayerCharacter* Pawn = GothicDevBench::ResolveLocalBenchPawn(World, TEXT("CycleArmorSet"));
    if (!Pawn)
    {
        return;
    }
    const TArray<TArray<FString>>& Sets = BenchArmorSets();
    const int32 SetIdx = GBenchArmorSetCursor % Sets.Num();
    ++GBenchArmorSetCursor;
    UE_LOG(LogVigilCombat, Log, TEXT("Bench|CycleArmorSet|set=%d|pieces=%d"), SetIdx, Sets[SetIdx].Num());
    for (const FString& Name : Sets[SetIdx])
    {
        Pawn->GrantBenchItem(Name);
    }
}

static FAutoConsoleCommandWithWorld GGothicBenchCycleArmorSetCmd(
    TEXT("Gothic.Bench.CycleArmorSet"),
    TEXT("Grant + equip every piece of the next full armor set (Salvage <-> Kept). ")
    TEXT("Dev bench only; no-ops elsewhere."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&GothicBenchCycleArmorSet));
