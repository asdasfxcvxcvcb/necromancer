#include "NetworkFix.h"

#include "../CFG.h"

MAKE_SIGNATURE(CL_ReadPackets, "engine.dll", "4C 8B DC 49 89 5B ? 55 56 57 41 54 41 55 41 56 41 57 48 83 EC ? 48 8B 05", 0x0);

// Need access to the SendNetMsg signature to call through the hook
MAKE_SIGNATURE(INetChannel_SendNetMsg, "engine.dll", "48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 48 8B F1 45 0F B6 F1", 0x0);

MAKE_HOOK(CL_ReadPackets, Signatures::CL_ReadPackets.Get(), void, __cdecl,
	bool bFinalTick)
{
	if (!CFG::Misc_Ping_Reducer)
	{
		CALL_ORIGINAL(bFinalTick);

		return;
	}

	if (F::NetworkFix->ShouldReadPackets())
	{
		CALL_ORIGINAL(bFinalTick);
	}
}

void CReadPacketState::Store()
{
	m_flFrameTimeClientState = I::ClientState->m_frameTime;
	m_flFrameTime = I::GlobalVars->frametime;
	m_flCurTime = I::GlobalVars->curtime;
	m_nTickCount = I::GlobalVars->tickcount;
}

void CReadPacketState::Restore()
{
	I::ClientState->m_frameTime = m_flFrameTimeClientState;
	I::GlobalVars->frametime = m_flFrameTime;
	I::GlobalVars->curtime = m_flCurTime;
	I::GlobalVars->tickcount = m_nTickCount;
}

void CNetworkFix::FixInputDelay(bool bFinalTick)
{
	if (!I::EngineClient->IsInGame())
	{
		return;
	}

	if (const auto pNetChannel = I::EngineClient->GetNetChannelInfo())
	{
		if (pNetChannel->IsLoopback())
		{
			return;
		}
	}

	CReadPacketState backup = {};

	backup.Store();

	Hooks::CL_ReadPackets::Hook.Original<Hooks::CL_ReadPackets::fn>()(bFinalTick);

	m_State.Store();

	backup.Restore();
}

bool CNetworkFix::ShouldReadPackets()
{
	if (!I::EngineClient->IsInGame())
		return true;

	if (const auto pNetChannel = I::EngineClient->GetNetChannelInfo())
	{
		if (pNetChannel->IsLoopback())
			return true;
	}

	m_State.Restore();

	return false;
}

void CNetworkFix::ApplyPingReducer()
{
	// Ping reducer: Sets cl_cmdrate directly to the slider value (0.1 - 1.0)
	
	static auto cl_cmdrate = I::CVar->FindVar("cl_cmdrate");
	static auto cl_updaterate = I::CVar->FindVar("cl_updaterate");

	if (!cl_cmdrate || !cl_updaterate)
		return;

	if (!CFG::Misc_Ping_Reducer_Active)
	{
		// Restore to default 66 when disabled
		if (m_flLastCmdRate > 0.0f)
		{
			cl_cmdrate->SetValue(66);
			cl_updaterate->SetValue(66);
			m_flLastCmdRate = -1.0f;
			m_flLastUpdateRate = -1.0f;
		}
		return;
	}

	if (!I::EngineClient->IsInGame() || !I::EngineClient->IsConnected())
		return;

	if (const auto pNetChannel = I::EngineClient->GetNetChannelInfo())
	{
		if (pNetChannel->IsLoopback())
			return;
	}

	// Use slider value directly as cmdrate (0.1 - 1.0)
	float flTargetCmdRate = CFG::Misc_Ping_Reducer_Value;
	float flTargetUpdateRate = CFG::Misc_Ping_Reducer_Value;

	// Update cmdrate
	if (m_flLastCmdRate != flTargetCmdRate)
	{
		m_flLastCmdRate = flTargetCmdRate;
		cl_cmdrate->SetValue(m_flLastCmdRate);
	}

	// Update updaterate
	if (m_flLastUpdateRate != flTargetUpdateRate)
	{
		m_flLastUpdateRate = flTargetUpdateRate;
		cl_updaterate->SetValue(m_flLastUpdateRate);
	}
}

void CNetworkFix::ResetRates()
{
	// Reset cached values to force resend on next ApplyPingReducer call
	// This is needed when changing maps as the server resets cl_cmdrate
	m_flLastCmdRate = -1.0f;
	m_flLastUpdateRate = -1.0f;
	m_nLastWeaponType = -1;
}

void CNetworkFix::ApplyAutoInterp()
{
	// Lerp removal is now handled by hooks:
	// - CClientState_GetClientInterpAmount.cpp
	// - GetClientInterpAmount.cpp
	// They check CFG::Visuals_Auto_Interp and return 0 when enabled
}
