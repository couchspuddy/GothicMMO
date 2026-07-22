// AnimNotify_GroundPoundImpact.h
// Single-frame anim notify that fires a radial AoE damage check
// at the boss's location on the ground pound impact frame.
//
// Unlike the hitbox component (which follows a bone through a swing),
// the ground pound is a radial AoE at a fixed point — simpler to
// implement as a notify that does a sphere overlap.
//
// In the ground pound montage, place this notify on the exact frame
// where the fists/body hit the ground.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_GroundPoundImpact.generated.h"

class UGameplayEffect;

UCLASS(meta=(DisplayName = "Ground Pound Impact"))
class GOTHICMMO_API UAnimNotify_GroundPoundImpact : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual FString GetNotifyName_Implementation() const override
	{
		return TEXT("Ground Pound");
	}

	virtual void Notify(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

protected:
	/** Radius of the ground pound AoE (cm). */
	UPROPERTY(EditAnywhere, Category = "Ground Pound")
	float ImpactRadius = 400.f;

	/** Damage dealt to players in the AoE. */
	UPROPERTY(EditAnywhere, Category = "Ground Pound")
	float ImpactDamage = 50.f;

	/** GameplayEffect that applies IncomingDamage. Uses Data.Damage SetByCaller. */
	UPROPERTY(EditAnywhere, Category = "Ground Pound")
	TSubclassOf<UGameplayEffect> DamageEffect;
};