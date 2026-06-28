// GothicPlayerState.h
// PlayerState hosts the ASC and AttributeSet for players.
// This survives seamless travel, respawns, and controller changes.
// The character grabs pointers from here in PossessedBy/OnRep_PlayerState.
//
// In your GameMode: set PlayerStateClass = AGothicPlayerState.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystem/GothicAttributeSet.h"
#include "GothicPlayerState.generated.h"


class UGothicAbilitySystemComponent;


UCLASS()
class GOTHICMMO_API AGothicPlayerState : public APlayerState, public IAbilitySystemInterface
{
    GENERATED_BODY()

public:
    AGothicPlayerState();

    // IAbilitySystemInterface
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

    UFUNCTION(BlueprintPure, Category = "Gothic|PlayerState")
    UGothicAbilitySystemComponent* GetGothicASC() const { return AbilitySystemComponent; }

    UFUNCTION(BlueprintPure, Category = "Gothic|PlayerState")
    UGothicAttributeSet* GetGothicAttributeSet() const { return AttributeSet; }

protected:
    // The ASC lives here — it is replicated and survives respawns.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gothic|Abilities")
    TObjectPtr<UGothicAbilitySystemComponent> AbilitySystemComponent;

    // The attribute set also lives here for the same reason.
    UPROPERTY()
    TObjectPtr<UGothicAttributeSet> AttributeSet;
};
