// GothicWorldPickup.h
// A dropped item in the world. Spawned by enemy death when the loot table
// rolls a drop. Players overlap the trigger and press interact to collect.
//
// Lifetime: despawns after PickupLifetime seconds if uncollected.
// Multiplayer: server-authoritative pickup. First player to interact gets it.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Items/GothicItemTypes.h"
#include "GothicWorldPickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UGothicItemDefinition;

UCLASS()
class GOTHICMMO_API AGothicWorldPickup : public AActor
{
    GENERATED_BODY()

public:
    AGothicWorldPickup();

    /** Initialize this pickup with a rolled item. Call immediately after spawning. */
    void InitializePickup(const FGothicItemInstance& InItem);

    /** The item this pickup contains. */
    UFUNCTION(BlueprintPure, Category = "Gothic|Pickup")
    const FGothicItemInstance& GetItem() const { return HeldItem; }

    /** Attempt to collect this pickup into the given player's inventory. */
    UFUNCTION(BlueprintCallable, Category = "Gothic|Pickup")
    bool TryCollect(AActor* Collector);

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Pickup")
    TObjectPtr<USphereComponent> InteractionSphere;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Pickup")
    TObjectPtr<UStaticMeshComponent> PickupMesh;

    /** How long (seconds) before the pickup despawns. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Pickup")
    float PickupLifetime = 30.f;

    /** Auto-pickup radius. Items within this range are collected automatically. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gothic|Pickup")
    float AutoPickupRadius = 150.f;

    /**
     * Item definition for designer-placed pickups. When set, BeginPlay rolls an
     * instance from it so the placed actor works without a code-side
     * InitializePickup call. Leave unset for pickups spawned by loot drops.
     */
    UPROPERTY(EditAnywhere, Category = "Gothic|Pickup")
    TObjectPtr<UGothicItemDefinition> PlacedItemDef;

    /**
     * When true, the collected item is equipped immediately if its EquipSlot is
     * empty. Default false so loot-drop pickups never swap the player's weapon;
     * placed guaranteed weapons (e.g. the tutorial Piece) set it true.
     */
    UPROPERTY(EditAnywhere, Category = "Gothic|Pickup")
    bool bAutoEquipIfSlotEmpty = false;

private:
    FGothicItemInstance HeldItem;
    bool bCollected = false;

    void DespawnPickup();

    UFUNCTION()
    void OnOverlapBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
        bool bFromSweep, const FHitResult& SweepResult);
};