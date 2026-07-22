// GothicMeleeHitboxComponent.cpp

#include "AI/GothicMeleeHitboxComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"

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
    SetCollisionEnabled(ECollisionEnabled::QueryOnly);

    UE_LOG(LogTemp, Log, TEXT("%s: Hitbox ENABLED"),
        GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"));
}

void UGothicMeleeHitboxComponent::DisableHitbox()
{
    bHitboxActive = false;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);

    UE_LOG(LogTemp, Log, TEXT("%s: Hitbox DISABLED | Hit %d actors this swing"),
        GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"),
        AlreadyHitThisSwing.Num());
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

    // Check target has an ASC (is a combat participant)
    UAbilitySystemComponent* TargetASC =
        UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(OtherActor);

    if (!TargetASC)
    {
        return;
    }

    // Check immunity tags (dead, invulnerable, etc.)
    if (ImmunityTags.Num() > 0 && TargetASC->HasAnyMatchingGameplayTags(ImmunityTags))
    {
        return;
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

    // Build and apply the damage effect — same pipeline as all Vigil damage
    FGameplayEffectContextHandle Context = OwnerASC->MakeEffectContext();
    Context.AddSourceObject(GetOwner());

    FGameplayEffectSpecHandle Spec = OwnerASC->MakeOutgoingSpec(
        DamageEffect, 1.f, Context);

    if (Spec.IsValid())
    {
        Spec.Data->SetSetByCallerMagnitude(
            FGameplayTag::RequestGameplayTag(FName("Data.Damage")),
            BaseDamage);

        OwnerASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);

        AlreadyHitThisSwing.Add(OtherActor);

        UE_LOG(LogTemp, Log, TEXT("%s: Hitbox dealt %.1f damage to %s"),
            GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"),
            BaseDamage,
            *OtherActor->GetName());
    }
}