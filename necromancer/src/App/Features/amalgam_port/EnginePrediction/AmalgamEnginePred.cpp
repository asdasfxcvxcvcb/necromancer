#include "AmalgamEnginePred.h"

#include "../Ticks/Ticks.h"

// account for interp and origin compression when simulating local player
void CAmalgamEnginePrediction::AdjustPlayers(C_BaseEntity* pLocal)
{
	m_mRestore.clear();

	for (auto pEntity : g_AmalgamEntitiesExt.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer == pLocal || !IsAlive(pPlayer) || IsAGhost(pPlayer))
			continue;

		m_mRestore[pPlayer] = { pPlayer->GetAbsOrigin(), pPlayer->m_vecMins(), pPlayer->m_vecMaxs() };

		pPlayer->SetAbsOrigin(pPlayer->m_vecOrigin());
		pPlayer->m_vecMins() += 0.125f;
		pPlayer->m_vecMaxs() -= 0.125f;
	}
}
void CAmalgamEnginePrediction::RestorePlayers()
{
	for (auto& [pPlayer, tRestore] : m_mRestore)
	{
		pPlayer->SetAbsOrigin(tRestore.m_vOrigin);
		pPlayer->m_vecMins() = tRestore.m_vMins;
		pPlayer->m_vecMaxs() = tRestore.m_vMaxs;
	}
}

void CAmalgamEnginePrediction::Simulate(C_TFPlayer* pLocal, CUserCmd* pCmd)
{
	const int nOldTickBase = pLocal->m_nTickBase();
	const bool bOldIsFirstPrediction = I::Prediction->m_bFirstTimePredicted;
	const bool bOldInPrediction = I::Prediction->m_bInPrediction;

	I::MoveHelper->SetHost(pLocal);
	pLocal->SetCurrentCommand(pCmd);
	*G::RandomSeed() = MD5_PseudoRandom(pCmd->command_number) & std::numeric_limits<int>::max();

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
	*G::RandomSeed() = -1;

	pLocal->m_nTickBase() = nOldTickBase;
	I::Prediction->m_bFirstTimePredicted = bOldIsFirstPrediction;
	I::Prediction->m_bInPrediction = bOldInPrediction;

	m_vOrigin = m_MoveData.m_vecAbsOrigin;
	m_vVelocity = m_MoveData.m_vecVelocity;
	m_vDirection = { m_MoveData.m_flForwardMove, -m_MoveData.m_flSideMove, m_MoveData.m_flUpMove };
	m_vAngles = m_MoveData.m_vecViewAngles;
}



void CAmalgamEnginePrediction::Start(C_TFPlayer* pLocal, CUserCmd* pCmd)
{
	m_bInPrediction = true;
	if (!IsAlive(pLocal))
		return;

	// Use helper functions instead of member functions
	auto pMap = GetPredDescMap(pLocal);
	if (!pMap)
	{
		// If no prediction map, just simulate without state save/restore
		m_nOldTickCount = I::GlobalVars->tickcount;
		m_flOldCurrentTime = I::GlobalVars->curtime;
		m_flOldFrameTime = I::GlobalVars->frametime;

		I::GlobalVars->tickcount = pLocal->m_nTickBase();
		I::GlobalVars->curtime = TICKS_TO_TIME(I::GlobalVars->tickcount);
		I::GlobalVars->frametime = I::Prediction->m_bEnginePaused ? 0.f : TICK_INTERVAL;

		Simulate(pLocal, pCmd);
		return;
	}

	m_nOldTickCount = I::GlobalVars->tickcount;
	m_flOldCurrentTime = I::GlobalVars->curtime;
	m_flOldFrameTime = I::GlobalVars->frametime;

	I::GlobalVars->tickcount = pLocal->m_nTickBase();
	I::GlobalVars->curtime = TICKS_TO_TIME(I::GlobalVars->tickcount);
	I::GlobalVars->frametime = I::Prediction->m_bEnginePaused ? 0.f : TICK_INTERVAL;

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

	CPredictionCopy copy = { PC_EVERYTHING, m_tLocal.m_pData, PC_DATA_PACKED, pLocal, PC_DATA_NORMAL };
	copy.TransferData("EnginePredictionStart", pLocal->entindex(), pMap);

	Simulate(pLocal, pCmd);
}

void CAmalgamEnginePrediction::End(C_TFPlayer* pLocal, CUserCmd* pCmd)
{
	m_bInPrediction = false;
	if (!IsAlive(pLocal))
		return;

	// Use helper function instead of member function
	auto pMap = GetPredDescMap(pLocal);
	if (!pMap)
	{
		// Just restore global vars if no prediction map
		I::GlobalVars->tickcount = m_nOldTickCount;
		I::GlobalVars->curtime = m_flOldCurrentTime;
		I::GlobalVars->frametime = m_flOldFrameTime;
		return;
	}

	I::GlobalVars->tickcount = m_nOldTickCount;
	I::GlobalVars->curtime = m_flOldCurrentTime;
	I::GlobalVars->frametime = m_flOldFrameTime;

	CPredictionCopy copy = { PC_EVERYTHING, pLocal, PC_DATA_NORMAL, m_tLocal.m_pData, PC_DATA_PACKED };
	copy.TransferData("EnginePredictionEnd", pLocal->entindex(), pMap);
}

void CAmalgamEnginePrediction::Unload()
{
	if (m_tLocal.m_pData)
	{
		I::MemAlloc->Free(m_tLocal.m_pData);
		m_tLocal = {};
	}
}
