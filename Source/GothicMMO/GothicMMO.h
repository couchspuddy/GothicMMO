// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
/**
 * Weapon trace channel — see Project Settings > Engine > Collision.
 *
 * Enemy skeletal meshes block this; capsules ignore it (the channel's default
 * response is Ignore, so nothing opts in accidentally). Traces on ECC_Pawn hit
 * the CAPSULE and stop — Hit.ImpactPoint then sits on a cylinder surface tens of
 * cm from any bone, which is why vital-point detection read as random and why
 * the Bestial Lucid took no hitscan damage until her capsule was resized.
 *
 * Anything doing precise hit location work traces this. Melee stays on ECC_Pawn
 * deliberately — a generous capsule hitbox is correct for melee.
 */
#define ECC_Weapon ECollisionChannel::ECC_GameTraceChannel1

