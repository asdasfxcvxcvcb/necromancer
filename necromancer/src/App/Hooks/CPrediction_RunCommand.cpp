#include "../../SDK/SDK.h"

#include "../Features/CFG.h"
#include "../Features/EnginePrediction/EnginePrediction.h"

MAKE_HOOK(CPrediction_RunCommand, Memory::GetVFunc(I::Prediction, 17), void, __fastcall,
	CPrediction* ecx, C_BasePlayer* player, CUserCmd* pCmd, IMoveHelper* moveHelper)
{
	if (Shifting::bRecharging)
	{
		if (const auto pLocal = H::Entities->GetLocal())
		{
			if (player == pLocal)
				return;
		}
	}

	// Adjust players for prediction (like Amalgam does)
	F::EnginePrediction->AdjustPlayers(player);
	CALL_ORIGINAL(ecx, player, pCmd, moveHelper);
	F::EnginePrediction->RestorePlayers();

	// NOTE: Animation updates are handled in LocalAnimations (CreateMove)
	// Do NOT call FrameAdvance here - it causes double animation speed
}
