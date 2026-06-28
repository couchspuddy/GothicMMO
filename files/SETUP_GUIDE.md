# GothicMMO — Prototype Setup Guide
**Engine: Unreal Engine 5.3+** | **Architecture: Dedicated Server + GAS**

---

## 1. Project Creation

1. Create a new **C++ project** in UE5 — choose the "Blank" template (not Third Person; we build our own).
2. Name it `GothicMMO`.
3. Copy all `.h` and `.cpp` files from this repo into `Source/GothicMMO/` matching the folder structure.

---

## 2. Enable Required Plugins

Open `GothicMMO.uproject` and add these to the `Plugins` array:

```json
{ "Name": "GameplayAbilities",   "Enabled": true },
{ "Name": "EnhancedInput",       "Enabled": true },
{ "Name": "ReplicationGraph",    "Enabled": true }
```

Or enable via **Edit > Plugins** in the editor.

---

## 3. Gameplay Tags Setup

Create `Config/DefaultGameplayTags.ini` with these required tags:

```ini
[/Script/GameplayTags.GameplayTagsSettings]
+GameplayTagList=(Tag="State.Dead",DevComment="Applied when Health = 0")
+GameplayTagList=(Tag="State.Stunned",DevComment="Blocks most abilities")
+GameplayTagList=(Tag="State.Attacking",DevComment="Active during attack abilities")
+GameplayTagList=(Tag="Input.Ability.LightAttack")
+GameplayTagList=(Tag="Input.Ability.HeavyAttack")
+GameplayTagList=(Tag="Input.Ability.Ability1")
+GameplayTagList=(Tag="Input.Ability.SuperAbility")
+GameplayTagList=(Tag="Event.Montage.HitWindow.Open",DevComment="Fired by anim notify at hit frame")
+GameplayTagList=(Tag="Data.Damage",DevComment="SetByCaller tag for damage magnitude")
+GameplayTagList=(Tag="Cooldown.LightAttack")
+GameplayTagList=(Tag="Cooldown.HeavyAttack")
```

---

## 4. Blueprint Classes to Create

After compiling, create these Blueprint children in the Content Browser:

| Blueprint Name              | Parent Class                  | Location                  |
|-----------------------------|-------------------------------|---------------------------|
| BP_GothicPlayerCharacter    | AGothicPlayerCharacter        | Content/Character/        |
| BP_GothicPlayerState        | AGothicPlayerState            | Content/Game/             |
| BP_GothicGameMode           | AGothicGameMode               | Content/Game/             |
| BP_GothicEnemyAIController  | AGothicEnemyAIController      | Content/AI/               |
| BP_Enemy_Draugr             | AGothicEnemyBase              | Content/Enemies/          |
| BP_GA_HuntersStrike         | UGA_HuntersStrike             | Content/Abilities/        |

---

## 5. GameplayEffect Assets to Create

In Content Browser, **right-click > Gameplay > GameplayEffect** and create:

### GE_InitStats_Player
- **Duration Policy:** Instant
- **Modifiers:**
  - `MaxHealth`  → Override → 200
  - `Health`     → Override → 200
  - `MaxStamina` → Override → 100
  - `Stamina`    → Override → 100
  - `MaxEther`   → Override → 80
  - `Ether`      → Override → 80
  - `AttackPower`→ Override → 15
  - `Defense`    → Override → 8

### GE_InitStats_Draugr
- **Duration Policy:** Instant
- **Modifiers:**
  - `MaxHealth`  → Override → 80
  - `Health`     → Override → 80
  - `AttackPower`→ Override → 10
  - `Defense`    → Override → 3

### GE_MeleeDamage
- **Duration Policy:** Instant
- **Modifiers:**
  - `IncomingDamage` → Additive → **SetByCaller** (Tag: `Data.Damage`)
  - SetByCaller base value: 20 (this is multiplied by `DamageMultiplier` in the ability)

### GE_StaminaRegen
- **Duration Policy:** Infinite
- **Period:** 0.1 (runs 10x/sec)
- **Modifiers:**
  - `Stamina` → Additive → 2.0
- **Application Requirement:** Add tag requirement: `NOT State.Dead`

---

## 6. BP_GothicPlayerCharacter Setup

Open the Blueprint and configure:

- **Mesh:** Assign your character skeletal mesh
- **Default Attribute Effect:** `GE_InitStats_Player`
- **Startup Abilities:** Add `BP_GA_HuntersStrike`
- **Input Mapping Context:** Create `IMC_Default` with these actions:

| Input Action    | Key Binding        |
|-----------------|--------------------|
| IA_Move         | WASD               |
| IA_Look         | Mouse XY           |
| IA_Jump         | Space              |
| IA_LightAttack  | Left Mouse Button  |
| IA_HeavyAttack  | Right Mouse Button |
| IA_Ability1     | Q                  |
| IA_SuperAbility | R                  |
| IA_Dodge        | Left Shift         |

---

## 7. BP_GA_HuntersStrike Setup

Open the Blueprint and configure:

- **Montage To Play:** Your melee attack AnimMontage
  - In the montage, add an **AnimNotify** at the hit frame
  - Set the notify to `AnimNotify_GameplayEvent`
  - Tag: `Event.Montage.HitWindow.Open`
- **Damage Effect Class:** `GE_MeleeDamage`
- **Hit Sphere Radius:** 80
- **Hit Range:** 200
- **Damage Multiplier:** 1.0
- **Cooldown Effect:** Create `GE_Cooldown_LightAttack` (Duration: Infinite for 0.5s with tag `Cooldown.LightAttack`)

---

## 8. BP_Enemy_Draugr Setup

- **Default Attribute Effect:** `GE_InitStats_Draugr`
- **AI Controller Class:** `BP_GothicEnemyAIController`
- **Enemy Tier:** Minion
- **Experience Reward:** 25
- **Mesh:** Your undead/draugr skeletal mesh

---

## 9. Behavior Tree (BT_EnemyCombat)

Create the BT in Content/AI/ with this structure:

```
Root
└── Selector
    ├── Sequence [IsInCombat == true]  ← Decorator: Blackboard bIsInCombat
    │   ├── BTTask_MoveToTarget        ← Move to TargetActor with AcceptanceRadius=200
    │   └── BTTask_ActivateAbility     ← Custom task: calls TryActivateAbilityBySlot(LightAttack)
    └── BTTask_Patrol                  ← Move to random point near PatrolOrigin (radius 500)
```

Create `BB_Enemy` Blackboard with keys matching `GothicBBKeys` namespace in the AI Controller.

---

## 10. World Settings / Game Mode

In your level's **World Settings:**
- **GameMode Override:** `BP_GothicGameMode`

In `BP_GothicGameMode`:
- **Default Pawn Class:** `BP_GothicPlayerCharacter`
- **Player State Class:** `BP_GothicPlayerState`
- **Respawn Delay:** 10.0

---

## 11. Testing Multiplayer in Editor

1. Set **Play > Number of Players** to 2
2. Set **Net Mode** to **Play as Listen Server**
3. Hit Play — Player 1 is the server, Player 2 connects via the same process
4. Both players share the same world; the server is authoritative for all GAS

For dedicated server testing:
```
UnrealEditor.exe GothicMMO -server -game -log
UnrealEditor.exe GothicMMO 127.0.0.1 -game
```

---

## Architecture Summary

```
┌─────────────────────────────────────────┐
│             DEDICATED SERVER            │
│  AGothicGameMode (authority)            │
│  AGothicPlayerState ──► ASC + Attrs     │
│  AGothicCharacterBase ──► Combat IFace  │
│  AGothicEnemyBase ──► ASC + Attrs       │
│  AGothicEnemyAIController ──► BT        │
└─────────────────────────────────────────┘
           │ Replication │
┌──────────▼─────────────▼────────────────┐
│           CLIENT (each player)          │
│  Mirror of PlayerState attrs            │
│  Local input → GAS ability activation  │
│  Predicted movement (client-side)       │
│  GameplayCues for VFX/SFX (cosmetic)    │
└─────────────────────────────────────────┘
```

---

## Next Steps (Design Prompts to Give Me)

The prototype is ready for you to hand me:
- **Covenant classes** (e.g., Van Helsing Hunter, Vampire Covenant, Witch Covenant)
- **Enemy types** (Vampire Thrall, Wraith, Frankenstein's Monster as a boss)
- **Ability designs** (silver bullet, stake throw, blood drain)
- **Zone/world structure** (gothic village hub, castle dungeon instance)
- **UI/HUD layout** (health, Ether meter, ability slots)
- **Loot system design**
- **Progression system** (level curve, gear tiers)
