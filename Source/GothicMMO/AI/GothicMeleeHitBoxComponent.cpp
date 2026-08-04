// GothicMeleeHitboxComponent.cpp

#include "AI/GothicMeleeHitboxComponent.h"
#include "GothicMMO.h"                      // LogVigilCombat
#include "AI/GothicEnemyBase.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicGameplayTags.h"
#include "Character/GothicCharacterBase.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"

namespace
{
    /**
     * The `t=` stamp every VigilTimeline line carries. Free function so this file
     * needs no header change to log, and it answers 0 rather than asserting for a
     * component logging outside a world — this is a diagnostic, not a rule.
     */
    float HitboxTimelineNow(const UActorComponent* Component)
    {
        const UWorld* World = Component ? Component->GetWorld() : nullptr;
        return World ? World->GetTimeSeconds() : 0.f;
    }
}

UGothicMeleeHitboxComponent::UGothicMeleeHitboxComponent(
    const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick = false;

    // Direct member set — avoids UpdateBodySetup() which calls NewObject
    BoxExtent = FVector(40.f, 60.f, 30.f);

    // Don't render in game
    bHiddenInGame = true;

#if WITH_EDITORONLY_DATA
    ShapeColor = FColor::Red;
#endif
}

void UGothicMeleeHitboxComponent::BeginPlay()
{
    Super::BeginPlay();

    OnComponentBeginOverlap.AddDynamic(
        this, &UGothicMeleeHitboxComponent::OnHitboxOverlap);

    // Collision setup here, not in constructor
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetCollisionResponseToAllChannels(ECR_Ignore);
    SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void UGothicMeleeHitboxComponent::EnableHitbox()
{
    bHitboxActive = true;
    AlreadyHitThisSwing.Empty();
    OverlapsThisWindow = 0;
    PaidThisWindow = 0;
    PruneDamageStamps();
    SetCollisionEnabled(ECollisionEnabled::QueryOnly);

    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|HitboxWindow|OPEN|hitbox=%s|socket=%s|baseDamage=%.0f"),
        HitboxTimelineNow(this),
        GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"),
        *GetName(),
        *GetAttachSocketName().ToString(),
        BaseDamage);
}

void UGothicMeleeHitboxComponent::PruneDamageStamps()
{
    // LastDamagedTime only ever grew: one entry per (actor, hitbox) pair for the
    // whole session, with dead actors' weak pointers left behind as permanent
    // rubble. Nothing reads a stamp older than HasRecentlyDamaged's window, so
    // anything past DamageStampHorizon is pure leak.
    //
    // Pruned at the start of a swing rather than on a timer: it is the only moment
    // the map is about to be written to, so the cost lands where the work already
    // is and an idle enemy pays nothing.
    const UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const double Now = World->GetTimeSeconds();

    for (auto It = LastDamagedTime.CreateIterator(); It; ++It)
    {
        if (!It.Key().IsValid() || (Now - It.Value()) > DamageStampHorizon)
        {
            It.RemoveCurrent();
        }
    }
}

void UGothicMeleeHitboxComponent::DisableHitbox()
{
    bHitboxActive = false;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // overlaps = events the open window actually processed; paid = those that
    // reached ApplyGameplayEffectSpecToTarget. overlaps>0 with paid=0 is a swing
    // rejected by the guards (self, Accursed-on-Accursed, dead, immune, no ASC);
    // overlaps=0 is reach or timing, and no line at all is a missing notify.
    UE_LOG(LogVigilCombat, Verbose,
        TEXT("VigilTimeline|t=%.3f|%s|HitboxWindow|CLOSE|overlaps=%d|paid=%d|hitbox=%s|socket=%s"),
        HitboxTimelineNow(this),
        GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"),
        OverlapsThisWindow,
        PaidThisWindow,
        *GetName(),
        *GetAttachSocketName().ToString());
}

void UGothicMeleeHitboxComponent::OnHitboxOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComp,
    int32 OtherBodyIndex,
    bool bFromSweep,
    const FHitResult& SweepResult)
{
    if (!bHitboxActive)
    {
        return;
    }

    // Counted before any guard: the point of the number is to say that something
    // WAS inside the box, whatever happened to it afterwards.
    ++OverlapsThisWindow;

    // Don't hit self
    if (!OtherActor || OtherActor == GetOwner())
    {
        return;
    }

    // Don't hit the same actor twice in one swing
    if (AlreadyHitThisSwing.Contains(OtherActor))
    {
        return;
    }

    // The Accursed do not kill each other.
    //
    // This box only excluded the swinger itself, so any Accursed standing inside a
    // neighbour's swing arc took the full hit. In a pack — which is how Thralls
    // fight — that is most of the time.
    //
    // Measured 2026-07-27 on encounter 1's reinforcement wave: two of the eight
    // Thralls were already at 68/80 and 56/80 before the player fired a single
    // shot. A Thrall's swing is BaseDamage 15, which is 12 after Defense, so those
    // are exactly one and two hits from each other. Every wave was arriving
    // pre-damaged, which quietly invalidates encounter tuning — a roster's real HP
    // was whatever the pack had left after chewing on itself on the way in.
    //
    // Checked by class rather than by a team ID because this project has no team
    // affiliation system; if one ever lands, this is the single place to swap.
    if (GetOwner() && GetOwner()->IsA(AGothicEnemyBase::StaticClass())
        && OtherActor->IsA(AGothicEnemyBase::StaticClass()))
    {
        return;
    }

    // Check target has an ASC (is a combat participant)
    UAbilitySystemComponent* TargetASC =
        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(OtherActor);

    if (!TargetASC)
    {
        return;
    }

    // ── The dead take no swings ──────────────────────────────────────────────
    //
    // ImmunityTags is authored data and it is EMPTY on both shipping enemy
    // Blueprints, so the old `ImmunityTags.Num() > 0 && ...` guard short-circuited
    // to "nothing is immune to anything". A player mid-death still took the swing,
    // and so did an Accursed already killed by an earlier hit in the same window.
    //
    // State.Dead is not tuning, it is a rule, so it is enforced unconditionally
    // here instead of depending on whoever remembers to fill the container in.
    // ImmunityTags stays as the per-enemy extension point ON TOP of that floor.
    //
    // Enforced in the hitbox rather than in the damage path deliberately.
    // UGothicAttributeSet::PostGameplayEffectExecute is the shared choke point for
    // EVERY damage source in Vigil, and a blanket "the dead take no damage" rule
    // there would also silence overkill accounting, any damage-over-time already
    // ticking on a corpse, and any future post-mortem effect — a far larger
    // behavioural change than this defect justifies. The hitbox is the narrowest
    // place that fixes exactly "enemy melee should not connect with a corpse".
    if (TargetASC->HasMatchingGameplayTag(GothicTags::State_Dead))
    {
        return;
    }

    if (ImmunityTags.Num() > 0 && TargetASC->HasAnyMatchingGameplayTags(ImmunityTags))
    {
        return;
    }

    // Health-zero targets that have not been tagged yet. State.Dead is added by
    // AGothicCharacterBase::OnDeath, which runs out of PostGameplayEffectExecute
    // once the killing blow has resolved — so two swings landing in the same frame
    // can both see an untagged corpse. IsAlive reads the attribute directly and
    // closes that window.
    if (const AGothicCharacterBase* TargetChar = Cast<AGothicCharacterBase>(OtherActor))
    {
        if (!TargetChar->IsAlive())
        {
            return;
        }
    }

    // Get the owning enemy's ASC for the damage context
    UAbilitySystemComponent* OwnerASC =
        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());

    if (!OwnerASC || !DamageEffect)
    {
        UE_LOG(LogTemp, Warning, TEXT("%s: Hitbox overlap but missing OwnerASC (%s) or DamageEffect (%s)"),
            GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"),
            OwnerASC ? TEXT("Valid") : TEXT("NULL"),
            DamageEffect ? TEXT("Valid") : TEXT("NULL"));
        return;
    }

    // Build and apply the damage effect — same pipeline as all Vigil damage.
    // The owning PAWN is the instigator: MakeEffectContext alone would name the
    // ASC's OwnerActor, which for an enemy is the AIController, and a Controller
    // has no ASC for the attribute set to read AttackPower off. That is why
    // Thrall melee measured 7 (15 + 0 - 8) instead of 17.
    FGameplayEffectContextHandle Context =
        UGothicAbilitySystemComponent::MakeDamageContext(OwnerASC, GetOwner());

    FGameplayEffectSpecHandle Spec = OwnerASC->MakeOutgoingSpec(
        DamageEffect, 1.f, Context);

    if (Spec.IsValid())
    {
        Spec.Data->SetSetByCallerMagnitude(
            FGameplayTag::RequestGameplayTag(FName("Data.Damage")),
            BaseDamage);

        OwnerASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);

        AlreadyHitThisSwing.Add(OtherActor);
        ++PaidThisWindow;

        if (const UWorld* World = GetWorld())
        {
            LastDamagedTime.Add(
                TWeakObjectPtr<const AActor>(OtherActor), World->GetTimeSeconds());
        }
    }
}

bool UGothicMeleeHitboxComponent::HasRecentlyDamaged(
    const AActor* Target, float WindowSeconds) const
{
    if (!Target)
    {
        return false;
    }

    const UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    if (const double* Stamp = LastDamagedTime.Find(TWeakObjectPtr<const AActor>(Target)))
    {
        return (World->GetTimeSeconds() - *Stamp) <= WindowSeconds;
    }

    return false;
}