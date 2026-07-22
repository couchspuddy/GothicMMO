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
#include "Components/StaticMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Items/GothicInventoryComponent.h"
#include "Items/GothicItemDefinition.h"

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
    GetCharacterMovement()->MaxWalkSpeed              = 500.f;  // Overwritten by WalkSpeed in BeginPlay
    GetCharacterMovement()->JumpZVelocity             = 700.f;

    // Weapon mesh — attached to camera, swapped per active weapon
    WeaponMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
    WeaponMeshComponent->SetupAttachment(FirstPersonCamera);
    WeaponMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WeaponMeshComponent->SetOnlyOwnerSee(true);  // Only the local player sees their own weapon

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

    // Initialize ammo for all weapon slots from their data assets
    for (FGothicWeaponSlot& Slot : WeaponSlots)
    {
        Slot.InitFromData();
    }

    // Show the starting weapon mesh
    if (WeaponSlots.IsValidIndex(ActiveWeaponIndex))
    {
        const UGothicWeaponData* StartWeapon = WeaponSlots[ActiveWeaponIndex].WeaponData;
        if (StartWeapon && WeaponMeshComponent)
        {
            WeaponMeshComponent->SetStaticMesh(StartWeapon->WeaponMesh);
            WeaponMeshComponent->SetRelativeLocation(StartWeapon->MeshOffset);
            WeaponMeshComponent->SetRelativeRotation(StartWeapon->MeshRotation);
            WeaponMeshComponent->SetRelativeScale3D(StartWeapon->MeshScale);

            UE_LOG(LogTemp, Log, TEXT("GothicPlayerCharacter: Starting weapon — %s"),
                *StartWeapon->WeaponName.ToString());
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

        UE_LOG(LogTemp, Log, TEXT("Selah changed: %.0f"), Data.NewValue);

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

    // Bind to inventory equipment changes so weapon slots update when gear is equipped
    if (UGothicInventoryComponent* Inventory = PS->GetInventory())
    {
        Inventory->OnItemEquipped.AddDynamic(this, &AGothicPlayerCharacter::OnEquipmentChanged);
        UE_LOG(LogTemp, Log, TEXT("GothicPlayerCharacter: Inventory equipment delegate bound"));

        // Sync weapon slots with anything already equipped (e.g. after respawn)
        for (int32 i = 0; i < WeaponSlots.Num(); ++i)
        {
            EGothicEquipSlot EquipSlot = static_cast<EGothicEquipSlot>(i);
            if (const FGothicItemInstance* Equipped = Inventory->GetEquippedItem(EquipSlot))
            {
                if (Equipped->Definition && Equipped->Definition->IsWeapon())
                {
                    WeaponSlots[i].WeaponData = Equipped->Definition->WeaponData;
                    WeaponSlots[i].InitFromData();
                    UE_LOG(LogTemp, Log, TEXT("GothicPlayerCharacter: Synced weapon slot %d — %s"),
                        i, *Equipped->Definition->WeaponData->WeaponName.ToString());
                }
            }
        }
        RefreshWeaponVisuals(ActiveWeaponIndex);
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

    if (SprintAction)
    {
        EIC->BindAction(SprintAction, ETriggerEvent::Started,   this, &AGothicPlayerCharacter::OnSprintStarted);
        EIC->BindAction(SprintAction, ETriggerEvent::Completed, this, &AGothicPlayerCharacter::OnSprintStopped);
    }

    // Weapon slot swap — 1/2/3 keys
    if (WeaponSlot1Action)
        EIC->BindAction(WeaponSlot1Action, ETriggerEvent::Started, this, &AGothicPlayerCharacter::OnWeaponSlot1);
    if (WeaponSlot2Action)
        EIC->BindAction(WeaponSlot2Action, ETriggerEvent::Started, this, &AGothicPlayerCharacter::OnWeaponSlot2);
    if (WeaponSlot3Action)
        EIC->BindAction(WeaponSlot3Action, ETriggerEvent::Started, this, &AGothicPlayerCharacter::OnWeaponSlot3);

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

    // Poll cooldown for each ability slot — totals pulled from actual GE durations
    static const EGothicAbilitySlot SlotsToTrack[] = {
        EGothicAbilitySlot::LightAttack,
        EGothicAbilitySlot::Ability1,
        EGothicAbilitySlot::Ability2,
        EGothicAbilitySlot::Ability3,
    };

    for (int32 i = 0; i < UE_ARRAY_COUNT(SlotsToTrack); ++i)
    {
        const float Remaining = AbilitySystemComponent->GetCooldownRemainingForSlot(SlotsToTrack[i]);
        const float Total     = AbilitySystemComponent->GetCooldownTotalForSlot(SlotsToTrack[i]);
        GothicHUD->UpdateAbilityCooldown(SlotsToTrack[i], Remaining, Total);
    }
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

void AGothicPlayerCharacter::OnSprintStarted()
{
    GetCharacterMovement()->MaxWalkSpeed = SprintSpeed;
}

void AGothicPlayerCharacter::OnSprintStopped()
{
    GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
}

void AGothicPlayerCharacter::TriggerSelahMoment()
{
    UE_LOG(LogTemp, Log, TEXT("TriggerSelahMoment: Selah moment triggered on %s"),
        *GetName());

    // Blueprint handles the visual and audio — call the event
    OnSelahMoment();
}

bool AGothicPlayerCharacter::HasRoundChambered() const
{
    if (!WeaponSlots.IsValidIndex(ActiveWeaponIndex))
    {
        return false;
    }
    return WeaponSlots[ActiveWeaponIndex].CurrentMagazine > 0;
}

void AGothicPlayerCharacter::ConsumeRound()
{
    if (WeaponSlots.IsValidIndex(ActiveWeaponIndex)
        && WeaponSlots[ActiveWeaponIndex].CurrentMagazine > 0)
    {
        WeaponSlots[ActiveWeaponIndex].CurrentMagazine--;

        UE_LOG(LogTemp, Log, TEXT("ConsumeRound: %d remaining in magazine"),
            WeaponSlots[ActiveWeaponIndex].CurrentMagazine);
    }
}

const UGothicWeaponData* AGothicPlayerCharacter::GetActiveWeaponData() const
{
    if (WeaponSlots.IsValidIndex(ActiveWeaponIndex))
    {
        return WeaponSlots[ActiveWeaponIndex].WeaponData;
    }
    return nullptr;
}

void AGothicPlayerCharacter::ApplyRecoilKick()
{
    // Stub — add camera punch here once feel tuning starts.
    // Typical pattern: additive pitch/yaw via AddControllerPitchInput
    // with a short interp-back timer.
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        PC->AddPitchInput(-0.5f);  // slight upward kick
    }
}

void AGothicPlayerCharacter::SwapWeapon(int32 NewIndex)
{
    if (NewIndex == ActiveWeaponIndex)
    {
        return;
    }

    if (!WeaponSlots.IsValidIndex(NewIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("SwapWeapon: Invalid index %d (have %d slots)"),
            NewIndex, WeaponSlots.Num());
        return;
    }

    ActiveWeaponIndex = NewIndex;
    const UGothicWeaponData* NewWeapon = WeaponSlots[NewIndex].WeaponData;

    if (!NewWeapon)
    {
        UE_LOG(LogTemp, Warning, TEXT("SwapWeapon: Slot %d has no WeaponData assigned"), NewIndex);
        WeaponMeshComponent->SetStaticMesh(nullptr);
        return;
    }

    // Update mesh and crosshair
    RefreshWeaponVisuals(NewIndex);

    UE_LOG(LogTemp, Log, TEXT("SwapWeapon: Switched to slot %d — %s | Mag: %d/%d | Reserve: %d"),
        NewIndex,
        *NewWeapon->WeaponName.ToString(),
        WeaponSlots[NewIndex].CurrentMagazine,
        NewWeapon->MagazineCapacity,
        WeaponSlots[NewIndex].CurrentReserve);

    // Blueprint hook for swap animation / audio
    OnWeaponSwapped(NewIndex, NewWeapon);
}

void AGothicPlayerCharacter::OnWeaponSlot1() { SwapWeapon(0); }
void AGothicPlayerCharacter::OnWeaponSlot2() { SwapWeapon(1); }
void AGothicPlayerCharacter::OnWeaponSlot3() { SwapWeapon(2); }

int32 AGothicPlayerCharacter::EquipSlotToWeaponIndex(EGothicEquipSlot Slot)
{
    switch (Slot)
    {
        case EGothicEquipSlot::Sidearm: return 0;
        case EGothicEquipSlot::Piece:   return 1;
        case EGothicEquipSlot::Rig:     return 2;
        default:                        return -1;
    }
}

void AGothicPlayerCharacter::OnEquipmentChanged(EGothicEquipSlot Slot, const FGothicItemInstance& Item)
{
    const int32 WeaponIndex = EquipSlotToWeaponIndex(Slot);
    if (WeaponIndex < 0)
    {
        // Not a weapon slot — armor equip, nothing to do here
        return;
    }

    if (!WeaponSlots.IsValidIndex(WeaponIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("OnEquipmentChanged: WeaponSlots index %d out of range"), WeaponIndex);
        return;
    }

    if (Item.Definition && Item.Definition->IsWeapon())
    {
        WeaponSlots[WeaponIndex].WeaponData = Item.Definition->WeaponData;
        WeaponSlots[WeaponIndex].InitFromData();

        UE_LOG(LogTemp, Log, TEXT("OnEquipmentChanged: Slot %d updated — %s | Mag: %d | Reserve: %d"),
            WeaponIndex,
            *Item.Definition->WeaponData->WeaponName.ToString(),
            WeaponSlots[WeaponIndex].CurrentMagazine,
            WeaponSlots[WeaponIndex].CurrentReserve);
    }
    else
    {
        // Unequipped or non-weapon item — clear the slot
        WeaponSlots[WeaponIndex].WeaponData = nullptr;
        WeaponSlots[WeaponIndex].CurrentMagazine = 0;
        WeaponSlots[WeaponIndex].CurrentReserve = 0;

        UE_LOG(LogTemp, Log, TEXT("OnEquipmentChanged: Slot %d cleared"), WeaponIndex);
    }

    // If this is the active slot, update visuals immediately
    if (WeaponIndex == ActiveWeaponIndex)
    {
        RefreshWeaponVisuals(WeaponIndex);
    }
}

void AGothicPlayerCharacter::RefreshWeaponVisuals(int32 SlotIndex)
{
    if (!WeaponMeshComponent || !WeaponSlots.IsValidIndex(SlotIndex))
    {
        return;
    }

    const UGothicWeaponData* WeaponData = WeaponSlots[SlotIndex].WeaponData;
    if (WeaponData)
    {
        WeaponMeshComponent->SetStaticMesh(WeaponData->WeaponMesh);
        WeaponMeshComponent->SetRelativeLocation(WeaponData->MeshOffset);
        WeaponMeshComponent->SetRelativeRotation(WeaponData->MeshRotation);
        WeaponMeshComponent->SetRelativeScale3D(WeaponData->MeshScale);

        if (IsLocallyControlled())
        {
            APlayerController* PC = Cast<APlayerController>(GetController());
            AGothicHUD* GothicHUD = PC ? Cast<AGothicHUD>(PC->GetHUD()) : nullptr;
            if (GothicHUD)
            {
                GothicHUD->SetCrosshairType(WeaponData->CrosshairType);
            }
        }
    }
    else
    {
        WeaponMeshComponent->SetStaticMesh(nullptr);
    }
}