#include "src/GameServer/Tutorial/TutorialGear.hpp"

#include "src/Database/Database.hpp"
#include "src/GameServer/Combat/MissionAlerts/SendAlert.hpp"
#include "src/GameServer/Inventory/Inventory.hpp"
#include "src/GameServer/Storage/ClientConnectionsData/ClientConnectionsData.hpp"
#include "src/GameServer/Storage/PawnSessions/PawnSessions.hpp"
#include "src/GameServer/Storage/PlayerRegistry/PlayerRegistry.hpp"
#include "src/GameServer/TgGame/TgPawn/KillDeployables/TgPawn__KillDeployables.hpp"
#include "src/GameServer/Utils/ObjectClassCache/ObjectClassCache.hpp"
#include "src/IpcClient/IpcClient.hpp"
#include "src/Shared/IpcProtocol.hpp"
#include "src/Utils/Logger/Logger.hpp"
#include "lib/nlohmann/json.hpp"
#include "lib/sqlite3/sqlite3.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <vector>

namespace TutorialGear {

namespace {

// Gameplay device slots the strip touches. Suit (6), helmet (12) and the class
// device (14) stay: they carry the character's look and base attributes.
const int kStripSlots[] = {1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 13};

// Same "no quality" value the beacon pickup path uses.
constexpr int kTutorialQuality = 1162;

struct Given {
	int deviceId = 0;
	int invId    = 0;
};

struct Record {
	bool active   = false;  // saved loadout swapped out on the pawn
	bool released = false;  // -restoregear / profile switch: stop swapping
	std::map<int, Given> devices;  // equip point -> tutorial device
	// Restored with no live pawn: the next spawn re-sends the inventory view.
	bool pendingSync = false;
	std::vector<int> pendingRemoved;
};

std::map<std::string, Record> g_records;  // session guid -> state

bool IsStripSlot(int slot) {
	for (int s : kStripSlots) if (s == slot) return true;
	return false;
}

bool IsBeacon(ATgDevice* d) {
	using IsBeaconFn = int(__fastcall*)(ATgDevice*, void*);
	return ((IsBeaconFn)0x10a19a40)(d, nullptr) != 0;
}

bool InList(const std::vector<int>& ids, int id) {
	return std::find(ids.begin(), ids.end(), id) != ids.end();
}

std::string GuidFor(ATgPawn* Pawn) {
	auto it = GPawnSessions.find(Pawn);
	return it == GPawnSessions.end() ? std::string() : it->second;
}

ATgPawn* PawnFor(const std::string& guid) {
	for (auto& kv : GPawnSessions) {
		if (kv.second == guid && kv.first && kv.first->Health > 0) return kv.first;
	}
	return nullptr;
}

void Alert(ATgPawn* Pawn, const char* msg) {
	if (!Pawn || !Pawn->Controller) return;
	if (!ObjectClassCache::ClassNameContains(Pawn->Controller, "PlayerController")) return;
	APlayerController* PC = (APlayerController*)Pawn->Controller;
	if (!PC->Player) return;
	const std::wstring wmsg(msg, msg + std::strlen(msg));
	SendAlert::SendTextAlert((UNetConnection*)PC->Player, wmsg.c_str(), 2, 0, 6.0f);
}

// asm_data_set_devices.slot_used_value_id (device category) -> equip point.
int EquipPointForDevice(int deviceId, const Record& r) {
	sqlite3* db = Database::GetConnection();
	if (!db) return 0;
	int category = 0;
	sqlite3_stmt* st = nullptr;
	if (sqlite3_prepare_v2(db,
	        "SELECT slot_used_value_id FROM asm_data_set_devices WHERE device_id = ?",
	        -1, &st, nullptr) == SQLITE_OK && st) {
		sqlite3_bind_int(st, 1, deviceId);
		if (sqlite3_step(st) == SQLITE_ROW) category = sqlite3_column_int(st, 0);
		sqlite3_finalize(st);
	}
	switch (category) {
		case 389:  return 1;   // Melee Weapon
		case 388:  return 2;   // Ranged Weapon
		case 806:  return 5;   // Jetpack
		case 394:  return 6;   // Suit
		case 476:  return 10;  // Morale Device
		case 741:  return 12;  // Helmet
		case 1482: return 13;  // Consumable
		case 390:              // Off-Hand
		case 981: {            // Specialty Device
			for (int s : {3, 7, 8, 9}) {
				auto it = r.devices.find(s);
				if (it == r.devices.end() || it->second.deviceId == deviceId) return s;
			}
			return 0;
		}
		default: return 0;
	}
}

// What the client's inventory map holds once the control server re-ships:
// the account pool + components (as at spawn) + the tutorial devices.
void StampItemCount(ATgPawn* Pawn, const std::string& guid, const Record& r) {
	if (!Pawn || !Pawn->InvManager) return;
	PlayerInfo* info = PlayerRegistry::GetByGuidPtr(guid);
	sqlite3* db = Database::GetConnection();
	if (!info || !db) return;
	int pool = -1;
	sqlite3_stmt* st = nullptr;
	if (sqlite3_prepare_v2(db,
	        "SELECT (SELECT COUNT(*) FROM ga_players_inventory "
	        "        WHERE user_id = c.user_id AND (profile_id = 0 OR profile_id = c.profile_id)) "
	        "     + (SELECT COUNT(*) FROM ga_user_components "
	        "        WHERE user_id = c.user_id AND quantity > 0) "
	        "FROM ga_characters c WHERE c.id = ?",
	        -1, &st, nullptr) == SQLITE_OK && st) {
		sqlite3_bind_int64(st, 1, info->selected_character_id);
		if (sqlite3_step(st) == SQLITE_ROW) pool = sqlite3_column_int(st, 0);
		sqlite3_finalize(st);
	}
	if (pool < 0) return;
	const int total = pool + (r.active ? (int)r.devices.size() : 0);
	ATgInventoryManager* Mgr = (ATgInventoryManager*)Pawn->InvManager;
	Mgr->r_ItemCount     = total;
	Mgr->bNetDirty       = 1;
	Mgr->bForceNetUpdate = 1;
	Logger::Log("loot", "[Loot] stamp tutorial-gear: char=%lld r_ItemCount=%d (tutorial=%d)\n",
		(long long)info->selected_character_id, total, r.active ? (int)r.devices.size() : 0);
}

// The control server re-ships SEND_INVENTORY from this: saved devices in the
// strip slots go out as bag items, the tutorial devices as equipped.
void SendState(ATgPawn* Pawn, const std::string& guid, const Record& r,
               const std::vector<int>& removedInvIds) {
	nlohmann::json devices = nlohmann::json::array();
	if (r.active) {
		for (const auto& kv : r.devices) {
			devices.push_back({{"slot", kv.first},
			                   {"device_id", kv.second.deviceId},
			                   {"inventory_id", kv.second.invId}});
		}
	}
	nlohmann::json ev;
	ev["type"]                  = IpcProtocol::MSG_GAME_EVENT;
	ev["subtype"]               = "tutorial_gear";
	ev["instance_id"]           = IpcClient::GetInstanceId();
	ev["session_guid"]          = guid;
	ev["pawn_id"]               = Pawn ? (int)Pawn->r_nPawnId : 0;
	ev["active"]                = r.active;
	ev["devices"]               = std::move(devices);
	ev["strip_slots"]           = r.active ? nlohmann::json(std::vector<int>(std::begin(kStripSlots), std::end(kStripSlots)))
	                                       : nlohmann::json::array();
	ev["removed_inventory_ids"] = removedInvIds;
	IpcClient::Send(ev.dump());
}

// Re-equip the saved loadout (active profile) into the given slots.
int EquipSavedLoadout(ATgPawn* Pawn, const std::string& guid, const std::set<int>& slots) {
	PlayerInfo* info = PlayerRegistry::GetByGuidPtr(guid);
	sqlite3* db = Database::GetConnection();
	if (!info || !db || !Pawn) return 0;
	int profile = 1;
	if (ObjectClassCache::ClassNameContains(Pawn, "TgPawn_Character")) {
		profile = ((ATgPawn_Character*)Pawn)->r_nItemProfileId;
		if (profile < 1 || profile > 5) profile = 1;
	}
	int equipped = 0;
	sqlite3_stmt* st = nullptr;
	if (sqlite3_prepare_v2(db,
	        "SELECT cd.equipped_slot, pi.device_id, pi.quality, pi.id, pi.mod_effect_group_ids "
	        "FROM ga_character_devices cd "
	        "JOIN ga_players_inventory pi ON pi.id = cd.inventory_id "
	        "WHERE cd.character_id = ? AND cd.item_profile_id = ? AND pi.device_id > 0 "
	        "GROUP BY cd.equipped_slot",
	        -1, &st, nullptr) != SQLITE_OK || !st) {
		return 0;
	}
	sqlite3_bind_int64(st, 1, info->selected_character_id);
	sqlite3_bind_int  (st, 2, profile);
	while (sqlite3_step(st) == SQLITE_ROW) {
		const int slot    = sqlite3_column_int(st, 0);
		const int devId   = sqlite3_column_int(st, 1);
		const int quality = sqlite3_column_int(st, 2);
		const int invId   = sqlite3_column_int(st, 3);
		if (!slots.count(slot)) continue;
		std::vector<int> mods;
		const char* csv = (const char*)sqlite3_column_text(st, 4);
		for (const char* p = csv; p && *p; ) {
			while (*p == ',' || *p == ' ') ++p;
			if (!*p) break;
			char* end = nullptr;
			const long v = std::strtol(p, &end, 10);
			if (end == p) break;
			mods.push_back((int)v);
			p = end;
		}
		ATgDevice* cur = Pawn->m_EquippedDevices[slot];
		if (cur && cur->r_nDeviceInstanceId == invId) continue;
		if (cur) {
			if (IsBeacon(cur)) continue;
			TgPawn__KillDeployables::KillFromDevice(Pawn, cur);
			Inventory::Unequip(Pawn, slot);
		}
		if (Inventory::Equip(Pawn, devId, slot, quality, invId, mods) != nullptr) ++equipped;
	}
	sqlite3_finalize(st);
	return equipped;
}

void CollectPawns(UTgSeqAct_EquipCharDevices* Act, std::vector<ATgPawn*>& out) {
	auto add = [&out](ATgPawn* p) {
		if (p && p->Health > 0 && std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
	};
	for (int v = 0; v < Act->VariableLinks.Num(); v++) {
		const FSeqVarLink& vl = Act->VariableLinks.Data[v];
		for (int k = 0; k < vl.LinkedVariables.Num(); k++) {
			USequenceVariable* var = vl.LinkedVariables.Data[k];
			if (!var) continue;
			if (ObjectClassCache::ClassNameContains(var, "SeqVar_Player")) {
				int taskForce = -1;
				if (ObjectClassCache::ClassNameContains(var, "TgSeqVar_Player")) {
					taskForce = ((UTgSeqVar_Player*)var)->nTaskForceNumber;
				}
				for (auto& kv : GPawnSessions) {
					ATgPawn* p = kv.first;
					if (!p) continue;
					if (taskForce > 0) {
						ATgRepInfo_Player* pri = (ATgRepInfo_Player*)p->PlayerReplicationInfo;
						if (!pri || !pri->r_TaskForce || pri->r_TaskForce->r_nTaskForce != taskForce) continue;
					}
					add(p);
				}
				continue;
			}
			if (!ObjectClassCache::ClassNameContains(var, "SeqVar_Object")) continue;
			UObject* held = ((USeqVar_Object*)var)->ObjValue;
			if (!held) continue;
			if (ObjectClassCache::ClassNameContains(held, "TgPawn")) {
				add((ATgPawn*)held);
			} else if (ObjectClassCache::ClassNameContains(held, "Controller")) {
				add((ATgPawn*)((AController*)held)->Pawn);
			}
		}
	}
}

} // namespace

void OnEquipCharDevicesActivated(UTgSeqAct_EquipCharDevices* Action) {
	if (!Action) return;
	std::vector<ATgPawn*> pawns;
	CollectPawns(Action, pawns);
	Logger::Log("tutorial", "EquipCharDevices activated: %d pawn(s)\n", (int)pawns.size());
	for (ATgPawn* p : pawns) Apply(p, Action);
}

void Apply(ATgPawn* Pawn, UTgSeqAct_EquipCharDevices* Action) {
	if (!Pawn || !Action) return;
	const std::string guid = GuidFor(Pawn);
	if (guid.empty()) {
		Logger::Log("tutorial", "EquipCharDevices: pawn=%p has no session; skipped\n", (void*)Pawn);
		return;
	}
	Record& r = g_records[guid];
	if (r.released) {
		Logger::Log("tutorial", "EquipCharDevices: guid=%s restored own gear; skipped\n", guid.c_str());
		return;
	}

	std::vector<int> ids;
	for (int i = 0; i < Action->m_DeviceIdList.Num(); i++) ids.push_back(Action->m_DeviceIdList.Data[i]);

	const bool wasActive = r.active;
	std::vector<int> removed;
	int unequipped = 0;
	int equipped = 0;

	if (Action->m_bRemoveOtherDevices) {
		for (int slot : kStripSlots) {
			auto rit = r.devices.find(slot);
			if (rit != r.devices.end() && !InList(ids, rit->second.deviceId)) {
				removed.push_back(rit->second.invId);
				r.devices.erase(rit);
			}
			ATgDevice* d = Pawn->m_EquippedDevices[slot];
			if (!d || IsBeacon(d) || InList(ids, d->r_nDeviceId)) continue;
			TgPawn__KillDeployables::KillFromDevice(Pawn, d);
			Inventory::Unequip(Pawn, slot);
			++unequipped;
		}
		r.active = true;
	}

	for (int id : ids) {
		const int slot = EquipPointForDevice(id, r);
		if (slot == 0) {
			Logger::Log("tutorial", "EquipCharDevices: device=%d has no player equip point; skipped\n", id);
			continue;
		}
		auto rit = r.devices.find(slot);
		int invId = 0;
		if (rit != r.devices.end() && rit->second.deviceId == id) {
			invId = rit->second.invId;
		} else {
			if (rit != r.devices.end()) removed.push_back(rit->second.invId);
			invId = Inventory::NextId();
		}
		r.devices[slot] = Given{id, invId};
		r.active = true;

		ATgDevice* cur = Pawn->m_EquippedDevices[slot];
		if (cur && cur->r_nDeviceInstanceId == invId) continue;
		if (cur) {
			TgPawn__KillDeployables::KillFromDevice(Pawn, cur);
			Inventory::Unequip(Pawn, slot);
			++unequipped;
		}
		if (Inventory::Equip(Pawn, id, slot, kTutorialQuality, invId, {}) != nullptr) ++equipped;
	}

	if (unequipped > 0 || equipped > 0) Inventory::Finalize(Pawn);
	Logger::Log("tutorial",
		"EquipCharDevices: guid=%s removeOthers=%d devices=%d unequipped=%d equipped=%d tutorial-slots=%d\n",
		guid.c_str(), (int)Action->m_bRemoveOtherDevices, (int)ids.size(), unequipped, equipped,
		(int)r.devices.size());
	if (!r.active) return;

	StampItemCount(Pawn, guid, r);
	SendState(Pawn, guid, r, removed);
	if (!wasActive) {
		Alert(Pawn, "Tutorial gear equipped. Your own gear comes back when you leave the tutorial, or type -restoregear.");
	}
}

void OnPlayerSpawned(ATgPawn* Pawn, const std::string& sessionGuid) {
	auto it = g_records.find(sessionGuid);
	if (!Pawn || it == g_records.end()) return;
	Record& r = it->second;
	if (r.pendingSync) {
		StampItemCount(Pawn, sessionGuid, r);
		SendState(Pawn, sessionGuid, r, r.pendingRemoved);
		r.pendingRemoved.clear();
		r.pendingSync = false;
	}
	if (!r.active || r.released) return;

	int unequipped = 0;
	int equipped = 0;
	for (int slot : kStripSlots) {
		ATgDevice* d = Pawn->m_EquippedDevices[slot];
		if (!d || IsBeacon(d)) continue;
		auto rit = r.devices.find(slot);
		if (rit != r.devices.end() && d->r_nDeviceInstanceId == rit->second.invId) continue;
		Inventory::Unequip(Pawn, slot);
		++unequipped;
	}
	for (const auto& kv : r.devices) {
		ATgDevice* cur = Pawn->m_EquippedDevices[kv.first];
		if (cur && cur->r_nDeviceInstanceId == kv.second.invId) continue;
		if (cur) {
			Inventory::Unequip(Pawn, kv.first);
			++unequipped;
		}
		if (Inventory::Equip(Pawn, kv.second.deviceId, kv.first, kTutorialQuality, kv.second.invId, {}) != nullptr) {
			++equipped;
		}
	}
	if (unequipped > 0 || equipped > 0) Inventory::Finalize(Pawn);
	StampItemCount(Pawn, sessionGuid, r);
	SendState(Pawn, sessionGuid, r, {});
	Logger::Log("tutorial", "respawn: guid=%s tutorial gear back on (unequipped=%d equipped=%d)\n",
		sessionGuid.c_str(), unequipped, equipped);
}

bool IsActive(const std::string& sessionGuid) {
	auto it = g_records.find(sessionGuid);
	return it != g_records.end() && it->second.active && !it->second.released;
}

void RestoreFromCommand(const std::string& sessionGuid) {
	Record& r = g_records[sessionGuid];
	ATgPawn* Pawn = PawnFor(sessionGuid);
	const bool wasActive = r.active && !r.released;
	r.released = true;
	if (!wasActive) {
		Logger::Log("tutorial", "restoregear: guid=%s nothing swapped; tutorial gear now off\n",
			sessionGuid.c_str());
		Alert(Pawn, "You already have your own gear. Tutorial gear will not replace it on this map.");
		return;
	}

	std::vector<int> removed;
	std::set<int> slots(std::begin(kStripSlots), std::end(kStripSlots));
	for (const auto& kv : r.devices) {
		removed.push_back(kv.second.invId);
		slots.insert(kv.first);
	}

	int unequipped = 0;
	int equipped = 0;
	if (Pawn) {
		for (int slot : slots) {
			ATgDevice* d = Pawn->m_EquippedDevices[slot];
			if (!d || IsBeacon(d)) continue;
			if (std::find(removed.begin(), removed.end(), d->r_nDeviceInstanceId) == removed.end()) continue;
			TgPawn__KillDeployables::KillFromDevice(Pawn, d);
			Inventory::Unequip(Pawn, slot);
			++unequipped;
		}
		equipped = EquipSavedLoadout(Pawn, sessionGuid, slots);
		if (unequipped > 0 || equipped > 0) Inventory::Finalize(Pawn);
	}

	r.devices.clear();
	r.active = false;
	if (Pawn) {
		StampItemCount(Pawn, sessionGuid, r);
		SendState(Pawn, sessionGuid, r, removed);
	} else {
		SendState(nullptr, sessionGuid, r, {});
		r.pendingSync = true;
		r.pendingRemoved = removed;
	}
	Alert(Pawn, "Your own gear is back. Tutorial gear will not replace it on this map.");
	Logger::Log("tutorial", "restoregear: guid=%s pawn=%p removed=%d tutorial device(s), equipped=%d saved device(s)\n",
		sessionGuid.c_str(), (void*)Pawn, unequipped, equipped);
}

void Release(ATgPawn* Pawn, const std::string& sessionGuid) {
	auto it = g_records.find(sessionGuid);
	if (it == g_records.end() || !it->second.active || it->second.released) return;
	Record& r = it->second;
	std::vector<int> removed;
	for (const auto& kv : r.devices) removed.push_back(kv.second.invId);
	r.devices.clear();
	r.active = false;
	r.released = true;
	SendState(Pawn, sessionGuid, r, removed);
	Logger::Log("tutorial", "profile switch: guid=%s tutorial gear released (%d device(s))\n",
		sessionGuid.c_str(), (int)removed.size());
}

void RefuseEquipSave(ATgPawn* Pawn, const std::string& sessionGuid) {
	auto it = g_records.find(sessionGuid);
	if (it == g_records.end()) return;
	StampItemCount(Pawn, sessionGuid, it->second);
	SendState(Pawn, sessionGuid, it->second, {});
	Alert(Pawn, "Loadout changes are not saved during the tutorial. Type -restoregear to use your own gear.");
	Logger::Log("tutorial", "equip-save refused: guid=%s (tutorial gear on)\n", sessionGuid.c_str());
}

} // namespace TutorialGear
