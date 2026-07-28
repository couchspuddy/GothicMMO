// GothicPlayerCharacter.cpp

#include "Character/GothicPlayerCharacter.h"
#include "Character/GothicInputHandlerComponent.h"
#include "AbilitySystem/GothicAbilitySet.h"
#include "AbilitySystem/GothicInputConfig.h"
#include "AbilitySystem/GothicAbilitySystemComponent.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "Game/GothicPlayerState.h"
#include "Game/GothicGameMode.h"
#include "Game/GothicGameState.h"
#include "GameFramework/PlayerStart.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "UI/GothicHUD.h"
#include "AI/GothicSteadfastComponent.h"
#include "AI/GothicCombatStateComponent.h"
#include "UI/GothicHUDWidget.h"
#include "UI/GothicInventoryWidget.h"
#include "Camera/CameraComponent.h"
#include "Net/UnrealNetwork.h"
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
#include "Game/GothicGameState.h"
#include "AI/GothicEnemyBase.h"
#include "AI/GothicEncounterVolume.h"
#include "UI/GothicHUD.h"
#include "GameFramework/PlayerController.h"
#include "Items/GothicItemDefinition.h"
#include "AbilitySystem/GA_TheLovedandTheLost.h"
#include "AbilitySystem/GA_NotAtAll.h"

AGothicPlayerCharacter::AGothicPlayerCharacter()
{

    PrimaryActorTick.bCanEverTick = true;

    // The player is the only actor that collides with the Bleed. AGothicBleedGate
    // types its barrier as ArenaBlock, whose project-default response is Ignore,
    // so this opt-in is what makes a gate solid — and its absence on the Accursed
    // is what lets them walk through one. Set here rather than on the Blueprint's
    // capsule so it is versioned with the gate that depends on it, and cannot be
    // lost to a Blueprint data revert.
    GetCapsuleComponent()->SetCollisionResponseToChannel(
        ECC_GameTraceChannel2 /*ArenaBlock*/, ECR_Block);

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

    // Whatever the Blueprint set is the resting FOV — read it once rather than
    // hardcoding 90 here and having the two disagree the moment someone tunes it.
    if (FirstPersonCamera)
    {
        HipFieldOfView = FirstPersonCamera->FieldOfView;
    }

    AnchorCameraToBone();


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
            WeaponMeshComponent->SetRelativeScale3D(StartWeapon->MeshScale);
        }

        ApplyWeaponAttachment(StartWeapon);
    }

    // A constructor SetOwnerNoSee is overridden by whatever the Blueprint serialized
    // on the inherited mesh, and the Blueprint had it off — which is the entire reason
    // the player could see their own shoulder swinging through frame. Enforce it here,
    // where the Blueprint cannot win.
    if (GetMesh() && bHideBodyInFirstPerson)
    {
        GetMesh()->SetOwnerNoSee(true);
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

        // First health readout, for the same reason and with a worse symptom.
        // WBP_HUD_LayoutA's Construct hardcodes the bar to 100%, and UpdateHealth
        // only fires on a health CHANGE — so a player who does not start at full
        // health saw a full bar that snapped to the truth on the first hit,
        // reading as one huge chunk of damage regardless of the amount. Push the
        // real ratio once here so the bar starts honest.
        if (GothicHUD && AttributeSet)
        {
            GothicHUD->UpdateHealth(AttributeSet->GetHealth(), AttributeSet->GetMaxHealth());
        }

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

            // Now that GAS and the inventory link are ready, hand the player
            // their starting kit (once, on authority) so a tester walks in
            // geared and can immediately swap drops in against it.
            Inventory->GrantStartingItems();
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

    // Interact — collect the shared Selah prompt when near the fallen corpse.
    if (InteractAction)
        EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &AGothicPlayerCharacter::OnInteract);

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

    // Aim down sights — hold, so it needs both edges like sprint and reload
    if (ADSAction)
    {
        EIC->BindAction(ADSAction, ETriggerEvent::Started,   this, &AGothicPlayerCharacter::OnADSPressed);
        EIC->BindAction(ADSAction, ETriggerEvent::Completed, this, &AGothicPlayerCharacter::OnADSReleased);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: ADSAction not assigned — aiming does nothing. Assign IA_ADS in BP_GothicPlayerCharacter."));
    }

    // Quit menu toggle
    if (QuitMenuAction)
    {
        EIC->BindAction(QuitMenuAction, ETriggerEvent::Started, this, &AGothicPlayerCharacter::ToggleQuitMenu);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("GothicPlayerCharacter: QuitMenuAction not assigned — the quit menu cannot be opened and a packaged build cannot be exited. Assign IA_QuitMenu in BP_GothicPlayerCharacter."));
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

    // Fell through the map. This is the reliable death-floor trigger: the
    // engine's KillZ usually sits far below any real floor, so a player who
    // clips through geometry falls forever and never reaches it. Catch them at
    // a level-tunable height and respawn. Server-authoritative.
    if (HasAuthority() && GetActorLocation().Z < FallRespawnZ)
    {
        TriggerFallRespawn();
        return;
    }

    // Ease the FOV toward the aim state. Interpolated rather than snapped: a hard
    // FOV cut reads as a glitch, and the ~0.15s pull-in is most of what makes
    // aiming feel like a weapon settling rather than a zoom toggle.
    if (IsLocallyControlled() && FirstPersonCamera)
    {
        const float TargetFOV = bIsAiming ? ADSFieldOfView : HipFieldOfView;
        const float CurrentFOV = FirstPersonCamera->FieldOfView;
        if (!FMath::IsNearlyEqual(CurrentFOV, TargetFOV, 0.05f))
        {
            FirstPersonCamera->SetFieldOfView(FMath::FInterpTo(
                CurrentFOV, TargetFOV, DeltaTime, FMath::Max(0.1f, ADSFieldOfViewInterpSpeed)));
        }
    }

    if (IsLocallyControlled())
    {
        // Ahead of the bHUDReady early-return below: the weapon is visible from the
        // first frame, and the HUD has nothing to do with it.
        UpdateFirstPersonWeaponPose(DeltaTime);

        UpdateSelahInteractPrompt();
    }

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
    // EnhancedInput keeps firing Move even under SetInputModeUIOnly, so opening
    // an interact panel or the inventory wouldn't otherwise stop the player. The
    // mouse cursor is only ever shown while a blocking UI is up in this project,
    // so it's a reliable "in a menu" signal — gate movement on it here, once,
    // instead of in every interactable.
    if (const APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (PC->bShowMouseCursor)
        {
            return;
        }
    }

    // The Selah moment holds you in place. Same reasoning as the cursor gate above:
    // refuse the input here, once, rather than in every system that could move.
    if (bSelahMomentLock)
    {
        return;
    }

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
    bIsSprinting = true;
    RefreshMovementSpeed();
}

void AGothicPlayerCharacter::OnSprintStopped()
{
    bIsSprinting = false;
    RefreshMovementSpeed();
}

void AGothicPlayerCharacter::OnInteract()
{
    AGothicGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGothicGameState>() : nullptr;

    // Nearest pending encounter that covers us — NOT "whichever completed last".
    // With overlapping volumes the single-slot version handed us a prompt anchored
    // to a different encounter entirely, and collecting ran against that one.
    AGothicEncounterVolume* Enc = GS ? GS->GetPromptEncounterFor(GetActorLocation()) : nullptr;
    if (!Enc)
    {
        UE_LOG(LogTemp, Warning, TEXT("Selah: interact pressed but no meditation prompt covers this spot (%d pending)"),
            GS ? (GS->HasPendingPrompt() ? 1 : 0) : -1);
        return;
    }

    UE_LOG(LogTemp, Verbose, TEXT("Selah: interact accepted at %.0fuu from %s"),
        FVector::Dist(GetActorLocation(), Enc->GetActorLocation()), *Enc->GetName());
    ServerCollectEncounterSelah(Enc);
}

void AGothicPlayerCharacter::ServerCollectEncounterSelah_Implementation(AGothicEncounterVolume* Enc)
{
    AGothicGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGothicGameState>() : nullptr;

    // Trust nothing from the client: the encounter must still be offering a prompt.
    if (!GS || !Enc || !GS->IsPromptPending(Enc))
    {
        return;
    }

    // Server-side range recheck (a little slack) so a client can't collect from afar.
    if (FVector::Dist(GetActorLocation(), Enc->GetActorLocation()) > Enc->GetMeditationRange() * 1.25f)
    {
        return;
    }

    Enc->CompleteCollection();
}

void AGothicPlayerCharacter::UpdateSelahInteractPrompt()
{
    APlayerController* PC = Cast<APlayerController>(GetController());
    AGothicHUD* HUD = PC ? Cast<AGothicHUD>(PC->GetHUD()) : nullptr;
    if (!HUD)
    {
        return;
    }

    AGothicGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGothicGameState>() : nullptr;
    AGothicEncounterVolume* Enc = GS ? GS->GetPromptEncounterFor(GetActorLocation()) : nullptr;

    if (Enc)
    {
        // Yield to any other interactable that already owns the prompt slot.
        //
        // The HUD keeps ONE prompt with one owner, and this runs every tick over a
        // radius the size of the whole arena. Asserting unconditionally meant
        // "Meditate" overwrote the Contract Board's prompt every single frame, and
        // the board's own ClearInteractPrompt was rejected because it wasn't the
        // owner — every interactable inside a completed encounter went dead.
        AActor* CurrentOwner = HUD->GetInteractPromptOwner();
        if (!CurrentOwner || CurrentOwner == Enc)
        {
            HUD->SetInteractPrompt(Enc, FText::FromString(TEXT("Meditate")));
            ShownSelahPromptCorpse = Enc;
        }
    }
    else if (ShownSelahPromptCorpse.IsValid())
    {
        HUD->ClearInteractPrompt(ShownSelahPromptCorpse.Get());
        ShownSelahPromptCorpse = nullptr;
    }
}

void AGothicPlayerCharacter::AnchorCameraToBone()
{
    if (!FirstPersonCamera || CameraAttachBoneName.IsNone())
    {
        return; // left on the mesh component — the original behaviour
    }

    USkeletalMeshComponent* MeshComp = GetMesh();
    if (!MeshComp || !MeshComp->DoesSocketExist(CameraAttachBoneName))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GothicPlayerCharacter: CameraAttachBoneName '%s' is not a bone or socket on "
                 "%s — camera left on the mesh component."),
            *CameraAttachBoneName.ToString(), *GetNameSafe(MeshComp ? MeshComp->GetSkeletalMeshAsset() : nullptr));
        return;
    }

    // KeepWorldTransform, so the eye does not move when the anchor changes. The
    // camera keeps the exact position the Blueprint placed it at and only starts
    // following a different parent — which makes this a clean A/B: anything that
    // looks different afterwards is the anchoring, not a shifted viewpoint.
    FirstPersonCamera->AttachToComponent(
        MeshComp, FAttachmentTransformRules::KeepWorldTransform, CameraAttachBoneName);

    if (!CameraBoneOffsetAdjust.IsNearlyZero())
    {
        FirstPersonCamera->AddLocalOffset(CameraBoneOffsetAdjust);
    }
}

void AGothicPlayerCharacter::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AGothicPlayerCharacter, bIsAiming);
}

void AGothicPlayerCharacter::OnADSPressed()  { SetAiming(true); }
void AGothicPlayerCharacter::OnADSReleased() { SetAiming(false); }

void AGothicPlayerCharacter::SetAiming(bool bNewAiming)
{
    if (bIsAiming == bNewAiming)
    {
        return;
    }

    // Sprinting and aiming are mutually exclusive — holding both would otherwise
    // leave you at sprint speed with a sniper's FOV.
    if (bNewAiming && bIsSprinting)
    {
        bIsSprinting = false;
    }

    bIsAiming = bNewAiming;
    RefreshMovementSpeed();

    // The crosshair has had this hook since it was written, with nothing calling it.
    if (const APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (AGothicHUD* GothicHUD = Cast<AGothicHUD>(PC->GetHUD()))
        {
            GothicHUD->NotifyAimDownSights(bIsAiming);
        }
    }

    if (!HasAuthority())
    {
        ServerSetAiming(bNewAiming);
    }
}

void AGothicPlayerCharacter::ServerSetAiming_Implementation(bool bNewAiming)
{
    // Authority copy only. Re-running the local half here would fight the owning
    // client's prediction on a listen server.
    bIsAiming = bNewAiming;
    RefreshMovementSpeed();
}

void AGothicPlayerCharacter::RefreshMovementSpeed()
{
    const float Base = bIsSprinting ? SprintSpeed : WalkSpeed;

    // MovementSpeed is an additive cm/s bonus from gear. Recomputed rather than
    // accumulated, so repeated equips cannot stack the bonus onto itself.
    float Bonus = 0.f;
    if (AbilitySystemComponent)
    {
        Bonus = AbilitySystemComponent->GetNumericAttribute(
            UGothicAttributeSet::GetMovementSpeedAttribute());
    }

    // Aiming multiplies the FINAL speed, gear bonus included, so a heavily kitted
    // player still gives up the same proportion for aiming.
    const float AimScale = bIsAiming ? FMath::Clamp(ADSMoveSpeedMultiplier, 0.1f, 1.f) : 1.f;

    GetCharacterMovement()->MaxWalkSpeed = FMath::Max(1.f, (Base + Bonus) * AimScale);
}

void AGothicPlayerCharacter::TriggerSelahMoment()
{
    // Hold the player still for the reveal. Movement and fire are refused while
    // this is set (see OnMove and UGA_Fire::CanActivateAbility); menus are not.
    bSelahMomentLock = true;

    // Kill any momentum already in flight, or a player who was sprinting when the
    // last enemy died keeps sliding through the whole moment.
    if (UCharacterMovementComponent* Move = GetCharacterMovement())
    {
        Move->StopMovementImmediately();
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            SelahMomentLockHandle, this, &AGothicPlayerCharacter::EndSelahMomentLock,
            FMath::Max(0.1f, SelahMomentLockSeconds), false);
    }

    // Blueprint handles the visual and audio — call the event
    OnSelahMoment();
}

void AGothicPlayerCharacter::EndSelahMomentLock()
{
    bSelahMomentLock = false;

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(SelahMomentLockHandle);
    }
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
    // Conversion is granted here, mid-hold — never deferred to release.
    if (!ConvertSteadfastToReserve())
    {
        // Nothing left to convert, or nowhere to put it. Stop rather than spin.
        //
        // Deliberately leave bSteadfastHoldThresholdReached false: the flag's only
        // job is to tell the release handler "the payoff was already given, don't
        // also tap-reload". No conversion happened, so the release must still
        // reload. Setting it unconditionally here made every press longer than
        // SteadfastHoldThreshold a no-op — and because StartingReserveAmmo equals
        // MaxReserveAmmo on every weapon, reserve begins full and this branch is
        // exactly what a fresh player hits.
        EndSteadfastHold();
        return;
    }

    bSteadfastHoldThresholdReached = true;
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

int32 AGothicPlayerCharacter::GetActiveGearPower() const
{
    if (WeaponSlots.IsValidIndex(ActiveWeaponIndex))
    {
        return WeaponSlots[ActiveWeaponIndex].GearPower;
    }
    return 0;
}

int32 AGothicPlayerCharacter::GetAggregateGearPower() const
{
    const AGothicPlayerState* PS = GetPlayerState<AGothicPlayerState>();
    const UGothicInventoryComponent* Inventory = PS ? PS->GetInventory() : nullptr;
    return Inventory ? Inventory->GetAggregateGearPower() : 0;
}

float AGothicPlayerCharacter::GetArchetypeDamageBonusPct(EGothicWeaponArchetype Archetype) const
{
    const AGothicPlayerState* PS = GetPlayerState<AGothicPlayerState>();
    const UGothicInventoryComponent* Inventory = PS ? PS->GetInventory() : nullptr;
    return Inventory ? Inventory->GetArchetypeDamageBonus(GetArchetypeDamageStat(Archetype)) : 0.f;
}

const UGA_TheLovedAndTheLost* AGothicPlayerCharacter::FindLovedAndLost() const
{
    if (!AbilitySystemComponent)
    {
        return nullptr;
    }

    // The passive is granted once and runs InstancedPerActor, so its single
    // primary instance holds the live ramp state.
    for (const FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
    {
        if (const UGA_TheLovedAndTheLost* Ramp =
                Cast<UGA_TheLovedAndTheLost>(Spec.GetPrimaryInstance()))
        {
            return Ramp;
        }
    }
    return nullptr;
}

float AGothicPlayerCharacter::GetLovedAndLostRamp() const
{
    const UGA_TheLovedAndTheLost* Ramp = FindLovedAndLost();
    return Ramp ? Ramp->GetRampAlpha() : 0.f;
}

bool AGothicPlayerCharacter::IsLovedAndLostActive() const
{
    const UGA_TheLovedAndTheLost* Ramp = FindLovedAndLost();
    return Ramp && Ramp->IsRampActive();
}

bool AGothicPlayerCharacter::IsReadActive() const
{
    return AbilitySystemComponent &&
        AbilitySystemComponent->HasMatchingGameplayTag(
            FGameplayTag::RequestGameplayTag(FName("State.Read")));
}

bool AGothicPlayerCharacter::IsReckoningActive() const
{
    return AbilitySystemComponent &&
        AbilitySystemComponent->HasMatchingGameplayTag(
            FGameplayTag::RequestGameplayTag(FName("State.Reckoning")));
}

bool AGothicPlayerCharacter::IsNotAtAllGranted() const
{
    if (!AbilitySystemComponent)
    {
        return false;
    }

    // Check the granted spec's ability class rather than a live instance — the
    // passive counts as "granted" the moment it's on the ASC, whether or not its
    // instance has spun up yet.
    for (const FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
    {
        if (Spec.Ability && Spec.Ability->IsA<UGA_NotAtAll>())
        {
            return true;
        }
    }
    return false;
}

void AGothicPlayerCharacter::TriggerFallRespawn()
{
    // Authority only. A fall through the map is NOT a death — routing it through
    // the game mode's RequestRespawn destroyed the pawn and left the controller
    // with no view target for the full 10s respawn delay, which reads as the
    // game freezing. Instead, just teleport the same pawn back onto solid ground
    // instantly (last checkpoint, else a PlayerStart) and kill its velocity. No
    // penalty, no delay, no dependency on the game mode being GothicGameMode.
    if (!HasAuthority())
    {
        return;
    }

    FVector SafeLocation(0.f, 0.f, 500.f);

    if (const AGothicGameState* GS =
            GetWorld() ? GetWorld()->GetGameState<AGothicGameState>() : nullptr)
    {
        if (!GS->CheckpointLocation.IsZero())
        {
            SafeLocation = GS->CheckpointLocation;
        }
        else if (const AActor* Start =
                     UGameplayStatics::GetActorOfClass(this, APlayerStart::StaticClass()))
        {
            SafeLocation = Start->GetActorLocation();
        }
    }

    SafeLocation.Z += 100.f;  // clear the floor so we don't spawn embedded

    SetActorLocation(SafeLocation, false, nullptr, ETeleportType::TeleportPhysics);
    if (UCharacterMovementComponent* Move = GetCharacterMovement())
    {
        Move->StopMovementImmediately();
    }
}

void AGothicPlayerCharacter::FellOutOfWorld(const UDamageType& DmgType)
{
    // Dropped below the world's KillZ — recover in place rather than the engine
    // default (destroy the pawn, strand the controller).
    if (HasAuthority())
    {
        TriggerFallRespawn();
        return;
    }

    Super::FellOutOfWorld(DmgType);
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

    // ...and abandons the Oversurge streak with it. The streak is a property of
    // sustained fire from one weapon; letting it carry across a swap would mean
    // building it up on the cheap repeater and spending it on a heavy hitter.
    ResetConsecutiveHits();

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

void AGothicPlayerCharacter::ToggleQuitMenu()
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

    // Escape backs out of the topmost screen. With the inventory up, that IS the
    // inventory — opening the quit menu on top of it would leave two screens
    // stacked, both wanting the cursor, and ToggleInventory's input-mode reset
    // would fight the menu's on the way back down.
    if (ActiveInventoryWidget)
    {
        ToggleInventory();
        return;
    }

    if (AGothicHUD* GothicHUD = Cast<AGothicHUD>(PC->GetHUD()))
    {
        GothicHUD->ToggleQuitMenu();
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GothicPlayerCharacter: no AGothicHUD — quit menu cannot open. Is the "
                 "GameMode's HUDClass set to BP_GothicHUD?"));
    }
}

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
    // Any equip can change the MovementSpeed secondary, and attribute changes do
    // not reach CharacterMovement on their own — armour included, so this runs
    // before the weapon-slot early-out below.
    RefreshMovementSpeed();

    const int32 WeaponIndex = EquipSlotToWeaponIndex(Slot);
    if (WeaponIndex < 0)
    {
        // Not a weapon slot — armor equip, nothing further here
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
        // Carry the rolled copy's Gear Power across. Without this the slot keeps
        // only the shared archetype asset, and every copy of a Revolver deals
        // identical damage regardless of what it rolled.
        WeaponSlots[WeaponIndex].GearPower = Item.GearPower;
        WeaponSlots[WeaponIndex].InitFromData();
    }
    else
    {
        // Unequipped or non-weapon item — clear the slot
        WeaponSlots[WeaponIndex].WeaponData = nullptr;
        WeaponSlots[WeaponIndex].CurrentMagazine = 0;
        WeaponSlots[WeaponIndex].CurrentReserve = 0;
        WeaponSlots[WeaponIndex].GearPower = 0;
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

void AGothicPlayerCharacter::ApplyWeaponAttachment(const UGothicWeaponData* WeaponData)
{
    if (!WeaponMeshComponent)
    {
        return;
    }

    // Only the local player gets the camera-parented weapon. A simulated proxy has no
    // meaningful camera of its own, and other players need to see the gun in the hand
    // where the animation puts it.
    const bool bCameraMounted =
        bAttachWeaponToCamera && IsLocallyControlled() && FirstPersonCamera != nullptr;

    if (bCameraMounted)
    {
        WeaponMeshComponent->AttachToComponent(
            FirstPersonCamera, FAttachmentTransformRules::SnapToTargetNotIncludingScale);

        WeaponMeshComponent->SetRelativeLocation(CameraWeaponOffset);
        WeaponMeshComponent->SetRelativeRotation(CameraWeaponRotation);
    }
    else if (GetMesh())
    {
        WeaponMeshComponent->AttachToComponent(
            GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("HandGrip_R"));

        if (WeaponData)
        {
            WeaponMeshComponent->SetRelativeLocation(WeaponData->MeshOffset);
            WeaponMeshComponent->SetRelativeRotation(WeaponData->MeshRotation);
        }
    }
}

void AGothicPlayerCharacter::AddWeaponFireKick()
{
    // Stack, then clamp. Repeating a single identical hop reads as a looping
    // animation rather than a gun fighting back, but unbounded stacking walks the
    // weapon out of frame on anything with a fast fire rate.
    CurrentFireKickLocation += FireKickOffset;
    CurrentFireKickRotation += FireKickRotation;

    const float Ceiling = FMath::Max(1.f, MaxStackedFireKick);

    const FVector MaxLoc = FireKickOffset * Ceiling;
    CurrentFireKickLocation.X = FMath::Clamp(CurrentFireKickLocation.X,
        FMath::Min(0.f, MaxLoc.X), FMath::Max(0.f, MaxLoc.X));
    CurrentFireKickLocation.Y = FMath::Clamp(CurrentFireKickLocation.Y,
        FMath::Min(0.f, MaxLoc.Y), FMath::Max(0.f, MaxLoc.Y));
    CurrentFireKickLocation.Z = FMath::Clamp(CurrentFireKickLocation.Z,
        FMath::Min(0.f, MaxLoc.Z), FMath::Max(0.f, MaxLoc.Z));

    const FRotator MaxRot = FireKickRotation * Ceiling;
    CurrentFireKickRotation.Pitch = FMath::Clamp(CurrentFireKickRotation.Pitch,
        FMath::Min(0.f, MaxRot.Pitch), FMath::Max(0.f, MaxRot.Pitch));
    CurrentFireKickRotation.Yaw = FMath::Clamp(CurrentFireKickRotation.Yaw,
        FMath::Min(0.f, MaxRot.Yaw), FMath::Max(0.f, MaxRot.Yaw));
    CurrentFireKickRotation.Roll = FMath::Clamp(CurrentFireKickRotation.Roll,
        FMath::Min(0.f, MaxRot.Roll), FMath::Max(0.f, MaxRot.Roll));
}

void AGothicPlayerCharacter::UpdateFirstPersonWeaponPose(float DeltaTime)
{
    if (!WeaponMeshComponent || !bAttachWeaponToCamera || !FirstPersonCamera)
    {
        return;
    }

    // Blended, not switched — the weapon should swing down as the sprint starts
    // rather than appear in the lowered pose on the first frame.
    SprintPoseAlpha = FMath::FInterpTo(
        SprintPoseAlpha, IsSprinting() ? 1.f : 0.f, DeltaTime, FMath::Max(0.5f, SprintPoseBlendSpeed));

    CurrentFireKickLocation = FMath::VInterpTo(
        CurrentFireKickLocation, FVector::ZeroVector, DeltaTime, FMath::Max(0.5f, FireKickRecoverySpeed));
    CurrentFireKickRotation = FMath::RInterpTo(
        CurrentFireKickRotation, FRotator::ZeroRotator, DeltaTime, FMath::Max(0.5f, FireKickRecoverySpeed));

    const FVector TargetLocation =
        CameraWeaponOffset + (SprintWeaponOffset * SprintPoseAlpha) + CurrentFireKickLocation;

    // Compose rotations as quaternions with the offsets OUTERMOST, so they act in
    // camera space. Adding Euler components instead would apply them in the weapon's
    // own frame — and this mesh is yawed -90, so its local pitch axis runs straight
    // down the barrel. "Muzzle rises" would have come out as the gun rolling on its
    // side. The same trap cost a session's worth of tuning on the spine pin.
    const FQuat BaseQ   = CameraWeaponRotation.Quaternion();
    const FQuat SprintQ = FRotator(SprintWeaponRotation.Pitch * SprintPoseAlpha,
                                   SprintWeaponRotation.Yaw   * SprintPoseAlpha,
                                   SprintWeaponRotation.Roll  * SprintPoseAlpha).Quaternion();
    const FQuat KickQ   = CurrentFireKickRotation.Quaternion();

    WeaponMeshComponent->SetRelativeLocation(TargetLocation);
    WeaponMeshComponent->SetRelativeRotation((KickQ * SprintQ * BaseQ).Rotator());
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
        WeaponMeshComponent->SetRelativeScale3D(WeaponData->MeshScale);

        // Location and rotation come from here, not from WeaponData directly — which
        // of the two offsets is correct depends on what the weapon is parented to.
        ApplyWeaponAttachment(WeaponData);

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