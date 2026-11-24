#include "src/GameServer/Tutorial/TutorialMission.hpp"

#include "src/GameServer/Storage/PawnSessions/PawnSessions.hpp"
#include "src/GameServer/Utils/ObjectClassCache/ObjectClassCache.hpp"
#include "src/IpcClient/IpcClient.hpp"
#include "src/Shared/IpcProtocol.hpp"
#include "src/Utils/Logger/Logger.hpp"
#include "lib/nlohmann/json.hpp"

#include <set>
#include <string>

namespace TutorialMission {

namespace {

// Session guids for the pawns/controllers the action's variable links hold.
void CollectGuids(UTgSeqAct_RequestMission* Act, std::set<std::string>& out) {
	for (int v = 0; v < Act->VariableLinks.Num(); v++) {
		const FSeqVarLink& vl = Act->VariableLinks.Data[v];
		for (int k = 0; k < vl.LinkedVariables.Num(); k++) {
			USequenceVariable* var = vl.LinkedVariables.Data[k];
			if (!var || !ObjectClassCache::ClassNameContains(var, "SeqVar_Object")) continue;
			UObject* held = ((USeqVar_Object*)var)->ObjValue;
			ATgPawn* pawn = nullptr;
			if (held && ObjectClassCache::ClassNameContains(held, "TgPawn")) {
				pawn = (ATgPawn*)held;
			} else if (held && ObjectClassCache::ClassNameContains(held, "Controller")) {
				pawn = (ATgPawn*)((AController*)held)->Pawn;
			}
			if (!pawn) continue;
			auto it = GPawnSessions.find(pawn);
			if (it != GPawnSessions.end() && !it->second.empty()) out.insert(it->second);
		}
	}
}

} // namespace

void OnRequestMissionActivated(UTgSeqAct_RequestMission* Action) {
	if (!Action) return;
	std::set<std::string> guids;
	CollectGuids(Action, guids);
	const bool fromLink = !guids.empty();
	// Nothing resolvable on the link: every player on the instance (the intro
	// missions run one private instance per player).
	if (!fromLink) {
		for (auto& kv : GPawnSessions) {
			if (kv.first && !kv.second.empty()) guids.insert(kv.second);
		}
	}
	for (const std::string& guid : guids) {
		nlohmann::json ev;
		ev["type"]         = IpcProtocol::MSG_GAME_EVENT;
		ev["subtype"]      = "request_mission";
		ev["instance_id"]  = IpcClient::GetInstanceId();
		ev["session_guid"] = guid;
		ev["map_game_id"]  = Action->m_nMapGameId;
		ev["difficulty"]   = (int)Action->m_eDifficultyLevel;
		IpcClient::Send(ev.dump());
	}
	Logger::Log("tutorial", "RequestMission: map_game_id=%d difficulty=%d -> %d player(s)%s\n",
		Action->m_nMapGameId, (int)Action->m_eDifficultyLevel, (int)guids.size(),
		fromLink ? "" : " (all players; Player link empty)");
}

} // namespace TutorialMission
