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

private:
    FGothicItemInstance HeldItem;
    bool bCollected = false;

    void DespawnPickup();

    UFUNCTION()
    void OnOverlapBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
        bool bFromSweep, const FHitResult& SweepResult);
};