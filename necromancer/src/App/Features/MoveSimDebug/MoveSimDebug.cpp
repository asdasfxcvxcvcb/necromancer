#include "MoveSimDebug.h"

#include "../CFG.h"
#include "../Menu/Menu.h"

#include <algorithm>
#include <cfloat>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace
{
	constexpr size_t kMaxRealSamples = 1024;
	constexpr size_t kMaxCaptures = 1024;
	constexpr int kMaxDebugHorizon = 66;
	constexpr int kTrackStaleCmds = 512;
	constexpr int kTrackHoldCmds = 132;
	constexpr const char* DEV_FOLDER = "C:\\necromancer_tf2\\dev";

	const char* ClassName(int iClass)
	{
		switch (iClass)
		{
		case TF_CLASS_SCOUT: return "scout";
		case TF_CLASS_SNIPER: return "sniper";
		case TF_CLASS_SOLDIER: return "soldier";
		case TF_CLASS_DEMOMAN: return "demoman";
		case TF_CLASS_MEDIC: return "medic";
		case TF_CLASS_HEAVYWEAPONS: return "heavy";
		case TF_CLASS_PYRO: return "pyro";
		case TF_CLASS_SPY: return "spy";
		case TF_CLASS_ENGINEER: return "engineer";
		default: return "unknown";
		}
	}
}

void CMoveSimDebug::NotifyProjectileTarget(C_TFPlayer* pPlayer, const std::vector<Vec3>& vecPath, bool bFresh)
{
	m_nNotifyTry++;
	m_bNotifyThisCmd = false;
	m_bFreshNotify = false;
	m_vecNotifyPath.clear();

	if (!pPlayer || pPlayer->deadflag() || vecPath.empty())
		return;

	m_nNotifyOk++;
	m_iNotifyIndex = pPlayer->entindex();
	m_iNotifyClass = pPlayer->m_iClass();
	m_vecNotifyPath = vecPath;
	m_bNotifyThisCmd = true;
	m_bFreshNotify = bFresh;
}

CMoveSimDebug::PlayerTrack_t* CMoveSimDebug::GetTrack(int iEntIndex)
{
	auto it = m_mTracks.find(iEntIndex);
	if (it == m_mTracks.end())
		it = m_mTracks.emplace(iEntIndex, PlayerTrack_t{}).first;
	return &it->second;
}

const MoveSimTickSample_t* CMoveSimDebug::FindRealSample(const PlayerTrack_t& tTrack, int nCmd) const
{
	for (const auto& tSample : tTrack.m_vecReal)
	{
		if (tSample.m_nTick == nCmd)
			return &tSample;
	}
	return nullptr;
}

const MoveSimCapture_t* CMoveSimDebug::FindNewestCompleted(int iPreferredIndex) const
{
	const MoveSimCapture_t* pPreferred = nullptr;
	const MoveSimCapture_t* pAny = nullptr;

	for (const auto& [nIndex, tTrack] : m_mTracks)
	{
		for (const auto& tCap : tTrack.m_vecCaptures)
		{
			if (!tCap.m_bComplete)
				continue;
			if (!pAny || tCap.m_nStartTick > pAny->m_nStartTick)
				pAny = &tCap;
			if (nIndex == iPreferredIndex && (!pPreferred || tCap.m_nStartTick > pPreferred->m_nStartTick))
				pPreferred = &tCap;
		}
	}

	return pPreferred ? pPreferred : pAny;
}

void CMoveSimDebug::Run(CUserCmd* pCmd)
{
	if (!I::EngineClient || !I::EngineClient->IsInGame() || !I::EngineClient->IsConnected() || !I::ClientEntityList)
	{
		m_mTracks.clear();
		m_iNotifyIndex = -1;
		m_bNotifyThisCmd = false;
		m_bFreshNotify = false;
		return;
	}

	if (!CFG::MoveSim_Debug)
	{
		m_mTracks.clear();
		m_iNotifyIndex = -1;
		m_bNotifyThisCmd = false;
		m_bFreshNotify = false;
		return;
	}

	C_TFPlayer* pLocal = H::Entities ? H::Entities->GetLocal() : nullptr;
	if (!pLocal || pLocal->deadflag() || !pCmd || !pCmd->command_number)
		return;

	static bool s_bSaveKeyWasDown = false;
	const bool bSaveKeyDown = CFG::MoveSim_Debug_Key && H::Input->IsDown(static_cast<short>(CFG::MoveSim_Debug_Key));
	const bool bSavePressed = bSaveKeyDown && !s_bSaveKeyWasDown;
	s_bSaveKeyWasDown = bSaveKeyDown;

	const int nNowCmd = pCmd->command_number;

	for (auto it = m_mTracks.begin(); it != m_mTracks.end();)
	{
		if (nNowCmd - it->second.m_nLastSeenCmd > kTrackStaleCmds)
			it = m_mTracks.erase(it);
		else
			++it;
	}

	auto fBuildSample = [](C_TFPlayer* pPlayer, int nCmd) {
		MoveSimTickSample_t tSample;
		tSample.m_nTick = nCmd;
		tSample.m_vOrigin = pPlayer->m_vecOrigin();
		tSample.m_vVelocity = pPlayer->m_vecVelocity();
		tSample.m_nFlags = pPlayer->m_fFlags();
		tSample.m_nButtons = pPlayer->m_nButtons();
		tSample.m_flEyeYaw = pPlayer->GetEyeAngles().y;
		tSample.m_flSimTime = pPlayer->m_flSimulationTime();
		return tSample;
	};

	bool bRecording = false;
	PlayerTrack_t* pTrack = nullptr;
	C_TFPlayer* pTrackedPlayer = nullptr;
	bool bFresh = false;
	std::vector<Vec3> vecNotifyPath;

	if (m_bNotifyThisCmd)
	{
		bFresh = m_bFreshNotify;
		vecNotifyPath = std::move(m_vecNotifyPath);
		m_vecNotifyPath.clear();
		m_bNotifyThisCmd = false;
		m_bFreshNotify = false;

		auto pEntity = I::ClientEntityList->GetClientEntity(m_iNotifyIndex);
		pTrackedPlayer = pEntity ? pEntity->As<C_TFPlayer>() : nullptr;

		if (pTrackedPlayer && !pTrackedPlayer->deadflag() && !pTrackedPlayer->IsDormant() && !vecNotifyPath.empty())
		{
			m_nTrackUntilCmd = nNowCmd + kTrackHoldCmds;
			pTrack = GetTrack(m_iNotifyIndex);
			pTrack->m_nLastSeenCmd = nNowCmd;
			bRecording = true;
		}
	}
	else if (m_iNotifyIndex > 0 && nNowCmd < m_nTrackUntilCmd)
	{
		auto pEntity = I::ClientEntityList->GetClientEntity(m_iNotifyIndex);
		pTrackedPlayer = pEntity ? pEntity->As<C_TFPlayer>() : nullptr;

		if (pTrackedPlayer && !pTrackedPlayer->deadflag() && !pTrackedPlayer->IsDormant())
		{
			pTrack = GetTrack(m_iNotifyIndex);
			pTrack->m_nLastSeenCmd = nNowCmd;
			bRecording = true;
		}
	}

	if (bRecording && pTrack)
	{
		if (pTrack->m_nLastCmd != nNowCmd)
		{
			pTrack->m_nLastCmd = nNowCmd;
			pTrack->m_vecReal.push_front(fBuildSample(pTrackedPlayer, nNowCmd));
			m_nRecordCount++;
			while (pTrack->m_vecReal.size() > kMaxRealSamples)
				pTrack->m_vecReal.pop_back();
		}

		if (bFresh && !vecNotifyPath.empty())
		{
			const MoveSimTickSample_t tStart = pTrack->m_vecReal.empty()
				? fBuildSample(pTrackedPlayer, nNowCmd)
				: pTrack->m_vecReal.front();

			MoveSimCapture_t tCapture;
			tCapture.m_nStartTick = nNowCmd;
			tCapture.m_nHorizon = std::min(std::max(0, static_cast<int>(vecNotifyPath.size()) - 1), kMaxDebugHorizon);
			tCapture.m_iEntIndex = m_iNotifyIndex;
			tCapture.m_iClass = m_iNotifyClass;
			tCapture.m_tStart = tStart;
			tCapture.m_vecPredicted.assign(vecNotifyPath.begin(), vecNotifyPath.begin() + tCapture.m_nHorizon + 1);
			pTrack->m_vecCaptures.push_front(std::move(tCapture));
			while (pTrack->m_vecCaptures.size() > kMaxCaptures)
				pTrack->m_vecCaptures.pop_back();
		}
	}

	for (auto& [nIndex, tTrack] : m_mTracks)
	{
		for (auto& tCap : tTrack.m_vecCaptures)
		{
			if (!tCap.m_bComplete && tCap.m_nHorizon > 0 && tCap.m_nStartTick + tCap.m_nHorizon <= nNowCmd)
				tCap.m_bComplete = true;
		}
	}

	if (bSavePressed)
		TrySave();
}

void CMoveSimDebug::Draw()
{
	if (!CFG::MoveSim_Debug || !I::EngineClient || !I::EngineClient->IsInGame() || !I::ClientEntityList)
		return;

	{
		size_t nCaptures = 0;
		int nComplete = 0;
		for (const auto& [nIndex, tTrack] : m_mTracks)
		{
			nCaptures += tTrack.m_vecCaptures.size();
			for (const auto& tCap : tTrack.m_vecCaptures)
			{
				if (tCap.m_bComplete)
					nComplete++;
			}
		}

		char szStatus[128];
		if (!m_nNotifyTry)
			snprintf(szStatus, sizeof(szStatus), "movesim db: waiting for projectile aimbot target");
		else
			snprintf(szStatus, sizeof(szStatus), "movesim db: aimbot try %d ok %d | samples %d | targets %zu | captures %zu | complete %d",
				m_nNotifyTry, m_nNotifyOk, m_nRecordCount, m_mTracks.size(), nCaptures, nComplete);

		const auto& font = H::Fonts->Get(EFonts::Menu);
		Color_t tStatus = nComplete ? Color_t { 120, 255, 120, 220 } : Color_t { 255, 200, 80, 220 };
		H::Draw->String(font, H::Draw->GetScreenW() / 2, 30, tStatus, POS_CENTERXY, "%s", szStatus);
	}

	auto itTrack = m_mTracks.find(m_iNotifyIndex);
	if (itTrack == m_mTracks.end() || itTrack->second.m_vecReal.size() < 2)
		return;

	const auto pEntity = I::ClientEntityList->GetClientEntity(m_iNotifyIndex);
	if (!pEntity || pEntity->IsDormant())
		return;

	std::vector<const MoveSimTickSample_t*> vecStreak;
	int nPrevTick = 0;
	for (const auto& tSample : itTrack->second.m_vecReal)
	{
		if (!vecStreak.empty() && nPrevTick - tSample.m_nTick > 2)
			break;
		vecStreak.push_back(&tSample);
		nPrevTick = tSample.m_nTick;
		if (vecStreak.size() >= static_cast<size_t>(kMaxDebugHorizon) + 1)
			break;
	}
	if (vecStreak.size() < 2)
		return;

	const Color_t tReal = { 255, 150, 40, 220 };
	for (size_t i = 0; i + 1 < vecStreak.size(); i++)
	{
		Vec3 vFromScreen, vToScreen;
		if (H::Draw->W2S(vecStreak[i]->m_vOrigin, vFromScreen) && H::Draw->W2S(vecStreak[i + 1]->m_vOrigin, vToScreen))
			H::Draw->Line(static_cast<int>(vFromScreen.x), static_cast<int>(vFromScreen.y),
				static_cast<int>(vToScreen.x), static_cast<int>(vToScreen.y), tReal);
	}
}

void CMoveSimDebug::TrySave()
{
	const MoveSimCapture_t* pCompleted = FindNewestCompleted(m_iNotifyIndex);
	if (!pCompleted)
	{
		F::Menu->AddToast("no completed move sim capture yet");
		return;
	}

	const auto itTrack = m_mTracks.find(pCompleted->m_iEntIndex);
	if (itTrack == m_mTracks.end())
	{
		F::Menu->AddToast("no completed move sim capture yet");
		return;
	}

	SaveCapture(*pCompleted, itTrack->second);
}

void CMoveSimDebug::SaveCapture(const MoveSimCapture_t& tCapture, const PlayerTrack_t& tTrack)
{
	std::error_code ec;
	std::filesystem::create_directories(DEV_FOLDER, ec);
	if (ec)
	{
		F::Menu->AddToast("could not create C:\\necromancer_tf2\\dev");
		return;
	}

	nlohmann::json j;
	j["build"] = __DATE__;
	j["map"] = I::EngineClient ? I::EngineClient->GetLevelName() : "";
	j["tickInterval"] = TICK_INTERVAL;
	j["horizon"] = tCapture.m_nHorizon;
	j["class"] = ClassName(tCapture.m_iClass);
	j["entIndex"] = tCapture.m_iEntIndex;
	j["startTick"] = tCapture.m_nStartTick;
	j["start"] = {
		{ "origin", { tCapture.m_tStart.m_vOrigin.x, tCapture.m_tStart.m_vOrigin.y, tCapture.m_tStart.m_vOrigin.z } },
		{ "velocity", { tCapture.m_tStart.m_vVelocity.x, tCapture.m_tStart.m_vVelocity.y, tCapture.m_tStart.m_vVelocity.z } },
		{ "flags", tCapture.m_tStart.m_nFlags },
		{ "buttons", tCapture.m_tStart.m_nButtons },
		{ "eyeYaw", tCapture.m_tStart.m_flEyeYaw },
		{ "simTime", tCapture.m_tStart.m_flSimTime }
	};

	j["predicted"] = nlohmann::json::array();
	for (const auto& v : tCapture.m_vecPredicted)
		j["predicted"].push_back({ v.x, v.y, v.z });

	j["real"] = nlohmann::json::array();
	for (int k = 0; k <= tCapture.m_nHorizon; k++)
	{
		const MoveSimTickSample_t* pSample = nullptr;
		if (k == 0)
			pSample = &tCapture.m_tStart;
		else
			pSample = FindRealSample(tTrack, tCapture.m_nStartTick + k);
		if (!pSample)
		{
			j["real"].push_back(nullptr);
			continue;
		}
		j["real"].push_back({
			{ "tick", pSample->m_nTick },
			{ "origin", { pSample->m_vOrigin.x, pSample->m_vOrigin.y, pSample->m_vOrigin.z } },
			{ "velocity", { pSample->m_vVelocity.x, pSample->m_vVelocity.y, pSample->m_vVelocity.z } },
			{ "flags", pSample->m_nFlags },
			{ "buttons", pSample->m_nButtons },
			{ "eyeYaw", pSample->m_flEyeYaw },
			{ "simTime", pSample->m_flSimTime }
			});
	}

	const std::time_t tNow = std::time(nullptr);
	tm tLocal = {};
	localtime_s(&tLocal, &tNow);

	char szPath[256];
	snprintf(szPath, sizeof(szPath), "%s\\movesim_%s_%04d%02d%02d_%02d%02d%02d.json", DEV_FOLDER,
		ClassName(tCapture.m_iClass), tLocal.tm_year + 1900, tLocal.tm_mon + 1, tLocal.tm_mday,
		tLocal.tm_hour, tLocal.tm_min, tLocal.tm_sec);

	std::ofstream out(szPath);
	if (!out.is_open())
	{
		F::Menu->AddToast("could not write the movesim file");
		return;
	}
	out << j.dump(1);
	out.close();

	F::Menu->AddToast("saved movement sim feedback into C:\\necromancer_tf2\\dev");
}
