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

/**
 * Combat diagnostics — vital-point geometry, damage resolution.
 *
 * The project's first named log category. Everything else still goes to LogTemp,
 * and that is the reason this exists: LogTemp defaults to Log, so Verbose combat
 * lines were being dropped silently and read as "the code never fires." A private
 * category can be raised or suppressed without turning the whole LogTemp firehose
 * on.
 *
 * Default runtime verbosity is Verbose deliberately, so these lines appear without
 * anyone having to know a console command first. Compile-time ceiling is All.
 * Quiet it with:  Log LogVigilCombat Warning
 */
DECLARE_LOG_CATEGORY_EXTERN(LogVigilCombat, Verbose, All);

