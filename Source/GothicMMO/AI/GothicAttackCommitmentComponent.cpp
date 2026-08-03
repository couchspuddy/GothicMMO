// GothicAttackCommitmentComponent.cpp

#include "AI/GothicAttackCommitmentComponent.h"
#include "AIController.h"

UGothicAttackCommitmentComponent::UGothicAttackCommitmentComponent()
{
    // Pure state. It is polled by a service that already ticks at 5Hz; giving
    // it a tick of its own would be a second clock to keep in step with the
    // first for no gain.
    PrimaryComponentTick.bCanEverTick = false;
    bWantsInitializeComponent = false;
}

UGothicAttackCommitmentComponent* UGothicAttackCommitmentComponent::FindOrAdd(AAIController* Controller)
{
    if (!Controller)
    {
        return nullptr;
    }

    if (UGothicAttackCommitmentComponent* Existing =
            Controller->FindComponentByClass<UGothicAttackCommitmentComponent>())
    {
        return Existing;
    }

    // Created on the controller rather than the pawn: a chain is a decision,
    // and decisions belong to the thing making them. It also means a pawn
    // swapped onto a different controller mid-fight cannot inherit a commitment
    // that was reasoned about by someone else.
    UGothicAttackCommitmentComponent* Component =
        NewObject<UGothicAttackCommitmentComponent>(Controller);
    Component->RegisterComponent();
    return Component;
}
