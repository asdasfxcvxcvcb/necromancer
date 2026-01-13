#include "EnginePrediction.h"

#include "../CFG.h"
#include "../amalgam_port/AmalgamCompat.h"

// Account for interp and origin compression when simulating local player
void CEnginePrediction::AdjustPlayers(C_BaseEntity* pLocal)
{
	m_mRestore.clear();

	const int nMaxClients = I::EngineClient->GetMaxClients();
	for (int i = 1; i <= nMaxClients; i++)
	{
		const auto pEntity = I::ClientEntityList->GetClientEntity(i);
		if (!pEntity || pEntity->GetClassId() != ETFClassIds::CTFPlayer)
			continue;

		const auto pPlayer = pEntity->As<C_TFPlayer>();
		if (pPlayer == pLocal || pPlayer->deadflag())
			continue;

		// Store original values
		const Vec3 vAbsOrigin = pPlayer->GetAbsOrigin();
		const Vec3 vMins = pPlayer->m_vecMins();
		const Vec3 vMaxs = pPlayer->m_vecMaxs();
		
		m_mRestore[pPlayer] = { vAbsOrigin, vMins, vMaxs };

		pPlayer->SetAbsOrigin(pPlayer->m_vecOrigin());
		pPlayer->m_vecMins() = vMins + 0.125f;
		pPlayer->m_vecMaxs() = vMaxs - 0.125f;
	}
}

void CEnginePrediction::RestorePlayers()
{
	for (auto& [pPlayer, tRestore] : m_mRestore)
	{
		pPlayer->SetAbsOrigin(tRestore.m_vOrigin);
		pPlayer->m_vecMins() = tRestore.m_vMins;
		pPlayer->m_vecMaxs() = tRestore.m_vMaxs;
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

	AdjustPlayers(pLocal);
	I::Prediction->SetupMove(pLocal, pCmd, I::MoveHelper, &m_MoveData);
	I::GameMovement->ProcessMovement(pLocal, &m_MoveData);
	I::Prediction->FinishMove(pLocal, pCmd, &m_MoveData);
	RestorePlayers();

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
	if (H::Input->IsDown(CFG::Misc_Edge_Jump_Key))
	{
		if ((flags & FL_ONGROUND) && !(pLocal->m_fFlags() & FL_ONGROUND))
		{
			pCmd->buttons |= IN_JUMP;
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
