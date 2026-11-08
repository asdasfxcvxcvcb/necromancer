#include "../../SDK/SDK.h"

#include "../Features/CFG.h"
#include "../Features/Triggerbot/AutoVaccinator/AutoVaccinator.h"
#include "../Features/Players/Players.h"
#include "../Features/Crits/Crits.h"
#include "../Features/Chat/Chat.h"
#include "../Features/Misc/SkinChanger/SkinChanger.h"
#include "../Features/StatusWindow/StatusWindow.h"

MAKE_SIGNATURE(CGameEventManager_FireEventIntern, "engine.dll", "44 88 44 24 ? 48 89 4C 24 ? 55 57", 0x0);

void OnVoteCast(IGameEvent* event)
{
	if (!CFG::Visuals_Chat_Teammate_Votes && !CFG::Visuals_Chat_Enemy_Votes)
		return;

	if (event->GetInt("team") == -1)
		return;

	const auto pLocal = H::Entities->GetLocal();

	if (!pLocal)
		return;

	if (!CFG::Visuals_Chat_Teammate_Votes && pLocal->m_iTeamNum() == event->GetInt("team"))
		return;

	if (!CFG::Visuals_Chat_Enemy_Votes && pLocal->m_iTeamNum() != event->GetInt("team"))
		return;

	player_info_t pi{};
	if (!I::EngineClient->GetPlayerInfo(event->GetInt("entityid"), &pi))
		return;

	if (event->GetInt("vote_option") == 0)
	{
		I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("\x1{} voted \x8{}YES", pi.name, Color_t{ 46, 204, 113, 255 }.toHexStr()).c_str());
	}

	if (event->GetInt("vote_option") == 1)
	{
		I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("\x1{} voted \x8{}NO", pi.name, Color_t{ 231, 76, 60, 255 }.toHexStr()).c_str());
	}
}

MAKE_HOOK(CGameEventManager_FireEventIntern, Signatures::CGameEventManager_FireEventIntern.Get(), bool, __fastcall,
	void* ecx, IGameEvent* event, bool bServerOnly, bool bClientOnly)
{
	if (event)
	{
		static constexpr auto vote_cast{ HASH_CT("vote_cast") };
		static constexpr auto player_hurt{ HASH_CT("player_hurt") };
		static constexpr auto player_death{ HASH_CT("player_death") };
		static constexpr auto revive_player_notify{ HASH_CT("revive_player_notify") };
		static constexpr auto player_connect_client{ HASH_CT("player_connect_client") };
		
		// Call crit event handler for all events
		const auto uHash = HASH_RT(event->GetName());
		F::CritHack->Event(event, uHash);

		if (uHash == vote_cast)
		{
			OnVoteCast(event);
		}

		if (uHash == player_hurt)
		{
			F::AutoVaccinator->ProcessPlayerHurt(event);
		}

		// Track kills and deaths
		if (uHash == player_death)
		{
			const auto pLocal = H::Entities->GetLocal();
			if (pLocal)
			{
				int nLocalIndex = pLocal->entindex();
				int nAttacker = event->GetInt("attacker");
				int nVictim = event->GetInt("userid");

				// Convert userids to entity indices - the outer loop only ever
				// acted on the local slot, so query it directly instead of
				// walking every client slot with nested O(maxClients^2) engine
				// calls per death event
				player_info_t localInfo{};
				if (I::EngineClient->GetPlayerInfo(nLocalIndex, &localInfo))
				{
				if (localInfo.userID == nVictim)
				{
					F::SkinChanger->OnDeath();

					for (int j = 1; j <= I::EngineClient->GetMaxClients(); j++)
					{
						player_info_t attackerInfo{};
						if (I::EngineClient->GetPlayerInfo(j, &attackerInfo) && attackerInfo.userID == nAttacker && j != nLocalIndex)
						{
							uint64_t attackerSteamID = static_cast<uint64_t>(attackerInfo.friendsID) + 0x0110000100000000ULL;
							F::Players->RecordDeath(attackerSteamID);
							break;
						}
					}
				}
				else if (localInfo.userID == nAttacker)
				{
					F::StatusWindow->OnLocalKill(event->GetString("weapon"), event->GetInt("customkill"), nVictim, (event->GetInt("damagebits") & (1 << 20)) != 0);

					for (int j = 1; j <= I::EngineClient->GetMaxClients(); j++)
					{
						player_info_t victimInfo{};
						if (I::EngineClient->GetPlayerInfo(j, &victimInfo) && victimInfo.userID == nVictim && j != nLocalIndex)
						{
							uint64_t victimSteamID = static_cast<uint64_t>(victimInfo.friendsID) + 0x0110000100000000ULL;
							F::Players->RecordKill(victimSteamID);
							F::SkinChanger->OnKill();
							OnKill(victimInfo.name, j);
							break;
						}
					}
				}
				else
				{
					for (int j = 1; j <= I::EngineClient->GetMaxClients(); j++)
					{
						player_info_t attackerInfo{};
						if (I::EngineClient->GetPlayerInfo(j, &attackerInfo) && attackerInfo.userID == nAttacker && j != nLocalIndex)
						{
							F::StatusWindow->OnHealTargetKill(j);
							break;
						}
					}
				}
				}
			}
		}

		if (uHash == player_connect_client && bClientOnly && CFG::Visuals_Chat_Player_List_Info)
		{
			PlayerPriority pi{};

			if (F::Players->GetInfoGUID(event->GetString("networkid"), pi))
			{
				const char* const name{ event->GetString("name") };

				// every tag announces on connect, in the same priority order the
				// ESP name/tag colors use
				if (pi.Cheater)
				{
					I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("\x1{} is marked as \x8{}[Cheater]", name, CFG::Color_Cheater.toHexStr()).c_str());
				}

				if (pi.Targeted)
				{
					I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("\x1{} is marked as \x8{}[Targeted]", name, CFG::Color_Targeted.toHexStr()).c_str());
				}

				if (pi.Nigger)
				{
					I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("\x1{} is marked as \x8{}[Nigger]", name, CFG::Color_Nigger.toHexStr()).c_str());
				}

				if (pi.RetardLegit)
				{
					I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("\x1{} is marked as \x8{}[Retard Legit]", name, CFG::Color_RetardLegit.toHexStr()).c_str());
				}

				if (pi.Streamer)
				{
					I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("\x1{} is marked as \x8{}[Streamer]", name, CFG::Color_Streamer.toHexStr()).c_str());
				}

				if (pi.Ignored)
				{
					I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("\x1{} is marked as \x8{}[Ignored]", name, CFG::Color_Friend.toHexStr()).c_str());
				}
			}
		}

		if (const auto pLocal = H::Entities->GetLocal())
		{
			if (CFG::Misc_MVM_Instant_Revive && uHash == revive_player_notify)
			{
				if (event->GetInt("entindex") == pLocal->entindex())
				{
					auto* kv = new KeyValues("MVM_Revive_Response");
					kv->SetInt("accepted", 1);

					I::EngineClient->ServerCmdKeyValues(kv);
				}
			}
		}
	}

	return CALL_ORIGINAL(ecx, event, bServerOnly, bClientOnly);
}
