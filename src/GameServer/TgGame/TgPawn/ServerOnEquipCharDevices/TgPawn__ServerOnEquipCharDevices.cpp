#include "src/GameServer/TgGame/TgPawn/ServerOnEquipCharDevices/TgPawn__ServerOnEquipCharDevices.hpp"
#include "src/GameServer/Tutorial/TutorialGear.hpp"
#include "src/Utils/Logger/Logger.hpp"

// The native gave no tutorial gear on this server; same body as the kismet path.
void __fastcall TgPawn__ServerOnEquipCharDevices::Call(ATgPawn* Pawn, void* edx, UTgSeqAct_EquipCharDevices* Action) {
	LogCallBegin();
	// CallOriginal(Pawn, edx, Action);
	TutorialGear::Apply(Pawn, Action);
	LogCallEnd();
}
