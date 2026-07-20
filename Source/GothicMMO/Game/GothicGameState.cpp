// GothicGameState.cpp

#include "Game/GothicGameState.h"
#include "Net/UnrealNetwork.h"

void AGothicGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGothicGameState, ActivePromptCorpse);
	DOREPLIFETIME(AGothicGameState, CheckpointLocation);
	DOREPLIFETIME(AGothicGameState, SelahNames);
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

void AGothicGameState::SetActivePromptCorpse(AGothicEnemyBase* NewPromptCorpse)
{
	if (!HasAuthority())
	{
		return;
	}

	if (ActivePromptCorpse == NewPromptCorpse)
	{
		return; // no actual change — don't re-fire the event
	}

	ActivePromptCorpse = NewPromptCorpse;
	ForceNetUpdate();

	// The authority does not receive its own OnRep. Call it directly so the
	// listen-server host — and the standalone PIE player — get the same event
	// every remote client gets.
	OnRep_ActivePromptCorpse();
}

void AGothicGameState::OnRep_ActivePromptCorpse()
{
	
	
	if (ActivePromptCorpse)
	{
		OnEncounterPromptActivated(ActivePromptCorpse);
	}
	else
	{
		OnEncounterPromptCollected();
	}
}