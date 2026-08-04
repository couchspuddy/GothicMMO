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
    ActivationBlockedTags.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Dead")));
    ActivationBlockedTags.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Stunned")));
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
        FGameplayTag::RequestGameplayTag(FName("Data.Cooldown")), FireInterval);

    ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, Spec);
}

void UGA_Fire::ReportFireNoise(AGothicPlayerCharacter* Char) const
{
    if (!Char)
    {
        return;
    }

    const UGothicWeaponData* WeaponData = Char->GetActiveWeaponData();
    const float Loudness = WeaponData ? WeaponData->FireNoiseLoudness : FallbackFireNoiseLoudness;

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

    const FVector AimDir = SpreadDegrees > 0.f
        ? FMath::VRandCone(ViewForward, FMath::DegreesToRadians(SpreadDegrees))
        : ViewForward;

    const FVector Start = ViewLocation;
    const FVector End   = Start + (AimDir * EffectiveRange);

    FHitResult Hit;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(Char);

    const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Weapon, Params);

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

    // Damage model — ITEMIZATION_AND_LOOT.md plus the Gear Power / Attack Power
    // split. Three factors, and no universal armor-damage anywhere:
    //
    //   Weapon base damage — EffectiveDamage. The weapon's own attack power, per
    //                        archetype. Armor never scales this universally.
    //   Gear Power floor   — the AGGREGATE power level across ALL equipped gear,
    //                        not just this weapon. It raises the floor of every
    //                        hit and is the intended activity gate. Relative to a
    //                        baseline so a starting loadout scales by ~1.0.
    //   Archetype bonus    — armor's per-archetype damage lines, and ONLY the one
    //                        matching the equipped weapon's archetype. A Revolver
    //                        line does nothing while a Rifle is out.
    const int32 AggregateGearPower = Char->GetAggregateGearPower();
    const float GearFloor = (BaselineGearPower > 0.f && AggregateGearPower > 0)
        ? static_cast<float>(AggregateGearPower) / BaselineGearPower
        : 1.f;

    const float ArchetypeBonusPct = WeaponData
        ? Char->GetArchetypeDamageBonusPct(WeaponData->Archetype)
        : 0.f;

    float FinalDamage = EffectiveDamage * GearFloor * (1.f + ArchetypeBonusPct / 100.f);
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
            FGameplayTag::RequestGameplayTag(FName("State.Reckoning")));
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
        FinalDamage *= EffectiveVitalMult;

        // The Read: vital hits hurt more against a target you have READ, and
        // only that target. Checked on TargetASC, not SourceASC -- as a caster
        // tag it sharpened every shot at every enemy in the room for the whole
        // window, which made it a flat damage cooldown rather than an act of
        // reading one opponent.
        if (TargetASC->HasMatchingGameplayTag(
                FGameplayTag::RequestGameplayTag(FName("State.Read.Marked"))))
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
        FGameplayTag::RequestGameplayTag(FName("Data.Damage")), FinalDamage);

    SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);

    // One line tying the geometry above to the number that went out on the wire.
    // sent=, not applied=: this is the SetByCaller magnitude, before the attribute
    // set adds AttackPower and subtracts Defense (GothicAttributeSet.cpp:222). It
    // is NOT the health delta, and reading it as one has cost a false anomaly.
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("Damage: target=%s sent=%.1f (pre-vital=%.1f vital=%s reckoning=%s read=%s oversurge=%s)"),
        *Hit.GetActor()->GetName(),
        FinalDamage,
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
            SuperSpec.Data->SetSetByCallerMagnitude(
                GothicTags::Data_SuperMeter, WeaponData->SuperGainOnHit);

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

    if (AGothicEnemyBase* HitEnemy = Cast<AGothicEnemyBase>(Hit.GetActor()))
    {
        // An Oversurge reads as a vital hit to the feedback layer so it gets the
        // heavier number and flash rather than passing as an ordinary tick.
        HitEnemy->MulticastOnHit(
            Hit.ImpactPoint, bIsVitalHit || bOversurged, FinalDamage, Char);
    }

}