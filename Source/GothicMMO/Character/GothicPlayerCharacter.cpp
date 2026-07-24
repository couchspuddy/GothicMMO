// GothicPlayerCharacter.cpp

#include "Character/GothicPlayerCharacter.h"
#include "Character/GothicInputHandlerComponent.h"
#include "AbilitySystem/GothicAbilitySet.h"
#include "AbilitySystem/GothicInputConfig.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "Game/GothicPlayerState.h"
#include "UI/GothicHUD.h"
#include "AI/GothicSteadfastComponent.h"
#include "AI/GothicCombatStateComponent.h"
#include "UI/GothicHUDWidget.h"
#include "UI/GothicInventoryWidget.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
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

    // Weapon mesh — socketed to the right-hand grip so it follows the character's
    // animations (reload, fire, idle sway) instead of being locked to the camera.
    // HandGrip_R is authored on SKM_Manny_Simple's hand_r bone; per-weapon grip
    // alignment comes from WeaponData MeshOffset/Rotation/Scale in RefreshWeaponVisuals.
    WeaponMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
    WeaponMeshComponent->SetupAttachment(GetMesh(), TEXT("HandGrip_R"));
    WeaponMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WeaponMeshComponent->SetOnlyOwnerSee(true);  // Only the local player sees their own weapon

    // Create the input handler component
    InputHandler = CreateDefaultSubobject<UGothicInputHandlerComponent>(TEXT("InputHandler"));

}

void AGothicPlayerCharacter::BeginPlay()
{
    Super::BeginPlay();


    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            if (DefaultMappingContext)
            {
                Subsystem->AddMappingContext(DefaultMappingContext, 0);
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

        }
    }

    // Force the hand-socket attachment at runtime. The Blueprint serialized an
    // attachment of WeaponMesh to the camera, which overrides the constructor's
    // SetupAttachment — so re-attach explicitly here to guarantee the weapon
    // follows the character's hand animation. Per-weapon grip alignment still
    // comes from WeaponData MeshOffset/Rotation/Scale (kept as relative transform).
    if (WeaponMeshComponent && GetMesh())
    {
        WeaponMeshComponent->AttachToComponent(
            GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, TEXT("HandGrip_R"));
    }

    // Delay HUD polling until widget is fully constructed
    bHUDReady = false;
    FTimerHandle HUDInitTimer;
    GetWorldTimerManager().SetTimer(HUDInitTimer, [this]()
    {
        APlayerController* PC = GetWorld()->GetFirstPlayerController();
        AGothicHUD* GothicHUD = PC ? Cast<AGothicHUD>(PC->GetHUD()) : nullptr;


        bHUDReady = true;

        // First ammo readout — nothing has fired or reloaded yet, so without this
        // the magazine display stays blank until the player's first shot.
        PushAmmoToHUD();

        // Apply the active weapon's crosshair on spawn. The HUD boots on Melee by
        // default and the starting weapon is equipped before the HUD widget exists,
        // so without this the crosshair stays Melee until the first weapon swap.
        if (GothicHUD && WeaponSlots.IsValidIndex(ActiveWeaponIndex))
        {
            if (const UGothicWeaponData* ActiveWeapon = WeaponSlots[ActiveWeaponIndex].WeaponData)
            {
                GothicHUD->SetCrosshairType(ActiveWeapon->CrosshairType);
            }
        }
    }, 0.1f, false);
}

void AGothicPlayerCharacter::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);
    InitGASFromPlayerState();
}

void AGothicPlayerCharacter::OnRep_PlayerState()
{
    Super::OnRep_PlayerState();
    InitGASFromPlayerState();
}

void AGothicPlayerCharacter::InitGASFromPlayerState()
{

    AGothicPlayerState* PS = GetPlayerState<AGothicPlayerState>();
    if (!PS)
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: PlayerState is null — skipping GAS init"));
        return;
    }

    AbilitySystemComponent = PS->GetGothicASC();
    AttributeSet           = PS->GetGothicAttributeSet();


    InitializeGAS();

    // Grant ability sets — data driven, replaces old StartupAbilities array
    if (HasAuthority() && !bAbilitiesGranted)
    {
        for (const TObjectPtr<UGothicAbilitySet>& AbilitySet : StartupAbilitySets)
        {
            if (AbilitySet)
            {
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
            InputHandler->SetupAbilityInputBindings(EIC, AbilitySystemComponent);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: InputComponent not yet available for ability binding"));
        }
    }

    // HUD attribute delegates
    if (IsLocallyControlled() && AbilitySystemComponent)
    {
        // Health delegate — drives the health bar fill and number.
        AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
            UGothicAttributeSet::GetHealthAttribute()).AddLambda(
            [this](const FOnAttributeChangeData& Data)
            {
                if (!IsLocallyControlled()) return;

                APlayerController* PC = GetWorld()->GetFirstPlayerController();
                if (!PC) return;

                AGothicHUD* GothicHUD = Cast<AGothicHUD>(PC->GetHUD());
                if (GothicHUD && AttributeSet)
                {
                    GothicHUD->UpdateHealth(Data.NewValue, AttributeSet->GetMaxHealth());
                }

                // Taking damage is a combat action — enters/refreshes combat state
                // so Steadfast fills even during purely defensive play.
                if (Data.NewValue < Data.OldValue)
                {
                    if (UGothicCombatStateComponent* Combat = FindComponentByClass<UGothicCombatStateComponent>())
                    {
                        Combat->NotifyCombatAction();
                    }
                }
            });

        // Selah delegate
        AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
    UGothicAttributeSet::GetSelahAttribute()).AddLambda(
    [this](const FOnAttributeChangeData& Data)
    {
        if (!IsLocallyControlled()) return;


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

    }

    // Bind to inventory equipment changes so weapon slots update when gear is equipped
    // Guarded: PossessedBy + OnRep_PlayerState both call InitGASFromPlayerState
    if (!bInventoryBound)
    {
        if (UGothicInventoryComponent* Inventory = PS->GetInventory())
        {
            Inventory->OnItemEquipped.AddDynamic(this, &AGothicPlayerCharacter::OnEquipmentChanged);
            bInventoryBound = true;

            // Sync weapon slots with anything already equipped (e.g. after respawn)
            static const EGothicEquipSlot WeaponEquipSlots[] = {
                EGothicEquipSlot::Sidearm,
                EGothicEquipSlot::Piece,
                EGothicEquipSlot::Rig
            };
            for (int32 i = 0; i < WeaponSlots.Num() && i < 3; ++i)
            {
                if (const FGothicItemInstance* Equipped = Inventory->GetEquippedItem(WeaponEquipSlots[i]))
                {
                    if (Equipped->Definition && Equipped->Definition->IsWeapon())
                    {
                        WeaponSlots[i].WeaponData = Equipped->Definition->WeaponData;
                        WeaponSlots[i].InitFromData();
                    }
                }
            }
            RefreshWeaponVisuals(ActiveWeaponIndex);
        }
    }
}

void AGothicPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);


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

    // Neither Fire nor Melee is bound here. Both route through
    // DA_GothicInputConfig — IA_Fire as Input.Ability.PrimaryFire -> GA_Fire,
    // IA_Melee as Input.Ability.Melee -> GA_HuntersStrike.
    //
    // Their predecessors (OnFire here, OnMelee on BP_GothicPlayerCharacter's
    // event graph) each ran *in addition* to the ability on every input: an
    // ECC_Pawn capsule trace applying a flat PistolDamage/MeleeDamage with no
    // authority check, no ammo cost, no cooldown and no vital-point resolution.
    // Because the trace hit the capsule rather than the mesh, they also dealt
    // damage on shots the ability itself counted as a miss. Both removed.

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

    // Reload — press starts the hold, release decides tap vs. already-converted
    if (ReloadAction)
    {
        EIC->BindAction(ReloadAction, ETriggerEvent::Started,   this, &AGothicPlayerCharacter::OnReloadPressed);
        EIC->BindAction(ReloadAction, ETriggerEvent::Completed, this, &AGothicPlayerCharacter::OnReloadReleased);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: ReloadAction not assigned — reload and Steadfast conversion cannot fire. Assign IA_Reload in BP_GothicPlayerCharacter."));
    }

    // Inventory screen toggle
    if (InventoryToggleAction)
    {
        EIC->BindAction(InventoryToggleAction, ETriggerEvent::Started, this, &AGothicPlayerCharacter::ToggleInventory);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: InventoryToggleAction not assigned — inventory cannot be opened. Assign IA_InventoryToggle in BP_GothicPlayerCharacter."));
    }

    // Ability inputs go through InputHandler → ASC tag pipeline
    // ASC may not be ready here — bindings are set up again in InitGASFromPlayerState
    if (InputHandler && AbilitySystemComponent)
    {
        InputHandler->SetupAbilityInputBindings(EIC, AbilitySystemComponent);
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

    // Blueprint handles the visual and audio — call the event
    OnSelahMoment();
}

bool AGothicPlayerCharacter::HasRoundChambered() const
{
    if (!WeaponSlots.IsValidIndex(ActiveWeaponIndex))
    {
        return false;
    }
    // HasAmmo covers the ammo-less case — a Heavy Melee Rig is always ready
    return WeaponSlots[ActiveWeaponIndex].HasAmmo();
}

void AGothicPlayerCharacter::ConsumeRound()
{
    if (!WeaponSlots.IsValidIndex(ActiveWeaponIndex))
    {
        return;
    }

    FGothicWeaponSlot& Slot = WeaponSlots[ActiveWeaponIndex];

    // Ammo-less weapons have nothing to spend
    if (Slot.WeaponData && !Slot.WeaponData->bUsesAmmo)
    {
        return;
    }

    if (Slot.CurrentMagazine > 0)
    {
        Slot.CurrentMagazine--;


        PushAmmoToHUD();
    }
}

int32 AGothicPlayerCharacter::GetActiveSteadfastRefillCost() const
{
    const UGothicWeaponData* WeaponData = GetActiveWeaponData();
    return WeaponData ? WeaponData->GetSteadfastRefillCost() : 0;
}

void AGothicPlayerCharacter::PushAmmoToHUD() const
{
    if (!IsLocallyControlled())
    {
        return;
    }

    APlayerController* PC = Cast<APlayerController>(GetController());
    AGothicHUD* GothicHUD = PC ? Cast<AGothicHUD>(PC->GetHUD()) : nullptr;
    if (!GothicHUD || !WeaponSlots.IsValidIndex(ActiveWeaponIndex))
    {
        return;
    }

    const FGothicWeaponSlot& Slot = WeaponSlots[ActiveWeaponIndex];
    if (!Slot.WeaponData)
    {
        GothicHUD->UpdateAmmo(0, 0, 0, 0);
        return;
    }

    GothicHUD->UpdateAmmo(
        Slot.CurrentMagazine,
        Slot.WeaponData->MagazineCapacity,
        Slot.CurrentReserve,
        Slot.WeaponData->MaxReserveAmmo);
}

// ═══════════════════════════════════════════════════════════════════════════
// Reload — tap to load from reserve, hold to convert Steadfast into reserve
// ═══════════════════════════════════════════════════════════════════════════

bool AGothicPlayerCharacter::ReloadActiveWeapon()
{
    if (!WeaponSlots.IsValidIndex(ActiveWeaponIndex))
    {
        return false;
    }

    FGothicWeaponSlot& Slot = WeaponSlots[ActiveWeaponIndex];
    if (!Slot.WeaponData || !Slot.WeaponData->bUsesAmmo)
    {
        return false;
    }

    const int32 Space = Slot.WeaponData->MagazineCapacity - Slot.CurrentMagazine;
    if (Space <= 0 || Slot.CurrentReserve <= 0)
    {
        return false;
    }

    const int32 Loaded = FMath::Min(Space, Slot.CurrentReserve);
    Slot.CurrentMagazine += Loaded;
    Slot.CurrentReserve  -= Loaded;


    PushAmmoToHUD();
    OnReloadPerformed();
    return true;
}

bool AGothicPlayerCharacter::ConvertSteadfastToReserve()
{
    if (!WeaponSlots.IsValidIndex(ActiveWeaponIndex))
    {
        return false;
    }

    FGothicWeaponSlot& Slot = WeaponSlots[ActiveWeaponIndex];
    if (!Slot.WeaponData || !Slot.WeaponData->bUsesAmmo)
    {
        return false;
    }

    const int32 ReserveSpace = Slot.WeaponData->MaxReserveAmmo - Slot.CurrentReserve;
    if (ReserveSpace <= 0)
    {
        return false;
    }

    UGothicSteadfastComponent* Steadfast = FindComponentByClass<UGothicSteadfastComponent>();
    if (!Steadfast)
    {
        UE_LOG(LogTemp, Warning, TEXT("ConvertSteadfastToReserve: no UGothicSteadfastComponent on %s"), *GetName());
        return false;
    }

    // Charges are a design-side unit; Steadfast itself is a float attribute.
    // Convert through the bar so the cost stays correct if MaxSteadfast is retuned.
    const int32 ChargeCost = Slot.WeaponData->GetSteadfastRefillCost();
    if (ChargeCost <= 0)
    {
        return false;
    }

    // A zero ceiling means the ASC isn't initialized yet. Without this guard the
    // per-charge cost computes to zero and every conversion is free.
    const float MaxSteadfast = Steadfast->GetMaxSteadfast();
    if (MaxSteadfast <= 0.f)
    {
        UE_LOG(LogTemp, Warning, TEXT("ConvertSteadfastToReserve: MaxSteadfast is 0 — ASC not ready, refusing a free conversion"));
        return false;
    }

    const float PerCharge = MaxSteadfast / FMath::Max(1, SteadfastChargesPerFullBar);
    const float Cost = ChargeCost * PerCharge;

    const int32 Requested = FMath::Min(Slot.WeaponData->SteadfastRefillAmount, ReserveSpace);
    const float Granted = Steadfast->TryConvertSteadfast(Cost, static_cast<float>(Requested));
    if (Granted <= 0.f)
    {
        return false;
    }

    const int32 Rounds = FMath::Min(FMath::FloorToInt(Granted), ReserveSpace);
    Slot.CurrentReserve += Rounds;


    PushAmmoToHUD();
    OnSteadfastConverted(Rounds);
    return true;
}

void AGothicPlayerCharacter::OnReloadPressed()
{
    // Inventory open — the key belongs to the UI
    if (ActiveInventoryWidget)
    {
        return;
    }

    bSteadfastConversionFired = false;
    bSteadfastHoldThresholdReached = false;

    GetWorldTimerManager().SetTimer(
        SteadfastHoldTimerHandle,
        this,
        &AGothicPlayerCharacter::HandleSteadfastHoldTick,
        SteadfastHoldThreshold,
        false);
}

void AGothicPlayerCharacter::OnReloadReleased()
{
    const bool bWasHeld = bSteadfastHoldThresholdReached;

    EndSteadfastHold();
    bSteadfastHoldThresholdReached = false;

    // Released before the threshold — this was a tap, so do a normal reload.
    // Past the threshold the payoff was already given mid-hold (or refused because
    // there was nothing to convert); either way releasing must not reload.
    if (!bWasHeld)
    {
        ReloadActiveWeapon();
    }
}

void AGothicPlayerCharacter::HandleSteadfastHoldTick()
{
    bSteadfastHoldThresholdReached = true;

    // Conversion is granted here, mid-hold — never deferred to release.
    if (!ConvertSteadfastToReserve())
    {
        // Nothing left to convert, or nowhere to put it. Stop rather than spin.
        EndSteadfastHold();
        return;
    }

    bSteadfastConversionFired = true;

    // Still held — queue the next conversion.
    GetWorldTimerManager().SetTimer(
        SteadfastHoldTimerHandle,
        this,
        &AGothicPlayerCharacter::HandleSteadfastHoldTick,
        SteadfastHoldRepeatInterval,
        false);
}

void AGothicPlayerCharacter::EndSteadfastHold()
{
    GetWorldTimerManager().ClearTimer(SteadfastHoldTimerHandle);

    if (bSteadfastConversionFired)
    {
        bSteadfastConversionFired = false;
        OnSteadfastConversionEnded();
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
    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC)
    {
        return;
    }

    const UGothicWeaponData* WeaponData = GetActiveWeaponData();
    const float Pitch = WeaponData ? WeaponData->RecoilPitch : -0.5f;
    const float YawSpread = WeaponData ? WeaponData->RecoilYawSpread : 0.f;

    PC->AddPitchInput(Pitch);

    if (YawSpread > 0.f)
    {
        PC->AddYawInput(FMath::FRandRange(-YawSpread, YawSpread));
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

    // A swap abandons any in-progress conversion — the cost was tied to the old weapon's tier
    EndSteadfastHold();

    // Update mesh and crosshair
    RefreshWeaponVisuals(NewIndex);
    PushAmmoToHUD();


    // Blueprint hook for swap animation / audio
    OnWeaponSwapped(NewIndex, NewWeapon);
}

void AGothicPlayerCharacter::OnWeaponSlot1() { SwapWeapon(0); }
void AGothicPlayerCharacter::OnWeaponSlot2() { SwapWeapon(1); }
void AGothicPlayerCharacter::OnWeaponSlot3() { SwapWeapon(2); }

// ═══════════════════════════════════════════════════════════════════════════
// Inventory UI
// ═══════════════════════════════════════════════════════════════════════════

void AGothicPlayerCharacter::ToggleInventory()
{
    if (!IsLocallyControlled())
    {
        return;
    }

    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC)
    {
        return;
    }

    AGothicHUD* GothicHUD = Cast<AGothicHUD>(PC->GetHUD());

    // Already open — tear it down. NativeDestruct unbinds the inventory delegates.
    if (ActiveInventoryWidget)
    {
        ActiveInventoryWidget->RemoveFromParent();
        ActiveInventoryWidget = nullptr;

        PC->SetInputMode(FInputModeGameOnly());
        PC->SetShowMouseCursor(false);

        if (GothicHUD)
        {
            GothicHUD->SetCrosshairVisible(true);
        }

        return;
    }

    if (!InventoryWidgetClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: InventoryWidgetClass not assigned — set it to WBP_Inventory in BP_GothicPlayerCharacter."));
        return;
    }

    // Inventory lives on the PlayerState alongside the ASC so it survives respawns
    AGothicPlayerState* PS = GetPlayerState<AGothicPlayerState>();
    UGothicInventoryComponent* Inventory = PS ? PS->GetInventory() : nullptr;
    if (!Inventory)
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: No inventory component on PlayerState — cannot open inventory."));
        return;
    }

    ActiveInventoryWidget = CreateWidget<UGothicInventoryWidget>(PC, InventoryWidgetClass);
    if (!ActiveInventoryWidget)
    {
        UE_LOG(LogTemp, Error, TEXT("GothicPlayerCharacter: CreateWidget failed for %s"), *InventoryWidgetClass->GetName());
        return;
    }

    // Z-order 10 — above the HUD layout (0) and crosshair (1)
    ActiveInventoryWidget->AddToViewport(10);

    // After AddToViewport so the Blueprint's Construct has run before the
    // OnInventoryRefreshed / OnEquipmentRefreshed events fire and populate the grid.
    ActiveInventoryWidget->InitializeFromInventory(Inventory);

    FInputModeGameAndUI InputMode;
    InputMode.SetWidgetToFocus(ActiveInventoryWidget->TakeWidget());
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    InputMode.SetHideCursorDuringCapture(false);
    PC->SetInputMode(InputMode);
    PC->SetShowMouseCursor(true);

    if (GothicHUD)
    {
        GothicHUD->SetCrosshairVisible(false);
    }

}

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

    }
    else
    {
        // Unequipped or non-weapon item — clear the slot
        WeaponSlots[WeaponIndex].WeaponData = nullptr;
        WeaponSlots[WeaponIndex].CurrentMagazine = 0;
        WeaponSlots[WeaponIndex].CurrentReserve = 0;

    }

    // Equipping into the active slot swaps the weapon out from under any in-progress
    // hold, so abandon it — the conversion cost was tied to the old weapon's tier.
    if (WeaponIndex == ActiveWeaponIndex)
    {
        EndSteadfastHold();
        RefreshWeaponVisuals(WeaponIndex);
        PushAmmoToHUD();
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