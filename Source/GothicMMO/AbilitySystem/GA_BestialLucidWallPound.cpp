// GA_BestialLucidWallPound.cpp

#include "AbilitySystem/GA_BestialLucidWallPound.h"
#include "AI/GothicRotundaPillar.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "TimerManager.h"

UGA_BestialLucidWallPound::UGA_BestialLucidWallPound()
{
    AbilitySlot = EGothicAbilitySlot::Ability2;
    // AssetTags set in the Blueprint child — see CONSOLIDATION notes from
    // earlier today: this is the field WeightedActionSelect, CombatSync, and
    // ActivateAbilityByTag all read, NOT AbilityInputTag.
}

void UGA_BestialLucidWallPound::ActivateAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    const bool bCommitted = CommitAbility(Handle, ActorInfo, ActivationInfo);
    if (!bCommitted)
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }

    // Fresh activation, fresh impact budget. Reset here rather than in
    // EndAbility so a force-ended or cancelled activation cannot leave the guard
    // latched and silently mute the NEXT Wall Pound.
    bImpactFiredThisActivation = false;

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            WallPoundSafetyTimerHandle, this,
            &UGA_BestialLucidWallPound::HandleWallPoundSafetyTimeout,
            FMath::Max(0.1f, WallPoundSafetyTimeout), false);
    }

    // Everything below this line used to live here: the overlap, the collapse,
    // and an EndAbility on the same frame that killed the montage Super had just
    // started. The graph owns the timing now — it calls ExecuteWallPoundImpact
    // at the strike frame and EndAbility when the montage is genuinely done.
    //
    // Nothing follows Super deliberately. Super runs K2_ActivateAbility, and a
    // graph that reaches EndAbility synchronously would already have destroyed
    // the activation by the time any trailing statement here ran.
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UGA_BestialLucidWallPound::EndAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    bool bReplicateEndAbility,
    bool bWasCancelled)
{
    // Unconditional, for the same reason Charge unwinds its leap unconditionally:
    // a normal end clears a timer that was never going to fire and costs nothing,
    // while ANY early end — cancel, stagger, phase transition, death — is exactly
    // the case where a surviving timer would fire an Error about a graph that was
    // working fine.
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(WallPoundSafetyTimerHandle);
    }

    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_BestialLucidWallPound::HandleWallPoundSafetyTimeout()
{
    UE_LOG(LogTemp, Error,
        TEXT("WallPound[%s]: force-ended after %.1fs — the Blueprint graph never called EndAbility. ")
        TEXT("Wire EndAbility to every completion pin of PlayMontageAndWait; until then this attack ")
        TEXT("burns its cooldown and would otherwise hold the attack-commitment lock forever."),
        *GetNameSafe(GetAvatarActorFromActorInfo()), WallPoundSafetyTimeout);

    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

void UGA_BestialLucidWallPound::ExecuteWallPoundImpact()
{
    // One pillar per activation. A notify on a looping montage section, or a
    // graph wired to call this on two pins, must not collapse the arena two
    // pillars at a time off a single attack.
    if (bImpactFiredThisActivation)
    {
        return;
    }

    AActor* Owner = GetOwningActorFromActorInfo();
    AActor* Avatar = GetAvatarActorFromActorInfo();

    if (!Owner || !Avatar || !Owner->HasAuthority())
    {
        return;
    }

    bImpactFiredThisActivation = true;

    TArray<AActor*> IgnoreActors;
    IgnoreActors.Add(Avatar);

    TArray<AActor*> Overlapping;
    UKismetSystemLibrary::SphereOverlapActors(
        Avatar,
        Avatar->GetActorLocation(),
        SearchRadius,
        TArray<TEnumAsByte<EObjectTypeQuery>>{ UEngineTypes::ConvertToObjectType(ECC_WorldStatic) },
        nullptr,
        IgnoreActors,
        Overlapping);

    AGothicRotundaPillar* NearestPillar = nullptr;
    float NearestDistSq = TNumericLimits<float>::Max();

    for (AActor* Candidate : Overlapping)
    {
        if (!Candidate || !Candidate->ActorHasTag(TerrainTag))
        {
            continue;
        }

        // Typed, not FindFunction("TriggerWallCollapse")/ProcessEvent. The name
        // lookup worked but bought nothing: it could not be checked by the
        // compiler, it silently degraded to a Warning if the name ever drifted,
        // and it could not ask the target anything about its state. The include
        // is cheap and this file already depends on the pillar system in every
        // way that matters.
        AGothicRotundaPillar* Pillar = Cast<AGothicRotundaPillar>(Candidate);
        if (!Pillar)
        {
            continue;
        }

        // Skip pillars that are already going down. The state flips at the START
        // of the 1.75s collapse telegraph, but the pillar keeps its collision
        // until the slab lands — so for that whole window the overlap still
        // returns it, and without this check a Wall Pound aimed at a collapsing
        // pillar spends the 20s cooldown re-smashing rubble. Costly in a design
        // where Wall Pound is the Pounce chain's finisher and the arena has only
        // four pillars to give.
        if (Pillar->IsDestroyed())
        {
            continue;
        }

        const float DistSq = FVector::DistSquared(Avatar->GetActorLocation(), Pillar->GetActorLocation());
        if (DistSq < NearestDistSq)
        {
            NearestDistSq = DistSq;
            NearestPillar = Pillar;
        }
    }

    if (NearestPillar)
    {
        // The pillar owns what "collapsing" looks like — telegraph, slab, damage
        // volume, nav blocker. This ability only decided which pillar and when.
        NearestPillar->TriggerWallCollapse();

        UE_LOG(LogTemp, Log,
            TEXT("WallPound[%s]: struck %s at %.0fuu — collapse telegraph started"),
            *Avatar->GetName(), *NearestPillar->GetName(), FMath::Sqrt(NearestDistSq));
    }
    else
    {
        // No longer a Warning about drifting radii. With Wall Pound as the
        // Pounce chain's finisher it fires wherever the chain ends, and the
        // chain does not know or care where the pillars are — a smash into bare
        // floor is the expected outcome of a player who kited her away from
        // them, and late in the fight there may be no pillars left at all.
        UE_LOG(LogTemp, Log,
            TEXT("WallPound[%s]: no surviving '%s' pillar within %.0f — the smash lands on empty floor"),
            *Avatar->GetName(), *TerrainTag.ToString(), SearchRadius);
    }
}
