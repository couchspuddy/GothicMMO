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
// Options
//
// Each option is a small stub: gate through ResolveLocalBenchPawn, then act on
// the pawn it returns. Adding an option is adding one of these plus its pawn-side
// body — the gate is never re-implemented, so a new option cannot accidentally
// escape it. Exactly one exemplar ships today (loadout readback); the shape is
// the deliverable, not the count.
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
