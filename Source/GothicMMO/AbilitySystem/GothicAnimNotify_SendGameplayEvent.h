// GothicAnimNotify_SendGameplayEvent.h
// Anim notify that sends a GameplayEvent to the owning actor's ASC.
// Place this on attack montages at the frame where the swing connects.
// The base ability's WaitGameplayEvent task picks it up and calls
// OnMontageHitWindow on the derived ability.
//
// Set EventTag to Event.Montage.HitWindow in the montage editor.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "GameplayTagContainer.h"
#include "GothicAnimNotify_SendGameplayEvent.generated.h"

UCLASS(DisplayName = "Send Gameplay Event")
class GOTHICMMO_API UGothicAnimNotify_SendGameplayEvent : public UAnimNotify
{
	GENERATED_BODY()

public:
	/** The gameplay event tag to send. Set to Event.Montage.HitWindow for attack montages. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gothic|Animation")
	FGameplayTag EventTag;

	virtual void Notify(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;
};