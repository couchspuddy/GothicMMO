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
    // Trace for a valid enemy with a vital point component
    TrackedVitalPoint = TraceForVitalPoint();

    if (!TrackedVitalPoint)
    {
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

    // Material-based highlight — shows on the mesh surface, follows animation
    TrackedVitalPoint->SetReadHighlight(NextLocation);

    // Subscribe to vital point shifts — event driven, no tick
    TrackedVitalPoint->OnVitalPointShifted.RemoveDynamic(this, &UGA_Read::OnVitalPointShifted);
    TrackedVitalPoint->OnVitalPointShifted.AddDynamic(this, &UGA_Read::OnVitalPointShifted);


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
    // Unbind the delegate and clear the material highlight
    if (TrackedVitalPoint)
    {
        TrackedVitalPoint->OnVitalPointShifted.RemoveDynamic(
            this, &UGA_Read::OnVitalPointShifted);
        TrackedVitalPoint->ClearReadHighlight();
    }

    // Clear the timer
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(ReadDurationHandle);
    }

    TrackedVitalPoint = nullptr;


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
    if (!TrackedVitalPoint) return;

    // The vital shifted — update the Read highlight to show the NEW next position
    const FVector NextLocation = TrackedVitalPoint->GetNextVitalWorldLocation();
    TrackedVitalPoint->SetReadHighlight(NextLocation);

}

void UGA_Read::OnReadExpired()
{

    const FGameplayAbilitySpecHandle Handle = GetCurrentAbilitySpecHandle();
    const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
    const FGameplayAbilityActivationInfo ActivationInfo = GetCurrentActivationInfo();

    EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}