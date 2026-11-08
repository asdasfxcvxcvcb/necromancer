#include "EnginePrediction.h"

#include "../CFG.h"
#include "../Misc/Misc.h"
#include "../amalgam_port/AmalgamCompat.h"

// Account for interp and origin compression when simulating local player
void CEnginePrediction::AdjustPlayers(C_BaseEntity* pLocal)
{
	// Clear all restore slots
	for (int i = 0; i < MAX_RESTORE_SLOTS; i++)
		m_mRestore[i].m_bActive = false;

	const int nMaxClients = std::min(I::EngineClient->GetMaxClients(), MAX_RESTORE_SLOTS - 1);
	for (int i = 1; i <= nMaxClients; i++)
	{
		const auto pEntity = I::ClientEntityList->GetClientEntity(i);
		if (!pEntity || pEntity->GetClassId() != ETFClassIds::CTFPlayer)
			continue;

		const auto pPlayer = pEntity->As<C_TFPlayer>();
		if (pPlayer == pLocal || pPlayer->deadflag())
			continue;

		// Store original values indexed by entity index
		const Vec3 vAbsOrigin = pPlayer->GetAbsOrigin();
		const Vec3 vMins = pPlayer->m_vecMins();
		const Vec3 vMaxs = pPlayer->m_vecMaxs();
		
		m_mRestore[i] = { vAbsOrigin, vMins, vMaxs, true };

		pPlayer->SetAbsOrigin(pPlayer->m_vecOrigin());
		pPlayer->m_vecMins() = vMins + 0.125f;
		pPlayer->m_vecMaxs() = vMaxs - 0.125f;
	}
}

void CEnginePrediction::RestorePlayers()
{
	for (int i = 0; i < MAX_RESTORE_SLOTS; i++)
	{
		auto& tRestore = m_mRestore[i];
		if (!tRestore.m_bActive)
			continue;

		// Look up entity by index - no dangling pointer risk
		const auto pEntity = I::ClientEntityList->GetClientEntity(i);
		if (!pEntity)
			continue;

		const auto pPlayer = pEntity->As<C_TFPlayer>();
		if (!pPlayer)
			continue;

		pPlayer->SetAbsOrigin(tRestore.m_vOrigin);
		pPlayer->m_vecMins() = tRestore.m_vMins;
		pPlayer->m_vecMaxs() = tRestore.m_vMaxs;
		tRestore.m_bActive = false;
	}
}

void CEnginePrediction::Simulate(C_TFPlayer* pLocal, CUserCmd* pCmd)
{
	const int nOldTickBase = pLocal->m_nTickBase();
	const bool bOldIsFirstPrediction = I::Prediction->m_bFirstTimePredicted;
	const bool bOldInPrediction = I::Prediction->m_bInPrediction;

	I::MoveHelper->SetHost(pLocal);
	pLocal->SetCurrentCommand(pCmd);
	*SDKUtils::RandomSeed() = MD5_PseudoRandom(pCmd->command_number) & std::numeric_limits<int>::max();

	I::Prediction->m_bFirstTimePredicted = false;
	I::Prediction->m_bInPrediction = true;
	I::Prediction->SetLocalViewAngles(pCmd->viewangles);

	// NOTE: AdjustPlayers/RestorePlayers are NOT called here.
	// They're called in CPrediction_RunSimulation where they belong.
	// Calling SetAbsOrigin here dirties the spatial partition handles,
	// which removes entities from the partition even after RestorePlayers
	// puts positions back. This breaks TraceRay for aimbot traces.

	I::Prediction->SetupMove(pLocal, pCmd, I::MoveHelper, &m_MoveData);
	I::GameMovement->ProcessMovement(pLocal, &m_MoveData);
	I::Prediction->FinishMove(pLocal, pCmd, &m_MoveData);

	I::MoveHelper->SetHost(nullptr);
	pLocal->SetCurrentCommand(nullptr);
	*SDKUtils::RandomSeed() = -1;

	pLocal->m_nTickBase() = nOldTickBase;
	I::Prediction->m_bFirstTimePredicted = bOldIsFirstPrediction;
	I::Prediction->m_bInPrediction = bOldInPrediction;

	m_vOrigin = m_MoveData.m_vecAbsOrigin;
	m_vVelocity = m_MoveData.m_vecVelocity;
	m_vDirection = { m_MoveData.m_flForwardMove, -m_MoveData.m_flSideMove, m_MoveData.m_flUpMove };
	m_vAngles = m_MoveData.m_vecViewAngles;
}

void CEnginePrediction::Start(C_TFPlayer* pLocal, CUserCmd* pCmd)
{
	m_bInPrediction = true;
	if (!pLocal || pLocal->deadflag())
		return;

	auto pMap = GetPredDescMap(pLocal);
	if (!pMap)
		return;

	// Store old flags for edge jump
	flags = pLocal->m_fFlags();

	m_nOldTickCount = I::GlobalVars->tickcount;
	m_flOldCurrentTime = I::GlobalVars->curtime;
	m_flOldFrameTime = I::GlobalVars->frametime;

	I::GlobalVars->tickcount = pLocal->m_nTickBase();
	I::GlobalVars->curtime = TICKS_TO_TIME(I::GlobalVars->tickcount);
	I::GlobalVars->frametime = I::Prediction->m_bEnginePaused ? 0.f : TICK_INTERVAL;

	// Allocate or reallocate datamap storage
	size_t iSize = GetIntermediateDataSize(pLocal);
	if (!m_tLocal.m_pData) 
	{
		m_tLocal.m_pData = reinterpret_cast<byte*>(I::MemAlloc->Alloc(iSize));
		m_tLocal.m_iSize = iSize;
	}
	else if (m_tLocal.m_iSize != iSize)
	{
		m_tLocal.m_pData = reinterpret_cast<byte*>(I::MemAlloc->Realloc(m_tLocal.m_pData, iSize));
		m_tLocal.m_iSize = iSize;
	}

	// Save current state
	CPredictionCopy copy = { PC_EVERYTHING, m_tLocal.m_pData, PC_DATA_PACKED, pLocal, PC_DATA_NORMAL };
	copy.TransferData("EnginePredictionStart", pLocal->entindex(), pMap);

	Simulate(pLocal, pCmd);
	
	// Edge jump
	if (CFG::Misc_EdgeJump || H::Input->IsDown(CFG::Misc_Edge_Jump_Key))
	{
		if ((flags & FL_ONGROUND) && !(pLocal->m_fFlags() & FL_ONGROUND) && !(pCmd->buttons & IN_JUMP))
		{
			pCmd->buttons |= IN_JUMP;
			F::Misc->m_flLastEdgeJump = I::EngineClient->Time();
		}
	}
}

void CEnginePrediction::End(C_TFPlayer* pLocal, CUserCmd* pCmd)
{
	m_bInPrediction = false;
	if (!pLocal || pLocal->deadflag())
		return;

	auto pMap = GetPredDescMap(pLocal);
	if (!pMap)
		return;

	I::GlobalVars->tickcount = m_nOldTickCount;
	I::GlobalVars->curtime = m_flOldCurrentTime;
	I::GlobalVars->frametime = m_flOldFrameTime;

	// Restore saved state
	CPredictionCopy copy = { PC_EVERYTHING, pLocal, PC_DATA_NORMAL, m_tLocal.m_pData, PC_DATA_PACKED };
	copy.TransferData("EnginePredictionEnd", pLocal->entindex(), pMap);
}

void CEnginePrediction::Unload()
{
	if (m_tLocal.m_pData)
	{
		I::MemAlloc->Free(m_tLocal.m_pData);
		m_tLocal = {};
	}
}
