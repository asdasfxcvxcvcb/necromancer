#include "../../SDK/SDK.h"
#include "../Features/EnginePrediction/EnginePrediction.h"
#include "../Features/TickbaseManip/TickbaseManip.h"

MAKE_SIGNATURE(CPrediction_RunSimulation, "client.dll", "48 83 EC 38 4C 8B 44", 0x0);

// Amalgam's tickbase fix for doubletap shifts (CPrediction_RunSimulation):
// the fix queues on the batch's SECOND command (their m_iShiftedTicks + 1 ==
// m_iShiftStart) and subtracts the shift from that command's predicted
// tickbase on every replay until the server acks it. The fire command itself
// predicts unretimed - Amalgam's does too, since their m_bShifting isn't set
// during its prediction - and the ack snapshot brings the weapon cooldown
// back to the server's backdated value afterwards
struct TickbaseFix_t
{
	CUserCmd* m_pCmd;
	int m_iLastOutgoingCommand;
	int m_iTickbaseShift;
};
static std::vector<TickbaseFix_t> s_vTickbaseFixes = {};

MAKE_HOOK(CPrediction_RunSimulation, Signatures::CPrediction_RunSimulation.Get(), void, __fastcall,
	void* rcx, int current_command, float curtime, CUserCmd* cmd, C_TFPlayer* localPlayer)
{
	if (!localPlayer || !cmd)
		return CALL_ORIGINAL(rcx, current_command, curtime, cmd, localPlayer);

	if (Shifting::bShifting && Shifting::nCurrentShiftTick == 0 && Shifting::nTotalShiftTicks > 0)
	{
		s_vTickbaseFixes.emplace_back(TickbaseFix_t{ G::CurrentUserCmd, I::ClientState->lastoutgoingcommand, Shifting::nTotalShiftTicks });
	}

	for (auto it = s_vTickbaseFixes.begin(); it != s_vTickbaseFixes.end();)
	{
		if (it->m_iLastOutgoingCommand < I::ClientState->last_command_ack)
		{
			it = s_vTickbaseFixes.erase(it);
			continue;
		}
		if (cmd == it->m_pCmd)
		{
			localPlayer->m_nTickBase() -= it->m_iTickbaseShift;
			break;
		}
		++it;
	}

	F::EnginePrediction->AdjustPlayers(localPlayer);
	CALL_ORIGINAL(rcx, current_command, curtime, cmd, localPlayer);
	F::EnginePrediction->RestorePlayers();
}

namespace TickbaseFix
{
	void Add(int nStartCommandNumber, int nTickbaseShift)
	{
	}

	void Clear()
	{
		s_vTickbaseFixes.clear();
	}
}
