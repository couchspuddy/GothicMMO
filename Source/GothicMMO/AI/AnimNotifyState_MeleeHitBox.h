// AnimNotifyState_MeleeHitbox.h
// An anim notify STATE (not a single-frame notify) that controls the
// hitbox window for enemy melee attacks.
//
// NotifyBegin → EnableHitbox()
// NotifyEnd   → DisableHitbox()
//
// Use a notify state rather than two separate notifies because:
//   1. The hitbox window is inherently a duration, not two events
//   2. If the montage is interrupted, NotifyEnd fires automatically
//      via the state's cleanup — prevents stuck-open hitboxes
//
// In the montage editor, drag this notify state across the active
// frames of the attack animation. The hitbox is live for exactly
// the duration of this notify bar.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_MeleeHitBox.generated.h"

UCLASS(meta=(DisplayName = "Melee Hitbox Window"))
class GOTHICMMO_API UAnimNotifyState_MeleeHitbox : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual FString GetNotifyName_Implementation() const override
	{
		return TEXT("Melee Hitbox");
	}

	virtual void NotifyBegin(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};