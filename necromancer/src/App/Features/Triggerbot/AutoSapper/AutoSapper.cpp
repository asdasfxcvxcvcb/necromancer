#include "AutoSapper.h"
#include "../../CFG.h"

// Track last sap time to prevent spam
static float s_flLastSapTime = 0.f;
static int s_nLastSappedBuilding = -1;

bool CAutoSapper::IsHoldingSapper(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	if (!pWeapon)
		return false;

	if (pWeapon->GetWeaponID() != TF_WEAPON_BUILDER)
		return false;

	if (pLocal->m_iClass() != TF_CLASS_SPY)
		return false;

	return true;
}

bool CAutoSapper::CanSapBuilding(C_BaseObject* pBuilding)
{
	if (!pBuilding)
		return false;

	if (pBuilding->m_bHasSapper())
		return false;

	if (pBuilding->m_bPlacing())
		return false;

	if (pBuilding->m_bCarried())
		return false;

	return true;
}

bool CAutoSapper::IsVisible(C_TFPlayer* pLocal, C_BaseObject* pBuilding, const Vec3& vTargetPos)
{
	Vec3 vLocalPos = pLocal->GetShootPos();

	trace_t trace = {};
	CTraceFilterHitscan filter = {};
	filter.m_pIgnore = pLocal;

	H::AimUtils->Trace(vLocalPos, vTargetPos, MASK_SHOT, &filter, &trace);

	return trace.m_pEnt == pBuilding || trace.fraction >= 1.0f;
}

bool CAutoSapper::FindTargets(C_TFPlayer* pLocal)
{
	const Vec3 vLocalPos = pLocal->GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	m_vecTargets.clear();

	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_ENEMIES))
	{
		if (!pEntity)
			continue;

		const auto pBuilding = pEntity->As<C_BaseObject>();

		if (!CanSapBuilding(pBuilding))
			continue;

		Vec3 vPos = pBuilding->GetCenter();
		float flDist = vLocalPos.DistTo(vPos);
		bool bInRange = flDist <= SAPPER_RANGE;
		bool bVisible = IsVisible(pLocal, pBuilding, vPos);

		Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
		float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);

		SapperTarget_t target = {};
		target.Building = pBuilding;
		target.Position = vPos;
		target.AngleTo = vAngleTo;
		target.FOVTo = flFOVTo;
		target.DistanceTo = flDist;
		target.bInRange = bInRange;
		target.bVisible = bVisible;

		m_vecTargets.push_back(target);
	}

	if (m_vecTargets.empty())
		return false;

	// Sort: in range + visible first, then by distance
	std::sort(m_vecTargets.begin(), m_vecTargets.end(), [](const SapperTarget_t& a, const SapperTarget_t& b) {
		int aPriority = (a.bInRange && a.bVisible) ? 2 : (a.bInRange ? 1 : 0);
		int bPriority = (b.bInRange && b.bVisible) ? 2 : (b.bInRange ? 1 : 0);

		if (aPriority != bPriority)
			return aPriority > bPriority;

		return a.DistanceTo < b.DistanceTo;
	});

	return true;
}

void CAutoSapper::Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!CFG::Triggerbot_AutoSapper_Always_On && !CFG::Triggerbot_AutoSapper_Active)
		return;

	if (!IsHoldingSapper(pLocal, pWeapon))
		return;

	// Check cooldown FIRST before doing anything else
	const float flCurTime = I::GlobalVars->curtime;
	if (flCurTime - s_flLastSapTime < 0.09f) // Global cooldown to prevent spam
		return;

	if (!FindTargets(pLocal))
		return;

	// Find best target (in range and visible)
	SapperTarget_t* pBestTarget = nullptr;
	for (auto& target : m_vecTargets)
	{
		if (target.bInRange && target.bVisible)
		{
			pBestTarget = &target;
			break;
		}
	}

	if (!pBestTarget)
		return;

	// Per-building cooldown - don't spam the same building
	const int nBuildingIndex = pBestTarget->Building->entindex();
	if (nBuildingIndex == s_nLastSappedBuilding && flCurTime - s_flLastSapTime < 0.08f)
		return;

	G::nTargetIndex = pBestTarget->Building->entindex();

	// Get angle to target
	Vec3 vAngleTo = I::EngineClient->GetViewAngles();

	// Rage mode: auto-aim to target
	if (CFG::Triggerbot_AutoSapper_Mode == 1)
	{
		vAngleTo = pBestTarget->AngleTo;
	}

	// Legit mode: check if we're looking at the building
	if (CFG::Triggerbot_AutoSapper_Mode == 0)
	{
		Vec3 vForward;
		Math::AngleVectors(vAngleTo, &vForward);
		Vec3 vTraceEnd = pLocal->GetShootPos() + (vForward * SAPPER_RANGE);

		trace_t trace = {};
		CTraceFilterHitscan filter = {};
		filter.m_pIgnore = pLocal;
		H::AimUtils->Trace(pLocal->GetShootPos(), vTraceEnd, MASK_SHOT, &filter, &trace);

		if (trace.m_pEnt != pBestTarget->Building)
			return;
	}

	// NOW apply aim and attack - only when we're actually going to sap
	if (CFG::Triggerbot_AutoSapper_Mode == 1)
	{
		Math::ClampAngles(vAngleTo);
		pCmd->viewangles = vAngleTo;

		if (CFG::Triggerbot_AutoSapper_Aim_Mode == 1)
			G::bPSilentAngles = true;
	}

	pCmd->buttons |= IN_ATTACK;
	s_flLastSapTime = flCurTime;
	s_nLastSappedBuilding = nBuildingIndex;
}

void CAutoSapper::DrawESP()
{
	if (!CFG::Triggerbot_AutoSapper_ESP)
		return;

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || !pLocal->IsAlive())
		return;

	if (pLocal->m_iClass() != TF_CLASS_SPY)
		return;

	const Vec3 vLocalPos = pLocal->GetShootPos();

	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_ENEMIES))
	{
		if (!pEntity)
			continue;

		const auto pBuilding = pEntity->As<C_BaseObject>();

		if (!CanSapBuilding(pBuilding))
			continue;

		Vec3 vBuildingPos = pBuilding->GetCenter();
		float flDist = vLocalPos.DistTo(vBuildingPos);

		// Color based on range
		Color_t color;
		if (flDist <= SAPPER_RANGE)
			color = { 0, 255, 0, 200 }; // Green - in range
		else if (flDist <= SAPPER_RANGE * 1.5f)
			color = { 255, 255, 0, 150 }; // Yellow - close
		else
			color = { 255, 100, 100, 100 }; // Red - out of range

		// Draw range circle around building
		constexpr int nSegments = 32;
		Vec3 vPrevScreen;
		bool bPrevValid = false;

		for (int i = 0; i <= nSegments; i++)
		{
			float flAngle = (float)i / (float)nSegments * 6.28318f;
			Vec3 vWorldPos = vBuildingPos + Vec3(cosf(flAngle) * SAPPER_RANGE, sinf(flAngle) * SAPPER_RANGE, 0);

			Vec3 vScreen;
			if (H::Draw->W2S(vWorldPos, vScreen))
			{
				if (bPrevValid)
				{
					H::Draw->Line(vPrevScreen.x, vPrevScreen.y, vScreen.x, vScreen.y, color);
				}
				vPrevScreen = vScreen;
				bPrevValid = true;
			}
			else
			{
				bPrevValid = false;
			}
		}

		// Draw distance text
		Vec3 vScreen;
		if (H::Draw->W2S(vBuildingPos, vScreen))
		{
			char szDist[32];
			snprintf(szDist, sizeof(szDist), "%.0fu", flDist);
			H::Draw->String(H::Fonts->Get(EFonts::ESP_SMALL), vScreen.x, vScreen.y + 15, color, POS_CENTERX, szDist);

			if (flDist <= SAPPER_RANGE)
			{
				H::Draw->String(H::Fonts->Get(EFonts::ESP), vScreen.x, vScreen.y + 28, { 0, 255, 0, 255 }, POS_CENTERX, "SAP");
			}
		}
	}
}
