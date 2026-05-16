#include "../../SDK/SDK.h"

#include "../Features/TickbaseManip/TickbaseManip.h"

MAKE_HOOK(CPrediction_RunCommand, Memory::GetVFunc(I::Prediction, 17), void, __fastcall,
	CPrediction* ecx, C_BasePlayer* player, CUserCmd* pCmd, IMoveHelper* moveHelper)
{
	// NOTE: AdjustPlayers/RestorePlayers are called in CPrediction_RunSimulation,
	// which wraps the entire prediction cycle including RunCommand.
	// Calling them here too causes double-adjustment which breaks ESP scaling.
	CALL_ORIGINAL(ecx, player, pCmd, moveHelper);

	// NOTE: Animation updates are handled in LocalAnimations (CreateMove)
	// Do NOT call FrameAdvance here - it causes double animation speed
}
