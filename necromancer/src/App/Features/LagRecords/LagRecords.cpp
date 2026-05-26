#include "LagRecords.h"

#include "../CFG.h"
#include "../Misc/AntiCheatCompat/AntiCheatCompat.h"

bool CLagRecords::IsSimulationTimeValid(float flCurSimTime, float flCmprSimTime)
{
	// SEOwnedDE uses a flat 0.2f for record retention — keeps ~13 records per player.
	// With fake latency, extend by fake_latency amount so backtrack records are retained.
	const float flMaxTime = m_flFakeLatency > 0.0f
		? std::min(m_flFakeLatency + 0.2f, m_flMaxUnlag)
		: 0.2f;
	return (flCurSimTime - flCmprSimTime) < flMaxTime;
}

float CLagRecords::GetServerAcceptedAge() const
{
	// Cache per frame — this is called per-record in every aimbot, avoid repeated NetChannel queries
	const int nFrame = I::GlobalVars ? I::GlobalVars->framecount : 0;
	if (m_nCacheFrame == nFrame && m_flCachedServerAcceptedAge >= 0.0f)
		return m_flCachedServerAcceptedAge;

	// Server's StartLagCompensation (Source SDK 2013):
	//   correct = nci->GetLatency(FLOW_OUTGOING) + lerpTime
	//   targettick = cmd->tick_count - TIME_TO_TICKS(lerpTime)
	//   deltaTime = correct - TICKS_TO_TIME(gpGlobals->tickcount - targettick)
	//   if |deltaTime| > 0.2s: force targettick
	//
	// Server's nci->GetLatency(FLOW_OUTGOING) = latency from client TO server
	//   = our outgoing latency (NOT our incoming — FLOW_OUTGOING on the server
	//   measures how long it takes for data to flow FROM the client TO the server)
	//
	// With fake latency, we rewind m_nInSequenceNr, making the server measure
	// our outgoing latency as (real_out + fake_latency).
	//
	// We send: tick_count = TIME_TO_TICKS(simTime + lerp)
	// Server subtracts lerp: targettick ≈ TIME_TO_TICKS(simTime)
	//
	// Server computes actual = serverCurTime - simTime
	// From client: serverCurTime ≈ curtime + real_outgoing
	// So actual = (curtime - simTime) + real_outgoing = clientAge + real_out
	//
	// correct = (real_out + fake_latency) + lerp
	//
	// Validation: |correct - actual| = |(real_out + fake_latency + lerp) - (clientAge + real_out)|
	//           = |fake_latency + lerp - clientAge|
	// Therefore: ideal clientAge = fake_latency + lerp
	//
	// Without fake latency: ideal clientAge = lerp (records ~15ms old = current model)
	// With fake latency: ideal clientAge = fake_latency + lerp (backtrack records)
	const float flLerp = SDKUtils::GetLerp();
	const float flFakeLatency = m_flFakeLatency;
	m_flCachedServerAcceptedAge = flFakeLatency + flLerp;
	m_nCacheFrame = nFrame;
	return m_flCachedServerAcceptedAge;
}

float CLagRecords::GetOutgoingLatency() const
{
	if (const auto pNetChan = I::EngineClient->GetNetChannelInfo())
		return pNetChan->GetLatency(FLOW_OUTGOING);
	return 0.0f;
}

float CLagRecords::GetServerAcceptedAgeRange() const
{
	// Cache per frame — same pattern as GetServerAcceptedAge
	const int nFrame = I::GlobalVars ? I::GlobalVars->framecount : 0;
	if (m_nCacheFrame == nFrame && m_flCachedAgeRange >= 0.0f)
		return m_flCachedAgeRange;

	// Dynamic window based on REAL outgoing latency (not fake latency).
	// Lower ping → wider window (server is more lenient with low-ping players).
	// Higher ping → tighter window (server's lag compensation is less forgiving).
	const float flRealPing = GetOutgoingLatency() * 1000.0f;  // Convert to ms

	float flRange;
	if (flRealPing <= 89.0f)
		flRange = 0.185f;     // 1-89ms ping → 185ms window
	else if (flRealPing <= 150.0f)
		flRange = 0.180f;     // 90-150ms ping → 180ms window
	else if (flRealPing <= 200.0f)
		flRange = 0.175f;     // 151-200ms ping → 175ms window
	else if (flRealPing <= 220.0f)
		flRange = 0.160f;     // 201-220ms ping → 160ms window
	else
		flRange = 0.130f;     // 221ms+ ping → 130ms window

	m_flCachedAgeRange = flRange;
	return m_flCachedAgeRange;
}

bool CLagRecords::IsRecordAgeValidForServer(float flRecordAge) const
{
	// Server validation: |correct - deltaTime| <= sv_maxunlag
	// where correct = outgoing_latency + lerp, deltaTime = curtime - simTime
	// This simplifies to: |outgoing_latency + lerp - clientAge| <= sv_maxunlag
	// The server's actual window is sv_maxunlag (default 1.0s), NOT the 0.2s we previously used.
	// Our old dynamic range (0.13-0.235s) was 4x more restrictive than the server,
	// filtering out records the server would accept — causing missed shots.
	const float flIdealAge = GetServerAcceptedAge();

	// Hard bounds: record must be non-negative and within sv_maxunlag
	if (flRecordAge > m_flMaxUnlag || flRecordAge < 0.0f)
		return false;

	// Server's actual check: |idealAge - clientAge| <= sv_maxunlag
	return fabsf(flRecordAge - flIdealAge) <= m_flMaxUnlag;
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

// SEH-safe bone setup helpers — entity internal state can be corrupted on
// high-player-count servers (40+ players), causing null pointer dereferences
// (RCX=0x0) inside InvalidateBoneCache/SetupBones. These helpers catch the
// access violation and skip the record instead of crashing the game.
// Must be plain functions (no C++ objects with destructors) for __try/__except.

static bool SafeInvalidateAndSetupBones(C_TFPlayer* pPlayer, bool bInvalidate, matrix3x4_t* pBoneMatrix, int nMaxBones, int nBoneMask, float flCurTime)
{
	__try {
		if (bInvalidate)
			pPlayer->InvalidateBoneCache();
		return pPlayer->SetupBones(pBoneMatrix, nMaxBones, nBoneMask, flCurTime) != 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static void SafeSetupAttachmentBones(C_BaseEntity* pAttach, float flCurTime)
{
	__try {
		pAttach->InvalidateBoneCache();
		pAttach->SetupBones(nullptr, -1, BONE_USED_BY_ANYTHING, flCurTime);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		// Silently skip corrupted attachments
	}
}

static bool SafeSetupBones(C_TFPlayer* pPlayer, matrix3x4_t* pBoneMatrix, int nMaxBones, int nBoneMask, float flCurTime)
{
	__try {
		return pPlayer->SetupBones(pBoneMatrix, nMaxBones, nBoneMask, flCurTime) != 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// SEH-safe entity validation — checks that the entity's internal state is safe
// for SetupBones. Must be a plain function (no C++ objects with destructors)
// because __try/__except can't coexist with object unwinding (C2712).
// Returns true if the entity is valid and ready for bone setup.
static bool SafeValidatePlayerForBones(C_TFPlayer* pPlayer)
{
	__try {
		if (!pPlayer->GetClientNetworkable() || !pPlayer->GetClientNetworkable()->GetClientClass())
			return false;

		// Validate the renderable interface — SetupBones is dispatched through
		// IClientRenderable. If the renderable is null or its vtable is corrupted,
		// SetupBones will crash with RCX=0x0.
		if (!pPlayer->GetClientRenderable())
			return false;

		// Check if player has a valid model
		const auto pModel = pPlayer->GetModel();
		if (!pModel)
			return false;

		// Validate the studio header is loaded — on high-player-count servers, entities
		// can have a model pointer but the studio data isn't loaded yet.
		const auto pStudioHdr = I::ModelInfoClient->GetStudiomodel(pModel);
		if (!pStudioHdr || pStudioHdr->numbones <= 0)
			return false;

		// Early death detection — skip SetupBones on dying/dead players.
		if (pPlayer->m_lifeState() != LIFE_ALIVE || pPlayer->m_iHealth() <= 0)
			return false;

		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void CLagRecords::AddRecord(C_TFPlayer* pPlayer, int nKnownIndex)
{
	// Validate player before doing anything
	if (!pPlayer)
		return;

	// Track entity index for crash context
	F::CrashHandler->s_Context.m_nEntityIndex = nKnownIndex;
	F::CrashHandler->s_Context.m_pszLastSubFeature = "SafeIsEntityValid";

	// Use SafeIsEntityValid with known index — avoids calling entindex() (virtual)
	// on a potentially corrupted entity. On high-player-count servers (40+), entity
	// slots are recycled rapidly and the pointer can become dangling between the
	// FrameStageNotify loop check and this function.
	if (nKnownIndex > 0 && !H::Entities->SafeIsEntityValid(pPlayer, nKnownIndex))
		return;

	// SEH-safe validation of entity internal state — virtual calls on a partially
	// destroyed entity can crash (RCX=0x0). SafeValidatePlayerForBones catches
	// any ACCESS_VIOLATION during validation and returns false.
	F::CrashHandler->s_Context.m_pszLastSubFeature = "ValidateEntity";
	if (!SafeValidatePlayerForBones(pPlayer))
		return;

	LagRecord_t newRecord = {};

	m_bSettingUpBones = true;

	const bool bSetupBonesOpt = CFG::Misc_SetupBones_Optimization;

	// BONE_USED_BY_ANYTHING — must use full bone mask, not BONE_USED_BY_HITBOX.
	// TF2's procedural bone controllers (IK, look-at) depend on bones outside the hitbox set.
	// BONE_USED_BY_HITBOX skips these controllers, producing slightly different hitbox bone positions.
	F::CrashHandler->s_Context.m_pszLastSubFeature = "SetupBones";
	const bool bResult = SafeInvalidateAndSetupBones(pPlayer, bSetupBonesOpt, newRecord.BoneMatrix, 128, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime);

	if (bSetupBonesOpt && bResult)
	{
		// Re-setup attachment bones after player bone invalidation — without this,
		// attachments (hats, weapons) render at stale positions for one frame because
		// their bone cache still references the old player bone transforms.
		F::CrashHandler->s_Context.m_pszLastSubFeature = "AttachmentBones";
		auto attach = pPlayer->FirstMoveChild();
		while (attach)
		{
			if (attach->ShouldDraw())
				SafeSetupAttachmentBones(attach, I::GlobalVars->curtime);

			attach = attach->NextMovePeer();
		}
	}

	m_bSettingUpBones = false;

	if (!bResult)
		return;

	F::CrashHandler->s_Context.m_pszLastSubFeature = "RecordData";

	newRecord.Player = pPlayer;
	newRecord.PlayerIndex = pPlayer->entindex();
	newRecord.SimulationTime = pPlayer->m_flSimulationTime();
	newRecord.AbsOrigin = pPlayer->GetAbsOrigin();
	newRecord.VecOrigin = pPlayer->m_vecOrigin();
	newRecord.AbsAngles = pPlayer->GetAbsAngles();
	newRecord.EyeAngles = pPlayer->GetEyeAngles();
	newRecord.Velocity = pPlayer->m_vecVelocity();
	newRecord.Center = pPlayer->GetCenter();
	newRecord.Flags = pPlayer->m_fFlags();
	newRecord.VecMinsPreScaled = pPlayer->m_vecMinsPreScaled();  // Server stores OBBMinsPreScaled
	newRecord.VecMaxsPreScaled = pPlayer->m_vecMaxsPreScaled();  // Server stores OBBMaxsPreScaled
	newRecord.bIsAlive = !pPlayer->deadflag();           // Server checks LC_ALIVE

	if (const auto pAnimState = pPlayer->GetAnimState())
		newRecord.FeetYaw = pAnimState->m_flCurrentFeetYaw;

	const int nIdx = pPlayer->entindex();
	if (nIdx < 0 || nIdx >= MAX_LAG_RECORDS_SLOTS)
		return;

	auto& records = m_LagRecords[nIdx];

	// Teleport dist check (from Amalgam) — detect when player teleported
	// Server uses sv_lagcompensation_teleport_dist to invalidate old positions
	bool bLagComp = false;
	if (!records.empty())
	{
		const LagRecord_t& lastRecord = records.front(); // newest existing record
		const Vec3 vDelta = newRecord.VecOrigin - lastRecord.VecOrigin;

		static auto sv_lagcompensation_teleport_dist = I::CVar->FindVar("sv_lagcompensation_teleport_dist");
		const float flTeleportDist = sv_lagcompensation_teleport_dist ? powf(sv_lagcompensation_teleport_dist->GetFloat(), 2.f) : powf(64.f, 2.f);

		if (vDelta.Length2DSqr() > flTeleportDist)
		{
			bLagComp = true;
			// If this is the first teleport detection, resize to keep only the last record
			if (!m_bLagCompensation[nIdx])
				records.resize(1);
			// Mark all older records as invalid
			for (auto& record : records)
				record.bInvalid = true;
		}

		// Fix invalid records — copy current record's data to them (Amalgam approach)
		for (auto& record : records)
		{
			if (!record.bInvalid)
				continue;

			record.VecOrigin = newRecord.VecOrigin;
			record.AbsOrigin = newRecord.AbsOrigin;
			record.VecMinsPreScaled = newRecord.VecMinsPreScaled;
			record.VecMaxsPreScaled = newRecord.VecMaxsPreScaled;
			memcpy(record.BoneMatrix, newRecord.BoneMatrix, sizeof(record.BoneMatrix));
		}
	}

	m_bLagCompensation[nIdx] = bLagComp;

	records.emplace_front(newRecord);
}

const LagRecord_t* CLagRecords::GetRecord(C_TFPlayer* pPlayer, int nRecord, bool bSafe)
{
	// Always validate the player pointer - it could be dangling after class change
	if (!pPlayer || !H::Entities->IsEntityValid(pPlayer))
		return nullptr;

	const int nIdx = pPlayer->entindex();
	if (nIdx < 0 || nIdx >= MAX_LAG_RECORDS_SLOTS)
		return nullptr;

	auto& records = m_LagRecords[nIdx];
	if (nRecord < 0 || nRecord >= static_cast<int>(records.size()))
		return nullptr;

	return &records[nRecord];
}

bool CLagRecords::HasRecords(C_TFPlayer* pPlayer, int* pTotalRecords)
{
	if (!pPlayer)
		return false;

	const int nIdx = pPlayer->entindex();
	if (nIdx < 0 || nIdx >= MAX_LAG_RECORDS_SLOTS)
		return false;

	const size_t nSize = m_LagRecords[nIdx].size();
	if (nSize == 0)
		return false;

	if (pTotalRecords)
		*pTotalRecords = static_cast<int>(nSize - 1);

	return true;
}

void CLagRecords::UpdateRecords()
{
	const auto pLocal = H::Entities->GetLocal();

	if (!pLocal || pLocal->deadflag() || pLocal->InCond(TF_COND_HALLOWEEN_GHOST_MODE) || pLocal->InCond(TF_COND_HALLOWEEN_KART))
	{
		// Clear all records when dead/disconnected
		for (int i = 0; i < MAX_LAG_RECORDS_SLOTS; i++)
			m_LagRecords[i].clear();
		return;
	}

	// Iterate flat array by entity index - much faster than unordered_map iteration
	const int nMaxClients = std::min(I::EngineClient->GetMaxClients(), MAX_LAG_RECORDS_SLOTS - 1);
	for (int i = 1; i <= nMaxClients; i++)
	{
		auto& records = m_LagRecords[i];
		if (records.empty())
			continue;

		// Validate the player at this entity index is still alive and matches our records
		const auto pEntity = I::ClientEntityList->GetClientEntity(i);
		if (!pEntity || pEntity->GetClassId() != ETFClassIds::CTFPlayer)
		{
			records.clear();
			continue;
		}

		const auto pPlayer = pEntity->As<C_TFPlayer>();
		if (pPlayer->deadflag())
		{
			records.clear();
			continue;
		}

		// Remove invalid records - optimized for ordered deque (newest first)
		// Records are added with emplace_front(), so they're ordered by simtime (descending)
		// Once we find an invalid record, all older records are also invalid
		const float flCurSimTime = pPlayer->m_flSimulationTime();
		
		// Find first invalid record (all records after it are also invalid)
		size_t nFirstInvalid = records.size();
		for (size_t j = 0; j < records.size(); j++)
		{
			if (!IsSimulationTimeValid(flCurSimTime, records[j].SimulationTime))
			{
				nFirstInvalid = j;
				break;
			}
		}
		
		// Erase tail in one operation (much faster than std::erase_if)
		if (nFirstInvalid < records.size())
			records.erase(records.begin() + nFirstInvalid, records.end());
	}
}

bool CLagRecords::DiffersFromCurrent(const LagRecord_t* pRecord)
{
	if (!pRecord)
		return false;

	const auto pPlayer = pRecord->Player;

	// Validate player pointer using stored index — avoids virtual call on dangling pointer
	if (!pPlayer)
		return false;
	if (pRecord->PlayerIndex > 0)
	{
		const auto pCheck = I::ClientEntityList->GetClientEntity(pRecord->PlayerIndex);
		if (pCheck != pPlayer)
			return false;
	}
	else
		return false;

	if ((pPlayer->m_vecOrigin() - pRecord->AbsOrigin).Length2DSqr() > 0.1f)
		return true;

	if ((pPlayer->GetEyeAngles() - pRecord->EyeAngles).Length() > 0.1f)
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

LagRecord_t CLagRecords::GetInterpolatedRecord(C_TFPlayer* pPlayer, float flTargetSimTime, bool* pOutIsInterpolated)
{
	if (pOutIsInterpolated)
		*pOutIsInterpolated = false;

	int numRecords = 0;
	if (!HasRecords(pPlayer, &numRecords))
	{
		// No records — return current position as a record
		// But don't SetupBones on dead/dying players (expensive + causes lag spikes)
		LagRecord_t current = {};
		current.Player = pPlayer;
		current.PlayerIndex = pPlayer->entindex();
		current.SimulationTime = pPlayer->m_flSimulationTime();
		current.AbsOrigin = pPlayer->GetAbsOrigin();
		current.VecMinsPreScaled = pPlayer->m_vecMinsPreScaled();
		current.VecMaxsPreScaled = pPlayer->m_vecMaxsPreScaled();
		current.EyeAngles = pPlayer->GetEyeAngles();
		current.AbsAngles = pPlayer->GetAbsAngles();
		current.bIsAlive = !pPlayer->deadflag();
		current.VecOrigin = pPlayer->m_vecOrigin();
		current.Velocity = pPlayer->m_vecVelocity();
		current.Flags = pPlayer->m_fFlags();
		if (pPlayer->m_lifeState() == LIFE_ALIVE && pPlayer->m_iHealth() > 0)
			SafeSetupBones(pPlayer, current.BoneMatrix, 128, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime);
		return current;
	}

	// Our deque: index 0 = newest (highest simtime), index N = oldest (lowest simtime)
	const LagRecord_t* pOlder = nullptr;
	const LagRecord_t* pNewer = nullptr;

	for (int n = 1; n <= numRecords; n++)
	{
		const auto record = GetRecord(pPlayer, n, true);
		if (!record || !record->bIsAlive)
			continue;

		if (record->SimulationTime <= flTargetSimTime)
		{
			pOlder = record;
			if (n > 1)
			{
				const auto prevRecord = GetRecord(pPlayer, n - 1, true);
				if (prevRecord && prevRecord->bIsAlive && prevRecord->SimulationTime > flTargetSimTime)
					pNewer = prevRecord;
			}
			break;
		}
		pNewer = record;
	}

	if (!pOlder)
	{
		const auto oldest = GetRecord(pPlayer, numRecords, true);
		if (oldest) pOlder = oldest;
	}
	if (!pNewer)
	{
		const auto newest = GetRecord(pPlayer, 1, true);
		if (newest) pNewer = newest;
	}

	if (!pOlder || !pNewer || pOlder == pNewer)
	{
		if (pOlder) return *pOlder;
		if (pNewer) return *pNewer;
		LagRecord_t current = {};
		current.Player = pPlayer;
		current.PlayerIndex = pPlayer->entindex();
		current.SimulationTime = pPlayer->m_flSimulationTime();
		current.AbsOrigin = pPlayer->GetAbsOrigin();
		current.VecMinsPreScaled = pPlayer->m_vecMinsPreScaled();
		current.VecMaxsPreScaled = pPlayer->m_vecMaxsPreScaled();
		current.EyeAngles = pPlayer->GetEyeAngles();
		current.AbsAngles = pPlayer->GetAbsAngles();
		current.bIsAlive = !pPlayer->deadflag();
		current.VecOrigin = pPlayer->m_vecOrigin();
		current.Velocity = pPlayer->m_vecVelocity();
		current.Flags = pPlayer->m_fFlags();
		SafeSetupBones(pPlayer, current.BoneMatrix, 128, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime);
		return current;
	}

	const float flTimeSpan = pNewer->SimulationTime - pOlder->SimulationTime;
	if (flTimeSpan <= 0.001f)
		return *pOlder;

	float flFrac = (flTargetSimTime - pOlder->SimulationTime) / flTimeSpan;
	flFrac = std::clamp(flFrac, 0.0f, 1.0f);

	if (pOutIsInterpolated)
		*pOutIsInterpolated = (flFrac > 0.001f && flFrac < 0.999f);

	return InterpolateRecords(pOlder, pNewer, flFrac);
}

void CLagRecordMatrixHelper::Set(const LagRecord_t* pRecord)
{
	if (!pRecord)
		return;

	const auto pPlayer = pRecord->Player;

	// Validate the player pointer using stored index — avoids virtual call on dangling pointer
	if (!pPlayer)
		return;
	if (pRecord->PlayerIndex > 0)
	{
		const auto pCheck = I::ClientEntityList->GetClientEntity(pRecord->PlayerIndex);
		if (pCheck != pPlayer)
			return;
	}
	else
		return;

	if (pPlayer->deadflag())
		return;

	// Server checks LC_ALIVE - dead records are never valid for lag compensation
	if (!pRecord->bIsAlive)
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

	// Server restores collision bounds (LC_SIZE_CHANGED) during lag compensation
	// Server calls SetSize(minsPreScaled, maxsPreScaled) which sets both pre-scaled and actual bounds
	// The trace system uses m_vecMins/m_vecMaxs for collision detection
	m_vRestoreMinsPreScaled = pPlayer->m_vecMinsPreScaled();
	m_vRestoreMaxsPreScaled = pPlayer->m_vecMaxsPreScaled();
	m_vRestoreMins = pPlayer->m_vecMins();
	m_vRestoreMaxs = pPlayer->m_vecMaxs();
	pPlayer->m_vecMinsPreScaled() = pRecord->VecMinsPreScaled;
	pPlayer->m_vecMaxsPreScaled() = pRecord->VecMaxsPreScaled;
	pPlayer->m_vecMins() = pRecord->VecMinsPreScaled;
	pPlayer->m_vecMaxs() = pRecord->VecMaxsPreScaled;

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
		m_vRestoreMinsPreScaled = {};
		m_vRestoreMaxsPreScaled = {};
		m_vRestoreMins = {};
		m_vRestoreMaxs = {};
		m_bSuccessfullyStored = false;
		return;
	}

	m_pPlayer->SetAbsOrigin(m_vAbsOrigin);
	m_pPlayer->SetAbsAngles(m_vAbsAngles);
	
	// Restore collision bounds (matching server's FinishLagCompensation LC_SIZE_CHANGED)
	m_pPlayer->m_vecMinsPreScaled() = m_vRestoreMinsPreScaled;
	m_pPlayer->m_vecMaxsPreScaled() = m_vRestoreMaxsPreScaled;
	m_pPlayer->m_vecMins() = m_vRestoreMins;
	m_pPlayer->m_vecMaxs() = m_vRestoreMaxs;

	const int nBoneCount = std::min(pCachedBoneData->Count(), 128);
	memcpy(pCachedBoneData->Base(), m_BoneMatrix, sizeof(matrix3x4_t) * nBoneCount);

	m_pPlayer = nullptr;
	m_vAbsOrigin = {};
	m_vAbsAngles = {};
	m_vRestoreMinsPreScaled = {};
	m_vRestoreMaxsPreScaled = {};
	m_vRestoreMins = {};
	m_vRestoreMaxs = {};
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
