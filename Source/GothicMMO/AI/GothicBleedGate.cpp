// GothicBleedGate.cpp

#include "AI/GothicBleedGate.h"
#include "AI/GothicEncounterVolume.h"
#include "Components/BoxComponent.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

AGothicBleedGate::AGothicBleedGate()
{
	PrimaryActorTick.bCanEverTick = false;

	// Unlike AGothicEncounterVolume — which runs independently on every machine
	// and so needs no replication — this actor owns collision the client must
	// agree with. A client whose barrier stayed solid after the server opened it
	// would be walled in by its own prediction.
	bReplicates = true;

	Barrier = CreateDefaultSubobject<UBoxComponent>(TEXT("Barrier"));
	SetRootComponent(Barrier);

	Barrier->SetBoxExtent(FVector(500.f, 100.f, 400.f));
	Barrier->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	// The Bleed stops the player and nothing else. A blocking pair needs BOTH
	// sides to say Block, so typing the barrier as ArenaBlock (a channel whose
	// project default response is Ignore) means only an actor that has opted in
	// to blocking ArenaBlock is stopped — and the player capsule is the sole
	// opt-in. Enemies use the stock Pawn profile, ignore ArenaBlock, and walk
	// through. Typing this as WorldStatic + Block Pawn instead would have walled
	// the Accursed in with the player and piled every wave against the barrier.
	Barrier->SetCollisionObjectType(ECC_GameTraceChannel2 /*ArenaBlock*/);
	Barrier->SetCollisionResponseToAllChannels(ECR_Ignore);
	Barrier->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Barrier->SetGenerateOverlapEvents(false);
	Barrier->SetHiddenInGame(true);
	Barrier->ShapeColor = FColor(180, 0, 60);

	// Not an obstacle for pathfinding either — belt and braces with the channel
	// choice above, so a gate can never distort the navmesh enemies path across.
	Barrier->SetCanEverAffectNavigation(false);
}

void AGothicBleedGate::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGothicBleedGate, bOpen);
}

void AGothicBleedGate::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		int32 Valid = 0;
		for (AGothicEncounterVolume* Enc : GatedEncounters)
		{
			if (!Enc)
			{
				continue;
			}
			++Valid;
			// Bind even to already-rewarded encounters' siblings; RefreshOpenState
			// below decides the outcome. BeginPlay ordering between actors is not
			// guaranteed, so an encounter may already have paid out by now.
			if (!Enc->IsRewarded())
			{
				Enc->OnEncounterRewarded.AddDynamic(
					this, &AGothicBleedGate::HandleEncounterRewarded);
			}
		}

		for (AGothicEncounterVolume* Enc : BreakoutEncounters)
		{
			if (!Enc)
			{
				continue;
			}
			++Valid;
			if (!Enc->IsBrokenOut())
			{
				Enc->OnEncounterBreakout.AddDynamic(
					this, &AGothicBleedGate::HandleEncounterBreakout);
			}
		}

		if (Valid == 0)
		{
			// Fail open, loudly. A silent blocking wall with nothing behind it is
			// an unfinishable level.
			UE_LOG(LogTemp, Warning,
				TEXT("AGothicBleedGate %s: nothing to wait on — staying open. Assign "
				     "GatedEncounters (opens on payout) and/or BreakoutEncounters "
				     "(opens on break-out)."),
				*GetName());
			bOpen = true;
		}
		else
		{
			RefreshOpenState();
		}
	}

	// Clients arrive with bOpen already replicated; running this on every machine
	// makes the initial state correct without waiting for an OnRep that a
	// still-closed gate will never send.
	ApplyOpenState();
}

void AGothicBleedGate::HandleEncounterRewarded(AGothicEncounterVolume* Encounter)
{
	if (!HasAuthority() || bOpen)
	{
		return;
	}
	RefreshOpenState();
}

void AGothicBleedGate::HandleEncounterBreakout(AGothicEncounterVolume* Encounter)
{
	if (!HasAuthority() || bOpen)
	{
		return;
	}
	RefreshOpenState();
}

void AGothicBleedGate::RefreshOpenState()
{
	if (!HasAuthority() || bOpen)
	{
		return;
	}

	for (AGothicEncounterVolume* Enc : GatedEncounters)
	{
		if (Enc && !Enc->IsRewarded())
		{
			return; // still at least one outstanding
		}
	}

	for (AGothicEncounterVolume* Enc : BreakoutEncounters)
	{
		if (Enc && !Enc->IsBrokenOut())
		{
			return;
		}
	}

	bOpen = true;
	UE_LOG(LogTemp, Verbose,
		TEXT("BleedGate[%s]: opening — %d payout + %d break-out condition(s) met"),
		*GetName(), GatedEncounters.Num(), BreakoutEncounters.Num());
	ApplyOpenState(); // the server does not get its own OnRep
}

// Escape hatch. A gate that fails to open is an unfinishable run, and that has
// already happened once during tuning, so this exists to get out of it without
// restarting PIE. Deliberately a console command rather than any automatic
// timeout: a gate that quietly dissolved on its own would hide exactly the bug
// this command lets you walk away from.
static FAutoConsoleCommandWithWorld GVigilOpenBleedGates(
	TEXT("Vigil.OpenBleedGates"),
	TEXT("Force every AGothicBleedGate in the current world open. Dev escape hatch."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (!World)
		{
			return;
		}
		int32 Opened = 0;
		for (TActorIterator<AGothicBleedGate> It(World); It; ++It)
		{
			if (!It->bOpen)
			{
				It->bOpen = true;
				It->ApplyOpenState();
				++Opened;
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("Vigil.OpenBleedGates: forced %d gate(s) open"), Opened);
	}));

void AGothicBleedGate::OnRep_Open()
{
	ApplyOpenState();
}

void AGothicBleedGate::ApplyOpenState()
{
	if (!Barrier)
	{
		return;
	}

	Barrier->SetCollisionEnabled(
		bOpen ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);

	// Visibility tracks collision, so a closed gate is something the player can
	// SEE they cannot pass. Without this the barrier stayed hidden and a gate read
	// as the player simply being stuck on nothing — and the header has always
	// claimed this function applied visibility, which it did not.
	//
	// The box's own wireframe is a PLACEHOLDER: it is legible but it is dev art.
	// The intended look comes through OnBleedGateStateChanged, which needs a
	// Blueprint subclass of this actor to implement — the raw C++ class placed in
	// the level has nowhere to hang the haze, the audio bed or the dissolve.
	Barrier->SetHiddenInGame(bOpen);

	OnBleedGateStateChanged(bOpen);
}
