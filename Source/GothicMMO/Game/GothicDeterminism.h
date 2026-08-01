// GothicDeterminism.h
// Opt-in deterministic mode for measurement runs.
//
// Combat measurement in this project has been quietly unreliable: the starting
// kit is rolled fresh on every spawn, the PlayerStart is chosen at random, and
// evasion rolls against every incoming hit. Three runs of the same encounter
// measured 24.33 / 23.09 / 27.9 raw player damage — the encounter did not
// change, the dice did.
//
// This does NOT remove randomness from the shipping game. It routes the handful
// of measurement-critical rolls through one seeded FRandomStream that a harness
// can switch on:
//
//     vigil.Deterministic 1          (default 0 — shipping behaviour)
//     vigil.DeterministicSeed 12345  (any int32; logged on activation)
//
// Both are ordinary console variables, so they can also be pinned from config:
//
//     [SystemSettings]
//     vigil.Deterministic=1
//     vigil.DeterministicSeed=12345
//
// The stream is reseeded at the start of every level (AGothicGameMode::InitGame)
// so each run replays the same sequence rather than continuing the previous
// run's. When active, the seed is logged under LogGothicDeterminism so a
// measurement can record which seed produced it.

#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

DECLARE_LOG_CATEGORY_EXTERN(LogGothicDeterminism, Log, All);

/**
 * Static access to the deterministic RNG. Call sites use the RandRange/FRandRange
 * wrappers, which fall through to the global unseeded FMath calls whenever
 * deterministic mode is off — so shipping behaviour is byte-for-byte unchanged.
 */
struct GOTHICMMO_API FGothicDeterminism
{
    /** True when vigil.Deterministic is non-zero. */
    static bool IsEnabled();

    /** The seed currently configured by vigil.DeterministicSeed. */
    static int32 GetSeed();

    /**
     * Reseed the shared stream and log the seed. Called from GameMode::InitGame;
     * a harness can also call it to restart the sequence mid-session.
     * No-op (beyond a log line) when deterministic mode is off.
     */
    static void ResetStream();

    /** The shared seeded stream. Only meaningful while IsEnabled(). */
    static FRandomStream& GetStream();

    /** Seeded when deterministic mode is on, FMath::RandRange otherwise. */
    static int32 RandRange(int32 Min, int32 Max);

    /** Seeded when deterministic mode is on, FMath::FRandRange otherwise. */
    static float FRandRange(float Min, float Max);
};
