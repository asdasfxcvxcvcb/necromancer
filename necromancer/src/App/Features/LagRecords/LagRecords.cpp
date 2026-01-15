#include "LagRecords.h"

#include <ranges>

#include "../CFG.h"
#include "../Misc/AntiCheatCompat/AntiCheatCompat.h"

bool CLagRecords::IsSimulationTimeValid(float flCurSimTime, float flCmprSimTime)
{
	// Base limit + fake latency extension
	// NOTE: Don't clamp fake latency here - the fake latency system works by manipulating
	// sequence numbers, which is separate from the interp system. Amalgam doesn't clamp
	// fake latency, only fake interp (which is sent to the server via cl_interp).
	const float flMaxTime = std::min(0.2f + m_flFakeLatency, m_flMaxUnlag);
	return (flCurSimTime - flCmprSimTime) < flMaxTime;
}

void CLagRecords::UpdateDatagram()
{
	auto pNetChan = reinterpret_cast<CNetChannel*>(I::EngineClient->GetNetChannelInfo());
	if (!pNetChan)
	{
		// Reset state when no net channel (disconnected/joining)
		m_dSequences.clear();
		m_iLastInSequence = 0;
		m_nLastInSequenceNr = 0;
		m_flFakeLatency = 0.0f;
		return;
	}

	const auto pLocal = H::Entities->GetLocal();
	if (pLocal)
		m_nOldTickBase = pLocal->m_nTickBase();

	// Detect sequence number reset (happens when joining new game)
	// If current sequence is much lower than last, we've joined a new game
	if (pNetChan->m_nInSequenceNr < m_iLastInSequence - 100)
	{
		// Reset fake latency state for new game
		m_dSequences.clear();
		m_iLastInSequence = 0;
		m_nLastInSequenceNr = 0;
		m_flFakeLatency = 0.0f;
	}

	// Track incoming sequences for fake latency
	if (pNetChan->m_nInSequenceNr > m_iLastInSequence)
	{
		m_iLastInSequence = pNetChan->m_nInSequenceNr;
		
		Sequence_t seq;
		seq.nInReliableState = pNetChan->m_nInReliableState;
		seq.nSequenceNr = pNetChan->m_nInSequenceNr;
		seq.flTime = I::GlobalVars->realtime;
		
		m_dSequences.emplace_front(seq);
	}

	// Keep only last 67 sequences (Amalgam's limit)
	if (m_dSequences.size() > 67)
		m_dSequences.pop_back();
}

void CLagRecords::AddRecord(C_TFPlayer* pPlayer)
{
	// Validate player before doing anything
	if (!pPlayer || !pPlayer->GetClientNetworkable() || !pPlayer->GetClientNetworkable()->GetClientClass())
		return;

	// Check if player has a valid model - if not, skip bone setup
	if (!pPlayer->GetModel())
		return;

	LagRecord_t newRecord = {};

	m_bSettingUpBones = true;

	const bool bSetupBonesOpt = CFG::Misc_SetupBones_Optimization;

	if (bSetupBonesOpt)
		pPlayer->InvalidateBoneCache();

	const bool bResult = pPlayer->SetupBones(newRecord.BoneMatrix, 128, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime);

	if (bSetupBonesOpt)
	{
		int nIterations = 0;
		for (auto attach = pPlayer->FirstMoveChild(); attach; attach = attach->NextMovePeer())
		{
			// Safety: limit iterations to prevent infinite loops on corrupted entity chains
			if (++nIterations > 32)
				break;

			// Validate the attachment is a valid entity with a model
			if (!attach->GetClientNetworkable() || !attach->GetClientNetworkable()->GetClientClass())
				continue;

			if (!attach->GetModel())
				continue;

			if (attach->ShouldDraw())
			{
				attach->InvalidateBoneCache();
				attach->SetupBones(nullptr, -1, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime);
			}
		}
	}

	m_bSettingUpBones = false;

	if (!bResult)
		return;

	newRecord.Player = pPlayer;
	newRecord.SimulationTime = pPlayer->m_flSimulationTime();
	newRecord.AbsOrigin = pPlayer->GetAbsOrigin();
	newRecord.VecOrigin = pPlayer->m_vecOrigin();
	newRecord.AbsAngles = pPlayer->GetAbsAngles();
	newRecord.EyeAngles = pPlayer->GetEyeAngles();
	newRecord.Velocity = pPlayer->m_vecVelocity();
	newRecord.Center = pPlayer->GetCenter();
	newRecord.Flags = pPlayer->m_fFlags();

	if (const auto pAnimState = pPlayer->GetAnimState())
		newRecord.FeetYaw = pAnimState->m_flCurrentFeetYaw;

	m_LagRecords[pPlayer].emplace_front(newRecord);
}

const LagRecord_t* CLagRecords::GetRecord(C_TFPlayer* pPlayer, int nRecord, bool bSafe)
{
	if (!bSafe)
	{
		if (!m_LagRecords.contains(pPlayer))
			return nullptr;

		if (nRecord < 0 || nRecord > static_cast<int>(m_LagRecords[pPlayer].size() - 1))
			return nullptr;
	}

	return &m_LagRecords[pPlayer][nRecord];
}

bool CLagRecords::HasRecords(C_TFPlayer* pPlayer, int* pTotalRecords)
{
	if (m_LagRecords.contains(pPlayer))
	{
		const size_t nSize = m_LagRecords[pPlayer].size();

		if (nSize <= 0)
			return false;

		if (pTotalRecords)
			*pTotalRecords = static_cast<int>(nSize - 1);

		return true;
	}

	return false;
}

void CLagRecords::UpdateRecords()
{
	const auto pLocal = H::Entities->GetLocal();

	if (!pLocal || pLocal->deadflag() || pLocal->InCond(TF_COND_HALLOWEEN_GHOST_MODE) || pLocal->InCond(TF_COND_HALLOWEEN_KART))
	{
		if (!m_LagRecords.empty())
			m_LagRecords.clear();
		return;
	}

	// Get the appropriate entity group once
	const auto& vPlayers = H::Entities->GetGroup(CFG::Misc_SetupBones_Optimization ? EEntGroup::PLAYERS_ALL : EEntGroup::PLAYERS_ENEMIES);

	// Remove invalid players - iterate the group directly instead of all records
	for (const auto pEntity : vPlayers)
	{
		if (!pEntity || pEntity == pLocal)
			continue;

		const auto pPlayer = pEntity->As<C_TFPlayer>();
		if (pPlayer && pPlayer->deadflag())
			m_LagRecords[pPlayer].clear();
	}

	// Remove invalid records
	for (auto& records : m_LagRecords | std::views::values)
	{
		std::erase_if(records, [](const LagRecord_t& curRecord) {
			return !curRecord.Player || !F::LagRecords->IsSimulationTimeValid(curRecord.Player->m_flSimulationTime(), curRecord.SimulationTime);
		});
	}
}

bool CLagRecords::DiffersFromCurrent(const LagRecord_t* pRecord)
{
	const auto pPlayer = pRecord->Player;

	if (!pPlayer)
		return false;

	if (static_cast<int>((pPlayer->m_vecOrigin() - pRecord->AbsOrigin).Length()) != 0)
		return true;

	if (static_cast<int>((pPlayer->GetEyeAngles() - pRecord->EyeAngles).Length()) != 0)
		return true;

	if (pPlayer->m_fFlags() != pRecord->Flags)
		return true;

	if (const auto pAnimState = pPlayer->GetAnimState())
	{
		if (fabsf(pAnimState->m_flCurrentFeetYaw - pRecord->FeetYaw) > 0.0f)
			return true;
	}

	return false;
}

void CLagRecordMatrixHelper::Set(const LagRecord_t* pRecord)
{
	if (!pRecord)
		return;

	const auto pPlayer = pRecord->Player;

	if (!pPlayer || pPlayer->deadflag())
		return;

	const auto pCachedBoneData = pPlayer->GetCachedBoneData();

	if (!pCachedBoneData || pCachedBoneData->Count() <= 0 || !pCachedBoneData->Base())
		return;

	m_pPlayer = pPlayer;
	m_vAbsOrigin = pPlayer->GetAbsOrigin();
	m_vAbsAngles = pPlayer->GetAbsAngles();
	
	const int nBoneCount = std::min(pCachedBoneData->Count(), 128);
	memcpy(m_BoneMatrix, pCachedBoneData->Base(), sizeof(matrix3x4_t) * nBoneCount);
	memcpy(pCachedBoneData->Base(), pRecord->BoneMatrix, sizeof(matrix3x4_t) * nBoneCount);

	pPlayer->SetAbsOrigin(pRecord->AbsOrigin);
	pPlayer->SetAbsAngles(pRecord->AbsAngles);

	m_bSuccessfullyStored = true;
}

void CLagRecordMatrixHelper::Restore()
{
	if (!m_bSuccessfullyStored || !m_pPlayer)
		return;

	const auto pCachedBoneData = m_pPlayer->GetCachedBoneData();

	if (!pCachedBoneData || pCachedBoneData->Count() <= 0 || !pCachedBoneData->Base())
	{
		// Reset state even if we can't restore bones
		m_pPlayer = nullptr;
		m_vAbsOrigin = {};
		m_vAbsAngles = {};
		std::memset(m_BoneMatrix, 0, sizeof(matrix3x4_t) * 128);
		m_bSuccessfullyStored = false;
		return;
	}

	m_pPlayer->SetAbsOrigin(m_vAbsOrigin);
	m_pPlayer->SetAbsAngles(m_vAbsAngles);
	
	const int nBoneCount = std::min(pCachedBoneData->Count(), 128);
	memcpy(pCachedBoneData->Base(), m_BoneMatrix, sizeof(matrix3x4_t) * nBoneCount);

	m_pPlayer = nullptr;
	m_vAbsOrigin = {};
	m_vAbsAngles = {};
	std::memset(m_BoneMatrix, 0, sizeof(matrix3x4_t) * 128);
	m_bSuccessfullyStored = false;
}


void CLagRecords::AdjustPing(INetChannel* pNetChanInterface)
{
	auto pNetChan = reinterpret_cast<CNetChannel*>(pNetChanInterface);
	if (!pNetChan)
		return;

	// Store original values
	m_nOldInSequenceNr = pNetChan->m_nInSequenceNr;
	m_nOldInReliableState = pNetChan->m_nInReliableState;

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || !pLocal->m_iClass())
		return;

	// Get desired fake latency from config (convert ms to seconds)
	// Always apply fake latency regardless of weapon type
	m_flWishFakeLatency = CFG::Aimbot_Hitscan_Fake_Latency / 1000.0f;
	
	// Update max unlag from server cvar
	static auto sv_maxunlag = I::CVar->FindVar("sv_maxunlag");
	if (sv_maxunlag)
		m_flMaxUnlag = sv_maxunlag->GetFloat();

	if (m_flWishFakeLatency <= 0.0f)
	{
		// No fake latency desired, smooth back to 0
		if (m_flFakeLatency > 0.0f)
		{
			m_flFakeLatency = std::max(0.0f, m_flFakeLatency - I::GlobalVars->interval_per_tick);
		}
		return;
	}

	// Calculate real latency from tickbase
	float flReal = TICKS_TO_TIME(pLocal->m_nTickBase() - m_nOldTickBase);
	static float flStaticReal = 0.0f;
	flStaticReal += (flReal + 5 * I::GlobalVars->interval_per_tick - flStaticReal) * 0.1f;

	// Find sequence that gives us the desired fake latency
	int nInReliableState = pNetChan->m_nInReliableState;
	int nInSequenceNr = pNetChan->m_nInSequenceNr;
	float flLatency = 0.0f;

	for (auto& seq : m_dSequences)
	{
		nInReliableState = seq.nInReliableState;
		nInSequenceNr = seq.nSequenceNr;
		flLatency = (I::GlobalVars->realtime - seq.flTime) - I::GlobalVars->interval_per_tick;

		// Stop if we've reached desired latency or limits
		if (flLatency > m_flWishFakeLatency || 
			m_nLastInSequenceNr >= seq.nSequenceNr || 
			flLatency > m_flMaxUnlag - flStaticReal)
			break;
	}

	// Failsafe: don't go over 1 second
	if (flLatency > 1.0f)
		return;

	// Apply the fake latency by rewinding sequence numbers
	pNetChan->m_nInSequenceNr = nInSequenceNr;
	pNetChan->m_nInReliableState = nInReliableState;
	
	m_nLastInSequenceNr = nInSequenceNr;

	// Smooth the fake latency value
	if (m_flWishFakeLatency > 0.0f || m_flFakeLatency > 0.0f)
	{
		float flDelta = flLatency - m_flFakeLatency;
		flDelta = std::clamp(flDelta, -I::GlobalVars->interval_per_tick, I::GlobalVars->interval_per_tick);
		m_flFakeLatency += flDelta * 0.1f;
		
		// Snap to 0 if very close
		if (!m_flWishFakeLatency && m_flFakeLatency < I::GlobalVars->interval_per_tick)
			m_flFakeLatency = 0.0f;
	}

	// NOTE: Anti-cheat compatibility clamping is done in GetFakeLatency() when the value is used,
	// not here. This preserves the internal state while only clamping what's sent to the server.
}

void CLagRecords::RestorePing(INetChannel* pNetChanInterface)
{
	auto pNetChan = reinterpret_cast<CNetChannel*>(pNetChanInterface);
	if (!pNetChan)
		return;

	// Restore original sequence numbers
	pNetChan->m_nInSequenceNr = m_nOldInSequenceNr;
	pNetChan->m_nInReliableState = m_nOldInReliableState;
}

float CLagRecords::GetFakeLatency() const
{
	// NOTE: Don't clamp fake latency for anti-cheat - Amalgam's GetFakeLatency() doesn't clamp.
	// Only GetFakeInterp() is clamped in Amalgam. The fake latency system works by manipulating
	// sequence numbers, which is separate from the interp system that sends cl_interp to server.
	return m_flFakeLatency;
}


// Resolver support - when resolver angles change, we need to invalidate cached records
// so they get rebuilt with the new resolver angles
void CLagRecords::ResolverUpdate(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return;
		
	// Clear all cached records for this player
	// They will be rebuilt on the next frame with updated resolver angles
	if (m_LagRecords.contains(pPlayer))
	{
		m_LagRecords[pPlayer].clear();
	}
}
