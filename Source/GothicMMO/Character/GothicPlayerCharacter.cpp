// GothicPlayerCharacter.cpp

#include "Character/GothicPlayerCharacter.h"
#include "Character/GothicInputHandlerComponent.h"
#include "AbilitySystem/GothicAbilitySet.h"
#include "AbilitySystem/GothicInputConfig.h"
#include "AI/GothicCombatStateComponent.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "Game/GothicPlayerState.h"
#include "UI/GothicHUD.h"
#include "AI/GothicSteadfastComponent.h"
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
#include "AI/GothicVitalPointComponent.h"
#include "Engine/Engine.h"
#include "Components/CapsuleComponent.h"
#include "AI/GothicVitalPointComponent.h"
#include "Engine/Engine.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Game/GothicGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "AI/GothicEnemyBase.h"
#include "AI/GothicEnemyAIController.h"
#include "AI/GothicEncounterVolume.h"
#include "AI/GothicCombatStateComponent.h"


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
    CombatStateComponent = CreateDefaultSubobject<UGothicCombatStateComponent>(TEXT("CombatStateComponent"));
    // Create the input handler component
    InputHandler = CreateDefaultSubobject<UGothicInputHandlerComponent>(TEXT("InputHandler"));
    SteadfastComponent = CreateDefaultSubobject<UGothicSteadfastComponent>(TEXT("SteadfastComponent"));
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

        if (GothicHUD)
        {
            GothicHUD->NotifyOwningPawnChanged(this);
        }

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
    
    // Restore SuperMeter if this is a respawn — everything else in
    // GE_InitStats_Player resets normally, but Reckoning progress persists
    // through death. HasAuthority() gated since this is a direct attribute
    // write and should be server-authoritative, same as InitializeGAS itself.
    if (HasAuthority() && AttributeSet)
    {
        const float CachedSuper = PS->GetCachedSuperMeterOnDeath();
        if (CachedSuper >= 0.f)
        {
            AttributeSet->SetSuperMeter(PS->ConsumeCachedSuperMeterOnDeath());
            UE_LOG(LogTemp, Log, TEXT("GothicPlayerCharacter: Restored SuperMeter to %.1f after respawn"), CachedSuper);
        }
    }


    // Grant ability sets — data driven, replaces old StartupAbilities array
    if (HasAuthority())
    {
        for (const TObjectPtr<UGothicAbilitySet>& AbilitySet : StartupAbilitySets)
        {
            if (AbilitySet)
            {
                UE_LOG(LogTemp, Log, TEXT("GothicPlayerCharacter: Granting ability set %s"),
                    *AbilitySet->GetName());
                AbilitySet->GiveToAbilitySystem(AbilitySystemComponent, this);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: Null ability set in StartupAbilitySets"));
            }
        }
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
            UGothicAttributeSet::GetHealthAttribute()).AddLambda(
            [this](const FOnAttributeChangeData& Data)
            {
                if (!IsLocallyControlled()) return;

                APlayerController* PC = GetWorld()->GetFirstPlayerController();
                if (!PC) return;

                AHUD* RawHUD = PC->GetHUD();

                UE_LOG(LogTemp, Log, TEXT("Health changed: %.1f | HUD: %s"),
                    Data.NewValue,
                    RawHUD ? *RawHUD->GetClass()->GetName() : TEXT("NULL"));

                AGothicHUD* GothicHUD = Cast<AGothicHUD>(RawHUD);
                if (GothicHUD)
                {
                    GothicHUD->UpdateHealth(Data.NewValue, AttributeSet->GetMaxHealth());
                }
            });

        // Super meter delegate
        AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
            UGothicAttributeSet::GetSuperMeterAttribute()).AddLambda(
            [this](const FOnAttributeChangeData& Data)
            {
                if (!IsLocallyControlled()) return;

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

    // Non-GAS combat inputs — direct bindings
    if (FireAction)
        EIC->BindAction(FireAction, ETriggerEvent::Started, this, &AGothicPlayerCharacter::OnFire);
    if (ADSAction)
    {
        EIC->BindAction(ADSAction, ETriggerEvent::Started,   this, &AGothicPlayerCharacter::OnADSStart);
        EIC->BindAction(ADSAction, ETriggerEvent::Completed, this, &AGothicPlayerCharacter::OnADSEnd);
    }
    
    if (ReloadAction)
    {
        EIC->BindAction(ReloadAction, ETriggerEvent::Started, this, &AGothicPlayerCharacter::OnReloadPressed);
        EIC->BindAction(ReloadAction, ETriggerEvent::Completed, this, &AGothicPlayerCharacter::OnReloadReleased);
    }
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
    float PreviousRecoilPitch = CurrentRecoilPitch;
    CurrentRecoilPitch = FMath::FInterpTo(CurrentRecoilPitch, 0.f, DeltaTime, RecoilRecoverySpeed);
    float RecoilDelta = CurrentRecoilPitch - PreviousRecoilPitch;
    if (Controller)
    {
        AddControllerPitchInput(RecoilDelta);
    }
    // ADS FOV interpolation
    if (FirstPersonCamera)
    {
        float TargetFOV = bIsADS ? ADSFOV : HipFireFOV;
        FirstPersonCamera->SetFieldOfView(
            FMath::FInterpTo(FirstPersonCamera->FieldOfView, TargetFOV, DeltaTime, ADSInterpSpeed));
    }
    if (!IsLocallyControlled() || !AbilitySystemComponent || !bHUDReady) return;

    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    AGothicHUD* GothicHUD = PC ? Cast<AGothicHUD>(PC->GetHUD()) : nullptr;
    if (!GothicHUD) return;

    UGothicHUDWidget* HUDWidget = GothicHUD->GetHUDWidget();
    if (!HUDWidget) return;
    
    // Poll cooldown for each ability slot
    GothicHUD->UpdateAbilityCooldown(EGothicAbilitySlot::Ability1,
        AbilitySystemComponent->GetCooldownRemainingForSlot(EGothicAbilitySlot::Ability1),
        4.0f);

    GothicHUD->UpdateAbilityCooldown(EGothicAbilitySlot::Ability2,
        AbilitySystemComponent->GetCooldownRemainingForSlot(EGothicAbilitySlot::Ability2),
        8.0f);

    GothicHUD->UpdateAbilityCooldown(EGothicAbilitySlot::Ability3,
        AbilitySystemComponent->GetCooldownRemainingForSlot(EGothicAbilitySlot::Ability3),
        3.5f);
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
    
    if (CurrentMagazineAmmo <= 0)
    {
        UE_LOG(LogTemp, Log, TEXT("OnFire: Out of ammo"));
        return;
    }
    CurrentMagazineAmmo--;

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
            float FinalDamage = PistolDamage;

            UGothicVitalPointComponent* VitalPoint =
                Hit.GetActor()->FindComponentByClass<UGothicVitalPointComponent>();

            bool bIsVitalHit = false;
            if (VitalPoint)
            {
                bIsVitalHit = IsReckoningActive() || VitalPoint->IsVitalPointHit(Hit.ImpactPoint);
            }

            if (bIsVitalHit)
            {
                FinalDamage *= 2.f;
                UE_LOG(LogTemp, Log, TEXT("OnFire: VITAL HIT on %s — damage %.1f"),
                    *Hit.GetActor()->GetName(), FinalDamage);
                GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Yellow, TEXT("VITAL HIT"));
            }

            FGameplayEffectContextHandle Context =
                AbilitySystemComponent->MakeEffectContext();
            FGameplayEffectSpecHandle Spec =
                AbilitySystemComponent->MakeOutgoingSpec(DamageEffectClass, 1.f, Context);

            if (Spec.IsValid())
            {
                Spec.Data->SetSetByCallerMagnitude(
                    FGameplayTag::RequestGameplayTag(FName("Data.Damage")),
                    FinalDamage);
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

void AGothicPlayerCharacter::TriggerSelahMoment_Implementation()
{
    UE_LOG(LogTemp, Log, TEXT("TriggerSelahMoment: Selah moment triggered on %s"),
        *GetName());
    OnSelahMoment();
}

void AGothicPlayerCharacter::ServerCollectEncounterSelah_Implementation(AGothicEncounterVolume* Encounter)
{
    if (Encounter)
    {
        Encounter->CompleteCollection();
    }
}

void AGothicPlayerCharacter::OnADSStart()
{
    bIsADS = true;
    GetCharacterMovement()->MaxWalkSpeed = ADSMovementSpeed;
}

void AGothicPlayerCharacter::OnADSEnd()
{
    bIsADS = false;
    GetCharacterMovement()->MaxWalkSpeed = 500.f;
}

void AGothicPlayerCharacter::CancelCollectionRite()
{
    UE_LOG(LogTemp, Log, TEXT("CancelCollectionRite: Called"));
}

// cpp implementations
void AGothicPlayerCharacter::OnReloadPressed()
{
    ReloadPressStartTime = GetWorld()->GetTimeSeconds();
}

void AGothicPlayerCharacter::OnReloadReleased()
{
    const float HeldDuration = GetWorld()->GetTimeSeconds() - ReloadPressStartTime;

    if (HeldDuration >= HoldThreshold)
    {
        HoldReload();
    }
    else
    {
        TapReload();
    }
}

void AGothicPlayerCharacter::TapReload()
{
    const int32 AmmoNeeded = MagazineCapacity - CurrentMagazineAmmo;
    const int32 AmmoToTransfer = FMath::Min(AmmoNeeded, CurrentReserveAmmo);

    CurrentMagazineAmmo += AmmoToTransfer;
    CurrentReserveAmmo -= AmmoToTransfer;

    UE_LOG(LogTemp, Log, TEXT("TapReload: Magazine %d/%d, Reserve %d"),
        CurrentMagazineAmmo, MagazineCapacity, CurrentReserveAmmo);
}

void AGothicPlayerCharacter::HoldReload()
{
    if (!SteadfastComponent)
    {
        return;
    }

    const float CurrentSteadfast = SteadfastComponent->GetCurrentSteadfast();

    // Tier thresholds — placeholder values, tune to feel once testable in engine.
    float SteadfastCost = 0.f;
    float AmmoGranted = 0.f;

    if (CurrentSteadfast >= 60.f)
    {
        // High tier
        SteadfastCost = 50.f;
        AmmoGranted = 12.f;
    }
    else if (CurrentSteadfast >= 30.f)
    {
        // Mid-tier
        SteadfastCost = 30.f;
        AmmoGranted = 8.f;
    }
    else if (CurrentSteadfast >= 10.f)
    {
        // Low tier
        SteadfastCost = 10.f;
        AmmoGranted = 4.f;
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("HoldReload: Insufficient Steadfast for any tier (%.1f current)"),
            CurrentSteadfast);
        return;
    }

    const float ActualAmmoGranted = SteadfastComponent->TryConvertSteadfast(SteadfastCost, AmmoGranted);

    if (ActualAmmoGranted > 0.f)
    {
        CurrentReserveAmmo = FMath::Min(CurrentReserveAmmo + FMath::RoundToInt(ActualAmmoGranted), MaxReserveAmmo);
        UE_LOG(LogTemp, Log, TEXT("HoldReload: Tier conversion — cost %.1f, granted %d ammo, Reserve now %d"),
            SteadfastCost, FMath::RoundToInt(ActualAmmoGranted), CurrentReserveAmmo);
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("HoldReload: Conversion failed unexpectedly"));
    }
}

bool AGothicPlayerCharacter::IsReckoningActive() const
{
    if (AbilitySystemComponent)
    {
        return AbilitySystemComponent->HasMatchingGameplayTag(
            FGameplayTag::RequestGameplayTag(FName("State.Reckoning")));
    }
    return false;
}

void AGothicPlayerCharacter::OnDeath_Implementation(AActor* Killer)
{
    if (AGothicPlayerState* PS = GetPlayerState<AGothicPlayerState>())
    {
        if (AttributeSet)
        {
            PS->CacheSuperMeterOnDeath(AttributeSet->GetSuperMeter());
        }
    }

    Super::OnDeath_Implementation(Killer);

    if (!HasAuthority())
    {
        return;
    }

    // Force out of combat immediately — don't wait for the grace-period timeout.
    if (UGothicCombatStateComponent* CombatState = FindComponentByClass<UGothicCombatStateComponent>())
    {
        CombatState->ForceLeaveCombat();
    }

    // Clear this pawn as a target from any enemy still tracking it — otherwise
    // their in-progress attack routine silently carries over onto whatever
    // pawn respawns here next.
    TArray<AActor*> Enemies;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGothicEnemyBase::StaticClass(), Enemies);
    for (AActor* EnemyActor : Enemies)
    {
        AGothicEnemyBase* Enemy = Cast<AGothicEnemyBase>(EnemyActor);
        if (Enemy && Enemy->GetCombatTarget() == this)
        {
            if (AGothicEnemyAIController* AIC = Cast<AGothicEnemyAIController>(Enemy->GetController()))
            {
                AIC->ClearCombatTarget();
            }
        }
    }

    if (AGothicGameMode* GM = GetWorld()->GetAuthGameMode<AGothicGameMode>())
    {
        GM->RequestRespawn(GetController());
    }
}