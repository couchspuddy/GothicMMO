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
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;

    // Interaction sphere — auto-collects on overlap
    InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
    InteractionSphere->SetSphereRadius(150.f);
    InteractionSphere->SetCollisionProfileName(FName("OverlapAllDynamic"));
    InteractionSphere->SetGenerateOverlapEvents(true);
    SetRootComponent(InteractionSphere);

    // Visual mesh — placeholder sphere, replace with proper loot mesh later
    PickupMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PickupMesh"));
    PickupMesh->SetupAttachment(InteractionSphere);
    PickupMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PickupMesh->SetRelativeScale3D(FVector(0.3f));
}

void AGothicWorldPickup::BeginPlay()
{
    Super::BeginPlay();

    InteractionSphere->OnComponentBeginOverlap.AddDynamic(
        this, &AGothicWorldPickup::OnOverlapBegin);

    // Despawn timer
    FTimerHandle DespawnTimer;
    GetWorldTimerManager().SetTimer(DespawnTimer, this,
        &AGothicWorldPickup::DespawnPickup, PickupLifetime, false);
}

void AGothicWorldPickup::InitializePickup(const FGothicItemInstance& InItem)
{
    HeldItem = InItem;

    if (HeldItem.Definition)
    {
        UE_LOG(LogTemp, Log, TEXT("WorldPickup: Initialized with %s (Rarity %d)"),
            *HeldItem.Definition->ItemID.ToString(),
            (int32)HeldItem.Definition->Rarity);
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
        UE_LOG(LogTemp, Log, TEXT("WorldPickup: %s collected %s"),
            *Collector->GetName(),
            *HeldItem.Definition->ItemID.ToString());
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
        UE_LOG(LogTemp, Log, TEXT("WorldPickup: %s despawned uncollected"),
            HeldItem.Definition ? *HeldItem.Definition->ItemID.ToString() : TEXT("Unknown"));
        Destroy();
    }
}