// GothicGameState.cpp

#include "Game/GothicGameState.h"
#include "AI/GothicEncounterVolume.h"
#include "UI/GothicSelahCollectBarWidget.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Engine/World.h"

void AGothicGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGothicGameState, PendingPromptEncounters);
	DOREPLIFETIME(AGothicGameState, ActivePromptCorpse);
	DOREPLIFETIME(AGothicGameState, CheckpointLocation);
	DOREPLIFETIME(AGothicGameState, SelahNames);
	DOREPLIFETIME(AGothicGameState, SelahCollectPhase);
	DOREPLIFETIME(AGothicGameState, SelahCollectDuration);
}

void AGothicGameState::SetCheckpointFromLocation(const FVector& WorldLocation, AActor* IgnoreActor)
{
	if (!HasAuthority())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Start a little ABOVE the point given, so a location already sitting a
	// fraction under the floor still traces down onto it instead of starting
	// inside the geometry and missing everything.
	const FVector TraceStart = WorldLocation + FVector(0.f, 0.f, 200.f);
	const FVector TraceEnd   = WorldLocation - FVector(0.f, 0.f, 10000.f);

	FCollisionQueryParams Params(TEXT("GothicCheckpointFloor"), false);
	Params.AddIgnoredActor(this);
	if (IgnoreActor)
	{
		Params.AddIgnoredActor(IgnoreActor);
	}

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params))
	{
		CheckpointLocation = Hit.ImpactPoint;
		return;
	}

	// Nothing underneath. Store the raw point rather than nothing at all — a
	// checkpoint somewhere beats a checkpoint nowhere — but say so loudly. A
	// checkpoint recorded in mid-air is the exact failure this function exists to
	// prevent, and it must never pass silently.
	UE_LOG(LogTemp, Warning,
		TEXT("GothicGameState: checkpoint floor trace found nothing under %s — storing the raw "
		     "location. Respawning here will drop the player from whatever height it sits at."),
		*WorldLocation.ToCompactString());

	CheckpointLocation = WorldLocation;
}

void AGothicGameState::SetSelahCollectPhase(uint8 NewPhase, float Duration)
{
	if (!HasAuthority())
	{
		return;
	}

	SelahCollectDuration = Duration;
	SelahCollectPhase = NewPhase;
	ForceNetUpdate();

	// Authority doesn't get its own OnRep — fire it directly so the listen-server
	// host and standalone PIE drive the bar exactly like remote clients.
	OnRep_SelahCollect();
}

void AGothicGameState::OnRep_SelahCollect()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return; // dedicated server / no local viewer — nothing to show
	}

	switch (SelahCollectPhase)
	{
	case 1: // collecting
		if (!CollectBarWidget && CollectBarWidgetClass)
		{
			CollectBarWidget = CreateWidget<UGothicSelahCollectBarWidget>(PC, CollectBarWidgetClass);
		}
		if (CollectBarWidget)
		{
			if (!CollectBarWidget->IsInViewport())
			{
				CollectBarWidget->AddToViewport();
			}
			CollectBarWidget->StartCollect(SelahCollectDuration);
		}
		break;

	case 2: // interrupted
		if (CollectBarWidget)
		{
			CollectBarWidget->InterruptCollect();
		}
		break;

	case 3: // completed
		if (CollectBarWidget)
		{
			CollectBarWidget->CompleteCollect();
		}
		break;

	default: // 0 — clear
		if (CollectBarWidget)
		{
			CollectBarWidget->RemoveFromParent();
			CollectBarWidget = nullptr;
		}
		break;
	}
}

void AGothicGameState::SetSelahNames(const TArray<FText>& Names)
{
	if (!HasAuthority()) return;

	SelahNames = Names;

	// Cap to MaxSelahNames — keep the last N (most recently killed)
	if (SelahNames.Num() > MaxSelahNames)
	{
		SelahNames.RemoveAt(0, SelahNames.Num() - MaxSelahNames);
	}

	ForceNetUpdate();
}

void AGothicGameState::AddEncounterPrompt(AGothicEncounterVolume* Encounter, AGothicEnemyBase* PromptCorpse)
{
	if (!HasAuthority() || !Encounter)
	{
		return;
	}

	if (PendingPromptEncounters.Contains(Encounter))
	{
		return; // already offering — don't re-fire the event
	}

	PendingPromptEncounters.Add(Encounter);
	ActivePromptCorpse = PromptCorpse;
	ForceNetUpdate();

	// The authority does not receive its own OnRep. Call it directly so the
	// listen-server host — and the standalone PIE player — get the same event
	// every remote client gets.
	OnRep_ActivePrompt();
}

void AGothicGameState::ClearEncounterPrompt(AGothicEncounterVolume* Encounter)
{
	if (!HasAuthority() || !Encounter)
	{
		return;
	}

	if (PendingPromptEncounters.Remove(Encounter) == 0)
	{
		return; // wasn't offering one
	}

	ForceNetUpdate();
	OnRep_ActivePrompt();
}

void AGothicGameState::ClearAllEncounterPrompts()
{
	if (!HasAuthority() || PendingPromptEncounters.Num() == 0)
	{
		return;
	}

	PendingPromptEncounters.Reset();
	ActivePromptCorpse = nullptr;
	ForceNetUpdate();
	OnRep_ActivePrompt();
}

AGothicEncounterVolume* AGothicGameState::GetPromptEncounterFor(const FVector& WorldLocation) const
{
	// Nearest wins, but only among encounters whose OWN radius covers the caller.
	// A single global range would make a large arena swallow a small ambush
	// standing inside it.
	AGothicEncounterVolume* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();

	for (const TObjectPtr<AGothicEncounterVolume>& Enc : PendingPromptEncounters)
	{
		if (!Enc)
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(WorldLocation, Enc->GetActorLocation());
		const float Range  = Enc->GetMeditationRange();

		if (DistSq <= Range * Range && DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Enc;
		}
	}

	return Best;
}

void AGothicGameState::OnRep_ActivePrompt()
{
	// The ENCOUNTER set (persistent) is the source of truth, not the corpse — so a
	// despawning corpse never drops a prompt. Fire Activated when the set grew and
	// Collected when it shrank, so Blueprint sees one event per real transition
	// rather than one per array replication.
	const int32 Count = PendingPromptEncounters.Num();

	if (Count > LastBroadcastPromptCount)
	{
		OnEncounterPromptActivated(ActivePromptCorpse);
	}
	else if (Count < LastBroadcastPromptCount)
	{
		OnEncounterPromptCollected();
	}

	LastBroadcastPromptCount = Count;
}