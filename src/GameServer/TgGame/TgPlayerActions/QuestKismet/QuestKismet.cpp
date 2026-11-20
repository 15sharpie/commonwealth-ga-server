#include "src/GameServer/TgGame/TgPlayerActions/QuestKismet/QuestKismet.hpp"

#include "src/pch.hpp"
#include "src/GameServer/Engine/World/GetWorldInfo/World__GetWorldInfo.hpp"
#include "src/GameServer/Engine/KismetDump/KismetDump.hpp"
#include "src/GameServer/Globals.hpp"
#include "src/GameServer/Utils/ClassPreloader/ClassPreloader.hpp"
#include "src/Utils/Logger/Logger.hpp"

#include <string>

namespace TgPlayerActions::QuestKismetCmd {

// Moved here from CGameClient__MarshalReceived's QUEST_UPDATE_REQUEST branch,
// which used to pick the link from the DLL's own DB before the control server
// had validated anything.
void Execute(const std::string& session_guid, int quest_id, int output_link) {
	if (quest_id <= 0 || output_link < 0) return;

	TArray<USequenceObject*> Events;
	AWorldInfo* WorldInfo = World__GetWorldInfo::CallOriginal((UWorld*)Globals::Get().GWorld, nullptr, 0);
	USequence* GameSequence = WorldInfo ? WorldInfo->GetGameSequence() : nullptr;
	if (GameSequence == nullptr) return;

	GameSequence->FindSeqObjectsByClass(
		ClassPreloader::GetClass("Class TgGame.TgSeqEvent_QuestUpdated"),
		1,
		&Events
	);

	int fired = 0;
	for (int i = 0; i < Events.Num(); i++) {
		UTgSeqEvent_QuestUpdated* Event = (UTgSeqEvent_QuestUpdated*)Events.Data[i];
		if (Event == nullptr || Event->QuestId != quest_id) continue;

		for (int j = 0; j < Event->OutputLinks.Num(); j++) {
			for (int k = 0; k < Event->OutputLinks.Data[j].Links.Num(); k++) {
				USequenceOp* Op = Event->OutputLinks.Data[j].Links.Data[k].LinkedOp;
				if (Op) Logger::Log("quest", "OutputLink [%d] Link [%d] Sequence Op: %s\n", j, k, Op->GetFullName());
			}
		}

		// Re-dump Kismet once after sub-levels have loaded (first quest event fires well after BeginPlay)
		static bool s_bKismetRedumped = false;
		if (!s_bKismetRedumped) {
			s_bKismetRedumped = true;
			DumpKismetSequence();
		}

		if (output_link >= Event->OutputLinks.Num()) {
			Logger::Log("quest", "[QuestKismet] quest=%d has no output link %d (%d links)\n",
				quest_id, output_link, Event->OutputLinks.Num());
			continue;
		}

		Event->ActivateOutputLink(output_link);
		fired++;
		Logger::Log("quest", "[QuestKismet] quest=%d guid=%s: activating output link %d\n",
			quest_id, session_guid.c_str(), output_link);

		// ForceActivateInput on non-latent ops to ensure immediate execution.
		// Latent ops (bLatentExecution = USequenceOp+0x008C bit 0x2) like SeqAct_Delay
		// must NOT be force-activated: their outputs fire asynchronously without an
		// instigator pawn in variable links, causing a null-deref crash downstream.
		auto& olink = Event->OutputLinks.Data[output_link];
		for (int k = 0; k < olink.Links.Num(); k++) {
			USequenceOp* Op = olink.Links.Data[k].LinkedOp;
			if (!Op) continue;
			bool bLatent = (*(uint32_t*)((char*)Op + 0x008C) & 0x2) != 0;
			// SeqAct_Interp is latent (derives from USeqAct_Latent) but safe to
			// force-activate — it just starts a matinee and doesn't fire async
			// outputs that need an instigator pawn in variable links.
			// IsA is unreliable on this build — match by class-name string instead.
			bool bIsInterp = false;
			if (Op->Class) {
				const char* ocRaw = Op->Class->GetFullName();
				if (ocRaw) {
					const std::string ocName = ocRaw;
					bIsInterp = ocName.find("SeqAct_Interp") != std::string::npos;
				}
			}
			if (bLatent && !bIsInterp) {
				Logger::Log("quest", "Skipping latent op %s\n", Op->GetFullName());
				continue;
			}
			int inputIdx = olink.Links.Data[k].InputLinkIdx;
			Logger::Log("quest", "ForceActivateInput(%d) on %s\n", inputIdx, Op->GetFullName());
			Op->ForceActivateInput(inputIdx);
		}
	}

	if (fired == 0) {
		Logger::Log("quest", "[QuestKismet] quest=%d guid=%s link=%d: no TgSeqEvent_QuestUpdated for this quest in this map\n",
			quest_id, session_guid.c_str(), output_link);
	}
}

} // namespace TgPlayerActions::QuestKismetCmd
