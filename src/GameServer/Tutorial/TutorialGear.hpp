#pragma once

#include "src/pch.hpp"
#include <string>

// TgSeqAct_EquipCharDevices: the tutorial maps strip the player's devices and
// hand out tutorial ones (jetpack, melee, ...). The stripped native left that
// doing nothing. The swap is engine-side only: ga_character_devices is never
// written, so the saved loadout comes back on the next map, on -restoregear,
// or on a loadout-profile switch.
namespace TutorialGear {

// SeqOpActivated: apply the action to every pawn its "Pawn" variable link names.
void OnEquipCharDevicesActivated(UTgSeqAct_EquipCharDevices* Action);

// One pawn. Also the body of the TgPawn.ServerOnEquipCharDevices native.
void Apply(ATgPawn* Pawn, UTgSeqAct_EquipCharDevices* Action);

// Respawn on the same map: put the tutorial devices back on the new pawn.
void OnPlayerSpawned(ATgPawn* Pawn, const std::string& sessionGuid);

// True while this player's saved loadout is swapped out for tutorial devices.
bool IsActive(const std::string& sessionGuid);

// -restoregear: re-equip the saved loadout and stop applying tutorial devices.
void RestoreFromCommand(const std::string& sessionGuid);

// Profile switch: the caller re-equips the new profile itself; this only drops
// the tutorial state and tells the control server.
void Release(ATgPawn* Pawn, const std::string& sessionGuid);

// The equip screen's Apply arrived while the tutorial loadout is on: nothing
// is saved; the client's inventory view is re-sent so it matches the pawn.
void RefuseEquipSave(ATgPawn* Pawn, const std::string& sessionGuid);

} // namespace TutorialGear
