// GA_Read.cpp

#include "AbilitySystem//GA_Read.h"
#include "AI/GothicVitalPointComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "TimerManager.h"

UGA_Read::UGA_Read()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_Read::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
    bool bCommitted = CommitAbility(Handle, ActorInfo, ActivationInfo);
    
    if (!bCommitted)
    {
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }
    // Trace for a valid enemy with a vital point component
    TrackedVitalPoint = TraceForVitalPoint();

    if (!TrackedVitalPoint)
    {
        UE_LOG(LogTemp, Log, TEXT("GA_Read: No valid target found — cancelling"));
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }
    if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
    {
        UE_LOG(LogTemp, Warning, TEXT("GA_Read: CommitAbility failed — cancelling"));
        CancelAbility(Handle, ActorInfo, ActivationInfo, true);
        return;
    }
    // Get the next vital location immediately on activation
    const FVector NextLocation = TrackedVitalPoint->GetNextVitalWorldLocation();

    // Spawn the Niagara indicator at the predicted location
    if (ReadIndicatorSystem)
    {
        ReadIndicator = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
            GetWorld(),
            ReadIndicatorSystem,
            NextLocation,
            FRotator::ZeroRotator,
            FVector(1.f),
            true,  // auto destroy when done — but we manage lifetime manually
            true,
            ENCPoolMethod::None);
    }

    // Subscribe to vital point shifts — event driven, no tick
    TrackedVitalPoint->OnVitalPointShifted.RemoveDynamic(this, &UGA_Read::OnVitalPointShifted);
    TrackedVitalPoint->OnVitalPointShifted.AddDynamic(this, &UGA_Read::OnVitalPointShifted);

    UE_LOG(LogTemp, Log, TEXT("GA_Read: Activated — tracking %s, next vital at %s"),
        *TrackedVitalPoint->GetOwner()->GetName(),
        *NextLocation.ToString());

    // Start duration timer
    GetWorld()->GetTimerManager().SetTimer(
        ReadDurationHandle,
        this,
        &UGA_Read::OnReadExpired,
        ReadDuration,
        false);
}

void UGA_Read::EndAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    bool bReplicateEndAbility,
    bool bWasCancelled)
{
    // Unbind the delegate
    if (TrackedVitalPoint && VitalShiftHandle.IsValid())
    {
        TrackedVitalPoint->OnVitalPointShifted.RemoveDynamic(
            this, &UGA_Read::OnVitalPointShifted);
    }

    // Destroy the indicator
    if (ReadIndicator)
    {
        ReadIndicator->DeactivateImmediate();
        ReadIndicator = nullptr;
    }

    // Clear the timer
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(ReadDurationHandle);
    }

    TrackedVitalPoint = nullptr;

    UE_LOG(LogTemp, Log, TEXT("GA_Read: Ended"));

    Super::EndAbility(Handle, ActorInfo, ActivationInfo,
        bReplicateEndAbility, bWasCancelled);
}

UGothicVitalPointComponent* UGA_Read::TraceForVitalPoint() const
{
    const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
    if (!Character) return nullptr;

    // Find the camera component for trace origin
    UCameraComponent* Camera = Character->FindComponentByClass<UCameraComponent>();
    if (!Camera) return nullptr;

    const FVector Start = Camera->GetComponentLocation();
    const FVector End   = Start + (Camera->GetForwardVector() * TraceRange);

    FHitResult Hit;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(Character);

    bool bHit = GetWorld()->LineTraceSingleByChannel(
        Hit, Start, End, ECC_Pawn, Params);

    if (!bHit || !Hit.GetActor()) return nullptr;

    return Hit.GetActor()->FindComponentByClass<UGothicVitalPointComponent>();
}

void UGA_Read::OnVitalPointShifted(int32 NewIndex, FVector NewWorldLocation)
{
    // Vital shifted — the new NewWorldLocation IS the next predicted location
    // We want to show where it's going NEXT, so query the component again
    if (!TrackedVitalPoint) return;

    const FVector NextLocation = TrackedVitalPoint->GetNextVitalWorldLocation();

    if (ReadIndicator)
    {
        ReadIndicator->SetWorldLocation(NextLocation);
    }

    UE_LOG(LogTemp, Log, TEXT("GA_Read: Vital shifted — indicator moved to %s"),
        *NextLocation.ToString());
}

void UGA_Read::OnReadExpired()
{
    UE_LOG(LogTemp, Log, TEXT("GA_Read: Duration expired"));

    const FGameplayAbilitySpecHandle Handle = GetCurrentAbilitySpecHandle();
    const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
    const FGameplayAbilityActivationInfo ActivationInfo = GetCurrentActivationInfo();

    EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}