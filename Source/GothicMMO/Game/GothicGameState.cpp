// GothicGameState.cpp

#include "Game/GothicGameState.h"
#include "Net/UnrealNetwork.h"

void AGothicGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGothicGameState, ActivePromptCorpse);
	DOREPLIFETIME(AGothicGameState, CheckpointLocation);
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