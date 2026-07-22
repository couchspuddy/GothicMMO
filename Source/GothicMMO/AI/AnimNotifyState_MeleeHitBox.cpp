// AnimNotifyState_MeleeHitbox.cpp

#include "AI/AnimNotifyState_MeleeHitbox.h"
#include "AI/GothicMeleeHitboxComponent.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimNotifyState_MeleeHitbox::NotifyBegin(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (!MeshComp || !MeshComp->GetOwner())
	{
		return;
	}

	// Find the hitbox component on the owning actor
	UGothicMeleeHitboxComponent* Hitbox =
		MeshComp->GetOwner()->FindComponentByClass<UGothicMeleeHitboxComponent>();

	if (Hitbox)
	{
		Hitbox->EnableHitbox();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("AnimNotifyState_MeleeHitbox: No GothicMeleeHitboxComponent on %s"),
			*MeshComp->GetOwner()->GetName());
	}
}

void UAnimNotifyState_MeleeHitbox::NotifyEnd(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (!MeshComp || !MeshComp->GetOwner())
	{
		return;
	}

	UGothicMeleeHitboxComponent* Hitbox =
		MeshComp->GetOwner()->FindComponentByClass<UGothicMeleeHitboxComponent>();

	if (Hitbox)
	{
		Hitbox->DisableHitbox();
	}
}