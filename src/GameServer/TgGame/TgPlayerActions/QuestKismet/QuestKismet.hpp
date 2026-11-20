#pragma once

#include <string>

namespace TgPlayerActions::QuestKismetCmd {

// Fire output link `output_link` of every TgSeqEvent_QuestUpdated in the map's
// Kismet whose QuestId is `quest_id` (0 = accepted, 2 = turned in). The control
// server sends this after it has accepted or completed the quest, so a refused
// turn-in plays nothing.
//
// MUST be called on the game thread (IpcClient::DrainInbound runs from
// Actor::Tick, which satisfies this).
void Execute(const std::string& session_guid, int quest_id, int output_link);

} // namespace TgPlayerActions::QuestKismetCmd
