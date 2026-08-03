// GA_BestialLucidCharge.cpp

#include "AbilitySystem/GA_BestialLucidCharge.h"
#include "GothicMMO.h"                      // LogVigilCombat
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/GothicGameplayTags.h"
#include "AI/GothicEnemyAIController.h"
#include "Character/GothicPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "TimerManager.h"

UGA_BestialLucidCharge::UGA_BestialLucidCharge()
{
    AbilitySlot = EGothicAbilitySlot::Ability1;

    // The graph applies its own single-target hit today. Once this class owns
    // the damage, that one is a duplicate — and unlike the Claw's, it cannot be
    // detected automatically, because the charge has no montage and therefore
    // no hitbox notify to find. Default it off here so a Blueprint reparented
    // to this class is correct before anyone touches its properties.
    bUseDirectDamageFallback = false;
}

void UGA_BestialLucidCharge::ActivateAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    // The damage window is NOT armed here. Activation is 0.8s of stationary
    // windup ahead of the launch, and arming it here spent nearly two thirds of
    // the window sweeping the boss's own start position while she stood still.
    // The graph calls BeginChargeDamageWindow immediately after LaunchCharacter
    // instead — see the header.
    //
    // The old ordering-before-Super concern is gone with it: there is now
    // nothing armed at the moment Super runs the graph, and a graph that reaches
    // EndAbility synchronously simply never opens a window.
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UGA_BestialLucidCharge::EndAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    bool bReplicateEndAbility,
    bool bWasCancelled)
{
    // Cancelled mid-charge (staggered, killed, phase transition) stops being
    // dangerous immediately. A charge that is no longer happening must not keep
    // running people over.
    EndChargeDamageWindow(bWasCancelled ? TEXT("cancelled") : TEXT("ability-end"));

    // And the boss must not be left mid-flight with a paused brain and zero air
    // control. Unconditional, not gated on bWasCancelled: a normal end that
    // happens after the landing finds nothing in flight and this is a no-op,
    // while ANY early end — cancel, stagger, phase transition, death — is
    // exactly the case that would otherwise wedge her. A pawn stuck brain-dead
    // is far worse than the mistimed window this class exists to fix.
    if (AGothicEnemyAIController* EnemyAIC = GetEnemyController())
    {
        EnemyAIC->AbortLeapFlight();
    }

    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

AGothicEnemyAIController* UGA_BestialLucidCharge::GetEnemyController() const
{
    const APawn* AvatarPawn = Cast<APawn>(GetAvatarActorFromActorInfo());
    return AvatarPawn ? Cast<AGothicEnemyAIController>(AvatarPawn->GetController()) : nullptr;
}

void UGA_BestialLucidCharge::HandleLeapLanded(bool bLanded)
{
    EndChargeDamageWindow(TEXT("landed"));
}

FVector UGA_BestialLucidCharge::ComputeChargeLaunchVelocity(const AActor* Target)
{
    const AActor* Avatar = GetAvatarActorFromActorInfo();
    if (!Avatar || !Target)
    {
        // Deliberately not a guess. A default direction here would launch her at
        // full speed into whatever the last valid facing happened to be.
        UE_LOG(LogVigilCombat, Warning,
            TEXT("Charge[%s]: ComputeChargeLaunchVelocity called with %s — returning zero, no leap."),
            *GetNameSafe(Avatar), Target ? TEXT("no avatar") : TEXT("no target"));
        return FVector::ZeroVector;
    }

    const FVector MyLoc     = Avatar->GetActorLocation();
    const FVector TargetLoc = Target->GetActorLocation();

    // Horizontal only, for the same reason the action pool's range bands are:
    // an actor's location is its capsule CENTRE, and this boss's centre sits
    // ~291uu above a floor-standing player's. Folding that constant into the
    // direction would tilt every charge downward by an amount that describes the
    // rig rather than the fight.
    FVector Delta2D = TargetLoc - MyLoc;
    Delta2D.Z = 0.f;

    const float CommitDistance = Delta2D.Size();
    if (CommitDistance <= KINDA_SMALL_NUMBER)
    {
        return FVector(0.f, 0.f, ChargeLaunchZSpeed);
    }

    const FVector Dir2D = Delta2D / CommitDistance;

    // ── Airtime ──────────────────────────────────────────────────────────────
    //
    // Ballistic, then calibrated against the leap guard's measurements. For a
    // launch that lands at or below its own height, t = 2*Vz/g.
    //
    //     g   = 980 uu/s^2 (UE default) * GravityScale
    //     t   = 2 * 400 / 980            = 0.8163s
    //     t'  = 0.8163 * 1.015           = 0.8286s
    //
    // Measured, nine guarded flights: 0.824-0.943s, median 0.828. The predicted
    // 0.8286 lands on that median, which is the whole reason this derivation is
    // trustworthy — see ChargeAirtimeCalibration for what the correction is
    // actually correcting.
    //
    // Gravity is read from the world and the movement component rather than
    // hardcoded at 980, so a project-wide gravity change or a GravityScale tweak
    // on her Blueprint moves the solve with it instead of silently invalidating
    // it. GravityScale is a Blueprint/CDO value on this character — nothing in
    // C++ sets it — so reading it is the only way to know it.
    float GravityZ = 980.f;
    if (const ACharacter* AvatarChar = Cast<ACharacter>(Avatar))
    {
        if (const UCharacterMovementComponent* Move = AvatarChar->GetCharacterMovement())
        {
            GravityZ = FMath::Abs(Move->GetGravityZ());
        }
    }
    GravityZ = FMath::Max(1.f, GravityZ);

    const float Airtime =
        FMath::Max(KINDA_SMALL_NUMBER,
            (2.f * ChargeLaunchZSpeed / GravityZ) * ChargeAirtimeCalibration);

    // ── Where she is actually aiming ─────────────────────────────────────────
    //
    // Short of the target, not at it. Clamped at zero so a target already inside
    // the standoff produces a stationary-ish launch rather than a charge that
    // flies BACKWARDS out of a negative distance.
    const float TravelDistance = FMath::Max(0.f, CommitDistance - ChargeLandingStandoff);

    const float DesiredSpeed = TravelDistance / Airtime;
    const float LaunchSpeed  = FMath::Clamp(DesiredSpeed,
        FMath::Min(MinChargeLaunchSpeed, MaxChargeLaunchSpeed), MaxChargeLaunchSpeed);

    const FVector LaunchVelocity = (Dir2D * LaunchSpeed) + FVector(0.f, 0.f, ChargeLaunchZSpeed);

    // predictedLand= is the whole point of this line: a verification pass can
    // diff it against the landing the guard reports and know immediately whether
    // a bad charge was a bad SOLVE or a bad FLIGHT (air control, geometry, a
    // stagger mid-leap). Those two failures are indistinguishable from the
    // outside and were being confused for each other.
    const UWorld* World = GetWorld();
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|Charge|LAUNCH-solve|commitDist=%.0f|standoff=%.0f|")
        TEXT("travel=%.0f|airtime=%.3f|speed=%.0f|clamped=%d|predictedLand=%.0f"),
        World ? World->GetTimeSeconds() : 0.f, *GetNameSafe(Avatar),
        CommitDistance, ChargeLandingStandoff, TravelDistance, Airtime, LaunchSpeed,
        FMath::IsNearlyEqual(LaunchSpeed, DesiredSpeed, 1.f) ? 0 : 1,
        LaunchSpeed * Airtime);

    return LaunchVelocity;
}

void UGA_BestialLucidCharge::BeginChargeDamageWindow()
{
    // Idempotent. A graph wired to call this twice, or a branch that re-enters,
    // must not restart the clock or wipe the already-hit set mid-charge — that
    // would let the same victim be hit a second time by one charge.
    if (bChargeWindowOpen)
    {
        return;
    }

    HitPawnsThisCharge.Reset();

    ChargeSweepsThisWindow = 0;
    ChargeMinDist2D        = TNumericLimits<float>::Max();
    ChargeMinDist3D        = TNumericLimits<float>::Max();

    const AActor* Owner = GetOwningActorFromActorInfo();
    UWorld* World = GetWorld();

    if (!World || !Owner || !Owner->HasAuthority())
    {
        return;
    }

    if (!ChargeDamageEffect)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("BestialLucidCharge[%s]: ChargeDamageEffect is unassigned — the charge moves the boss and damages nobody"),
            *GetNameSafe(GetAvatarActorFromActorInfo()));
        return;
    }

    bChargeWindowOpen = true;
    ChargeWindowStartSeconds = World->GetTimeSeconds();

    // The real closer. ChargeDamageWindow is only the backstop for a landing
    // that never comes — a leap cut short by geometry or a wall should stop
    // being dangerous the moment it stops travelling, not when its timer says
    // it might still be in the air.
    if (AGothicEnemyAIController* EnemyAIC = GetEnemyController())
    {
        EnemyAIC->OnLeapLanded.AddDynamic(this, &UGA_BestialLucidCharge::HandleLeapLanded);
    }
    else
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("BestialLucidCharge[%s]: no AGothicEnemyAIController — the damage window can only "
                 "close on its %.2fs backstop, and the leap itself is unguarded"),
            *GetNameSafe(GetAvatarActorFromActorInfo()), ChargeDamageWindow);
    }

    // Sweep once immediately: anyone already standing on the boss when she
    // launches should be hit by the start of the charge, not by its second
    // sample 50ms later.
    SweepChargeDamage();

    // SweepChargeDamage can close the window on the spot (no ASC, no avatar), so
    // only arm the repeat if it is still open.
    if (bChargeWindowOpen)
    {
        // Bound through a delegate rather than the member-pointer overload
        // because the sweep now takes a flag; the repeating sweeps are all
        // ordinary ones.
        FTimerDelegate SweepDelegate = FTimerDelegate::CreateUObject(
            this, &UGA_BestialLucidCharge::SweepChargeDamage, /*bFinalSweep=*/false);

        World->GetTimerManager().SetTimer(
            ChargeSweepTimerHandle, SweepDelegate,
            FMath::Max(0.01f, ChargeSweepInterval), true);
    }
}

void UGA_BestialLucidCharge::EndChargeDamageWindow(const TCHAR* ClosedBy)
{
    // Nothing open, or we are already inside the close path (a final sweep that
    // finds no ASC calls back in here). Either way there is no window to sweep,
    // no clock to stop and nothing to summarise.
    if (!bChargeWindowOpen || bClosingChargeWindow)
    {
        bChargeWindowOpen = false;
        return;
    }

    bClosingChargeWindow = true;

    // ── The touchdown sweep ──────────────────────────────────────────────────
    //
    // One last sweep, synchronously, before anything is torn down — and it runs
    // on BOTH close paths, the landing and the backstop.
    //
    // The window used to close ON the landed signal, which threw away the single
    // most dangerous instant of the whole move. The boss's closest approach to a
    // player who does not dodge is the landing itself: chained charges touch down
    // 124-156uu from the player (the chain branch that fires them is gated at
    // <=160uu), while the minimum MID-FLIGHT approach measured 328uu+. So every
    // sample the window ever took was of the half of the charge that cannot
    // connect, and the half that can was discarded a frame before it was
    // measured. The final sweep tests the overlap at touchdown instead.
    //
    // It cannot double-hit: HitPawnsThisCharge is still populated and still
    // consulted, so anyone already run over this charge is skipped here.
    SweepChargeDamage(/*bFinalSweep=*/true);

    bChargeWindowOpen = false;

    UWorld* World = GetWorld();

    if (World)
    {
        World->GetTimerManager().ClearTimer(ChargeSweepTimerHandle);
    }

    // Unbind even if the landing is what closed us — an ability instance can be
    // re-activated, and a surviving binding would have the next charge's window
    // closed by the previous charge's delegate list.
    if (AGothicEnemyAIController* EnemyAIC = GetEnemyController())
    {
        EnemyAIC->OnLeapLanded.RemoveDynamic(this, &UGA_BestialLucidCharge::HandleLeapLanded);
    }

    // One line per charge that answers the geometric question outright: how
    // close did she actually get, and did anything land. A min approach well
    // outside the inflated capsule means the sweep never had a chance and the
    // charge needs steering or reach, not a bigger number.
    const float Now = World ? World->GetTimeSeconds() : 0.f;

    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|ChargeSweep|WINDOW-closed|hits=%d|sweeps=%d|")
        TEXT("minDist2D=%.1f|minDist3D=%.1f|closedBy=%s|open=%.3f"),
        Now, *GetNameSafe(GetAvatarActorFromActorInfo()),
        HitPawnsThisCharge.Num(), ChargeSweepsThisWindow,
        ChargeMinDist2D == TNumericLimits<float>::Max() ? -1.f : ChargeMinDist2D,
        ChargeMinDist3D == TNumericLimits<float>::Max() ? -1.f : ChargeMinDist3D,
        ClosedBy ? ClosedBy : TEXT("unknown"),
        World ? (World->GetTimeSeconds() - ChargeWindowStartSeconds) : -1.0);

    bClosingChargeWindow = false;
}

void UGA_BestialLucidCharge::SweepChargeDamage(bool bFinalSweep)
{
    UWorld* World = GetWorld();
    AActor* Avatar = GetAvatarActorFromActorInfo();

    if (!World || !Avatar || !ChargeDamageEffect)
    {
        EndChargeDamageWindow(TEXT("no-avatar-or-effect"));
        return;
    }

    // The backstop is not consulted on the final sweep: the window is already
    // being closed, and an expired backstop is one of the two things that closes
    // it. Testing it here would refuse the touchdown overlap in exactly the case
    // the final sweep was added to cover.
    if (!bFinalSweep && World->GetTimeSeconds() - ChargeWindowStartSeconds >= ChargeDamageWindow)
    {
        EndChargeDamageWindow(TEXT("backstop"));
        return;
    }

    UAbilitySystemComponent* BossASC = GetAbilitySystemComponentFromActorInfo();
    if (!BossASC)
    {
        EndChargeDamageWindow(TEXT("no-asc"));
        return;
    }

    ++ChargeSweepsThisWindow;

    TArray<AActor*> IgnoreActors;
    IgnoreActors.Add(Avatar);

    // The overlap is the boss's OWN BODY, inflated — not a sphere at her origin.
    //
    // A sphere centred on GetActorLocation() is a point-sized reading of a pawn
    // that is anything but a point. Her actor origin is the capsule centre,
    // ~291uu above a floor-standing player's origin, so a 120uu sphere there
    // could not reach the floor at any horizontal distance: measured minimum 3D
    // separation over six charges was 324-394uu, and not one of them dealt
    // damage. Taking the extent from UCapsuleComponent instead makes the check
    // scale with whatever is charging, with no per-creature radius to retune.
    float BodyRadius = 0.f;
    float BodyHalfHeight = 0.f;
    FVector SweepCentre = Avatar->GetActorLocation();

    if (const ACharacter* AvatarCharacter = Cast<ACharacter>(Avatar))
    {
        if (const UCapsuleComponent* AvatarCapsule = AvatarCharacter->GetCapsuleComponent())
        {
            // Scaled, so the boss's 1.5x RelativeScale3D is already in these.
            AvatarCapsule->GetScaledCapsuleSize(BodyRadius, BodyHalfHeight);
            SweepCentre = AvatarCapsule->GetComponentLocation();
        }
    }

    if (BodyHalfHeight <= 0.f)
    {
        // Not a Character, or no capsule: fall back to the root primitive's
        // collision cylinder, which is still measured from the actor rather than
        // guessed. Only a truly collision-less avatar reaches the floor below.
        Avatar->GetSimpleCollisionCylinder(BodyRadius, BodyHalfHeight);
    }

    // ── Min-approach telemetry ───────────────────────────────────────────────
    //
    // Measured against the LOCAL PLAYER PAWNS rather than the boss's combat
    // target: the ability has no cheap, honest read of the Blackboard target
    // from here (it would mean walking the controller to its BT component), that
    // target can be null or stale exactly when a charge is in flight, and the
    // question this number answers — "could this sweep ever have touched the
    // player" — is about the player, not about who the AI believes it is
    // fighting. In a single-player session the two are the same actor anyway.
    //
    // Actor-origin to actor-origin, both 2D and 3D, because that is the pair the
    // chain's range gates and the old hand-reconstructed probe samples are
    // expressed in. The capsule offset that makes 3D read ~165uu high vs 2D is
    // precisely why both are logged instead of one.
    const FVector AvatarLocation = Avatar->GetActorLocation();

    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* PC = It->Get();
        const APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;

        if (!PlayerPawn)
        {
            continue;
        }

        const FVector PlayerLocation = PlayerPawn->GetActorLocation();

        ChargeMinDist2D = FMath::Min(ChargeMinDist2D,
            static_cast<float>(FVector::Dist2D(AvatarLocation, PlayerLocation)));
        ChargeMinDist3D = FMath::Min(ChargeMinDist3D,
            static_cast<float>(FVector::Dist(AvatarLocation, PlayerLocation)));
    }

    TArray<AActor*> Overlapping;
    UKismetSystemLibrary::CapsuleOverlapActors(
        Avatar,
        SweepCentre,
        FMath::Max(1.f, BodyRadius + ChargeHitInflation),
        FMath::Max(1.f, BodyHalfHeight + ChargeHitInflation),
        TArray<TEnumAsByte<EObjectTypeQuery>>{ UEngineTypes::ConvertToObjectType(ECC_Pawn) },
        AGothicPlayerCharacter::StaticClass(),
        IgnoreActors,
        Overlapping);

    for (AActor* Victim : Overlapping)
    {
        if (!Victim || HitPawnsThisCharge.Contains(Victim))
        {
            continue;
        }

        UAbilitySystemComponent* TargetASC =
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Victim);
        if (!TargetASC)
        {
            continue;
        }

        // Same construction as GothicMeleeHitboxComponent and the BT task the
        // sweep came from: flat damage rides Data.Damage and the target's
        // Defense is subtracted by the attribute set. Nothing charge-specific.
        //
        // The instigator is the boss AVATAR. It used to be
        // GetOwningActorFromActorInfo() — the AIController — which has no ASC,
        // so the boss's AttackPower never reached the pipeline and the charge
        // landed 37 rather than the 57 its tuning comment claimed.
        FGameplayEffectContextHandle Context =
            UGothicAbilitySystemComponent::MakeDamageContext(BossASC, Avatar);

        FGameplayEffectSpecHandle Spec = BossASC->MakeOutgoingSpec(
            ChargeDamageEffect, GetAbilityLevel(), Context);

        if (!Spec.IsValid())
        {
            continue;
        }

        Spec.Data->SetSetByCallerMagnitude(GothicTags::Data_Damage, ChargeDamage);
        BossASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);

        HitPawnsThisCharge.Add(Victim);

        // finalSweep=, not final=.
        //
        // The field was always bFinalSweep — "is this the last sweep of the
        // damage window" — but sitting next to raw= it read as "final damage",
        // and every HIT line in the last verification run said final=0 while a
        // 57 HP drop was landing on the player. The number was true and the name
        // was a lie, which is worse than either.
        //
        // The applied magnitude is genuinely not available here. ChargeDamageEffect
        // is instant, so ApplyGameplayEffectSpecToTarget returns an invalid handle
        // with nothing to read off it, and the arithmetic that turns raw into
        // applied (Defense subtraction, the instigator's AttackPower) happens
        // inside UGothicAttributeSet::PostGameplayEffectExecute on the TARGET's
        // ASC — after this call has returned. Reporting it would mean plumbing a
        // result back out of the attribute set for one log line. raw= plus the
        // attribute set's own damage logging is the honest pair; this line does
        // not get to claim a final number it never sees.
        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VigilTimeline|t=%.3f|%s|ChargeSweep|HIT|victim=%s|raw=%.0f|tWindow=%.3f|finalSweep=%d"),
            World->GetTimeSeconds(), *Avatar->GetName(), *Victim->GetName(), ChargeDamage,
            World->GetTimeSeconds() - ChargeWindowStartSeconds, bFinalSweep ? 1 : 0);
    }
}
