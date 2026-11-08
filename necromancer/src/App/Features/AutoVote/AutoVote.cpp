#include "AutoVote.h"

#include "../CFG.h"
#include "../Players/Players.h"

#include <format>

static bool IsValidPlayerIndex(int iIndex)
{
	return iIndex > 0 && iIndex <= I::EngineClient->GetMaxClients();
}

static bool IsSteamFriendIndex(int iIndex)
{
	if (!IsValidPlayerIndex(iIndex))
		return false;

	auto pEntity = I::ClientEntityList->GetClientEntity(iIndex);
	if (!pEntity || pEntity->GetClassId() != ETFClassIds::CTFPlayer)
		return false;

	return pEntity->As<C_TFPlayer>()->IsPlayerOnSteamFriendsList();
}

static bool IsIgnoredIndex(int iIndex)
{
	if (!IsValidPlayerIndex(iIndex))
		return false;

	PlayerPriority priority{};
	return F::Players->GetInfo(iIndex, priority) && priority.Ignored;
}

// Amalgam's AutoF1 basis: the TARGET is tagged as a cheater in the playerlist
static bool IsPrioritizedIndex(int iIndex)
{
	if (!IsValidPlayerIndex(iIndex))
		return false;

	PlayerPriority priority{};
	if (!F::Players->GetInfo(iIndex, priority))
		return false;

	return priority.Cheater || priority.RetardLegit || priority.Targeted;
}

static bool IsPartyMemberIndex(int iIndex)
{
	return IsValidPlayerIndex(iIndex) && H::Entities->GetPartyIndex(iIndex) == 1;
}

static constexpr float VOTE_RETRY_TIME = 4.0f;

static bool IsUnclassifiedIndex(int iIndex)
{
	if (!IsValidPlayerIndex(iIndex))
		return false;

	auto pEntity = I::ClientEntityList->GetClientEntity(iIndex);
	return !pEntity || pEntity->GetClassId() != ETFClassIds::CTFPlayer;
}

static bool CanRetryTarget(int iTarget)
{
	if (!IsValidPlayerIndex(iTarget))
		return false;

	if (IsUnclassifiedIndex(iTarget))
		return true;

	player_info_t info{};
	if (!I::EngineClient->GetPlayerInfo(iTarget, &info))
		return true;

	return !info.fakeplayer;
}

void CAutoVote::UserMessage(bf_read& msgData)
{
	if (!CFG::Misc_Auto_F1 && !CFG::Misc_Auto_F2)
		return;

	const int nStartBit = msgData.GetNumBitsRead();

	msgData.SetAssertOnOverflow(false);

	msgData.ReadByte();
	const int iVoteID = msgData.ReadLong();
	const int iCaller = msgData.ReadByte();
	char szReason[256]{}; msgData.ReadString(szReason, sizeof(szReason));
	char szTargetName[256]{}; msgData.ReadString(szTargetName, sizeof(szTargetName));
	const int iTarget = msgData.ReadByte() >> 1;

	const bool bOverflow = msgData.IsOverflowed();
	msgData.Seek(nStartBit);

	if (bOverflow || !iVoteID)
		return;

	for (const auto& entry : m_vecQueue)
	{
		if (entry.iVoteID == iVoteID)
			return;
	}

	for (const auto& entry : m_vecFired)
	{
		if (entry.iVoteID == iVoteID)
			return;
	}

	if (CFG::Misc_Auto_F2 && (IsSteamFriendIndex(iTarget) || IsIgnoredIndex(iTarget) || IsPartyMemberIndex(iTarget)))
	{
		FireVote(iVoteID, 2);
		return;
	}

	// Auto F1 fires on prioritized (tagged) targets like Amalgam's
	// AutoF1Priority, or when a steam friend called the vote. The old
	// caller-only check almost never matched, which made F1 look broken.
	if (CFG::Misc_Auto_F1 && (IsPrioritizedIndex(iTarget) || IsSteamFriendIndex(iCaller))
		&& !IsPartyMemberIndex(iTarget) && !IsSteamFriendIndex(iTarget))
	{
		FireVote(iVoteID, 1);
		return;
	}

	// Neither condition matched yet - entity info often isn't classified
	// when the vote message arrives, so defer and let Tick() retry
	if ((CFG::Misc_Auto_F2 || CFG::Misc_Auto_F1)
		&& (CanRetryTarget(iTarget) || (CFG::Misc_Auto_F1 && IsUnclassifiedIndex(iCaller))))
	{
		QueueVote(iVoteID, 0, iTarget, iCaller);
	}
}

void CAutoVote::FireVote(int iVoteID, int iOption)
{
	I::EngineClient->ClientCmd_Unrestricted(std::format("vote {} option{}", iVoteID, iOption).c_str());
	m_vecFired.push_back({ iVoteID, iOption, 0, 0, I::EngineClient->Time() + VOTE_RETRY_TIME });
}

void CAutoVote::QueueVote(int iVoteID, int iOption, int iTarget, int iCaller)
{
	if (m_vecQueue.size() >= 64)
		m_vecQueue.erase(m_vecQueue.begin());

	m_vecQueue.push_back({ iVoteID, iOption, iTarget, iCaller, I::EngineClient->Time() + VOTE_RETRY_TIME });
}

void CAutoVote::Tick()
{
	if (!I::EngineClient || !I::EngineClient->IsInGame() || (!CFG::Misc_Auto_F1 && !CFG::Misc_Auto_F2))
	{
		m_vecQueue.clear();
		m_vecFired.clear();
		return;
	}

	const float flNow = I::EngineClient->Time();

	for (size_t i = 0; i < m_vecFired.size();)
	{
		if (flNow >= m_vecFired[i].flDeadline)
			m_vecFired.erase(m_vecFired.begin() + i);
		else
			i++;
	}

	for (size_t i = 0; i < m_vecQueue.size();)
	{
		const auto entry = m_vecQueue[i];

		if (flNow >= entry.flDeadline)
		{
			m_vecQueue.erase(m_vecQueue.begin() + i);
			continue;
		}

		// Option 0 = deferred entry: fire once either the F2 or the F1
		// condition becomes true for this vote
		if (entry.iOption == 0)
		{
			if (CFG::Misc_Auto_F2 && (IsSteamFriendIndex(entry.iTarget) || IsIgnoredIndex(entry.iTarget) || IsPartyMemberIndex(entry.iTarget)))
			{
				FireVote(entry.iVoteID, 2);
				m_vecQueue.erase(m_vecQueue.begin() + i);
				continue;
			}

			if (CFG::Misc_Auto_F1 && (IsPrioritizedIndex(entry.iTarget) || IsSteamFriendIndex(entry.iCaller))
				&& !IsPartyMemberIndex(entry.iTarget) && !IsSteamFriendIndex(entry.iTarget))
			{
				FireVote(entry.iVoteID, 1);
				m_vecQueue.erase(m_vecQueue.begin() + i);
				continue;
			}

			i++;
			continue;
		}

		const bool bEligible = entry.iOption == 2
			? (CFG::Misc_Auto_F2 && (IsSteamFriendIndex(entry.iTarget) || IsIgnoredIndex(entry.iTarget) || IsPartyMemberIndex(entry.iTarget)))
			: (CFG::Misc_Auto_F1 && IsSteamFriendIndex(entry.iCaller));

		if (bEligible)
		{
			FireVote(entry.iVoteID, entry.iOption);
			m_vecQueue.erase(m_vecQueue.begin() + i);
			continue;
		}

		i++;
	}
}
