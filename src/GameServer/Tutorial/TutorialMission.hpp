#pragma once

#include "src/pch.hpp"

// TgSeqAct_RequestMission: the intro missions end by requesting the next one
// (m_nMapGameId). The control server owns travel, so each player the action's
// "Player" link names is handed over as a "request_mission" game event.
namespace TutorialMission {

void OnRequestMissionActivated(UTgSeqAct_RequestMission* Action);

} // namespace TutorialMission
