// GothicPlayerCharacter.cpp

#include "Character/GothicPlayerCharacter.h"
#include "Character/GothicInputHandlerComponent.h"
#include "AbilitySystem/GothicAbilitySet.h"
#include "AbilitySystem/GothicInputConfig.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "Game/GothicPlayerState.h"
#include "UI/GothicHUD.h"
#include "UI/GothicHUDWidget.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "AbilitySystemBlueprintLibrary.h"

AGothicPlayerCharacter::AGothicPlayerCharacter()
{
    UE_LOG(LogTemp, Log, TEXT("GothicPlayerCharacter: Constructor called"));

    PrimaryActorTick.bCanEverTick = true;

    FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    FirstPersonCamera->SetupAttachment(GetMesh());
    FirstPersonCamera->SetRelativeLocation(FVector(20.f, 0.f, 170.f));
    FirstPersonCamera->bUsePawnControlRotation = true;

    GetMesh()->SetOwnerNoSee(true);

    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw   = true;
    bUseControllerRotationRoll  = false;

    GetCharacterMovement()->bOrientRotationToMovement = false;
    GetCharacterMovement()->MaxWalkSpeed              = 500.f;
    GetCharacterMovement()->JumpZVelocity             = 700.f;

    // Create the input handler component
    InputHandler = CreateDefaultSubobject<UGothicInputHandlerComponent>(TEXT("InputHandler"));

    UE_LOG(LogTemp, Log, TEXT("GothicPlayerCharacter: Constructor complete"));
}

void AGothicPlayerCharacter::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogTemp, Log, TEXT("GothicPlayerCharacter: BeginPlay called"));

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            if (DefaultMappingContext)
            {
                Subsystem->AddMappingContext(DefaultMappingContext, 0);
                UE_LOG(LogTemp, Log, TEXT("GothicPlayerCharacter: Default mapping context added"));
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: DefaultMappingContext is null"));
            }
        }
    }

    // Delay HUD polling until widget is fully constructed
    bHUDReady = false;
    FTimerHandle HUDInitTimer;
    GetWorldTimerManager().SetTimer(HUDInitTimer, [this]()
    {
        APlayerController* PC = GetWorld()->GetFirstPlayerController();
        AGothicHUD* GothicHUD = PC ? Cast<AGothicHUD>(PC->GetHUD()) : nullptr;

        UE_LOG(LogTemp, Log, TEXT("HUD Timer fired | PC: %s | GothicHUD: %s | Widget: %s"),
            PC ? TEXT("Valid") : TEXT("NULL"),
            GothicHUD ? TEXT("Valid") : TEXT("NULL"),
            GothicHUD && GothicHUD->GetHUDWidget() ? TEXT("Valid") : TEXT("NULL"));

        bHUDReady = true;
    }, 0.1f, false);
}

void AGothicPlayerCharacter::PossessedBy(AController* NewController)
{
    UE_LOG(LogTemp, Log, TEXT("GothicPlayerCharacter: PossessedBy called"));
    Super::PossessedBy(NewController);
    InitGASFromPlayerState();
}

void AGothicPlayerCharacter::OnRep_PlayerState()
{
    UE_LOG(LogTemp, Log, TEXT("GothicPlayerCharacter: OnRep_PlayerState called"));
    Super::OnRep_PlayerState();
    InitGASFromPlayerState();
}

void AGothicPlayerCharacter::InitGASFromPlayerState()
{
    UE_LOG(LogTemp, Log, TEXT("GothicPlayerCharacter: InitGASFromPlayerState called"));

    AGothicPlayerState* PS = GetPlayerState<AGothicPlayerState>();
    if (!PS)
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: PlayerState is null — skipping GAS init"));
        return;
    }

    AbilitySystemComponent = PS->GetGothicASC();
    AttributeSet           = PS->GetGothicAttributeSet();

    UE_LOG(LogTemp, Log, TEXT("GothicPlayerCharacter: ASC: %s | AttributeSet: %s"),
        AbilitySystemComponent ? TEXT("Valid") : TEXT("NULL"),
        AttributeSet ? TEXT("Valid") : TEXT("NULL"));

    InitializeGAS();

    // Grant ability sets — data driven, replaces old StartupAbilities array
    if (HasAuthority() && !bAbilitiesGranted)
    {
        for (const TObjectPtr<UGothicAbilitySet>& AbilitySet : StartupAbilitySets)
        {
            if (AbilitySet)
            {
                UE_LOG(LogTemp, Log, TEXT("GothicPlayerCharacter: Granting ability set %s"),
                    *AbilitySet->GetName());
                AbilitySet->GiveToAbilitySystem(AbilitySystemComponent, this);
            }
        }
        bAbilitiesGranted = true;
    }

    // Setup ability input bindings now that ASC is confirmed valid
    if (IsLocallyControlled() && InputHandler && AbilitySystemComponent)
    {
        if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
        {
            UE_LOG(LogTemp, Log, TEXT("GothicPlayerCharacter: Binding ability inputs via InputHandler"));
            InputHandler->SetupAbilityInputBindings(EIC, AbilitySystemComponent);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: InputComponent not yet available for ability binding"));
        }
    }

    // Health attribute delegate
    if (IsLocallyControlled() && AbilitySystemComponent)
    {
        AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
    UGothicAttributeSet::GetSelahAttribute()).AddLambda(
    [this](const FOnAttributeChangeData& Data)
    {
        if (!IsLocallyControlled()) return;

        UE_LOG(LogTemp, Log, TEXT("Selah lambda fired: %.0f"), Data.NewValue);

        APlayerController* PC = GetWorld()->GetFirstPlayerController();
        if (!PC) return;

        AGothicHUD* GothicHUD = Cast<AGothicHUD>(PC->GetHUD());
        if (GothicHUD)
        {
            GothicHUD->UpdateSelah(Data.NewValue);
        }
    });

        // Super meter delegate
        AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
    UGothicAttributeSet::GetSuperMeterAttribute()).AddLambda(
    [this](const FOnAttributeChangeData& Data)
    {
        if (!IsLocallyControlled()) return;

        UE_LOG(LogTemp, Log, TEXT("SuperMeter lambda fired: %.1f"), Data.NewValue);

        APlayerController* PC = GetWorld()->GetFirstPlayerController();
        if (!PC) return;

        AGothicHUD* GothicHUD = Cast<AGothicHUD>(PC->GetHUD());
        if (GothicHUD)
        {
            GothicHUD->UpdateSuperMeter(Data.NewValue, AttributeSet->GetMaxSuperMeter());
        }
    });

        UE_LOG(LogTemp, Log, TEXT("GothicPlayerCharacter: Attribute delegates registered"));
    }
}

void AGothicPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    UE_LOG(LogTemp, Log, TEXT("GothicPlayerCharacter: SetupPlayerInputComponent called"));

    UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
    if (!EIC)
    {
        UE_LOG(LogTemp, Error, TEXT("GothicPlayerCharacter: Failed to cast to UEnhancedInputComponent"));
        return;
    }

    // Movement — direct bindings, not through GAS
    if (MoveAction)
        EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AGothicPlayerCharacter::OnMove);

    if (LookAction)
        EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &AGothicPlayerCharacter::OnLook);

    if (JumpAction)
    {
        EIC->BindAction(JumpAction, ETriggerEvent::Started,   this, &ACharacter::Jump);
        EIC->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
    }
    
    if (SprintAction)
    {
        EIC->BindAction(SprintAction, ETriggerEvent::Started, 
            this, &AGothicPlayerCharacter::OnSprintStart);
        EIC->BindAction(SprintAction, ETriggerEvent::Completed, 
            this, &AGothicPlayerCharacter::OnSprintStop);
    }

    // Non-GAS combat inputs — direct bindings
    if (FireAction)
        EIC->BindAction(FireAction, ETriggerEvent::Started, this, &AGothicPlayerCharacter::OnFire);

    // Ability inputs go through InputHandler → ASC tag pipeline
    // ASC may not be ready here — bindings are set up again in InitGASFromPlayerState
    if (InputHandler && AbilitySystemComponent)
    {
        UE_LOG(LogTemp, Log, TEXT("GothicPlayerCharacter: ASC ready at SetupPlayerInputComponent — binding now"));
        InputHandler->SetupAbilityInputBindings(EIC, AbilitySystemComponent);
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("GothicPlayerCharacter: ASC not ready at SetupPlayerInputComponent — deferred to InitGASFromPlayerState"));
    }
}

void AGothicPlayerCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!IsLocallyControlled() || !AbilitySystemComponent || !bHUDReady) return;

    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    AGothicHUD* GothicHUD = PC ? Cast<AGothicHUD>(PC->GetHUD()) : nullptr;
    if (!GothicHUD) return;

    UGothicHUDWidget* HUDWidget = GothicHUD->GetHUDWidget();
    if (!HUDWidget) return;

    // Poll cooldown for each ability slot
    GothicHUD->UpdateAbilityCooldown(0,
        AbilitySystemComponent->GetCooldownRemainingForSlot(EGothicAbilitySlot::LightAttack),
        0.5f);

    GothicHUD->UpdateAbilityCooldown(1,
        AbilitySystemComponent->GetCooldownRemainingForSlot(EGothicAbilitySlot::Ability1),
        12.f);

    GothicHUD->UpdateAbilityCooldown(2,
        AbilitySystemComponent->GetCooldownRemainingForSlot(EGothicAbilitySlot::Ability2),
        18.f);
}

void AGothicPlayerCharacter::OnMove(const FInputActionValue& Value)
{
    const FVector2D MoveVec = Value.Get<FVector2D>();
    if (Controller)
    {
        const FRotator Rotation    = Controller->GetControlRotation();
        const FRotator YawRotation(0.f, Rotation.Yaw, 0.f);

        const FVector ForwardDir = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
        const FVector RightDir   = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

        AddMovementInput(ForwardDir, MoveVec.Y);
        AddMovementInput(RightDir,   MoveVec.X);
    }
}

void AGothicPlayerCharacter::OnLook(const FInputActionValue& Value)
{
    const FVector2D LookVec = Value.Get<FVector2D>();
    AddControllerYawInput(LookVec.X);
    AddControllerPitchInput(LookVec.Y);
}

void AGothicPlayerCharacter::OnFire()
{
    UE_LOG(LogTemp, Log, TEXT("OnFire: Called"));

    if (!FirstPersonCamera)
    {
        UE_LOG(LogTemp, Warning, TEXT("OnFire: FirstPersonCamera is null"));
        return;
    }

    const FVector Start = FirstPersonCamera->GetComponentLocation();
    const FVector End   = Start + (FirstPersonCamera->GetForwardVector() * 5000.f);

    FHitResult Hit;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(this);

    bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params);

    if (bHit && Hit.GetActor())
    {
        UE_LOG(LogTemp, Log, TEXT("OnFire: Hit %s"), *Hit.GetActor()->GetName());

        UAbilitySystemComponent* TargetASC =
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Hit.GetActor());

        if (TargetASC && DamageEffectClass)
        {
            FGameplayEffectContextHandle Context =
                AbilitySystemComponent->MakeEffectContext();
            FGameplayEffectSpecHandle Spec =
                AbilitySystemComponent->MakeOutgoingSpec(DamageEffectClass, 1.f, Context);

            if (Spec.IsValid())
            {
                Spec.Data->SetSetByCallerMagnitude(
                    FGameplayTag::RequestGameplayTag(FName("Data.Damage")),
                    PistolDamage);

                AbilitySystemComponent->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
                UE_LOG(LogTemp, Log, TEXT("OnFire: Damage applied to %s"), *Hit.GetActor()->GetName());
            }
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("OnFire: Hit %s but no ASC or no DamageEffectClass"),
                *Hit.GetActor()->GetName());
        }
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("OnFire: No hit"));
    }
}

void AGothicPlayerCharacter::OnMelee()
{
    UE_LOG(LogTemp, Log, TEXT("OnMelee: Called"));

    if (!FirstPersonCamera)
    {
        UE_LOG(LogTemp, Warning, TEXT("OnMelee: FirstPersonCamera is null"));
        return;
    }

    const FVector Start = FirstPersonCamera->GetComponentLocation();
    const FVector End   = Start + (FirstPersonCamera->GetForwardVector() * 150.f);

    FHitResult Hit;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(this);

    bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params);

    if (bHit && Hit.GetActor())
    {
        UE_LOG(LogTemp, Log, TEXT("OnMelee: Hit %s"), *Hit.GetActor()->GetName());

        UAbilitySystemComponent* TargetASC =
            UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Hit.GetActor());

        if (TargetASC && DamageEffectClass)
        {
            FGameplayEffectContextHandle Context =
                AbilitySystemComponent->MakeEffectContext();
            FGameplayEffectSpecHandle Spec =
                AbilitySystemComponent->MakeOutgoingSpec(DamageEffectClass, 1.f, Context);

            if (Spec.IsValid())
            {
                Spec.Data->SetSetByCallerMagnitude(
                    FGameplayTag::RequestGameplayTag(FName("Data.Damage")),
                    MeleeDamage);

                AbilitySystemComponent->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
                UE_LOG(LogTemp, Log, TEXT("OnMelee: Damage applied to %s"), *Hit.GetActor()->GetName());
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("OnMelee: No hit"));
    }
}

void AGothicPlayerCharacter::OnSprintStart()
{
    UE_LOG(LogTemp, Log, TEXT("Sprint started"));
    GetCharacterMovement()->MaxWalkSpeed = 900.f;
}

void AGothicPlayerCharacter::OnSprintStop()
{
    UE_LOG(LogTemp, Log, TEXT("Sprint stopped"));
    GetCharacterMovement()->MaxWalkSpeed = 500.f;
}

void AGothicPlayerCharacter::TriggerSelahMoment()
{
    UE_LOG(LogTemp, Log, TEXT("TriggerSelahMoment: Selah moment triggered on %s"),
        *GetName());

    // Blueprint handles the visual and audio — call the event
    OnSelahMoment();
}
