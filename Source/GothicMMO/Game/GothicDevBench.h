// GothicDevBench.h
// The dev measurement-bench test-option framework — a skeleton seam for console
// options that are meaningful only on L_DEV_FeelBox and structurally inert
// everywhere else.
//
// ── The hard gate ────────────────────────────────────────────────────────────
//
// Every option routes through ResolveLocalBenchPawn FIRST and does nothing on a
// null return. That single chokepoint is the whole safety story: it hands back a
// pawn ONLY when that pawn's level is in its DevBenchMaps allow-list
// (AGothicPlayerCharacter::IsDevBenchLevel), and null — with a Bench|INERT log
// line — anywhere else. There is no per-option map test to forget and no path to
// an option body that does not pass through here, so an option added tomorrow is
// gated by construction rather than by remembering to check.
//
// The allow-list itself lives on the pawn (AGothicPlayerCharacter::DevBenchMaps),
// the SAME list the pinned canonical loadout reads — one source of truth for
// "where the bench is", shared by both bench behaviours.
//
// Console commands are the surface because this project already drives PIE
// through the console (Gothic.Revive, Gothic.SetDowned, Gothic.ForceWipe…); the
// file-static FAutoConsoleCommand pattern is copied from those. Console commands
// register at module load and cannot be registration-gated, so the gate is at
// ACTIVATION — the first thing every handler does.

#pragma once

#include "CoreMinimal.h"

class AGothicPlayerCharacter;
class UWorld;

namespace GothicDevBench
{
    /**
     * The single activation gate for every dev-bench option. Resolves the local
     * player pawn and returns it ONLY if its level is a dev bench; returns null
     * (and logs Bench|<OptionName>|INERT with the reason) otherwise. OptionName is
     * used purely for the log line so a captured session says which option bailed.
     */
    GOTHICMMO_API AGothicPlayerCharacter* ResolveLocalBenchPawn(UWorld* World, const TCHAR* OptionName);
}
