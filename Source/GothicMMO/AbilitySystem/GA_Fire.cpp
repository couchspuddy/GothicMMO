// GA_Fire.cpp

#include "AbilitySystem/GA_Fire.h"

#include "GothicMMO.h"                          // ECC_Weapon
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"   // AttackPower scalar
#include "AbilitySystem/GothicGameplayTags.h"   // Data_SuperMeter
#include "AI/GothicEnemyBase.h"
#include "AI/GothicVitalPointComponent.h"
#include "Character/GothicPlayerCharacter.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Controller.h"           // GetPlayerViewPoint, the no-camera fallback
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Perception/AISense_Hearing.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"                        // TActorIterator — magnetism enemy sweep

namespace
{
    // ── Weapon perk coefficients (WEAPON_PERK_TABLES.md) ────────────────────
    //
    // Constants rather than catalog reads, for the reason the handling perks give
    // in GothicPlayerCharacter.cpp: FGothicWeaponPerkEntry::Magnitude is 0 in the
    // authored asset, so reading it today would silently zero every effect. Data
    // can supersede these without moving a call site.

    /** Jolt — "8% chance per hit to stagger target 1s". */
    constexpr float JoltStaggerChance = 0.08f;

    /** Drumbeat — "every 8th consecutive unmissed hit on one target staggers it". */
    constexpr int32 DrumbeatHitsRequired = 8;

    /** Steady Read — "+0.25 VitalDamageMultiplier while stationary". Additive to the multiplier, not a percentage. */
    constexpr float SteadyReadVitalMultiplierBonus = 0.25f;

    /** Steady Read — "no movement in the last 0.5s". */
    constexpr float SteadyReadStillnessSeconds = 0.5f;

    /** Moving Target — "vital hits while sprinting/strafing: flat +20% damage". */
    constexpr float MovingTargetVitalDamageBonus = 0.20f;

    /** Marksman's Due — "a vital hit returns 1 round to the magazine". */
    constexpr int32 MarksmansDueRoundsReturned = 1;

    /** Muffled Work — "hearing-aggro radius x0.5". */
    constexpr float MuffledWorkLoudnessScale = 0.5f;

    /** Dread Report — "hearing-aggro radius x1.5". */
    constexpr float DreadReportLoudnessScale = 1.5f;
}

UGA_Fire::UGA_Fire()
{
    // One instance per shot. Fire holds no state between activations.
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;

    // The base class defaults to ServerInitiated, which is correct for Reckoning
    // (a little activation delay before a super is unnoticeable) and wrong for a
    // trigger pull. LocalPredicted lets the client run the cosmetic half instantly
    // while the server independently runs the trace and damage.
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

    // Slot is a default only — DA_HunterAbilitySet overrides it at grant time.
    AbilitySlot = EGothicAbilitySlot::PrimaryFire;

    // The gating OnFire() never had. A dead or stunned player could previously
    // still trace and deal damage.
    ActivationBlockedTags.AddTag(GothicTags::State_Dead);
    ActivationBlockedTags.AddTag(GothicTags::State_Stunned);

    // Sprinting costs you the gun. This is the whole of the fire block — the
    // trigger is dead while the tag is held, with no queue and no auto-unsprint:
    // the player releases sprint, and only then does the shot exist.
    //
    // Belt and braces only. BP_GA_Fire is a Blueprint child of this class and
    // serializes its own copy of ActivationBlockedTags, so this default may not
    // reach the ability the game actually grants — the enforcement that cannot be
    // lost is the CanActivateAbility check below. Adding State.Sprinting to
    // BP_GA_Fire's container in the editor is still worth doing, so the block is
    // visible where a designer looks for it.
    ActivationBlockedTags.AddTag(GothicTags::State_Sprinting);
}

bool UGA_Fire::CanActivateAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayTagContainer* SourceTags,
    const FGameplayTagContainer* TargetTags,
    OUT FGameplayTagContainer* OptionalRelevantTags) const
{
    if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
    {
        return false;
    }

    const AGothicPlayerCharacter* Char =
        ActorInfo ? Cast<AGothicPlayerCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;

    // No shooting through the Selah moment. Refused at CanActivate rather than
    // swallowed later so no round is consumed and no cooldown is paid — the
    // trigger simply does nothing, and the ammo counter does not tick down on a
    // shot the player never got.
    if (Char && Char->IsSelahMomentLocked())
    {
        return false;
    }

    // No shooting out of a sprint, refused for the same reasons and in the same
    // place — no round spent, no cooldown paid, no queued shot waiting for the
    // sprint to end.
    //
    // Asked of the character directly rather than left to the State.Sprinting
    // entry in ActivationBlockedTags, because BP_GA_Fire is a Blueprint child and
    // serializes its own copy of that container: the constructor's tag does not
    // survive into the granted ability. This override does. The tag is still
    // applied and still worth having — it is how anything data-driven (a GE, a
    // future ability's blocked tags, an anim BP) can see the state — but the
    // guarantee lives here.
    if (Char && Char->AreGunActionsBlocked())
    {
        return false;
    }

    return Char && Char->HasRoundChambered();
}

void UGA_Fire::PlayFireMontage(AGothicPlayerCharacter* Char) const
{
    if (!FireMontage || !Char)
    {
        return;
    }

    USkeletalMeshComponent* Mesh = Char->GetMesh();
    UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
    if (!Anim)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GA_Fire: no AnimInstance on %s's mesh — fire montage cannot play."),
            *GetNameSafe(Char));
        return;
    }

    // Montage_Play, not PlayMontageAndWait: the ability is about to EndAbility on
    // this same frame and an ability task would be torn down with it. The montage
    // outliving the ability is the correct behaviour here — the shot is over, the
    // animation is still finishing.
    //
    // Restarting from the top on every shot is intentional: at a fire rate faster
    // than the animation, a re-triggered montage should snap back to the recoil
    // rather than politely queue, which is what "the gun keeps jogging" looked like.
    const float PlayedLength = Anim->Montage_Play(FireMontage, FMath::Max(0.01f, FireMontagePlayRate));

    // Diagnostic, and worth keeping. Montage_Play is silent on failure: a skeleton
    // mismatch, a montage with no valid segment, or a blocked slot all return 0.0
    // and look exactly like the animation "not playing" — which is how a slot-name
    // mismatch (Arms vs UpperBody) hid here for a whole round of debugging.
    //
    // A non-zero length means the montage IS playing and anything still wrong is
    // downstream in the AnimGraph — the slot not reaching the output pose, or a
    // layered blend overwriting it. Zero means it never started at all.
    //
    // Verbose, not Warning. This fires on EVERY shot, and at the Oversurge
    // Repeater's fire rate that is a line of "Warning" per few frames — enough
    // to bury the genuine warnings a boss encounter produces and make an
    // otherwise clean run look alarming. Nothing here is wrong when it prints;
    // it is a measurement. Turn it on with `Log LogTemp Verbose` when the gun's
    // animation is the thing under investigation.
    UE_LOG(LogTemp, Verbose,
        TEXT("GA_Fire: Montage_Play('%s') returned %.3f  [slot must be sampled by %s]"),
        *GetNameSafe(FireMontage), PlayedLength, *GetNameSafe(Anim->GetClass()));
}

void UGA_Fire::ActivateAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    // Exactly one CommitAbility. Two pays the cooldown twice (Slicer, Read); zero
    // makes the ability free (Reckoning). Both have already happened in this project.
    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }

    AGothicPlayerCharacter* Char = Cast<AGothicPlayerCharacter>(GetAvatarActorFromActorInfo());
    if (!Char)
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }
    
    // Also Verbose — same reasoning as the Montage_Play line above. Successful
    // activation is the expected case, once per shot, and logging it at Warning
    // made a normal magazine indistinguishable from a fault.
    UE_LOG(LogTemp, Verbose, TEXT("GA_Fire Activate: Auth=%d Local=%d | CooldownGE=%s | DamageGE=%s"),
    HasAuthority(&ActivationInfo) ? 1 : 0,
    ActorInfo->IsLocallyControlled() ? 1 : 0,
    *GetNameSafe(GetCooldownGameplayEffect()),
    *GetNameSafe(DamageEffectClass));

    // Runs on the predicting client and on the server. Both hold their own
    // unreplicated copy and run the same deterministic decrement, so they agree.
    // Not yet server-authoritative — see the AUTHORITY NOTE on FOnAmmoChanged.
    Char->ConsumeRound();

    // Cosmetic half — instant, local, never waits on the server.
    if (ActorInfo->IsLocallyControlled())
    {
        Char->ApplyRecoilKick();      // moves the player's aim
        Char->AddWeaponFireKick();    // moves the weapon in frame
        PlayFireMontage(Char);
        OnFireCosmetic();
    }

    // Authoritative half — the server alone decides what was hit and for how much,
    // and the server alone owns the perception system, so the noise is reported here
    // rather than alongside the cosmetics. Reported BEFORE the trace so a shot that
    // hits nothing is still just as loud.
    if (HasAuthority(&ActivationInfo))
    {
        // Reactive "is shooting" window on the player ASC. GA_Fire owns no
        // ActivationOwnedTags and EndAbility()s on this same frame, so without this a
        // reactive enemy affix's deferred BT re-check would never observe the shot.
        // On the authority ASC because loose tags do not replicate and the affix
        // decorators read the player tag server-side; the timer is ASC/world-anchored
        // so removal fires after this instance is gone, and re-firing just restarts it.
        if (UGothicAbilitySystemComponent* GothicASC =
                Cast<UGothicAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo()))
        {
            GothicASC->ApplyTimedLooseTag(GothicTags::State_Firing, FiringTagDuration);
        }

        ReportFireNoise(Char);
        PerformFireTrace(Char);
    }

    // Every path above reaches EndAbility. Fire has no projectile or async wait,
    // so there is no miss branch that can strand it (cf. the Slicer soft-lock).
    EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UGA_Fire::ApplyCooldown(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo) const
{
    const AGothicPlayerCharacter* Char =
        ActorInfo ? Cast<AGothicPlayerCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
    const UGothicWeaponData* WeaponData = Char ? Char->GetActiveWeaponData() : nullptr;

    // A weapon may bring its own cooldown GE; almost none will. Either way the
    // duration comes from the SetByCaller below, not from the asset.
    TSubclassOf<UGameplayEffect> CooldownGE = WeaponData && WeaponData->CooldownEffect
        ? WeaponData->CooldownEffect : CooldownGameplayEffectClass;

    if (!CooldownGE)
    {
        return;
    }

    FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(CooldownGE, GetAbilityLevel());
    if (!Spec.IsValid())
    {
        return;
    }

    float FireInterval = WeaponData
        ? WeaponData->GetFireInterval()
        : (FallbackRoundsPerMinute > 0.f ? 60.f / FallbackRoundsPerMinute : 0.f);

    // AbilityHaste is a percent cooldown reduction, clamped so gear can shorten
    // the interval but never reach zero. Note this only reaches cooldowns driven
    // by a Data.Cooldown SetByCaller — of the project's abilities that is Fire
    // alone; the rest carry fixed durations on their GE assets and are unaffected
    // until they are converted to the same pattern.
    if (const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
    {
        const float Haste = FMath::Clamp(
            ASC->GetNumericAttribute(UGothicAttributeSet::GetAbilityHasteAttribute()),
            0.f, MaxAbilityHastePercent);

        FireInterval *= (1.f - (Haste / 100.f));
    }

    Spec.Data->SetSetByCallerMagnitude(
        GothicTags::Data_Cooldown, FireInterval);

    ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, Spec);
}

TSubclassOf<UGameplayEffect> UGA_Fire::ResolvePerkStaggerEffect(const UGothicWeaponData* WeaponData) const
{
    if (PerkStaggerEffect)
    {
        return PerkStaggerEffect;
    }

    // The Shock stun is the only State.Stunned asset the weapon layer already
    // owns. Borrowing it means a perk on the electrical Rig works the day it
    // rolls, at that GE's duration rather than the doc's 1s — an honest
    // approximation, and it is why PerkStaggerEffect wins when it is set.
    return WeaponData ? WeaponData->ShockStunEffect : nullptr;
}

void UGA_Fire::ReportFireNoise(AGothicPlayerCharacter* Char) const
{
    if (!Char)
    {
        return;
    }

    const UGothicWeaponData* WeaponData = Char->GetActiveWeaponData();
    float Loudness = WeaponData ? WeaponData->FireNoiseLoudness : FallbackFireNoiseLoudness;

    // Muffled Work / Dread Report — "hearing-aggro radius x0.5 / x1.5".
    //
    // Scaling Loudness IS scaling the radius: the sense compares distance against
    // HearingRangeSq * Loudness^2, so the audible radius is
    // ListenerHearingRange * Loudness (see UGothicWeaponData::FireNoiseLoudness).
    // A x0.5 loudness therefore halves the radius exactly, per listener, and
    // leaves each enemy's own HearingRange the authority it is supposed to be.
    //
    // Opposite halves of one pair, so the else-if only guards a hand-authored
    // instance carrying both.
    if (Char->HasWeaponPerk(GothicTags::Perk_Weapon_VerbB_MuffledWork))
    {
        Loudness *= MuffledWorkLoudnessScale;
    }
    else if (Char->HasWeaponPerk(GothicTags::Perk_Weapon_VerbB_DreadReport))
    {
        Loudness *= DreadReportLoudnessScale;
    }

    // A weapon can be authored silent. Skip rather than report a zero-loudness
    // event the sense would discard anyway.
    if (Loudness <= 0.f)
    {
        return;
    }

    // The shooter, not the weapon mesh, and not the camera:
    //
    //   - Instigator MUST be the pawn. It becomes the perceived actor on the
    //     listener's side, which is the actor OnPerceptionUpdated hands to
    //     SetCombatTarget. Passing the ability, the weapon mesh, or nullptr would
    //     make enemies aggro onto something that is not the player (or, for
    //     nullptr, onto nothing while still burning the stimulus).
    //   - The location is the pawn's, not the camera's or the muzzle's. The
    //     weapon is hand-socket attached and has no guaranteed muzzle socket, and
    //     at an 800 cm audible radius the tens of centimetres between eye, hand
    //     and root are noise in the arithmetic, not signal.
    //
    // MaxRange is deliberately 0 — "no absolute limit". That leaves each
    // listener's own HearingRange as the authority on how far it can hear, which
    // is where the 800 cm on AGothicEnemyBase is configured. Passing a range here
    // would silently override that per-enemy tuning from the shooter's side.
    UAISense_Hearing::ReportNoiseEvent(
        Char->GetWorld(),
        Char->GetActorLocation(),
        Loudness,
        Char,
        /*MaxRange=*/0.f,
        FName(TEXT("Gunshot")));
}

namespace
{
    /**
     * How far along the aim direction to restart a trace that began inside the
     * victim. Larger than the depenetration distances Chaos reports on this
     * project's rigs, small enough that it cannot skip past a target the player
     * is standing in.
     */
    constexpr float PointBlankReTraceNudge = 25.f;

    /** Impacts closer than this to the trace start are treated as degenerate. */
    constexpr float PointBlankEpsilon = 1.f;

    /**
     * True when the trace began inside the thing it hit, so Hit.ImpactPoint
     * carries no usable geometry.
     *
     * Per FHitResult (Engine/HitResult.h): on an initial overlap Distance is 0,
     * ImpactPoint is set equal to Location "because there is no meaningful
     * single impact point to report", and Location for a line test is the start.
     * IsValidBlockingHit() is documented as false in exactly this case. The
     * shot still HIT -- bBlockingHit is true and the actor is real -- it is only
     * the impact POSITION that is meaningless.
     */
    bool IsDegenerateImpact(const FHitResult& Hit)
    {
        return Hit.bStartPenetrating
            || Hit.Distance <= PointBlankEpsilon
            || FVector::DistSquared(Hit.ImpactPoint, Hit.TraceStart)
                 <= (PointBlankEpsilon * PointBlankEpsilon);
    }

    /**
     * Recover a point on the line of fire that vital adjudication can honestly
     * use when the camera trace started inside the victim.
     *
     * Why this exists: GA_Fire traces from the CAMERA, so as soon as an enemy
     * closes to contact the ray originates inside its collision. Measured
     * against the 4m Lucid the impact degenerates to exactly the trace start,
     * distToVital reads 256+, and vital is 0 -- point-blank, the range at which
     * hitting her should be EASIEST, was the one range at which a vital could
     * not be scored at all.
     *
     * Two attempts, in order of how much they claim:
     *
     *   (a) Re-trace from slightly along the aim direction. If that lands a
     *       valid blocking hit on the SAME victim it is a real surface point
     *       and nothing is being invented.
     *   (b) Otherwise take the point on the aim ray nearest the vital. A ray
     *       cast from inside a convex body frequently reports initial overlap
     *       again rather than an exit surface, so (a) is not guaranteed; this
     *       falls back to the question the geometry can still answer honestly,
     *       "did the line of fire pass through the vital sphere", and lets
     *       IsVitalPointHit adjudicate it on its own terms. PenetrationDepth is
     *       deliberately unused -- it runs along Normal, not along the ray.
     *
     * Returns false when there is no vital component to resolve against, in
     * which case the caller leaves the shot exactly as it is today.
     */
    bool ResolvePointBlankImpact(
        UWorld* World, const AActor* Shooter, const AActor* Victim,
        const FVector& Start, const FVector& AimDir, float Range,
        FVector& OutImpactPoint)
    {
        if (!World || !Victim)
        {
            return false;
        }

        FCollisionQueryParams ReParams;
        ReParams.AddIgnoredActor(Shooter);

        FHitResult ReHit;
        const FVector ReStart = Start + (AimDir * PointBlankReTraceNudge);
        const FVector ReEnd   = Start + (AimDir * Range);

        if (World->LineTraceSingleByChannel(ReHit, ReStart, ReEnd, ECC_Weapon, ReParams)
            && ReHit.IsValidBlockingHit()
            && ReHit.GetActor() == Victim)
        {
            OutImpactPoint = ReHit.ImpactPoint;
            return true;
        }

        const UGothicVitalPointComponent* Vital =
            Victim->FindComponentByClass<UGothicVitalPointComponent>();
        if (!Vital)
        {
            return false;
        }

        // Closest point on the aim segment to the vital centre. Clamped to the
        // segment so a vital behind the shooter cannot resolve to a hit.
        const FVector VitalLoc = Vital->GetCurrentVitalWorldLocation();
        const float Along = FMath::Clamp(
            FVector::DotProduct(VitalLoc - Start, AimDir), 0.f, Range);

        OutImpactPoint = Start + (AimDir * Along);
        return true;
    }

    /**
     * Bullet magnetism target search — mouse-only aim assist.
     *
     * Returns the enemy a near-miss should bend onto: the one whose bearing
     * from the shot origin sits TIGHTEST to where the player is already aiming
     * (smallest angle), among those inside the MaxAngleDeg half-cone and within
     * Range. Tightest-to-aim rather than nearest-body is deliberate — it snaps
     * to the shot the player most nearly made, so pointing "close enough" reads
     * as intent honoured rather than the reticle grabbing whatever is closest.
     *
     * Dead/downed enemies are filtered by AGothicCharacterBase::IsFightableActor,
     * the project's single fightable predicate, so magnetism can never pull a
     * shot onto a corpse. No visibility test: the CALLER re-traces at the chosen
     * target and lets world geometry block it, so a target behind cover is found
     * here but the redirected trace still stops at the wall.
     *
     * Linear scan over live enemies — no spatial structure by design; the enemy
     * counts this game fields do not warrant one, and the search runs once per
     * shot only on the miss path.
     */
    AActor* FindMagnetismTarget(
        const UWorld* World, const AActor* Shooter,
        const FVector& Start, const FVector& AimDir, float Range, float MaxAngleDeg)
    {
        if (!World || MaxAngleDeg <= 0.f)
        {
            return nullptr;
        }

        // Compare cosines rather than angles: a larger dot is a smaller angle, so
        // the cone edge becomes a single floor the winner must beat.
        const float CosThreshold = FMath::Cos(FMath::DegreesToRadians(MaxAngleDeg));
        const float RangeSq = Range * Range;

        AActor* Best = nullptr;
        float BestDot = CosThreshold;

        for (TActorIterator<AGothicEnemyBase> It(World); It; ++It)
        {
            AGothicEnemyBase* Enemy = *It;
            if (Enemy == Shooter || !AGothicCharacterBase::IsFightableActor(Enemy))
            {
                continue;
            }

            const FVector ToEnemy = Enemy->GetActorLocation() - Start;
            if (ToEnemy.SizeSquared() > RangeSq)
            {
                continue;
            }

            const FVector Dir = ToEnemy.GetSafeNormal();
            const float Dot = FVector::DotProduct(AimDir, Dir);
            if (Dot > BestDot)
            {
                BestDot = Dot;
                Best = Enemy;
            }
        }

        return Best;
    }
}

void UGA_Fire::PerformFireTrace(AGothicPlayerCharacter* Char)
{
    UWorld* World = Char->GetWorld();

    // Only half of the old `!Camera || !World` guard survives as a bail. A missing
    // world is unrecoverable and should never happen; a missing camera is neither,
    // and treating it as fatal is what made a remote player's shots vanish. It also
    // returned in silence, above every line in this function, which is why the
    // symptom read as "activated: true and then nothing at all".
    if (!World)
    {
        UE_LOG(LogVigilCombat, Warning,
            TEXT("GA_Fire: %s has no world — fire trace abandoned."), *GetNameSafe(Char));
        return;
    }

    // ── Where the shot comes from ────────────────────────────────────────────
    // This function used to read the camera unconditionally. That is right for the
    // pawn whose camera it actually is and wrong everywhere else, because GA_Fire is
    // LocalPredicted: the authoritative half that calls this runs on the SERVER for
    // every player, remote ones included. A remote pawn's server-side copy does own a
    // UCameraComponent — it is a default subobject on AGothicPlayerCharacter, so it is
    // present on every instance — but nothing makes it point where that player is
    // looking. It is parented to an animation bone and resolves bUsePawnControlRotation
    // against a control rotation the server only learns at move-update cadence.
    //
    // So the camera is used exactly where it is the genuine view — locally controlled —
    // and everything else falls through to the two sources the engine provides for
    // precisely this case:
    //
    //   controller — AController::GetPlayerViewPoint, which resolves through the view
    //                target and works server-side for a player controller.
    //   aim        — the pawn's eye height plus GetBaseAimRotation, which is built to
    //                answer "where is this pawn aiming" without a local view.
    //
    // The locally-controlled path is byte-identical to the old code: the camera's
    // component rotation Vector() is its forward vector, and Start is its location.
    // origin= in the Fire line below records which source fired, so a multiplayer pass
    // can separate a bad view from a bad trace instead of inferring it.
    FVector  ViewLocation = FVector::ZeroVector;
    FRotator ViewRotation = FRotator::ZeroRotator;
    const TCHAR* OriginSource = TEXT("aim");
    bool bHaveView = false;

    UCameraComponent* Camera = Char->IsLocallyControlled()
        ? Char->FindComponentByClass<UCameraComponent>()
        : nullptr;

    if (Camera)
    {
        ViewLocation = Camera->GetComponentLocation();
        ViewRotation = Camera->GetComponentRotation();
        OriginSource = TEXT("camera");
        bHaveView = true;
    }
    else if (AController* OwningController = Char->GetController())
    {
        OwningController->GetPlayerViewPoint(ViewLocation, ViewRotation);
        OriginSource = TEXT("controller");
        bHaveView = true;
    }

    // A view point at the world origin is not a view point. GetPlayerViewPoint fills
    // its outputs unconditionally, so a controller with no view target yet hands back
    // zeroes rather than failing — which would fire every shot from (0,0,0) and read
    // in the log as a clean miss forever.
    if (!bHaveView || ViewLocation.IsNearlyZero())
    {
        ViewLocation = Char->GetActorLocation() + FVector(0.f, 0.f, Char->BaseEyeHeight);
        ViewRotation = Char->GetBaseAimRotation();
        OriginSource = TEXT("aim");
    }

    // Read stats from the active weapon data, falling back to GA_Fire's own defaults
    const UGothicWeaponData* WeaponData = Char->GetActiveWeaponData();
    const float EffectiveDamage     = WeaponData ? WeaponData->Damage             : Damage;
    const float EffectiveVitalMult  = WeaponData ? WeaponData->VitalDamageMultiplier : VitalDamageMultiplier;
    const float EffectiveRange      = WeaponData ? WeaponData->TraceRange         : TraceRange;
    TSubclassOf<UGameplayEffect> EffectiveDamageGE = WeaponData && WeaponData->DamageEffect
        ? WeaponData->DamageEffect : DamageEffectClass;

    // Spread, off by default — both cones are 0, so this is still the perfect ray
    // the weapon has always fired. Wired now so accuracy can become the reason to
    // aim later without touching the trace again; until then aiming pays off in FOV
    // and the crosshair only.
    //
    // Note the crosshair has always drawn a spread the bullets did not have. Turning
    // these on is what finally makes the reticle honest.
    const float SpreadDegrees = Char->IsAiming() ? ADSSpreadDegrees : HipFireSpreadDegrees;

    const FVector ViewForward = ViewRotation.Vector();

    // AimDir/End/bHit are not const: bullet magnetism (below) may redirect the
    // shot onto a near-miss target after the first trace, and everything
    // downstream — vital adjudication, point-blank recovery, telemetry — must
    // read the direction the bullet ACTUALLY travelled, not the raw aim.
    FVector AimDir = SpreadDegrees > 0.f
        ? FMath::VRandCone(ViewForward, FMath::DegreesToRadians(SpreadDegrees))
        : ViewForward;

    // Not const: the muzzle re-origin below (two-stage hybrid trace) rewrites Start to
    // the gun muzzle once the camera ray has found the aim point, so telemetry and every
    // downstream reader report the origin the bullet ACTUALLY left from.
    FVector Start = ViewLocation;
    FVector End = Start + (AimDir * EffectiveRange);

    FHitResult Hit;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(Char);

    bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Weapon, Params);

    // ── Bullet magnetism (mouse aim assist) ──────────────────────────────
    // PC aim assist bends the SHOT, never the camera or crosshair. A shot that
    // missed, or that hit only environment, is snapped onto the enemy sitting
    // tightest inside the weapon's magnetism cone — so pointing "close enough"
    // lands without the reticle ever moving. A shot that already hit a damage
    // target is left exactly as it was: the assist only rescues shots the raw
    // aim did not already land.
    //
    // "Hit a damage target" is the same discrimination the environment-rejection
    // below uses — an actor with an ASC. Gated on the weapon data so a weapon
    // (bEnableMagnetism off, or MagnetismAngleDeg 0) opts out, and so the cone is
    // authored, never hardcoded here.
    const bool bHitDamageTarget = bHit && Hit.GetActor()
        && UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Hit.GetActor()) != nullptr;

    if (!bHitDamageTarget && WeaponData && WeaponData->bEnableMagnetism)
    {
        if (AActor* SnapTarget = FindMagnetismTarget(
                World, Char, Start, AimDir, EffectiveRange, WeaponData->MagnetismAngleDeg))
        {
            const FVector SnapDir = (SnapTarget->GetActorLocation() - Start).GetSafeNormal();
            if (!SnapDir.IsNearlyZero())
            {
                // Redirect at the target's pivot and take whatever THIS ray hits.
                // Geometry still blocks it — a target behind cover is found but the
                // redirected trace stops at the wall, so magnetism never shoots
                // through walls. Aiming at the pivot (not a vital) is per brief; a
                // vitals-first snap is the named follow-up.
                AimDir = SnapDir;
                End    = Start + (AimDir * EffectiveRange);

                FHitResult SnapHit;
                const bool bSnapHit = World->LineTraceSingleByChannel(
                    SnapHit, Start, End, ECC_Weapon, Params);

                UE_LOG(LogVigilCombat, Verbose,
                    TEXT("VigilTimeline|t=%.3f|%s|Magnetism|snapTarget=%s|redirectHit=%s|coneDeg=%.2f"),
                    World->GetTimeSeconds(), *GetNameSafe(Char), *GetNameSafe(SnapTarget),
                    (bSnapHit && SnapHit.GetActor()) ? *SnapHit.GetActor()->GetName() : TEXT("none"),
                    WeaponData->MagnetismAngleDeg);

                Hit  = SnapHit;
                bHit = bSnapHit;
            }
        }
    }

    // ── Muzzle re-origin (two-stage hybrid trace) ────────────────────────────
    // Everything above was STAGE ONE: the camera/reticle ray (magnetism included)
    // establishing WHERE the player is aiming — its first blocking hit, or its end at
    // max range on a clean miss. That point is authoritative for accuracy and must not
    // move. STAGE TWO re-runs the actual damage trace from the gun MUZZLE, aimed THROUGH
    // that same point and extended onward to the weapon's full range, so the shot leaves
    // the barrel (fixing gun/reticle parallax) while the reticle stays honest by
    // construction. See the user directive: "a line trace from the muzzle of the gun to
    // the reticle and onward."
    const FVector CameraAimPoint = bHit ? Hit.ImpactPoint : End;

    FVector MuzzleLocation = FVector::ZeroVector;
    const TCHAR* MuzzleSource = TEXT("camera-fallback");
    const bool bHaveMuzzle =
        Char->ResolveMuzzleLocation(Char->IsLocallyControlled(), MuzzleLocation, MuzzleSource);

    if (bHaveMuzzle)
    {
        const FVector ToAim = CameraAimPoint - MuzzleLocation;

        // Degenerate direction (aim point sitting on top of the muzzle) or an aim point
        // BEHIND the muzzle — extreme close range where the barrel has already passed the
        // target. The muzzle ray would point the wrong way, so keep the camera trace
        // result exactly as it stands. Dot against the camera AimDir answers "is the aim
        // point still downrange of the muzzle".
        const bool bBehind = FVector::DotProduct(ToAim, AimDir) <= 0.f;

        if (!ToAim.IsNearlyZero() && !bBehind)
        {
            const FVector MuzzleDir = ToAim.GetSafeNormal();
            const FVector MuzzleEnd = MuzzleLocation + (MuzzleDir * EffectiveRange);

            FHitResult MuzzleHit;
            // Same ignore set as stage one — the shooter, which also covers the player's
            // own first-person arms/weapon meshes (they are components of Char, so the
            // ray starting AT the gun can never catch the barrel it left).
            FCollisionQueryParams MuzzleParams;
            MuzzleParams.AddIgnoredActor(Char);

            const bool bMuzzleHit = World->LineTraceSingleByChannel(
                MuzzleHit, MuzzleLocation, MuzzleEnd, ECC_Weapon, MuzzleParams);

            // Adopt the muzzle trace as the authoritative shot; Start/AimDir/End follow so
            // adjudication, point-blank recovery, and telemetry all read the true origin.
            Hit         = MuzzleHit;
            bHit        = bMuzzleHit;
            Start       = MuzzleLocation;
            AimDir      = MuzzleDir;
            End         = MuzzleEnd;
            OriginSource = MuzzleSource;
        }
        else
        {
            // Close-range fallback: shot keeps the camera trace result.
            OriginSource = TEXT("camera-closerange");
        }
    }

    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|Fire|TRACE|origin=%s|muzzle=%s|aimPoint=%s|range=%.0f"),
        World->GetTimeSeconds(), *GetNameSafe(Char), OriginSource,
        bHaveMuzzle ? *MuzzleLocation.ToCompactString() : TEXT("none"),
        *CameraAimPoint.ToCompactString(), EffectiveRange);

    // ── Fire telemetry ───────────────────────────────────────────────────────
    // Exactly one line per fire resolution, miss included, on every path out of
    // this function below the trace. A silent miss is the precise thing this
    // exists to catch: a verification pass measured vital-aimed shots dealing
    // zero damage while body shots landed, same spawn, clean A/B, and could not
    // adjudicate it because a missed trace and a landed non-vital hit were
    // indistinguishable in the log — neither wrote anything.
    //
    // distToVital is the number that settles it. It is the impact point to the
    // victim's CURRENT projected vital, so a systematic aim-vs-trace parallax
    // shows up as a steady non-zero offset across shots rather than a mystery,
    // and traceStart/aimDir give the origin to compare the camera ray against.
    //
    // Observation only. Nothing in this block changes what a shot does.
    const float TimelineNow = World->GetTimeSeconds();

    // Set below when the camera trace started inside the victim and the impact
    // point had to be recovered. Declared here so the telemetry lambda reports
    // it on every path out.
    bool bPointBlank = false;

    auto EmitFireTimeline =
        [&](const AActor* Victim, const FVector& ImpactPoint, bool bVital, float RawDamage)
    {
        float DistToVital = -1.f;
        if (Victim)
        {
            if (const UGothicVitalPointComponent* VictimVital =
                    Victim->FindComponentByClass<UGothicVitalPointComponent>())
            {
                DistToVital = FVector::Dist(ImpactPoint, VictimVital->GetCurrentVitalWorldLocation());
            }
        }

        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VigilTimeline|t=%.3f|%s|Fire|%s|victim=%s|impact=%s|vital=%d|distToVital=%.1f|")
            TEXT("raw=%.1f|traceStart=%s|aimDir=%s|pointBlank=%d|origin=%s"),
            TimelineNow,
            *GetNameSafe(Char),
            Victim ? TEXT("HIT") : TEXT("MISS"),
            Victim ? *Victim->GetName() : TEXT("none"),
            *ImpactPoint.ToCompactString(),
            bVital ? 1 : 0,
            DistToVital,
            RawDamage,
            *Start.ToCompactString(),
            *AimDir.ToCompactString(),
            bPointBlank ? 1 : 0,
            OriginSource);
    };

    if (!bHit || !Hit.GetActor())
    {
        EmitFireTimeline(nullptr, bHit ? Hit.ImpactPoint : End, /*bVital=*/ false, /*RawDamage=*/ 0.f);

        // Whiff — a wasted shot, one of the openings the Retaliator affix punishes.
        // Open State.Whiffed on the player ASC (a timed loose tag, self-clearing) so
        // the affix has an observable window. Signal used is the PURE trace-miss:
        // this branch is the bullet's final (post-muzzle-re-origin) trace hitting
        // NOTHING at all — not a damage check — so a shot that connected with world
        // geometry or an immune target is NOT a whiff (it hit something). Runs on the
        // authority path only; PerformFireTrace is called under HasAuthority.
        if (UGothicAbilitySystemComponent* GothicASC =
                Cast<UGothicAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo()))
        {
            GothicASC->ApplyTimedLooseTag(GothicTags::State_Whiffed, WhiffTagDuration);
        }

        // A miss breaks the Oversurge streak. Done here rather than on the
        // damage path so shooting a wall counts as a miss too -- the streak is
        // "hits without missing", not "hits since the last hit".
        Char->ResetConsecutiveHits();
        return;
    }

    UAbilitySystemComponent* TargetASC =
        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Hit.GetActor());
    UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();

    if (!TargetASC || !SourceASC || !EffectiveDamageGE)
    {
        // A landed shot that will never apply damage — the world geometry case,
        // and the one that reads as "the shot did nothing" without a line.
        EmitFireTimeline(Hit.GetActor(), Hit.ImpactPoint, /*bVital=*/ false, /*RawDamage=*/ 0.f);
        return;
    }

    // Damage model — WEAPON_ARCHETYPES.md, DECIDED 2026-08-04. Three factors,
    // and no universal armor-damage anywhere:
    //
    //   Weapon base damage — EffectiveDamage. The weapon's authored number, which
    //                        the Calibration Reference states as the Tier-1 value.
    //   Weapon tier        — this weapon INSTANCE's own Gear Power over the Tier-1
    //                        baseline. Not the armor average: ten armor slots used
    //                        to vote on how hard a Revolver hits while the
    //                        Revolver's own rarity did nothing, and the average
    //                        diluted every armor upgrade to a tenth of its face
    //                        value. Armor tier now pays out through Gear Score →
    //                        Attack Power instead (ITEMIZATION_AND_LOOT.md), which
    //                        lands in GothicAttributeSet, not here.
    //   Archetype bonus    — armor's per-archetype damage lines, and ONLY the one
    //                        matching the equipped weapon's archetype. A Revolver
    //                        line does nothing while a Rifle is out.
    const float WeaponTierMult = Char->GetActiveWeaponTierMultiplier();

    const float ArchetypeBonusPct = WeaponData
        ? Char->GetArchetypeDamageBonusPct(WeaponData->Archetype)
        : 0.f;

    float FinalDamage = EffectiveDamage * WeaponTierMult * (1.f + ArchetypeBonusPct / 100.f);
    bool bIsVitalHit = false;

    // The point vital adjudication is measured from. Normally the impact; at
    // point-blank the impact is the trace start and means nothing, so recover
    // something on the line of fire that does. Damage itself is untouched by
    // this -- the shot hit, and it applies exactly as it did before either way.
    FVector VitalTestPoint = Hit.ImpactPoint;
    if (IsDegenerateImpact(Hit))
    {
        FVector Recovered;
        if (ResolvePointBlankImpact(World, Char, Hit.GetActor(),
                                    Start, AimDir, EffectiveRange, Recovered))
        {
            VitalTestPoint = Recovered;
            bPointBlank = true;
        }
    }

    // Diagnostics only — the pre-vital damage and the two "why was this a vital"
    // flags, kept so the final line below can tie the geometry to the outcome.
    const float PreVitalDamage = FinalDamage;
    bool bReckoningForced = false;
    bool bReadAmplified   = false;

    if (UGothicVitalPointComponent* VitalPoint =
            Hit.GetActor()->FindComponentByClass<UGothicVitalPointComponent>())
    {
        const bool bReckoning = SourceASC->HasMatchingGameplayTag(
            GothicTags::State_Reckoning);
        const float RadiusBonus = SourceASC->GetNumericAttribute(
            UGothicAttributeSet::GetVitalPointRadiusAttribute());

        bIsVitalHit = bReckoning || VitalPoint->IsVitalPointHit(VitalTestPoint, RadiusBonus);

        bReckoningForced = bReckoning;

        // Reckoning short-circuits the || — when it is up the geometry test never
        // runs and no VitalGeometry line is emitted at all. This line exists so a
        // reader can tell a genuine geometric vital from a forced one instead of
        // assuming the component silently failed.
        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VitalResult: target=%s vital=%s via=%s (RadiusBonus=%.1f, testPoint=%s%s)"),
            *Hit.GetActor()->GetName(),
            bIsVitalHit ? TEXT("YES") : TEXT("NO"),
            bReckoning ? TEXT("RECKONING-FORCED, geometry not evaluated") : TEXT("geometry"),
            RadiusBonus,
            *VitalTestPoint.ToCompactString(),
            bPointBlank ? TEXT(" POINT-BLANK, recovered from a degenerate impact")
                        : TEXT(""));
    }
    else
    {
        UE_LOG(LogVigilCombat, Verbose,
            TEXT("VitalResult: target=%s has no UGothicVitalPointComponent — body hit by definition"),
            *Hit.GetActor()->GetName());
    }

    if (bIsVitalHit)
    {
        // Steady Read — "+0.25 VitalDamageMultiplier while stationary (no movement
        // input in the last 0.5s)". ADDITIVE to the multiplier, so a 2.0x weapon
        // becomes 2.25x rather than gaining a 25% damage line; that is what the
        // doc's units say and it is the difference between +0.25 and +25%.
        float VitalMult = EffectiveVitalMult;
        if (Char->HasWeaponPerk(GothicTags::Perk_Weapon_VerbA_SteadyRead) &&
            Char->GetStationaryDuration() >= SteadyReadStillnessSeconds)
        {
            VitalMult += SteadyReadVitalMultiplierBonus;
        }

        FinalDamage *= VitalMult;

        // Moving Target — "vital hits while sprinting/strafing: flat +20% damage".
        // The mirror of Steady Read, and mutually exclusive with it in practice
        // rather than by code: one pays for standing still, the other for not.
        if (Char->HasWeaponPerk(GothicTags::Perk_Weapon_VerbA_MovingTarget) &&
            Char->IsMovingUnderOwnPower())
        {
            FinalDamage *= (1.f + MovingTargetVitalDamageBonus);
        }

        // Marksman's Due — "a vital hit returns 1 round to the magazine". Creates
        // the round rather than pulling it from reserve, and no-ops on a full
        // magazine (see AddRoundsToMagazine). Granted on the vital, before the
        // damage goes out, so a kill still pays it.
        if (Char->HasWeaponPerk(GothicTags::Perk_Weapon_VerbA_MarksmansDue))
        {
            Char->AddRoundsToMagazine(MarksmansDueRoundsReturned);
        }

        // The Read: vital hits hurt more against a target you have READ, and
        // only that target. Checked on TargetASC, not SourceASC -- as a caster
        // tag it sharpened every shot at every enemy in the room for the whole
        // window, which made it a flat damage cooldown rather than an act of
        // reading one opponent.
        if (TargetASC->HasMatchingGameplayTag(
                GothicTags::State_Read_Marked))
        {
            FinalDamage *= ReadVitalDamageMultiplier;
            bReadAmplified = true;
        }
    }

    // Unchanged behaviour, now expressed through the shared helper — GA_Fire is
    // where the avatar-as-instigator pattern was already correct, and it is the
    // shape every other damage site has been brought up to.
    FGameplayEffectContextHandle Context =
        UGothicAbilitySystemComponent::MakeDamageContext(SourceASC, Char);

    FGameplayEffectSpecHandle Spec =
        SourceASC->MakeOutgoingSpec(EffectiveDamageGE, GetAbilityLevel(), Context);

    if (!Spec.IsValid())
    {
        EmitFireTimeline(Hit.GetActor(), VitalTestPoint, bIsVitalHit, /*RawDamage=*/ 0.f);
        return;
    }

    // ── Shock: streak, Oversurge, stun ───────────────────────────────────
    // Registered before the damage spec is finalised so an Oversurge can scale
    // THIS shot rather than the next one.
    Char->RegisterWeaponHit();

    bool bOversurged = false;
    if (WeaponData && WeaponData->OversurgeHitsRequired > 0 &&
        Char->GetConsecutiveHits() >= WeaponData->OversurgeHitsRequired &&
        FMath::FRand() < WeaponData->OversurgeChance)
    {
        FinalDamage *= WeaponData->OversurgeDamageMultiplier;
        bOversurged = true;

        // Spend the streak. Without this every subsequent hit keeps rolling at
        // full chance, which turns a payoff into a sustained damage multiplier.
        Char->ResetConsecutiveHits();
    }

    Spec.Data->SetSetByCallerMagnitude(
        GothicTags::Data_Damage, FinalDamage);

    // Stamp vital-ness onto the spec so the choke point can branch the flinch and
    // shape passes on it. GA_Fire is the only site that resolves a vital today, so
    // it is the only site that sets this; every other damage path leaves it unset,
    // which the choke point reads as a body hit (default 0). No GE modifier reads
    // this channel — it is carried purely so PostGameplayEffectExecute can see it.
    Spec.Data->SetSetByCallerMagnitude(
        GothicTags::Data_VitalHit, bIsVitalHit ? 1.f : 0.f);

    SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);

    // One line tying the geometry above to the number that went out on the wire.
    // sent=, not applied=: this is the SetByCaller magnitude — the doc's `Raw`,
    // before the attribute set applies the base AP/Def core and the two gear
    // ratios (UGothicAttributeSet::PostGameplayEffectExecute). It is NOT the
    // health delta, and reading it as one has cost a false anomaly.
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("Damage: target=%s sent=%.1f (base=%.1f weaponTier=x%.2f pre-vital=%.1f ")
        TEXT("vital=%s reckoning=%s read=%s oversurge=%s)"),
        *Hit.GetActor()->GetName(),
        FinalDamage,
        EffectiveDamage,
        WeaponTierMult,
        PreVitalDamage,
        bIsVitalHit      ? TEXT("yes") : TEXT("no"),
        bReckoningForced ? TEXT("yes") : TEXT("no"),
        bReadAmplified   ? TEXT("yes") : TEXT("no"),
        bOversurged      ? TEXT("yes") : TEXT("no"));

    // The landed-and-damaged case, in the correlatable format. raw= is the
    // magnitude that just went into the SetByCaller, after every multiplier.
    EmitFireTimeline(Hit.GetActor(), VitalTestPoint, bIsVitalHit, FinalDamage);

    // Super meter on a LANDED hit. Applied here rather than on activation so a
    // miss builds nothing -- the weapon assets have authored
    // SuperGainOnHitEffect since they were written, but nothing read it, so
    // shooting was worth zero super and melee/kills were the only sources.
    // Same SetByCaller contract as GA_HuntersStrike.
    if (WeaponData && WeaponData->SuperGainOnHitEffect && WeaponData->SuperGainOnHit > 0.f)
    {
        FGameplayEffectContextHandle SuperContext =
            UGothicAbilitySystemComponent::MakeDamageContext(SourceASC, Char);

        FGameplayEffectSpecHandle SuperSpec =
            SourceASC->MakeOutgoingSpec(WeaponData->SuperGainOnHitEffect, 1.f, SuperContext);

        if (SuperSpec.IsValid())
        {
            // Kindling — "SuperGainOnHit +60% (5 -> 8)" (WEAPON_PERK_TABLES.md,
            // Verb Bucket A). The first effect wired, and still the shape every
            // other one takes: read the perk off the character at the site that
            // already owns the number.
            const float SuperGain = Char->HasWeaponPerk(GothicTags::Perk_Weapon_VerbA_Kindling)
                ? WeaponData->SuperGainOnHit * 1.6f
                : WeaponData->SuperGainOnHit;

            SuperSpec.Data->SetSetByCallerMagnitude(
                GothicTags::Data_SuperMeter, SuperGain);

            // To SELF: the meter belongs to the shooter, not the thing shot.
            SourceASC->ApplyGameplayEffectSpecToSelf(*SuperSpec.Data.Get());
        }
    }

    // Stun rolls independently of Oversurge -- they are separate hooks and a
    // single shot is allowed to do both.
    if (WeaponData && WeaponData->ShockStunEffect && WeaponData->StunChance > 0.f &&
        FMath::FRand() < WeaponData->StunChance)
    {
        UGothicAbilitySystemComponent::ApplyEffectToASC(
            TargetASC, WeaponData->ShockStunEffect, Char);
    }

    // ── Perk staggers: Jolt (chance) and Drumbeat (streak) ───────────────
    //
    // Both ride the same mechanism the Shock stun above uses — a GE carrying
    // State.Stunned, applied to the victim's ASC. They roll independently of it
    // and of each other for the same reason Oversurge and the stun do: separate
    // hooks, and one shot is allowed to do more than one thing.
    //
    // The GE comes from ResolvePerkStaggerEffect, which prefers the ability's own
    // PerkStaggerEffect and falls back to the weapon's ShockStunEffect. With
    // neither assigned both perks are inert and say so once per shot at Verbose —
    // GE_Stagger is EMPTY in data, so a stagger asset is a real editor follow-up
    // and not something this code can conjure.
    const bool bWantsJolt = Char->HasWeaponPerk(GothicTags::Perk_Weapon_VerbA_Jolt) &&
        FMath::FRand() < JoltStaggerChance;

    // Drumbeat — "every 8th consecutive unmissed hit". RegisterWeaponHit already
    // ran above, so the streak includes THIS hit and the 8th shot is the one that
    // staggers. Reload does not touch the streak (nothing in the reload path
    // calls ResetConsecutiveHits); a miss and a weapon swap both clear it, which
    // is exactly the semantics the doc asks for.
    //
    // CAVEAT, flagged rather than worked around: the streak is shared with
    // Oversurge, which SPENDS it on a proc. On a weapon carrying both, an
    // Oversurge resets the count and pushes Drumbeat's next stagger out. Today
    // only the electrical Rig sets OversurgeHitsRequired, so the overlap is one
    // weapon; splitting the counters is a design call, not a bug fix.
    // The doc's "on one target" is likewise NOT enforced — the streak is
    // per-character, not per-victim, and making it per-victim is a wider change.
    const bool bWantsDrumbeat = Char->HasWeaponPerk(GothicTags::Perk_Weapon_VerbA_Drumbeat) &&
        Char->GetConsecutiveHits() > 0 &&
        (Char->GetConsecutiveHits() % DrumbeatHitsRequired) == 0;

    if (bWantsJolt || bWantsDrumbeat)
    {
        if (TSubclassOf<UGameplayEffect> StaggerGE = ResolvePerkStaggerEffect(WeaponData))
        {
            UGothicAbilitySystemComponent::ApplyEffectToASC(TargetASC, StaggerGE, Char);
        }
        else
        {
            UE_LOG(LogVigilCombat, Verbose,
                TEXT("PerkStagger: %s wanted to stagger %s but no stagger GE is assigned ")
                TEXT("(BP_GA_Fire::PerkStaggerEffect and the weapon's ShockStunEffect are both null)"),
                bWantsJolt ? TEXT("Jolt") : TEXT("Drumbeat"),
                *Hit.GetActor()->GetName());
        }
    }

    if (AGothicEnemyBase* HitEnemy = Cast<AGothicEnemyBase>(Hit.GetActor()))
    {
        // An Oversurge reads as a vital hit to the feedback layer so it gets the
        // heavier number and flash rather than passing as an ordinary tick.
        HitEnemy->MulticastOnHit(
            Hit.ImpactPoint, bIsVitalHit || bOversurged, FinalDamage, Char);
    }

}