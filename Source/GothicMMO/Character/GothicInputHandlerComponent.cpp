// GothicInputHandlerComponent.cpp

#include "Character/GothicInputHandlerComponent.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicInputConfig.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Pawn.h"
#include "InputMappingContext.h"
#include "GameFramework/PlayerController.h"

UGothicInputHandlerComponent::UGothicInputHandlerComponent()
{
    PrimaryComponentTick.bCanEverTick = false;

}

void UGothicInputHandlerComponent::SetupAbilityInputBindings(
    UEnhancedInputComponent* InputComponent,
    UGothicAbilitySystemComponent* ASC)
{
    if (!InputComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("GothicInputHandlerComponent: SetupAbilityInputBindings — null InputComponent"));
        return;
    }

    if (!ASC)
    {
        UE_LOG(LogTemp, Error, TEXT("GothicInputHandlerComponent: SetupAbilityInputBindings — null ASC"));
        return;
    }

    if (!InputConfig)
    {
        UE_LOG(LogTemp, Error, TEXT("GothicInputHandlerComponent: SetupAbilityInputBindings — null InputConfig"));
        return;
    }


    // Clear previous bindings if any
    for (uint32 Handle : AbilityBindingHandles)
    {
        InputComponent->RemoveBindingByHandle(Handle);
    }
    AbilityBindingHandles.Empty();

    // Cache the ASC for use in callbacks
    CachedASC = ASC;

    // Bind each action from the config
    for (const FGothicInputAction& ActionEntry : InputConfig->AbilityInputActions)
    {
        if (!ActionEntry.InputAction)
        {
            UE_LOG(LogTemp, Warning, TEXT("GothicInputHandlerComponent: Null InputAction in config — skipping"));
            continue;
        }

        if (!ActionEntry.InputTag.IsValid())
        {
            UE_LOG(LogTemp, Warning, TEXT("GothicInputHandlerComponent: Invalid tag in config — skipping"));
            continue;
        }

        // Bind pressed
        uint32 PressHandle = InputComponent->BindAction(
            ActionEntry.InputAction,
            ETriggerEvent::Started,
            this,
            &UGothicInputHandlerComponent::HandleAbilityInputPressed,
            ActionEntry.InputTag).GetHandle();

        // Bind released
        uint32 ReleaseHandle = InputComponent->BindAction(
            ActionEntry.InputAction,
            ETriggerEvent::Completed,
            this,
            &UGothicInputHandlerComponent::HandleAbilityInputReleased,
            ActionEntry.InputTag).GetHandle();

        AbilityBindingHandles.Add(PressHandle);
        AbilityBindingHandles.Add(ReleaseHandle);

    }

}

void UGothicInputHandlerComponent::SwapMappingContext(
    UInputMappingContext* OldContext,
    UInputMappingContext* NewContext,
    int32 Priority)
{
    APawn* OwnerPawn = Cast<APawn>(GetOwner());
    if (!OwnerPawn)
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicInputHandlerComponent: SwapMappingContext — owner is not a Pawn"));
        return;
    }

    APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
    if (!PC)
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicInputHandlerComponent: SwapMappingContext — no PlayerController"));
        return;
    }

    UEnhancedInputLocalPlayerSubsystem* Subsystem =
        ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());

    if (!Subsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("GothicInputHandlerComponent: SwapMappingContext — no EnhancedInput subsystem"));
        return;
    }

    if (OldContext)
    {
        Subsystem->RemoveMappingContext(OldContext);
    }

    if (NewContext)
    {
        Subsystem->AddMappingContext(NewContext, Priority);
    }
}

void UGothicInputHandlerComponent::HandleAbilityInputPressed(FGameplayTag InputTag)
{
    if (!CachedASC)
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicInputHandlerComponent: Input pressed but ASC is null | Tag: %s"),
            *InputTag.ToString());
        return;
    }


    CachedASC->AbilityInputTagPressed(InputTag);
}

void UGothicInputHandlerComponent::HandleAbilityInputReleased(FGameplayTag InputTag)
{
    if (!CachedASC)
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicInputHandlerComponent: Input released but ASC is null | Tag: %s"),
            *InputTag.ToString());
        return;
    }


    CachedASC->AbilityInputTagReleased(InputTag);
}