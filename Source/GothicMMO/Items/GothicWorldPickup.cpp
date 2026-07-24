// GothicWorldPickup.cpp

#include "Items/GothicWorldPickup.h"
#include "Items/GothicInventoryComponent.h"
#include "Items/GothicItemDefinition.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "TimerManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"

AGothicWorldPickup::AGothicWorldPickup()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;

    // Interaction sphere — auto-collects on overlap
    InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
    InteractionSphere->SetSphereRadius(150.f);
    InteractionSphere->SetCollisionProfileName(FName("OverlapAllDynamic"));
    InteractionSphere->SetGenerateOverlapEvents(true);
    SetRootComponent(InteractionSphere);

    // Visual mesh — default engine sphere, scaled small
    PickupMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PickupMesh"));
    PickupMesh->SetupAttachment(InteractionSphere);
    PickupMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PickupMesh->SetRelativeScale3D(FVector(0.25f));
    PickupMesh->SetRelativeLocation(FVector(0.f, 0.f, 20.f));

    // Default sphere mesh — engine built-in
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
        TEXT("/Engine/BasicShapes/Sphere"));
    if (SphereMesh.Succeeded())
    {
        PickupMesh->SetStaticMesh(SphereMesh.Object);
    }
}

void AGothicWorldPickup::BeginPlay()
{
    Super::BeginPlay();

    InteractionSphere->OnComponentBeginOverlap.AddDynamic(
        this, &AGothicWorldPickup::OnOverlapBegin);

    // No despawn timer — items persist until collected
}

void AGothicWorldPickup::InitializePickup(const FGothicItemInstance& InItem)
{
    HeldItem = InItem;

    if (HeldItem.Definition)
    {

        // Set mesh color based on rarity
        if (PickupMesh)
        {
            FLinearColor RarityColor;
            switch (HeldItem.Definition->Rarity)
            {
                case EGothicItemRarity::Salvage:     RarityColor = FLinearColor(0.6f, 0.6f, 0.6f); break;
                case EGothicItemRarity::Kept:        RarityColor = FLinearColor(0.2f, 0.8f, 0.2f); break;
                case EGothicItemRarity::Remembered:  RarityColor = FLinearColor(0.3f, 0.4f, 1.0f); break;
                case EGothicItemRarity::Resonant:    RarityColor = FLinearColor(0.6f, 0.1f, 0.9f); break;
                case EGothicItemRarity::Pure:        RarityColor = FLinearColor(1.0f, 0.85f, 0.0f); break;
                default:                             RarityColor = FLinearColor::White; break;
            }

            UMaterialInstanceDynamic* DynMat = PickupMesh->CreateAndSetMaterialInstanceDynamic(0);
            if (DynMat)
            {
                DynMat->SetVectorParameterValue(FName("BaseColor"), RarityColor);
                // Emissive glow so it's visible on the ground
                DynMat->SetVectorParameterValue(FName("EmissiveColor"), RarityColor * 3.f);
            }
        }
    }
}

void AGothicWorldPickup::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Gentle hover rotation so items are visible on the floor
    if (PickupMesh)
    {
        FRotator Rot = PickupMesh->GetRelativeRotation();
        Rot.Yaw += 60.f * DeltaTime;
        PickupMesh->SetRelativeRotation(Rot);

        // Subtle bob up and down
        float BobOffset = FMath::Sin(GetGameTimeSinceCreation() * 2.f) * 5.f;
        FVector Loc = PickupMesh->GetRelativeLocation();
        Loc.Z = 20.f + BobOffset;
        PickupMesh->SetRelativeLocation(Loc);
    }
}

bool AGothicWorldPickup::TryCollect(AActor* Collector)
{
    if (bCollected || !Collector || !HeldItem.IsValid())
    {
        return false;
    }

    if (!HasAuthority())
    {
        return false;
    }

    // Find the inventory component — lives on PlayerState
    UGothicInventoryComponent* Inventory = nullptr;

    // Check the collector's PlayerState first (players)
    if (APawn* Pawn = Cast<APawn>(Collector))
    {
        if (APlayerState* PS = Pawn->GetPlayerState())
        {
            Inventory = PS->FindComponentByClass<UGothicInventoryComponent>();
        }
    }

    if (!Inventory)
    {
        UE_LOG(LogTemp, Warning, TEXT("WorldPickup: No inventory on %s"), *Collector->GetName());
        return false;
    }

    if (Inventory->AddItem(HeldItem))
    {
        bCollected = true;
        Destroy();
        return true;
    }

    UE_LOG(LogTemp, Warning, TEXT("WorldPickup: %s could not collect — inventory full?"),
        *Collector->GetName());
    return false;
}

void AGothicWorldPickup::OnOverlapBegin(UPrimitiveComponent* OverlappedComponent,
    AActor* OtherActor, UPrimitiveComponent* OtherComp,
    int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    if (bCollected || !HasAuthority())
    {
        return;
    }

    // Auto-collect when a player walks over it
    if (APawn* Pawn = Cast<APawn>(OtherActor))
    {
        if (Pawn->IsPlayerControlled())
        {
            TryCollect(OtherActor);
        }
    }
}

void AGothicWorldPickup::DespawnPickup()
{
    if (!bCollected)
    {
        Destroy();
    }
}