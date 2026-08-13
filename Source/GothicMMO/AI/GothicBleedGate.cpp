// GothicBleedGate.cpp

#include "AI/GothicBleedGate.h"
#include "AI/GothicEncounterVolume.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
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

	// The visible stand-in. Parented to Barrier so it inherits the wall's placement
	// and the actor's own scale; OnConstruction sizes it to the box span. It carries
	// no collision and no nav footprint — the Barrier box above owns every bit of the
	// blocking, and doubling it here would wall enemies in and distort the navmesh.
	BarrierMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BarrierMesh"));
	BarrierMesh->SetupAttachment(Barrier);
	BarrierMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BarrierMesh->SetCollisionProfileName(TEXT("NoCollision"));
	BarrierMesh->SetGenerateOverlapEvents(false);
	BarrierMesh->SetCanEverAffectNavigation(false);
	BarrierMesh->CastShadow = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		BarrierMesh->SetStaticMesh(CubeMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BarrierMat(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BarrierMat.Succeeded())
	{
		BarrierMesh->SetMaterial(0, BarrierMat.Object);
	}
}

void AGothicBleedGate::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGothicBleedGate, bOpen);
}

void AGothicBleedGate::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Refit the whitebox whenever the gate is placed or its Barrier is re-scaled in
	// the editor, so all three gates' authored spans and yaws show up without a
	// rebuild. Runtime open/close is handled separately in ApplyOpenState.
	SyncBarrierMesh();
}

void AGothicBleedGate::SyncBarrierMesh()
{
	if (!BarrierMesh || !Barrier)
	{
		return;
	}

	// The engine cube spans 100u per side (±50 from its pivot), so one unit of box
	// half-extent is 50u of mesh. Scaling by extent/50 makes the whitebox exactly
	// fill the Barrier's authored span; because the mesh is parented to the box, any
	// actor-level scale the designer applied multiplies both alike and they stay
	// matched.
	const FVector Extent = Barrier->GetUnscaledBoxExtent();
	BarrierMesh->SetRelativeScale3D(Extent / 50.f);

	// Tint the placeholder toward the Bleed's crimson so it reads as a deliberate
	// barrier rather than a stray grey block. BasicShapeMaterial exposes a "Color"
	// vector param; if that name ever drifts the cube simply stays untinted and is
	// still a visible wall.
	if (UMaterialInstanceDynamic* MID = BarrierMesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.12f, 0.01f, 0.03f));
	}

	BarrierMesh->SetVisibility(bShowDefaultBarrierMesh && !bOpen);
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
	if (Barrier)
	{
		Barrier->SetCollisionEnabled(
			bOpen ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);

		// The box itself stays hidden in game (its wireframe is editor-only dev art);
		// what the player actually sees is BarrierMesh below.
		Barrier->SetHiddenInGame(bOpen);
	}

	// Visibility tracks state, so a closed gate is something the player can SEE they
	// cannot pass — the fix for the invisible-wall confusion. This is the whitebox
	// stand-in; the intended look still comes through OnBleedGateStateChanged, which
	// a Blueprint subclass implements (and which turns bShowDefaultBarrierMesh off so
	// its own art replaces the placeholder).
	if (BarrierMesh)
	{
		BarrierMesh->SetVisibility(bShowDefaultBarrierMesh && !bOpen);
	}

	OnBleedGateStateChanged(bOpen);
}
