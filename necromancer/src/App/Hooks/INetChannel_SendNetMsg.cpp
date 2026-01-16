#include "../../SDK/SDK.h"
#include "../Features/LagRecords/LagRecords.h"
#include "../Features/Misc/AntiCheatCompat/AntiCheatCompat.h"
#include "../Features/NetworkFix/NetworkFix.h"
#include "../Features/CFG.h"
#include "../Features/TickbaseManip/TickbaseManip.h"

MAKE_SIGNATURE(INetChannel_SendNetMsg, "engine.dll", "48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 48 8B F1 45 0F B6 F1", 0x0);
MAKE_SIGNATURE(WriteUsercmd, "client.dll", "40 56 57 48 83 EC ? 41 8B 40", 0x0);
MAKE_SIGNATURE(CNetChannel_SendDatagram, "engine.dll", "40 55 57 41 56 48 8D AC 24", 0x0);

//credits: KGB

// Simple FNV1A hash for string comparison (like Amalgam uses)
namespace FNV1A
{
	constexpr uint32_t Hash32Const(const char* str, uint32_t hash = 2166136261u)
	{
		return *str ? Hash32Const(str + 1, (hash ^ static_cast<uint32_t>(*str)) * 16777619u) : hash;
	}
	
	inline uint32_t Hash32(const char* str)
	{
		uint32_t hash = 2166136261u;
		while (*str)
		{
			hash ^= static_cast<uint32_t>(*str++);
			hash *= 16777619u;
		}
		return hash;
	}
}

bool WriteUsercmdDeltaToBuffer(bf_write* buf, int from, int to)
{
	CUserCmd nullcmd = {};
	CUserCmd* f = nullptr;

	if (from == -1)
	{
		f = &nullcmd;
	}
	else
	{
		f = I::Input->GetUserCmd(from);

		if (!f)
		{
			f = &nullcmd;
		}
	}

	CUserCmd* t = I::Input->GetUserCmd(to);

	if (!t)
	{
		t = &nullcmd;
	}

	reinterpret_cast<void(__cdecl *)(bf_write*, CUserCmd*, CUserCmd*)>(Signatures::WriteUsercmd.Get())(buf, t, f);

	return !buf->m_bOverflow;
}

MAKE_HOOK(INetChannel_SendNetMsg, Signatures::INetChannel_SendNetMsg.Get(), bool, __fastcall,
	CNetChannel* pNet, INetMessage& msg, bool bForceReliable, bool bVoice)
{
	// Intercept net_SetConVar messages for ping reducer
	if (msg.GetType() == net_SetConVar)
	{
		auto pMsg = reinterpret_cast<NET_SetConVar*>(&msg);
		
		// Intercept and replace cl_cmdrate/cl_updaterate values
		for (int i = 0; i < pMsg->m_ConVars.Count(); i++)
		{
			NET_SetConVar::CVar_t* localCvar = &pMsg->m_ConVars[i];
			
			switch (FNV1A::Hash32(localCvar->Name))
			{
			case FNV1A::Hash32Const("cl_cmdrate"):
				// If ping reducer is active, replace with our desired cmdrate
				if (CFG::Misc_Ping_Reducer_Active && F::NetworkFix->m_flLastCmdRate > 0.0f)
				{
					sprintf_s(localCvar->Value, "%f", F::NetworkFix->m_flLastCmdRate);
				}
				// Anti-cheat: clamp to minimum 10 to avoid detection
				if (CFG::Misc_AntiCheat_Enabled)
				{
					try {
						float flValue = std::stof(localCvar->Value);
						sprintf_s(localCvar->Value, "%f", std::max(flValue, 10.0f));
					} catch (...) {}
				}
				break;
				
			case FNV1A::Hash32Const("cl_updaterate"):
				// If ping reducer is active, replace with our desired updaterate
				if (CFG::Misc_Ping_Reducer_Active && F::NetworkFix->m_flLastUpdateRate > 0.0f)
				{
					sprintf_s(localCvar->Value, "%f", F::NetworkFix->m_flLastUpdateRate);
				}
				break;
				
			case FNV1A::Hash32Const("cl_interp"):
				// Anti-cheat: clamp interp to max 0.1
				if (CFG::Misc_AntiCheat_Enabled)
				{
					try {
						float flValue = std::stof(localCvar->Value);
						sprintf_s(localCvar->Value, "%f", std::min(flValue, 0.1f));
					} catch (...) {}
				}
				break;
				
			case FNV1A::Hash32Const("cl_interp_ratio"):
			case FNV1A::Hash32Const("cl_interpolate"):
				// Force to 1 for optimal backtrack
				strncpy_s(localCvar->Value, "1", MAX_OSPATH);
				break;
			}
		}
		
		// Anti-cheat compatibility: validate and clamp network cvars
		if (CFG::Misc_AntiCheat_Enabled)
		{
			F::AntiCheatCompat->ValidateNetworkCvars(pMsg);
		}
	}
	
	// Spoof cvar query responses - important for ping reducer and anti-cheat
	if (msg.GetType() == clc_RespondCvarValue)
	{
		auto pMsg = reinterpret_cast<uintptr_t*>(&msg);
		if (pMsg)
		{
			auto cvarName = reinterpret_cast<const char*>(pMsg[6]);
			if (cvarName)
			{
				auto pConVar = I::CVar->FindVar(cvarName);
				if (pConVar)
				{
					// Use static buffer to ensure string stays valid
					static char szSpoofValue[64] = {};
					bool bSpoofed = false;
					
					switch (FNV1A::Hash32(cvarName))
					{
					case FNV1A::Hash32Const("cl_cmdrate"):
						// Spoof cmdrate if ping reducer is active
						if (CFG::Misc_Ping_Reducer_Active && F::NetworkFix->m_flLastCmdRate > 0.0f)
						{
							float flValue = F::NetworkFix->m_flLastCmdRate;
							// Anti-cheat: clamp to minimum 10
							if (CFG::Misc_AntiCheat_Enabled)
								flValue = std::max(flValue, 10.0f);
							sprintf_s(szSpoofValue, "%f", flValue);
							bSpoofed = true;
						}
						break;
						
					case FNV1A::Hash32Const("cl_updaterate"):
						// Spoof updaterate if ping reducer is active
						if (CFG::Misc_Ping_Reducer_Active && F::NetworkFix->m_flLastUpdateRate > 0.0f)
						{
							sprintf_s(szSpoofValue, "%f", F::NetworkFix->m_flLastUpdateRate);
							bSpoofed = true;
						}
						break;
						
					case FNV1A::Hash32Const("cl_interp"):
						// Anti-cheat: clamp interp
						if (CFG::Misc_AntiCheat_Enabled)
						{
							try {
								float flValue = std::stof(pConVar->GetString());
								sprintf_s(szSpoofValue, "%f", std::min(flValue, 0.1f));
								bSpoofed = true;
							} catch (...) {}
						}
						break;
						
					case FNV1A::Hash32Const("cl_interp_ratio"):
						strcpy_s(szSpoofValue, "1");
						bSpoofed = true;
						break;
						
					case FNV1A::Hash32Const("r_aspectratio"):
						// Always spoof aspect ratio to default (0) to avoid server bans
						// This runs automatically when aspect ratio is modified
						if (CFG::Visuals_Freecam_AspectRatio > 0.0f)
						{
							strcpy_s(szSpoofValue, "0");
							bSpoofed = true;
						}
						break;
					}
					
					if (bSpoofed)
					{
						pMsg[7] = uintptr_t(szSpoofValue);
					}
				}
			}
		}
		
		// Also run anti-cheat spoof if enabled
		if (CFG::Misc_AntiCheat_Enabled)
		{
			F::AntiCheatCompat->SpoofCvarResponse(&msg);
		}
	}
	
	if (msg.GetType() == clc_Move)
	{
		const auto pMsg = reinterpret_cast<CLC_Move*>(&msg);

		const int nLastOutGoingCommand = I::ClientState->lastoutgoingcommand;
		const int nChokedCommands = I::ClientState->chokedcommands;
		const int nNextCommandNr = nLastOutGoingCommand + nChokedCommands + 1;

		unsigned char data[4000] = {};
		pMsg->m_DataOut.StartWriting(data, sizeof(data));

		// Dynamic command priority: use DT setting during doubletap shifts to push attack commands faster
		// Use normal setting when idle to reduce bandwidth/improve stability
		// If auto settings enabled, use F::Ticks->GetOptimal* functions
		int nMaxNewCommands;
		if (Shifting::bShifting && !Shifting::bShiftingWarp)
		{
			// During doubletap
			nMaxNewCommands = std::min(F::Ticks->GetOptimalDTCommands(), static_cast<int>(MAX_NEW_COMMANDS));
		}
		else
		{
			// Normal/idle
			nMaxNewCommands = std::min(F::Ticks->GetOptimalMaxCommands(), MAX_NEW_COMMANDS);
		}
		
		pMsg->m_nNewCommands = 1 + nChokedCommands;
		pMsg->m_nNewCommands = std::clamp(pMsg->m_nNewCommands, 0, nMaxNewCommands);

		const int nExtraCommands = nChokedCommands + 1 - pMsg->m_nNewCommands;
		const int nCmdBackup = std::max(2, nExtraCommands);

		pMsg->m_nBackupCommands = std::clamp(nCmdBackup, 0, MAX_BACKUP_COMMANDS);

		const int nNumCmds = pMsg->m_nNewCommands + pMsg->m_nBackupCommands;
		int nFrom = -1;

		bool bOk = true;

		for (int nTo = nNextCommandNr - nNumCmds + 1; nTo <= nNextCommandNr; nTo++)
		{
			bOk = bOk && WriteUsercmdDeltaToBuffer(&pMsg->m_DataOut, nFrom, nTo);
			nFrom = nTo;
		}

		if (bOk)
		{
			if (nExtraCommands > 0)
			{
				pNet->m_nChokedPackets -= nExtraCommands;
			}

			CALL_ORIGINAL(pNet, reinterpret_cast<INetMessage&>(*pMsg), bForceReliable, bVoice);
		}

		// ============================================
		// Deficit Tracking (from Amalgam)
		// ============================================
		// Track when we send more commands than the server can process.
		// The server has sv_maxusrcmdprocessticks (default 24) limit.
		// If we exceed it, commands get dropped and we need to compensate.
		if (CFG::Exploits_RapidFire_Deficit_Tracking && !Shifting::bShiftingWarp) // Don't track during warp
		{
			// Get sv_maxusrcmdprocessticks
			static auto sv_maxusrcmdprocessticks = I::CVar->FindVar("sv_maxusrcmdprocessticks");
			Shifting::nMaxUsrCmdProcessTicks = sv_maxusrcmdprocessticks ? sv_maxusrcmdprocessticks->GetInt() : 24;
			
			// Calculate how many commands we're allowed to send
			const int nAllowedNewCommands = std::max(Shifting::nMaxUsrCmdProcessTicks - Shifting::nAvailableTicks, 0);
			
			// Calculate how many we actually sent (minus the 3 backup commands that don't count)
			const int nCmdCount = pMsg->m_nNewCommands + pMsg->m_nBackupCommands - 3;
			
			// If we sent more than allowed, track the deficit
			if (nCmdCount > nAllowedNewCommands)
			{
				Shifting::nDeficit = nCmdCount - nAllowedNewCommands;
			}
		}

		return true;
	}

	return CALL_ORIGINAL(pNet, msg, bForceReliable, bVoice);
}

MAKE_HOOK(CNetChannel_SendDatagram, Signatures::CNetChannel_SendDatagram.Get(), int, __fastcall,
	CNetChannel* pNetChan, bf_write* datagram)
{
	// Only apply fake latency when datagram is NULL (actual packet send)
	if (!datagram)
	{
		// Apply fake latency before sending
		F::LagRecords->AdjustPing(pNetChan);
		
		const int iReturn = CALL_ORIGINAL(pNetChan, datagram);
		
		// Restore real ping after sending
		F::LagRecords->RestorePing(pNetChan);
		
		return iReturn;
	}

	return CALL_ORIGINAL(pNetChan, datagram);
}
