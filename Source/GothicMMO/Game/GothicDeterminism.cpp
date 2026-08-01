// GothicDeterminism.cpp

#include "Game/GothicDeterminism.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY(LogGothicDeterminism);

namespace
{
    int32 GGothicDeterministic = 0;
    int32 GGothicDeterministicSeed = 12345;

    FAutoConsoleVariableRef CVarGothicDeterministic(
        TEXT("vigil.Deterministic"),
        GGothicDeterministic,
        TEXT("Route measurement-critical RNG (starting gear rolls, PlayerStart choice, evasion) ")
        TEXT("through a seeded stream so combat measurements repeat.\n")
        TEXT("  0: shipping behaviour, unseeded (default)\n")
        TEXT("  1: deterministic — see vigil.DeterministicSeed"),
        ECVF_Default);

    FAutoConsoleVariableRef CVarGothicDeterministicSeed(
        TEXT("vigil.DeterministicSeed"),
        GGothicDeterministicSeed,
        TEXT("Seed used when vigil.Deterministic is 1. Changing it reseeds immediately."),
        ECVF_Default);

    /** The one stream every deterministic call site draws from. */
    FRandomStream& GothicStream()
    {
        static FRandomStream Stream(GGothicDeterministicSeed);
        return Stream;
    }
}

bool FGothicDeterminism::IsEnabled()
{
    return GGothicDeterministic != 0;
}

int32 FGothicDeterminism::GetSeed()
{
    return GGothicDeterministicSeed;
}

FRandomStream& FGothicDeterminism::GetStream()
{
    return GothicStream();
}

void FGothicDeterminism::ResetStream()
{
    if (!IsEnabled())
    {
        UE_LOG(LogGothicDeterminism, Verbose,
            TEXT("Deterministic mode OFF — RNG is unseeded. Set vigil.Deterministic 1 to enable."));
        return;
    }

    GothicStream().Initialize(GGothicDeterministicSeed);

    // Loud on purpose: a measurement run has to be able to record its seed, and
    // "which seed was that?" has already cost this project two rounds of numbers.
    UE_LOG(LogGothicDeterminism, Log,
        TEXT("DETERMINISTIC MODE ACTIVE — seed %d. Gear rolls, PlayerStart selection and ")
        TEXT("evasion are reproducible for this run."),
        GGothicDeterministicSeed);
}

int32 FGothicDeterminism::RandRange(int32 Min, int32 Max)
{
    return IsEnabled() ? GothicStream().RandRange(Min, Max) : FMath::RandRange(Min, Max);
}

float FGothicDeterminism::FRandRange(float Min, float Max)
{
    return IsEnabled() ? GothicStream().FRandRange(Min, Max) : FMath::FRandRange(Min, Max);
}
