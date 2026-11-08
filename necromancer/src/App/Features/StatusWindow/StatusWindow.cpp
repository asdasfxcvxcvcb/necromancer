#include "StatusWindow.h"

#include "../CFG.h"
#include "../Menu/Menu.h"
#include "../VisualUtils/VisualUtils.h"
#include "../FakeLag/FakeLag.h"
#include "../FakeAngle/FakeAngle.h"
#include "../SeedPred/SeedPred.h"
#include "../Triggerbot/AutoAirblast/AutoAirblast.h"
#include "../Triggerbot/AutoBackstab/AutoBackstab.h"
#include "../Triggerbot/AutoDetonate/AutoDetonate.h"
#include "../Triggerbot/AutoVaccinator/AutoVaccinator.h"
#include "../Misc/Misc.h"
#include "../NavBot/NavBot.h"
#include "../Crits/Crits.h"

#include <cmath>
#include <cstring>
#include <cstdlib>

constexpr float ACTION_LATCH_TIME = 0.75f;
constexpr float MULTIKILL_WINDOW = 3.0f;
constexpr float HEALKILL_WINDOW = 4.0f;
constexpr float SPREE_TIME = 2.8f;
constexpr float CHAOS_TIME = 4.0f;
constexpr float CHAOS_WINDOW = 1.0f;

static Color_t HueColor(float flPhase, byte nAlpha)
{
	const int r = static_cast<int>(std::cos(flPhase) * 127.5f + 127.5f);
	const int g = static_cast<int>(std::cos(flPhase + 2.0943951f) * 127.5f + 127.5f);
	const int b = static_cast<int>(std::cos(flPhase + 4.1887902f) * 127.5f + 127.5f);
	return Color_t{ static_cast<byte>(r), static_cast<byte>(g), static_cast<byte>(b), nAlpha };
}

static void DrawMuzzleStar(int x, int y, int nSize, Color_t tCore, Color_t tOuter)
{
	H::Draw->FilledTriangle({ Vec2(x - nSize, y), Vec2(x, y - 3), Vec2(x, y + 3) }, tOuter);
	H::Draw->FilledTriangle({ Vec2(x + nSize, y), Vec2(x, y - 3), Vec2(x, y + 3) }, tOuter);
	H::Draw->FilledTriangle({ Vec2(x, y - nSize), Vec2(x - 3, y), Vec2(x + 3, y) }, tOuter);
	H::Draw->FilledTriangle({ Vec2(x, y + nSize), Vec2(x - 3, y), Vec2(x + 3, y) }, tOuter);
	H::Draw->FilledCircle(x, y, std::max(3, nSize / 3), 10, tCore);
}

static void DrawExplosion(int x, int y, float t, byte nAlpha)
{
	if (t < 0.18f)
	{
		const float flP = t / 0.18f;
		H::Draw->FilledCircle(x, y, static_cast<int>(6 + flP * 10.0f), 14, Color_t{ 255, 245, 200, nAlpha });
		H::Draw->FilledCircle(x, y, static_cast<int>(3 + flP * 5.0f), 10, Color_t{ 255, 255, 255, nAlpha });
	}
	else if (t < 0.65f)
	{
		const float flP = (t - 0.18f) / 0.47f;
		const int nR = static_cast<int>(16.0f - flP * 4.0f);
		H::Draw->FilledCircle(x, y, nR, 14, Color_t{ 255, 130, 40, static_cast<byte>(nAlpha * (1.0f - flP * 0.4f)) });
		H::Draw->FilledCircle(x, y, static_cast<int>(nR * 0.5f), 10, Color_t{ 255, 220, 120, static_cast<byte>(nAlpha * (1.0f - flP * 0.5f)) });
		for (int n = 0; n < 7; n++)
		{
			const float flAng = (6.2831853f / 7.0f) * n + 0.3f;
			const int nLen = static_cast<int>(10.0f + flP * 16.0f);
			H::Draw->Line(x, y, x + static_cast<int>(std::cos(flAng) * nLen), y + static_cast<int>(std::sin(flAng) * nLen), Color_t{ 255, 200, 80, static_cast<byte>(nAlpha * (1.0f - flP)) });
		}
	}
	else
	{
		const float flP = std::min((t - 0.65f) / 0.5f, 1.0f);
		for (int n = 0; n < 3; n++)
		{
			const int nSX = x + (n - 1) * 9 + static_cast<int>(flP * 4.0f * ((n % 2) ? 1 : -1));
			const int nSY = y - static_cast<int>(flP * 16.0f) - n * 2;
			H::Draw->FilledCircle(nSX, nSY, 4 + n + static_cast<int>(flP * 4.0f), 10, Color_t{ 120, 120, 125, static_cast<byte>(nAlpha * 0.5f * (1.0f - flP)) });
		}
	}
}
struct StatusEntry_t
{
	const char* m_pszName;
	char m_szStatus[32];
	Color_t m_tColor;
	bool m_bRainbow = false;
};

void CStatusWindow::Drag()
{
	const int nMouseX = H::Input->GetMouseX();
	const int nMouseY = H::Input->GetMouseY();

	static bool bDragging = false;

	if (!bDragging && F::Menu->IsMenuWindowHovered())
		return;

	static int nDeltaX = 0;
	static int nDeltaY = 0;

	const int nWindowX = CFG::Visuals_StatusWindow_Pos_X;
	const int nWindowY = CFG::Visuals_StatusWindow_Pos_Y;

	const bool bHovered = nMouseX > nWindowX && nMouseX < nWindowX + CFG::Visuals_StatusWindow_Width
		&& nMouseY > nWindowY && nMouseY < nWindowY + CFG::Menu_Drag_Bar_Height;

	if (bHovered && H::Input->IsPressed(VK_LBUTTON))
	{
		nDeltaX = nMouseX - nWindowX;
		nDeltaY = nMouseY - nWindowY;
		bDragging = true;
	}

	if (!H::Input->IsPressed(VK_LBUTTON) && !H::Input->IsHeld(VK_LBUTTON))
		bDragging = false;

	if (bDragging)
	{
		CFG::Visuals_StatusWindow_Pos_X = nMouseX - nDeltaX;
		CFG::Visuals_StatusWindow_Pos_Y = nMouseY - nDeltaY;
	}
}

void CStatusWindow::Run()
{
	if (!CFG::Visuals_StatusWindow_Active)
		return;

	if (CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot())
		return;

	if (!F::Menu->IsOpen() && (I::EngineVGui->IsGameUIVisible() || SDKUtils::BInEndOfMatch()))
		return;

	if (F::Menu->IsOpen())
		Drag();

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pLocal->deadflag())
		return;

	const auto pWeapon = H::Entities->GetWeapon();
	const float flNow = I::EngineClient->Time();

	const auto tGray = CFG::Menu_Text_Inactive;
	const auto tWhite = CFG::Menu_Text;
	const Color_t tGreen = { 32, 255, 64, 255 };
	const Color_t tYellow = { 255, 255, 64, 255 };

	StatusEntry_t entries[18] = {};
	int nCount = 0;

	if (CFG::Visuals_StatusWindow_Show_Aimbot)
	{
		const bool bEngaged = CFG::Aimbot_Active
			&& (CFG::Aimbot_Always_On || !CFG::Aimbot_Key || H::Input->IsDown(CFG::Aimbot_Key));

		auto& e = entries[nCount++];
		e.m_pszName = "Aimbot";

		if (!bEngaged)
		{
			strcpy_s(e.m_szStatus, "OFF");
			e.m_tColor = tGray;
		}
		else if (G::nTargetIndex > 0)
		{
			strcpy_s(e.m_szStatus, "ACTIVE");
			e.m_tColor = tGreen;
		}
		else
		{
			strcpy_s(e.m_szStatus, "IDLE");
			e.m_tColor = tWhite;
		}
	}

	if (CFG::Visuals_StatusWindow_Show_FakeLag && F::FakeLag->m_bEnabled)
	{
		auto& e = entries[nCount++];
		e.m_pszName = "Fake Lag";
		strcpy_s(e.m_szStatus, "CHOKING");
		e.m_tColor = tGreen;
	}

	if (CFG::Visuals_StatusWindow_Show_AntiAim && F::FakeAngle->AntiAimOn())
	{
		auto& e = entries[nCount++];
		e.m_pszName = "Anti-Aim";

		if (CFG::Exploits_LegitAA_Enabled)
		{
			strcpy_s(e.m_szStatus, "ACTIVE (Legit)");
			e.m_tColor = tGreen;
		}
		else
		{
			strcpy_s(e.m_szStatus, "ACTIVE");
			e.m_tColor = tGreen;
		}
	}

	if (CFG::Visuals_StatusWindow_Show_NoSpread && F::SeedPred->IsAllowed() && pWeapon
		&& H::AimUtils->GetWeaponType(pWeapon) == EWeaponType::HITSCAN && pWeapon->GetWeaponSpread() > 0.0f)
	{
		auto& e = entries[nCount++];
		e.m_pszName = "No Spread";

		if (F::SeedPred->IsSynced())
		{
			const float flStep = F::SeedPred->GetMantissaStep();
			const bool bLaser = flStep < 1.0f || flStep >= 128.0f;
			const char* pszTier;
			if (flStep < 1.0f)
				pszTier = "Laser pointer";
			else if (flStep < 2.0f)
				pszTier = "shit";
			else if (flStep < 4.0f)
				pszTier = "ass";
			else if (flStep < 8.0f)
				pszTier = "bad";
			else if (flStep < 16.0f)
				pszTier = "meh";
			else if (flStep < 32.0f)
				pszTier = "okay";
			else if (flStep < 64.0f)
				pszTier = "good";
			else if (flStep < 128.0f)
				pszTier = "GREAT";
			else
				pszTier = "Laser pointer";

			if (flStep < 1.0f)
				strcpy_s(e.m_szStatus, pszTier);
			else
				sprintf_s(e.m_szStatus, "%s (step %.0f)", pszTier, flStep);
			e.m_tColor = tGreen;
			e.m_bRainbow = bLaser;
		}
		else
		{
			strcpy_s(e.m_szStatus, "no laser");
			e.m_tColor = tYellow;
		}
	}

	if (CFG::Visuals_StatusWindow_Show_Airblast
		&& F::AutoAirblast->m_flLastReflect > 0.f && flNow - F::AutoAirblast->m_flLastReflect < ACTION_LATCH_TIME)
	{
		auto& e = entries[nCount++];
		e.m_pszName = "Airblast";
		strcpy_s(e.m_szStatus, "REFLECTING");
		e.m_tColor = tYellow;
	}

	C_WeaponMedigun* pMedigun = nullptr;
	if (pWeapon && pWeapon->GetClassId() == ETFClassIds::CWeaponMedigun)
		pMedigun = pWeapon->As<C_WeaponMedigun>();

	if (CFG::Visuals_StatusWindow_Show_AutoUber && CFG::AutoUber_Active && pMedigun
		&& pMedigun->GetChargeType() == MEDIGUN_CHARGE_INVULN)
	{
		auto& e = entries[nCount++];
		e.m_pszName = "Auto-Uber";
		const int iCharge = static_cast<int>(pMedigun->m_flChargeLevel() * 100.0f);
		sprintf_s(e.m_szStatus, "%d%%", iCharge);
		e.m_tColor = iCharge >= 100 ? tGreen : tWhite;
	}

	if (CFG::Visuals_StatusWindow_Show_AutoVacc && CFG::Triggerbot_AutoVaccinator_Active
		&& pMedigun && pMedigun->GetChargeType() >= 3)
	{
		auto& e = entries[nCount++];
		e.m_pszName = "Auto Vacc";

		const int iCharge = static_cast<int>(pMedigun->m_flChargeLevel() * 100.0f);
		const float flLastPop = F::AutoVaccinator->GetLastPopTime();

		if (flLastPop > 0.f && flNow - flLastPop < ACTION_LATCH_TIME)
		{
			strcpy_s(e.m_szStatus, "POP");
			e.m_tColor = tYellow;
		}
		else if (F::AutoVaccinator->IsKeyPopQueued())
		{
			strcpy_s(e.m_szStatus, "QUEUED");
			e.m_tColor = tYellow;
		}
		else if (F::AutoVaccinator->IsChangingRes())
		{
			const int nGoal = F::AutoVaccinator->GetGoalResType();
			const int nActual = pMedigun->GetResistType();
			const auto pszType = [](int n) { return (n >= 0 && n < 3) ? (n == 0 ? "B" : n == 1 ? "L" : "F") : "?"; };
			sprintf_s(e.m_szStatus, "CYC %s>%s", pszType(nGoal), pszType(nActual));
			e.m_tColor = tYellow;
		}
		else
		{
			int nRes = F::AutoVaccinator->GetGoalResType();
			if (nRes < MEDIGUN_BULLET_RESIST || nRes >= MEDIGUN_NUM_RESISTS)
				nRes = pMedigun->GetResistType();
			if (nRes < MEDIGUN_BULLET_RESIST || nRes >= MEDIGUN_NUM_RESISTS)
				nRes = MEDIGUN_BULLET_RESIST;

			static const char* pszResists[] = { "BULLET", "BLAST", "FIRE" };
			sprintf_s(e.m_szStatus, "%s %d%%", pszResists[nRes], iCharge);
			e.m_tColor = iCharge >= 25 ? tGreen : tWhite;
		}
	}

	if (CFG::Visuals_StatusWindow_Show_AutoHeal && CFG::AutoUber_AutoHeal_Active && pMedigun && pMedigun->m_bHealing())
	{
		auto& e = entries[nCount++];
		e.m_pszName = "Auto Heal";
		strcpy_s(e.m_szStatus, "HEALING");
		e.m_tColor = tGreen;
	}

	if (CFG::Visuals_StatusWindow_Show_AutoDetonate
		&& F::AutoDetonate->m_flLastDetonate > 0.f && flNow - F::AutoDetonate->m_flLastDetonate < ACTION_LATCH_TIME)
	{
		auto& e = entries[nCount++];
		e.m_pszName = "Detonate";
		strcpy_s(e.m_szStatus, "BOOM");
		e.m_tColor = tYellow;
	}

	if (CFG::Visuals_StatusWindow_Show_AutoBackstab
		&& F::AutoBackstab->m_flLastStab > 0.f && flNow - F::AutoBackstab->m_flLastStab < ACTION_LATCH_TIME)
	{
		auto& e = entries[nCount++];
		e.m_pszName = "Backstab";
		strcpy_s(e.m_szStatus, "STAB");
		e.m_tColor = tYellow;
	}

	if (CFG::Visuals_StatusWindow_Show_DuckAir && CFG::Misc_Crouch_While_Airborne && pLocal
		&& !(pLocal->m_fFlags() & FL_ONGROUND) && (pLocal->m_fFlags() & FL_DUCKING))
	{
		auto& e = entries[nCount++];
		e.m_pszName = "Duck Air";
		strcpy_s(e.m_szStatus, "DUCKING");
		e.m_tColor = tGreen;
	}

	if (CFG::Visuals_StatusWindow_Show_Jumpbug
		&& F::Misc->m_flLastJumpbug > 0.f && flNow - F::Misc->m_flLastJumpbug < ACTION_LATCH_TIME)
	{
		auto& e = entries[nCount++];
		e.m_pszName = "Jumpbug";
		strcpy_s(e.m_szStatus, "JUMPBUG");
		e.m_tColor = tYellow;
	}

	if (CFG::Visuals_StatusWindow_Show_EdgeJump
		&& F::Misc->m_flLastEdgeJump > 0.f && flNow - F::Misc->m_flLastEdgeJump < ACTION_LATCH_TIME)
	{
		auto& e = entries[nCount++];
		e.m_pszName = "Edge Jump";
		strcpy_s(e.m_szStatus, "JUMPED");
		e.m_tColor = tYellow;
	}

	if (CFG::Visuals_StatusWindow_Show_RocketJump && F::Misc->IsAutoRocketJumpRunning())
	{
		auto& e = entries[nCount++];
		e.m_pszName = "Rocket Jump";
		strcpy_s(e.m_szStatus, "RUNNING");
		e.m_tColor = tYellow;
	}

	if (CFG::Visuals_StatusWindow_Show_FaNJump
		&& F::Misc->m_flLastFaN > 0.f && flNow - F::Misc->m_flLastFaN < ACTION_LATCH_TIME)
	{
		auto& e = entries[nCount++];
		e.m_pszName = "FaN Jump";
		strcpy_s(e.m_szStatus, "BOOSTING");
		e.m_tColor = tYellow;
	}

	if (CFG::Visuals_StatusWindow_Show_Thirdperson
		&& CFG::Visuals_Thirdperson_Active && I::Input->CAM_IsThirdPerson())
	{
		auto& e = entries[nCount++];
		e.m_pszName = "Thirdperson";
		strcpy_s(e.m_szStatus, "ACTIVE");
		e.m_tColor = tGreen;
	}

	if (CFG::Visuals_StatusWindow_Show_Warp
		&& Shifting::flLastWarp > 0.f && flNow - Shifting::flLastWarp < ACTION_LATCH_TIME)
	{
		auto& e = entries[nCount++];
		e.m_pszName = "Warp";
		strcpy_s(e.m_szStatus, "WARPED");
		e.m_tColor = tYellow;
	}

	if (CFG::Visuals_StatusWindow_Show_NavBot && CFG::NavBot_Enabled && g_NavBot.IsActive())
	{
		auto& e = entries[nCount++];
		e.m_pszName = "NavBot";
		strcpy_s(e.m_szStatus, "NAVIGATING");
		e.m_tColor = tGreen;
	}

	if (!nCount)
		return;

	const int nX = CFG::Visuals_StatusWindow_Pos_X;
	const int nY = CFG::Visuals_StatusWindow_Pos_Y;
	const int nW = CFG::Visuals_StatusWindow_Width;
	const int nBarH = CFG::Menu_Drag_Bar_Height;
	const int nMaxRows = std::max((CFG::Visuals_StatusWindow_Height - nBarH) / nBarH, 0);
	const int nVisible = std::min(nCount, nMaxRows);

	if (!nVisible)
		return;

	const auto& font = H::Fonts->Get(EFonts::Menu);
	const bool bChaos = CFG::Visuals_StatusWindow_Show_Killspree && flNow < m_flChaosEnd;
	const bool bMonster = CFG::Visuals_StatusWindow_Show_Killspree && m_iKillChain >= 5 && flNow < m_flSpreeEnd;
	const float flChaosPhase = I::GlobalVars->realtime * 6.0f;

	auto outlineColor = F::VisualUtils->GetAccentSecondaryWithAlpha(CFG::Visuals_StatusWindow_Outline_Alpha);
	if (bChaos || bMonster)
		outlineColor = HueColor(I::GlobalVars->realtime * 3.0f, static_cast<byte>(CFG::Visuals_StatusWindow_Outline_Alpha * 255.0f));

	const auto bgColor = F::VisualUtils->GetAlphaColor(CFG::Menu_Background, CFG::Visuals_StatusWindow_Background_Alpha);

	H::Draw->Rect(nX, nY, nW, nBarH, bgColor);

	if (bChaos)
		DrawScrambledText(font, nX + (nW / 2), nY + (nBarH / 2), HueColor(flChaosPhase + 1.7f, 255), "Status", 1, flChaosPhase);
	else
		H::Draw->String(font, nX + (nW / 2), nY + (nBarH / 2), CFG::Menu_Text, POS_CENTERXY, "Status");

	H::Draw->OutlinedRect(nX, nY, nW, nBarH, outlineColor);

	for (int n = 0; n < nVisible; n++)
	{
		const auto& entry = entries[n];
		const int iPos = n + 1;
		const int nRowY = nY + (nBarH * iPos) - 1;

		H::Draw->Rect(nX, nRowY, nW, nBarH + 1, bgColor);

		if (bChaos)
		{
			const auto tChaos = HueColor(flChaosPhase + n * 0.7f, 255);
			DrawScrambledText(font, nX + 6, nRowY + (nBarH / 2) + 1, tChaos, entry.m_pszName, 0, flChaosPhase + n);
			DrawScrambledText(font, nX + nW - 6, nRowY + (nBarH / 2) + 1, tChaos, entry.m_szStatus, 2, flChaosPhase + n + 0.4f);
		}
		else
		{
			H::Draw->String(font, nX + 6, nRowY + (nBarH / 2) + 1, CFG::Menu_Text, POS_CENTERY, "%s", entry.m_pszName);
			if (entry.m_bRainbow)
				DrawRainbowText(font, nX + nW - 6, nRowY + (nBarH / 2) + 1, entry.m_szStatus, 2, flNow);
			else
				H::Draw->String(font, nX + nW - 6, nRowY + (nBarH / 2) + 1, entry.m_tColor, POS_CENTERY | POS_LEFT, "%s", entry.m_szStatus);
		}

		H::Draw->OutlinedRect(nX, nRowY, nW, nBarH + 1, outlineColor);
	}

	if (CFG::Visuals_StatusWindow_Show_Killspree && flNow < m_flSpreeEnd)
		DrawSpree(nX, nY, nW, nBarH, nVisible, flNow);
}

void CStatusWindow::StartSpree(int iAnim, const char* pszText, bool bFull, float flNow)
{
	m_iKillAnim = iAnim;
	strcpy_s(m_szAnnounce, pszText);
	m_flSpreeStart = flNow;
	m_flSpreeEnd = flNow + SPREE_TIME;
	m_bSpreeFull = bFull;
}

void CStatusWindow::OnLocalKill(const char* pszWeapon, int iCustomKill, int nVictim, bool bCritKill)
{
	if (!CFG::Visuals_StatusWindow_Show_Killspree)
		return;

	const float flNow = I::EngineClient->Time();

	static int iLastVictim = -1;
	static float flLastVictimTime = 0.f;

	if (nVictim == iLastVictim && flNow - flLastVictimTime < 0.5f)
		return;

	iLastVictim = nVictim;
	flLastVictimTime = flNow;

	m_flKillTimes[m_iKillIdx % 8] = flNow;
	m_iKillIdx++;

	int iBurst = 0;
	for (const float fl : m_flKillTimes)
	{
		if (fl > 0.f && flNow - fl <= CHAOS_WINDOW)
			iBurst++;
	}

	if (iBurst >= 3)
		m_flChaosEnd = flNow + CHAOS_TIME;

	if (m_iKillChain > 0 && flNow - m_flLastChainKill <= MULTIKILL_WINDOW)
		m_iKillChain++;
	else
		m_iKillChain = 1;

	m_flLastChainKill = flNow;

	if (m_iKillChain < 2)
		return;

	const auto pLocal = H::Entities->GetLocal();
	const char* pszW = pszWeapon ? pszWeapon : "";
	const auto W = [pszW](const char* s) { return strstr(pszW, s) != nullptr; };

	m_bSpreeHeadshot = iCustomKill == 1;
	m_bSpreeTeamRed = !pLocal || pLocal->m_iTeamNum() == TF_TEAM_RED;

	const bool bCrit = bCritKill || (pLocal && (pLocal->IsCritBoosted() || F::CritHack->m_bForcingCrit));

	int iAnim = KILLA_NONE;

	if (m_iKillChain >= 3)
	{
		const int nClass = pLocal ? pLocal->m_iClass() : 0;

		if (W("sentry"))
			iAnim = KILLA_SENTRY;
		else if (iCustomKill == 2 || W("knife"))
			iAnim = KILLA_KNIFE;
		else if (W("compound_bow") || W("huntsman"))
			iAnim = KILLA_SKULLBOW;
		else if (m_bSpreeHeadshot)
			iAnim = KILLA_SKULL;
		else
		{
			switch (nClass)
			{
			case TF_CLASS_SCOUT:
				if (W("scattergun") || W("force_a_nature") || W("shortstop"))
					iAnim = KILLA_SCATTER;
				else if (W("handgun") || W("pistol") || W("lugermorph"))
					iAnim = KILLA_PISTOL;
				else
					iAnim = KILLA_BAT;
				break;
			case TF_CLASS_SOLDIER:
				if (W("rocketlauncher") || W("rocket"))
					iAnim = KILLA_ROCKET;
				else if (W("shotgun"))
					iAnim = KILLA_SHOTGUN;
				else
					iAnim = KILLA_SHOVEL;
				break;
			case TF_CLASS_PYRO:
				if (W("flamethrower"))
					iAnim = KILLA_BURN;
				else if (W("flaregun") || W("flare") || W("detonator") || W("scorch") || W("manmelter"))
					iAnim = KILLA_FLARE;
				else if (W("shotgun"))
					iAnim = KILLA_SHOTGUN;
				else
					iAnim = KILLA_AXE;
				break;
			case TF_CLASS_DEMOMAN:
				if (W("launcher") || W("pipe"))
					iAnim = KILLA_STICKYARC;
				else
					iAnim = KILLA_BOTTLE;
				break;
			case TF_CLASS_HEAVYWEAPONS:
				if (W("minigun") || W("natascha") || W("brass_beast") || W("tomislav") || W("iron_curtain") || W("long_heatmaker"))
					iAnim = KILLA_MINIGUN;
				else if (W("shotgun") || W("family_business"))
					iAnim = KILLA_SHOTGUN;
				else
					iAnim = KILLA_GLOVES;
				break;
			case TF_CLASS_ENGINEER:
				if (W("handgun") || W("pistol"))
					iAnim = KILLA_PISTOL;
				else if (W("shotgun") || W("widowmaker") || W("frontier") || W("pomson") || W("short_circuit"))
					iAnim = KILLA_SHOTGUN;
				else
					iAnim = KILLA_WRENCH;
				break;
			case TF_CLASS_MEDIC:
				iAnim = KILLA_MEDIC;
				break;
			case TF_CLASS_SNIPER:
				if (W("smg") || W("submachine"))
					iAnim = KILLA_SMG;
				else if (W("sniperrifle"))
					iAnim = KILLA_SKULL;
				else
					iAnim = KILLA_KUKRI;
				break;
			case TF_CLASS_SPY:
				if (W("revolver") || W("ambassador") || W("letranger") || W("enforcer") || W("diamondback") || W("handgun") || W("pistol"))
				{
					if (bCrit)
						m_bSpreeHeadshot = true;
					iAnim = bCrit ? KILLA_SKULL : KILLA_REVOLVER;
				}
				else
					iAnim = KILLA_KNIFE;
				break;
			default:
				iAnim = KILLA_NONE;
				break;
			}
		}
	}

	static constexpr const char* pszTiers[] = { "DOUBLE KILL", "TRIPPLE KILL", "QUAD KILL", "MONSTER KILL", "OH DADDY MORE" };
	const char* pszTier = pszTiers[std::min(m_iKillChain, 6) - 2];

	char szText[64] = {};
	if (pLocal && pLocal->IsCritBoosted())
		sprintf_s(szText, "CRIT %s", pszTier);
	else
		sprintf_s(szText, "%s", pszTier);

	StartSpree(iAnim, szText, m_iKillChain >= 6, flNow);
}

void CStatusWindow::OnHealTargetKill(int nAttackerIndex)
{
	if (!CFG::Visuals_StatusWindow_Show_Killspree)
		return;

	const auto pLocal = H::Entities->GetLocal();
	const auto pWeapon = H::Entities->GetWeapon();

	if (!pLocal || !pWeapon || pWeapon->GetClassId() != ETFClassIds::CWeaponMedigun)
		return;

	auto pMedigun = pWeapon->As<C_WeaponMedigun>();
	if (!pMedigun->m_bHealing())
		return;

	const auto pHealTarget = pMedigun->m_hHealingTarget().Get();
	if (!pHealTarget || pHealTarget->entindex() != nAttackerIndex)
		return;

	const float flNow = I::EngineClient->Time();

	if (m_iHealChain > 0 && flNow - m_flLastHealKill <= HEALKILL_WINDOW)
		m_iHealChain++;
	else
		m_iHealChain = 1;

	m_flLastHealKill = flNow;

	if (m_iHealChain >= 3)
		StartSpree(KILLA_MEDIC, "GOD MEDICK", false, flNow);
}

static void DrawSkull(int nSX, int nSY)
{
	H::Draw->FilledCircle(nSX, nSY, 13, 18, Color_t{ 236, 236, 232, 255 });
	H::Draw->Rect(nSX - 7, nSY + 9, 14, 7, Color_t{ 236, 236, 232, 255 });
	H::Draw->FilledCircle(nSX - 5, nSY - 1, 3, 10, Color_t{ 25, 25, 28, 255 });
	H::Draw->FilledCircle(nSX + 5, nSY - 1, 3, 10, Color_t{ 25, 25, 28, 255 });
	H::Draw->FilledTriangle({ Vec2(nSX, nSY + 2), Vec2(nSX - 2, nSY + 6), Vec2(nSX + 2, nSY + 6) }, Color_t{ 25, 25, 28, 255 });
	H::Draw->Line(nSX - 3, nSY + 10, nSX - 3, nSY + 15, Color_t{ 180, 180, 176, 255 });
	H::Draw->Line(nSX + 3, nSY + 10, nSX + 3, nSY + 15, Color_t{ 180, 180, 176, 255 });
}

void CStatusWindow::DrawSpree(int nX, int nY, int nW, int nBarH, int nRows, float flNow)
{
	const float flLife = flNow - m_flSpreeStart;
	const float flRemain = m_flSpreeEnd - flNow;

	float flAlpha = 1.0f;
	if (flLife < 0.15f)
		flAlpha = flLife / 0.15f;
	if (flRemain < 0.5f)
		flAlpha = std::min(flAlpha, flRemain / 0.5f);
	const byte nAlpha = static_cast<byte>(std::clamp(flAlpha, 0.0f, 1.0f) * 255.0f);
	if (!nAlpha)
		return;

	const int nFullH = nBarH * (nRows + 1);
	const int nBodyY = nY + nBarH;
	const int nBodyH = nBarH * nRows;
	const int nCX = nX + (nW / 2);
	const int nBodyCY = nBodyY + (nBodyH / 2);
	const float flPhase = flLife * 5.0f;
	const int nShakeX = m_bSpreeFull ? static_cast<int>(std::sin(flNow * 43.0f) * 2.0f) : 0;

	if (m_bSpreeFull)
	{
		H::Draw->Rect(nX, nY, nW, nFullH, Color_t{ 10, 10, 14, static_cast<byte>(nAlpha * 0.82f) });
		H::Draw->OutlinedRect(nX, nY, nW, nFullH, HueColor(flPhase, nAlpha));
	}

	if (m_iKillAnim != KILLA_NONE && nAlpha > 76)
		DrawKillAnim(m_iKillAnim, nCX - 42 + nShakeX, nBodyCY - 54, 84, 60, flLife);

	const auto& font = H::Fonts->Get(EFonts::StatusAnnounce);
	const int nTextY = (m_iKillAnim != KILLA_NONE) ? nBodyCY + 18 : nBodyCY;

	for (int n = 0; n < 4; n++)
	{
		const float flAngle = (6.2831853f / 4.0f) * n + flLife * 3.0f;
		H::Draw->String(font, nCX + nShakeX + static_cast<int>(std::cos(flAngle) * 2.0f), nTextY + static_cast<int>(std::sin(flAngle) * 2.0f), HueColor(flPhase, static_cast<byte>(nAlpha / 3)), POS_CENTERXY, "%s", m_szAnnounce);
	}

	H::Draw->String(font, nCX + nShakeX, nTextY, HueColor(flPhase, nAlpha), POS_CENTERXY, "%s", m_szAnnounce);
}

static const Color_t colKillaBat[] = { { 40, 39, 38, 255 }, { 44, 44, 42, 255 }, { 106, 104, 102, 255 }, { 116, 116, 113, 255 }, { 26, 26, 24, 255 }, { 71, 69, 65, 255 }, { 19, 18, 15, 255 }, { 50, 48, 40, 255 }, { 94, 84, 88, 255 }, { 35, 31, 33, 255 }, { 48, 48, 47, 255 }, { 57, 58, 58, 255 }, { 127, 128, 125, 255 }, { 152, 155, 153, 255 } };

static const char* const szKillaBat0[] =
{
	"nnnndddddmnlll............",
	"dmmmmmddmmnmnlll..........",
	"dbbmddddmnnnmmnkkk........",
	"..aaacddddmdddddmceee.....",
	".......bccccdcacchfhfgeaca",
	".........aaaa.aa.ahhhhhfdb",
	".................g.gghhfcb",
	".....................ggic.",
	".......................j..",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
};

static const char* const szKillaBat1[] =
{
	"..........................",
	"..........................",
	"........................a.",
	"....................e..acb",
	"..........l.lllkkk.eeghfcb",
	"ll.llk.kmnmlnlnmmcfhhhhhfc",
	"nmmmmmkmmmnmmmdmdchhhghgii",
	"nmmdddddmnnnddccaag.g.g.j.",
	"nnnddmdmmdmdcbba..........",
	"nnnnmddddddcca............",
	"mmdmmdcabaa...............",
	"mdbbaa.a..................",
	"b.........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
};

static const char* const szKillaBat2[] =
{
	"..........................",
	".......................ab.",
	"......................acdb",
	"......................efcc",
	"....................eghhfi",
	"...................efhhhgj",
	".................kefhhgg..",
	"...............lkmchhg....",
	".............llnmdcag.....",
	"...........llnmmdca.......",
	"..........lnmmmdca........",
	".........kmmnnddb.........",
	"......lkkmdmnmdca.........",
	".....lnmdddmddca..........",
	"...lkmmdddmddca...........",
	"..lnmmddmmddba............",
	".lnnnnnnmdca..............",
	"lnnnnnnmmca...............",
	"idnnnnmdba................",
	"iidnnmdb..................",
	"jiimmdb...................",
	".jiijb....................",
	"..jj......................",
	"..........................",
};

static const char* const szKillaBat3[] =
{
	".....................bdc..",
	".....................ccci.",
	"....................aeffjj",
	".....................ehhg.",
	".....................ghhg.",
	"....................efhg..",
	"....................efhg..",
	".....................fhg..",
	"....................ecag..",
	"....................mcc...",
	"...................lmda...",
	"...................nnda...",
	"..................llmdc...",
	"..................lnmdda..",
	"..................lmmdca..",
	"..................nnnmca..",
	".................lmmndda..",
	".................kmmmdca..",
	"..................mdmdba..",
	".................kddmdd...",
	"................lmddmca...",
	"................nndmmda...",
	"................lmdnmma...",
	"................kmnnmba...",
};

static const char* const* const szKillaBatF[] = { szKillaBat0, szKillaBat1, szKillaBat2, szKillaBat3 };

static const Color_t colKillaAxe[] = { { 46, 40, 33, 255 }, { 43, 35, 26, 255 }, { 123, 106, 87, 255 }, { 114, 93, 71, 255 }, { 110, 74, 56, 255 }, { 89, 66, 50, 255 }, { 33, 25, 19, 255 }, { 41, 28, 21, 255 }, { 45, 12, 14, 255 }, { 119, 33, 39, 255 }, { 125, 96, 67, 255 }, { 47, 36, 25, 255 }, { 117, 109, 107, 255 }, { 44, 41, 40, 255 } };

static const char* const szKillaAxe0[] =
{
	"geeejjjjhdfeeeddccaa......",
	"nmmcjjji...ghheeddcccaa...",
	"mmmmmdei......h.hefddcca..",
	"nmmmcmh..........hgfffedaa",
	"nnmmmm..............gfeecb",
	"...nnn..............gggfed",
	"......................gfeb",
	"......................gfff",
	".......................g..",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
};

static const char* const szKillaAxe1[] =
{
	"jjii......................",
	"jjjii...............aaa.a.",
	"jjjjji..a.......aaaaccdcdb",
	"jjjjjlkacaaacaaaccddfdeeed",
	"jfjjjjkkckkcccdddfffffgfee",
	"ffjjjjkkkkkddedeehg.g.ggff",
	"fejjjjekkedeehh.........gg",
	"ffjjjjeekdfg..............",
	"ifjjjjjehbbg..............",
	"ijjjjjjji.................",
	"ijjjjjjji.................",
	".ijjejdje.................",
	".geeecdmh.................",
	"..fmmmccn.................",
	"..mmmmmmn.................",
	"..nnmnnnn.................",
	"...nnn....................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
};

static const char* const szKillaAxe2[] =
{
	".....................abbb.",
	"...................aacddef",
	"...................acdeeff",
	"...................cdeffgg",
	"..................adffgg..",
	".................acfgg....",
	"................acdg......",
	"...............acdf.......",
	"..............aacde.......",
	".............accdeh.......",
	".............ckdeh........",
	"..........aaakdeh.........",
	".iii.....acckkeh..........",
	".jjjiiiilkkkkdfg..........",
	".jjjjjjjjjkkkkdb..........",
	"..ijjjjjjjjekkh...........",
	"...ijjjjjjjjeei...........",
	"...ijjfjjjjjjjji..........",
	"....gffejjjjjjjeh.........",
	".....gfefjjjjjddmn........",
	"......gffjjjjjcmcmn.......",
	".......gijjjjemmmmn.......",
	"........iiijemmmmn........",
	"...........gfmmmn.........",
};

static const char* const szKillaAxe3[] =
{
	"...................bddff..",
	"...................cdefgg.",
	"...................aeffg..",
	"..................acefg...",
	"..................acdf....",
	"...................cfgg...",
	"..................acff....",
	"..................aadg....",
	"...................cdh....",
	"...................cde....",
	"...................ade....",
	"...................adeh...",
	"..................acce....",
	"...................cke....",
	"...................cddh...",
	"..................akkef...",
	".................aakkeeb..",
	"..................ckkkdh..",
	".................akkkkeiii",
	"................ilkkeeejjj",
	"...............iijjjjjjjjj",
	"..............iijjjjjjjjjj",
	"............iiijjjjjjjjjje",
	"...........jijjjjjjejjjjjj",
};

static const char* const* const szKillaAxeF[] = { szKillaAxe0, szKillaAxe1, szKillaAxe2, szKillaAxe3 };

static const Color_t colKillaShovel[] = { { 39, 38, 38, 255 }, { 103, 102, 101, 255 }, { 124, 123, 121, 255 }, { 47, 46, 45, 255 }, { 96, 95, 94, 255 }, { 88, 86, 84, 255 }, { 33, 32, 31, 255 }, { 80, 78, 76, 255 }, { 30, 29, 28, 255 }, { 24, 23, 22, 255 }, { 65, 61, 58, 255 }, { 36, 36, 35, 255 }, { 36, 28, 23, 255 }, { 95, 75, 62, 255 } };

static const char* const szKillaShovel0[] =
{
	"ekhhkhhg..................",
	"hkhkkkhi..................",
	"fkkkkkkh..................",
	"kjkkjjkj..................",
	".jhe......................",
	".iefg.....................",
	".jhhg.....................",
	".jkfg.....................",
	".jkfg.....................",
	".jffm.....................",
	".mnnm.....................",
	"..nnm.....................",
	".mnnm.....................",
	".mnnm.....................",
	"..nnm.....................",
	".mnnm.....................",
	".mnnm.....................",
	".mnnm.....................",
	"..nnm.....................",
	".mnnm.....................",
	".mnnm.....................",
	"..nnn.....................",
	".mnnm.....................",
	".mnm......................",
};

static const char* const szKillaShovel1[] =
{
	"...........abbbeefffhi....",
	"..........abbbeefhhhhi....",
	".........abbbeefhhffhi....",
	"........gfbeehhhhhfehh....",
	".........jkfhhkhhhefhi....",
	"..........jkkkkkhheli.....",
	"...........jkkkkhhg.......",
	"..........ihkjkkhf........",
	".........jkeeg.jkh........",
	".........jkhf...ji........",
	".........kffg.............",
	"........jffg..............",
	".......mnnm...............",
	"......mnnmm...............",
	"......mnnm................",
	".....mnnn.................",
	"....mnnnm.................",
	"....nnnm..................",
	"...mnnm...................",
	"...mnnm...................",
	"..mnnm....................",
	".mnnn.....................",
	"mnnnm.....................",
	".mnm......................",
};

static const char* const szKillaShovel2[] =
{
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"......................aab.",
	".....................abbbb",
	"...................aabbbbe",
	".................gfbbbbbbe",
	".................jkbeebeee",
	".................jjfhkeeef",
	"..................jkhhhhfh",
	".................ihkkkhhhh",
	"..............jkkekkkkkhhf",
	"............mjkkhfejkkhhee",
	".........mmnnffffgg.jkhhel",
	"......mmmnnnnnmg....kkhfg.",
	".....mnnnnnmmm......ji....",
	"..mnmnnnnnm...............",
	"mmnnnnmm.m................",
	"mnnnnmm...................",
	".nmm......................",
};

static const char* const szKillaShovel3[] =
{
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"mm.m......................",
	"mnnnmmmm............jga...",
	"nnnnnnnmmnmm........jffbba",
	".mmnnnnnnnnmnnjj....jfbbbb",
};

static const char* const* const szKillaShovelF[] = { szKillaShovel0, szKillaShovel1, szKillaShovel2, szKillaShovel3 };

static const Color_t colKillaKukri[] = { { 24, 20, 15, 255 }, { 51, 51, 50, 255 }, { 135, 136, 132, 255 }, { 64, 53, 42, 255 }, { 71, 71, 71, 255 }, { 187, 189, 187, 255 }, { 210, 211, 208, 255 }, { 79, 80, 79, 255 }, { 217, 218, 215, 255 }, { 82, 82, 81, 255 }, { 222, 223, 220, 255 }, { 220, 221, 218, 255 }, { 83, 83, 82, 255 } };

static const char* const szKillaKukri0[] =
{
	"lllkliiggggggggfcdddaddb..",
	"illigfffffffcffcfcccddddc.",
	"fggfcfccccbbcbcccddddddddc",
	"ccccccbbb....bbbddaddddddd",
	"ccbcb.b.........aa..aaadda",
	"bbb...................a...",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
};

static const char* const szKillaKukri1[] =
{
	".................a.aadab..",
	".............ebcdddddddccb",
	".....h.h.hhgfgffcccddddddd",
	"..hhhhhhggggggffcdddddddda",
	"jjigiiigggffcccccdadaaadaa",
	"llllliffccbcbbbbaa.a......",
	"llliffcccb................",
	"iggffccbb.................",
	"iffccbb...................",
	"cccbcb....................",
	"cccb......................",
	"bb........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
};

static const char* const szKillaKukri2[] =
{
	"....................abccb.",
	"...................addcdda",
	"..................adddddda",
	"................aadddddda.",
	"...............addcdddaa..",
	"..............bcccddda....",
	".............efffcdaa.....",
	"............efgfccda......",
	"...........hggfccba.......",
	"..........hggfcbb.........",
	".........hggfcb...........",
	".......hhggfcb............",
	"......hgiifcb.............",
	".....hgiifccb.............",
	"....jiklgfcb..............",
	"...jillifccb..............",
	"..jilllgfcb...............",
	".mlkligfccb...............",
	"jilkkifccb................",
	"jikllfcccb................",
	"jilifcccb.................",
	"hggfcccb..................",
	"bccccbb...................",
	"bccbb.....................",
};

static const char* const szKillaKukri3[] =
{
	".....................cdaa.",
	"...................bccdd..",
	"...................adddda.",
	"..................adddda..",
	"...................dddda..",
	"..................addda...",
	"..................acddd...",
	".................adddaa...",
	".................adccda...",
	".................bcfcda...",
	".................bffcb....",
	".................egfcb....",
	".................fgfbb....",
	".................ggfb.....",
	".................ggcb.....",
	"................hggcb.....",
	"................hgfcb.....",
	"................hgfcb.....",
	"...............hhifcb.....",
	"...............hgifcbb....",
	"...............hiigcc.....",
	"...............hglfccb....",
	"...............iklgfcb....",
	"...............jllgfcb....",
};

static const char* const* const szKillaKukriF[] = { szKillaKukri0, szKillaKukri1, szKillaKukri2, szKillaKukri3 };

static const Color_t colKillaKnife[] = { { 34, 33, 34, 255 }, { 90, 89, 91, 255 }, { 28, 28, 27, 255 }, { 75, 75, 73, 255 }, { 20, 20, 19, 255 }, { 54, 53, 52, 255 }, { 68, 65, 64, 255 }, { 45, 44, 43, 255 }, { 17, 16, 16, 255 }, { 43, 45, 47, 255 }, { 115, 119, 125, 255 }, { 128, 132, 137, 255 }, { 48, 50, 52, 255 } };

static const char* const szKillaKnife0[] =
{
	"..........................",
	"....................aa....",
	"...................abba...",
	".................jjjjbdc..",
	"...............mjkkkkddba.",
	"..............mllkkkjbdddc",
	".............mlllklmdddfe.",
	"............mllllllmdgfe..",
	"...........mllllllmffee...",
	"..........mlllllmmhhi.....",
	".........mlllkkjdhii......",
	".........llkkjjgfe........",
	".........kkkjgfee.........",
	"............bba...........",
	"............aa............",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
};

static const char* const szKillaKnife1[] =
{
	"......jjkj................",
	"......jkkm..........aa....",
	"......kkklm........abba...",
	"......jlklmm......cddbdc..",
	"......lllllm.....cdddddba.",
	"......mlllmm....cddddbdddc",
	"......mllll....effggdddfe.",
	".......lllkj.ceffhfgdgfe..",
	".......mllkjcdddfffffee...",
	".......mllkjdddgdfhhi.....",
	"........llkjdgdddhii......",
	"........llkkgddgfe........",
	"........mlk.dgfee.........",
	".........kk.bba...........",
	"............aa............",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
};

static const char* const szKillaKnife2[] =
{
	"..........................",
	"....................aa....",
	"...................abba...",
	"..................cddbdc..",
	".................cdddddba.",
	"................cddddbdddc",
	"...............effggdddfe.",
	"mmmmm........ceffhfgdgfe..",
	"klllm.......cdddfffffee...",
	"kklllljj...cdddgdfhhi.....",
	"klllllkkjjabdgdddhii......",
	"jlllllllkkkggddgfe........",
	".mmlllllllk.dgfee.........",
	".....mmlllkkbba...........",
	".........m..aa............",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
};

static const char* const szKillaKnife3[] =
{
	"..........................",
	"....................aa....",
	"...................abba...",
	"..................cddbdc..",
	".................cdddddba.",
	"................cddddbdddc",
	"...............effggdddfe.",
	".............ceffhfgdgfe..",
	"............cdddfffffee...",
	"...........cdddgdfhhi.....",
	"..........abdgdddhii......",
	".........abggddgfe........",
	"........jkkkdgfee.........",
	"......jjkkllbba...........",
	".....jkklllmaa............",
	"...mmlllllm...............",
	"..mllllllm................",
	".mllllllm.................",
	".mlklllm..................",
	"jkkkllm...................",
	"kkkkjm....................",
	"jjjj......................",
	"..........................",
	"..........................",
};

static const char* const* const szKillaKnifeF[] = { szKillaKnife0, szKillaKnife1, szKillaKnife2, szKillaKnife3 };

static const Color_t colKillaWrench[] = { { 17, 16, 14, 255 }, { 17, 15, 12, 255 }, { 46, 44, 39, 255 }, { 45, 41, 33, 255 }, { 34, 29, 24, 255 }, { 38, 34, 30, 255 }, { 14, 12, 11, 255 }, { 12, 11, 9, 255 }, { 9, 8, 7, 255 }, { 25, 23, 19, 255 }, { 22, 22, 21, 255 }, { 13, 12, 10, 255 }, { 60, 60, 57, 255 }, { 4, 4, 3, 255 } };

static const char* const szKillaWrench0[] =
{
	"...i.haa...ijefdffffgb....",
	"............ijccedccfedb..",
	".............ggaaheddfeaa.",
	"..............g..hheddfca.",
	"..................heeefca.",
	"....................hhefa.",
	".....................h....",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
};

static const char* const szKillaWrench1[] =
{
	"fccejeccjaa.bb...gg.......",
	"fcddjjcddccdddacfgfgbdbaa.",
	"ecddjcccdmddddcceefffeecca",
	"ecdeejcceecfdddcfffcdfdfca",
	"ecddljcccccjjjldeeddedfff.",
	"jccdlldccmmfeeffchheheehe.",
	"jecclldccfgajjjca...h.h...",
	"jjdcjllca...iiggg.........",
	"jjjcjllc..................",
	"ijjfclcca.................",
	"ijefjieh..................",
	".hh.g.....................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
};

static const char* const szKillaWrench2[] =
{
	".......................aa.",
	".....................bacca",
	"....................bdefcf",
	"..................ggfefdfe",
	".................gfffddfeh",
	"................gfefcdeeh.",
	"..............baceffdehh..",
	".............bddccfehh....",
	"..........iabddddjdca.....",
	".......kaijccdddjlfca.....",
	"...kk.kmccdjmcfjjejfg.....",
	"..kmmnlecccdemcfejig......",
	".kmmcdejjcccccmcai........",
	"hecmccdfjjccccfg..........",
	"ijjfccddeljdcca...........",
	"ejjjfcdddllldca...........",
	"ijjjjecdcdllldca..........",
	".ijjjjeccccjllca..........",
	"..iijjjjedccjleh..........",
	"....ijjjjjjfcji...........",
	".....iijjjjjfg............",
	".......iiijeh.............",
	"..........ih..............",
	"..........................",
};

static const char* const szKillaWrench3[] =
{
	".....................aacf.",
	".....................acffe",
	".....................bedfe",
	".....................deffh",
	".....................fddee",
	"....................gffdhh",
	"....................fffde.",
	"....................fefdh.",
	"...................gfefeaa",
	"....................ccfdca",
	"..................badcjfff",
	"...................dddjejg",
	"..................bdddjjj.",
	"..................bddfjeai",
	".................icccccca.",
	".................jcjemmfg.",
	"................acdddccf..",
	"...............kcccccccca.",
	"...............kmccccdcddc",
	"...............lljjjjjlldl",
	".............kmneffellllll",
	".............mmccdddddcjcj",
	".............mmcccddccccff",
	"............kmcfccecccdjjj",
};

static const char* const* const szKillaWrenchF[] = { szKillaWrench0, szKillaWrench1, szKillaWrench2, szKillaWrench3 };

static const Color_t colKillaBottle[] = { { 36, 31, 25, 255 }, { 45, 40, 36, 255 }, { 97, 82, 68, 255 }, { 120, 107, 96, 255 }, { 71, 60, 48, 255 }, { 66, 61, 56, 255 }, { 82, 58, 31, 255 }, { 174, 163, 148, 255 }, { 93, 68, 40, 255 }, { 26, 22, 18, 255 }, { 31, 22, 11, 255 }, { 35, 25, 15, 255 }, { 40, 28, 15, 255 }, { 15, 10, 5, 255 } };

static const char* const szKillaBottle0[] =
{
	".......................aa.",
	".....................bacca",
	"....................bdccce",
	"...........fbbabb.baccceeg",
	"..........fhddcddbdcccieej",
	"........ffhddddccddccggej.",
	".......fhhhhhdddccdcgggk..",
	"......fhdhhhhhhddcccgkk...",
	"....bbdhhhhdhhhddcica.....",
	"...bddddhhhhhhhhciiil.....",
	".bbdddddddhhdhdhciiil.....",
	"bddcccdddddhhddhciiil.....",
	"jeeecccdddddhhdhgiiil.....",
	"mmmmeicdddcichhdiiil......",
	"mmmmmeccdciiigggiigk......",
	"mmmmmmgiiiiiggiigkk.......",
	"nmmmmmmgiiiiiiggk.........",
	"nmmmmmmgiiiiigkk..........",
	".nmmmmmmgiiggk............",
	".nmmmmmmgigkk.............",
	"..nmmmmmmgk...............",
	"...nnmmmnk................",
	".....nnn..................",
	"..........................",
};

static const char* const szKillaBottle1[] =
{
	".......mmmmmedb...........",
	".....nnmmmmmedb...........",
	"....nmmmmmmmecdb..........",
	"...nmmmmmmmeccddb.........",
	"...nmmmmmmeiccddb.........",
	"..nmmmmmmgcccddddf........",
	"..nmmmmmgicdddddhhfff.....",
	"...nmmmmiiidddddhhdhf.....",
	"....nmggiiicddddhhhhhf....",
	"....kgiiiiiicddhhhhhdhf...",
	".....kgiiiiiidhhhdhhddb...",
	"......kgiigichhdhhhdddb...",
	"......kggigghhdhhhhddca...",
	".......kkgighdddhhhdcdb...",
	"........kgigdhhhhddccdb...",
	".........kgiigcccddcdb....",
	"..........kiiiiiiccdddb...",
	"..........kgiiiiiicccca...",
	"............kliiiicggcccb.",
	"..............llllagggccdb",
	"...................kggicca",
	"...................kkeeecc",
	".....................jeecc",
	"......................jgea",
};

static const char* const szKillaBottle2[] =
{
	"..........................",
	"..........................",
	"...................nnn....",
	".................knmmmnn..",
	"................kgmmmmmmn.",
	"..............kkgigmmmmmmn",
	".............kggiigmmmmmmn",
	"...........kkgiiiiigmmmmmm",
	"..........kggiiiiiigmmmmmm",
	"........kkgiiggiiiiigmmmmm",
	".......kgiigggiiicdccemmmm",
	".......liiidhhcicdddciemmm",
	"......liiighdhhdddddccceee",
	"......liiichddhhdddddcccdd",
	"......liiichdhdhhdddddddbb",
	"......liiichhhhhhhhddddb..",
	"......acicddhhhdhhhhdbb...",
	"....kkgcccddhhhhhhdhf.....",
	"...kgggcdccdddhhhhhf......",
	"..jeggccddccddddhff.......",
	".jeeicccdbddcddhf.........",
	".geecccab.bbabbf..........",
	".ecccdb...................",
	".accab....................",
};

static const char* const szKillaBottle3[] =
{
	"..........................",
	".aegj.....................",
	"acceej....................",
	"acceeekk..................",
	".acciggk..................",
	".bdccgggallll.............",
	"..bcccggciiiilk...........",
	"....acccciiiiiigk.........",
	"....bdddcciiiiiik.........",
	".....bdcddcccgiigk........",
	"....bdccddhhhhdgigk.......",
	"....bdcdhhhdddhgigkk......",
	"....acddhhhhdhhggiggk.....",
	"....bdddhhhdhhcigiigk.....",
	"....bddhhdhhhdiiiiiigk....",
	"....fhdhhhhhddciiiiiigk...",
	".....fhhhhhddddciiiggmn...",
	"......fhdhhdddddiiimmmmn..",
	"......fffhhdddddcigmmmmmn.",
	".........fddddcccgmmmmmmn.",
	"..........bddcciemmmmmmn..",
	"..........bddccemmmmmmmn..",
	"...........bdcemmmmmmmn...",
	"............bdemmmmmnn....",
};

static const char* const szKillaBottle4[] =
{
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	".........g................",
	"....a.f....f.cg...........",
	"..e....ac...e..e..........",
	"...b..b...d...f..d........",
	".....d....d.e...c.........",
	"..........................",
};

static const char* const* const szKillaBottleF[] = { szKillaBottle0, szKillaBottle1, szKillaBottle2, szKillaBottle3, szKillaBottle4 };

static const Color_t colKillaMinigun[] = { { 17, 17, 16, 255 }, { 46, 45, 44, 255 }, { 12, 11, 11, 255 }, { 32, 31, 30, 255 }, { 6, 6, 6, 255 }, { 18, 17, 17, 255 }, { 9, 9, 9, 255 }, { 25, 24, 24, 255 }, { 2, 2, 2, 255 }, { 7, 7, 7, 255 }, { 81, 80, 79, 255 }, { 30, 30, 30, 255 }, { 198, 198, 191, 255 }, { 75, 75, 72, 255 } };

static const char* const szKillaMinigun0[] =
{
	"..................aba.....",
	".................cdaba....",
	"................cdbefe....",
	"..........gca..ghbdaba....",
	".........ghdbaaghijbba....",
	".........ghefdbhdcdhdc....",
	".........ghhhfdbbdbbdc....",
	".........ghhdddbdbbdba....",
	"........ijjfdddbbddbkbaa..",
	"........ijffffhbbhjkbbbba.",
	"........ghhddffbbdffhdbba.",
	".......ccdbdbhjhdbbdfjdkkl",
	".....gcddbddbdhhbkkkbfhkkl",
	"...aghfhddbddddhdkmmkbfkkl",
	"..abddbhhbbbbkkkkmmmkbfkl.",
	"eabbbdddhlkkbkkmmmmmmbba..",
	"fjdbbbbhglkbbbkkmmmmkba...",
	"fjjbkbdc.lkbdbkkmmmmkl....",
	"fjjfbba...abkbkkmmmnl.....",
	"ijjhba....lkkbkkkkl.......",
	".efea.....ababkkll........",
	"..e........acdcl..........",
	"...........aba............",
	"............a.............",
};

static const char* const szKillaMinigun1[] =
{
	"..................aba.....",
	".................cdaba....",
	"................cdbefe....",
	"..........gca..ghbdaba....",
	".........ghdbaaghijbba....",
	".........ghefdbhdcdhdc....",
	".........ghhhfdbbdbbdc....",
	".............ddbdbbdba....",
	".........ighhddbbddbkbaa..",
	".........ijjffhbbhjkbbbba.",
	".........gjffffbbdffhdbba.",
	"........cchhdhjhdbbdfjdkkl",
	"...a.gcddbbdbdhhbkkkbfhkkl",
	"..aaghfhddddbddhdkmmkbfkkl",
	"eabbddbhhbbddkkkkmmmkbfkl.",
	"fjdbbdddhlbbbkkmmmmmmbba..",
	"fjjbbbbhglkkbbkkmmmmkba...",
	"fjjbkbdc.lkbbbkkmmmmkl....",
	"jjhbba...kkbdbkkmmmnl.....",
	"efeba....abk.bkkkkl.......",
	".e.a.....lkk.bkkll........",
	".........aba.dcl..........",
	"..........ac.a............",
	"............a.............",
};

static const char* const szKillaMinigun2[] =
{
	"..................aba.....",
	".................cdaba....",
	"................cdbefe....",
	"..........gca..ghbdaba....",
	".........ghdbaaghijbba....",
	".........ghefdbhdcdhdc....",
	".........ghhhfdbbdbbdc....",
	"........gjfd.ddbdbbdba....",
	".......ijfff.ddbbddbkbaa..",
	".......ijhdd.fhbbhjkbbbba.",
	".......ghbdb.ffbbdffhdbba.",
	"......ccddddbhjhdbbdfjdkkl",
	".....gcdddbdddhhbkkkbfhkkl",
	"...aghfhddbbbddhdkmmkbfkkl",
	"...bddbhhbkkbkkkkmmmkbfkl.",
	"..abbdddhlkbbkkmmmmmmbba..",
	"eabbbbbhglkbdbkkmmmmkba...",
	"fjdbkbdc.labkbkkmmmmkl....",
	"fjjjfbba...lkbkkmmmnl.....",
	".fjjhba....abbkkkkl.......",
	".ijjea......abkkll........",
	"..ef........adcl..........",
	"...e.........a............",
	"............a.............",
};

static const char* const* const szKillaMinigunF[] = { szKillaMinigun0, szKillaMinigun1, szKillaMinigun2 };

static const Color_t colKillaSentry[] = { { 46, 31, 30, 255 }, { 123, 83, 81, 255 }, { 115, 104, 102, 255 }, { 91, 85, 84, 255 }, { 34, 32, 31, 255 }, { 60, 60, 56, 255 }, { 107, 68, 67, 255 }, { 38, 11, 8, 255 }, { 101, 29, 23, 255 }, { 42, 40, 35, 255 }, { 15, 15, 13, 255 }, { 22, 22, 21, 255 } };

static const char* const szKillaSentry0[] =
{
	".....abcdddea.............",
	"....abddffdgba............",
	"....hiigbgggddee..........",
	"....hiiiiiffdddde.........",
	"....hiiiiijjffiijk........",
	"....hiiiijjjjiijjk........",
	".....hiiijjjjijjjjk.......",
	"......hhkjjjjijjjjk.......",
	"........kjjfjjkkkk........",
	"........kjjjjk............",
	".........klfjflk..........",
	"..........lffjjjk.........",
	"........klfffjjjk.........",
	".......kjjjfjjjjk.........",
	".....kllffjjjjjjfl........",
	"....kjfjffjjjjkkjfl.......",
	"...kjjjjfjlfjk..kjjk......",
	"....kkkklklfjk...kkjk.....",
	"..........lfl......lfl....",
	"..........lfl......lffl...",
	"..........lfl.......ll....",
	"..........lfl.............",
	"..........lffl............",
	"..........kjjk............",
};

static const char* const szKillaSentry1[] =
{
	".....abcdddea.............",
	"....abddffdgba............",
	"....hiigbgggddee..........",
	"....hiiiiiffdddde.........",
	"....hiiiiijjffiijk........",
	"....hiiiijjjjiijjk........",
	".....hiiijjjjijjjjk.......",
	"......hhkjjjjijjjjk.......",
	"........kjjfjjkkkk........",
	"........kjjjjk............",
	".........klfjflk..........",
	"..........lffjjjk.........",
	"........klfffjjjk.........",
	".......kjjjfjjjjk.........",
	".....kllffjjjjjjfl........",
	"....kjfjffjjjjkkjfl.......",
	"...kjjjjfjlfjk..kjjk......",
	"....kkkklklfjk...kkjk.....",
	"..........lfl......lfl....",
	"..........lfl......lffl...",
	"..........lfl.......ll....",
	"..........lfl.............",
	"..........lffl............",
	"..........kjjk............",
};

static const char* const* const szKillaSentryF[] = { szKillaSentry0, szKillaSentry1 };

static const Color_t colKillaSentryBlue[] = { { 46, 31, 30, 255 }, { 123, 83, 81, 255 }, { 115, 104, 102, 255 }, { 91, 85, 84, 255 }, { 34, 32, 31, 255 }, { 60, 60, 56, 255 }, { 107, 68, 67, 255 }, { 38, 11, 8, 255 }, { 101, 29, 23, 255 }, { 42, 40, 35, 255 }, { 15, 15, 13, 255 }, { 22, 22, 21, 255 }, { 52, 116, 184, 255 }, { 34, 86, 138, 255 }, { 31, 61, 125, 255 }, { 64, 148, 255, 255 } };

static const char* const szKillaSentryBlue0[] =
{
	".....jbckkkej.............",
	"....jbkkffklbj............",
	"....miilblllkkee..........",
	"....miiiiiffkkkke.........",
	"....miiiiijjffiijk........",
	"....miiiijjjjiijjk........",
	".....miiijjjjijjjjk.......",
	"......mmkjjjjijjjjk.......",
	"........kjjfjjkkkk........",
	"........kjjjjk............",
	".........klfjflk..........",
	"..........lffjjjk.........",
	"........klfffjjjk.........",
	".......kjjjfjjjjk.........",
	".....kllffjjjjjjfl........",
	"....kjfjffjjjjkkjfl.......",
	"...kjjjjfjlfjk..kjjk......",
	"....kkkklklfjk...kkjk.....",
	"..........lfl......lfl....",
	"..........lfl......lffl...",
	"..........lfl.......ll....",
	"..........lfl.............",
	"..........lffl............",
	"..........kjjk............",
};

static const char* const szKillaSentryBlue1[] =
{
	".....jbckkkej.............",
	"....jbkkffklbj............",
	"....miilblllkkee..........",
	"....miiiiiffkkkke.........",
	"....miiiiijjffiijk........",
	"....miiiijjjjiijjk........",
	".....miiijjjjijjjjk.......",
	"......mmkjjjjijjjjk.......",
	"........kjjfjjkkkk........",
	"........kjjjjk............",
	".........klfjflk..........",
	"..........lffjjjk.........",
	"........klfffjjjk.........",
	".......kjjjfjjjjk.........",
	".....kllffjjjjjjfl........",
	"....kjfjffjjjjkkjfl.......",
	"...kjjjjfjlfjk..kjjk......",
	"....kkkklklfjk...kkjk.....",
	"..........lfl......lfl....",
	"..........lfl......lffl...",
	"..........lfl.......ll....",
	"..........lfl.............",
	"..........lffl............",
	"..........kjjk............",
};

static const char* const* const szKillaSentryBlueF[] = { szKillaSentryBlue0, szKillaSentryBlue1 };

static const Color_t colKillaRocket[] = { { 19, 18, 17, 255 }, { 52, 49, 47, 255 }, { 30, 30, 30, 255 }, { 26, 25, 26, 255 }, { 70, 68, 69, 255 }, { 63, 61, 60, 255 }, { 80, 80, 80, 255 }, { 76, 76, 76, 255 }, { 23, 23, 22, 255 }, { 78, 75, 75, 255 }, { 74, 74, 74, 255 }, { 28, 28, 28, 255 }, { 255, 170, 60, 255 }, { 255, 220, 90, 255 } };

static const char* const szKillaRocket0[] =
{
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"...................a......",
	"...................ba.....",
	"................cgghb.....",
	"...............cggghb.....",
	"............cibbfjkea.....",
	".........deehgkbbil.......",
	"........ckfbehkbb.........",
	".......ghkeefel...........",
	".......hhhkefe............",
	".....abbkhkfa.............",
	".....abnlld...............",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
};

static const char* const szKillaRocket1[] =
{
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"...................a......",
	"...................ba.....",
	"................cgghb.....",
	"...............cggghb.....",
	"............cibbfjkea.....",
	".........deehgkbbil.......",
	"........ckfbehkbb.........",
	".......ghkeefel...........",
	".......hhhkefe............",
	".....abbkhkfa.............",
	".....aballd...............",
	"......ann.................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
};

static const char* const* const szKillaRocketF[] = { szKillaRocket0, szKillaRocket1 };

static const Color_t colKillaRevolver[] = { { 18, 18, 18, 255 }, { 49, 48, 48, 255 }, { 79, 77, 78, 255 }, { 30, 29, 29, 255 }, { 205, 194, 193, 255 }, { 127, 124, 124, 255 }, { 48, 47, 47, 255 }, { 248, 245, 244, 255 }, { 94, 93, 92, 255 }, { 36, 35, 36, 255 }, { 15, 14, 15, 255 }, { 77, 73, 73, 255 }, { 13, 13, 13, 255 }, { 5, 5, 5, 255 } };

static const char* const szKillaRevolver0[] =
{
	"..........................",
	"......................a...",
	"..................aa.aba..",
	".................abbaba...",
	"...............aabbbbcd...",
	"..............abbbccebfg..",
	"............aabbbcffcbhhi.",
	"...........abbbccjfcjfhhi.",
	".........aabbbfcbkjjjeheel",
	"........abbbcfcjjkjjchhfhi",
	".....amabjbffbdjjjjkceehhh",
	"...aabjbjcfcjbcbbdkkbkfhhh",
	"..abjjbjcfbbbbdjjjkknkchhh",
	".mjjjjbfcbbbjm.nkkknnkchhh",
	"mjjjjbfcjbjmm...nnn.nkchhh",
	"mjbjccbbbjm........abkfhhh",
	"jjbccbbbam.........abkehhh",
	"kkbbbbjm...........mjjehii",
	"nkjbbjm.............mmli..",
	"nkbbam....................",
	"nkba......................",
	".na.......................",
	"..........................",
	"..........................",
};

static const char* const szKillaRevolver1[] =
{
	"..........................",
	"......................a...",
	"..................aa.aba..",
	".................abbaba...",
	"...............aabbbbcd...",
	"..............abbbccebfg..",
	"............aabbbcffcbhhi.",
	"...........abbbccjfcjfhhi.",
	".........aabbbfcbkjjjeheel",
	"........abbbcfcjjkjjchhfhi",
	".....amabjbffbjdjjjkceehhh",
	"...aabjbjcfcjdcbbjkkbkfhhh",
	"..abjjbjcfbbbbmjjjkknkchhh",
	".mjjjjbfcbbbjm.ndkknnkchhh",
	"mjjjjbfcjbjmm...nnn.nkchhh",
	"mjbjccbbbjm........abkfhhh",
	"jjbccbbbam.........abkehhh",
	"kkbbbbjm...........mjjehii",
	"nkjbbjm.............mmli..",
	"nkbbam....................",
	"nkba......................",
	".na.......................",
	"..........................",
	"..........................",
};

static const char* const szKillaRevolver2[] =
{
	"..........................",
	"......................a...",
	"..................aa.aba..",
	".................abbaba...",
	"...............aabbbbcd...",
	"..............abbbccebfg..",
	"............aabbbcffcbhhi.",
	"...........abbbccjfcjfhhi.",
	".........aabbbfcbkjjjeheel",
	"........abbbcfcjdkjjchhfhi",
	".....amabjbffbjjjjjkceehhh",
	"...aabjbjcfcjbcbbjkkbkfhhh",
	"..abjjbjcfbbbdmjjjkknkchhh",
	".mjjjjbfcbbbjmdnkkknnkchhh",
	"mjjjjbfcjbjmm...nnn.nkchhh",
	"mjbjccbbbjm........abkfhhh",
	"jjbccbbbam.........abkehhh",
	"kkbbbbjm...........mjjehii",
	"nkjbbjm.............mmli..",
	"nkbbam....................",
	"nkba......................",
	".na.......................",
	"..........................",
	"..........................",
};

static const char* const* const szKillaRevolverF[] = { szKillaRevolver0, szKillaRevolver1, szKillaRevolver2 };

static const Color_t colKillaBurn[] = { { 180, 50, 20, 255 }, { 240, 100, 30, 255 }, { 255, 190, 70, 255 }, { 228, 180, 144, 255 }, { 188, 132, 100, 255 }, { 140, 88, 64, 255 }, { 38, 22, 20, 255 }, { 235, 228, 214, 255 }, { 242, 240, 232, 255 }, { 30, 26, 26, 255 }, { 46, 40, 38, 255 }, { 160, 84, 48, 255 }, { 30, 18, 14, 255 }, { 70, 45, 30, 255 }, { 255, 150, 70, 255 } };

static const char* const szKillaBurn0[] =
{
	"mmmmmmmmmnnnnnnnnmmmmmmmmm",
	"mmmmmmmnnnnnnnnnnnnmmmmmmm",
	"mmmmmmnnddddddddnnmmmmmmmm",
	"mmmmmddddddddddddddddmmmmm",
	"mmmmmdffffddddddffffdmmmmm",
	"mmmmmddijiiiddijiiiddmmmmm",
	"mmmmmddiiaiiddiiiiiddmmmmm",
	"mmmmmddeeoeedddeoaoedmmmmm",
	"mmmmmddeeeedddddeoeedmmmmm",
	"mmmmmdddddddeeedddddddmmmm",
	"mmmmmdddddddeeedddddddmmmm",
	"mmmmmdddddddddddddddddmmmm",
	"mmmmmddddhhghhghhhddddmmmm",
	"mmmmmddddgggggggggdddmmmmm",
	"mmddmddddgggggggggdmmmddmm",
	"ddddmmddddddddddddmmmmdddd",
	"ddddmmmdddddddmmmmmmmmdddd",
	"ddddmmmddddddmmmmmmmmmdddd",
	"ddddmmmmdddddmmmmmmmmmdddd",
	"ddddmmmmmddddmmmmmmmmmdddd",
	"dddobmmmmmddommmmmmmmmdddd",
	"ddddmmmmmmmoambmmmmmmaoddd",
	"dooommmaaambbbmmaaammmoood",
	"mbcbmmmbcbmbcbmmbcbmmmbcbm",
};

static const char* const szKillaBurn1[] =
{
	"mmmmmmmmmnnnnnnnnmmmmmmmmm",
	"mmmmmmmnnnnnnnnnnnnmmmmmmm",
	"mmmmmmnnddddddddnnmmmmmmmm",
	"mmmmmddddddddddddddddmmmmm",
	"mmmmmdffffddddddffffdmmmmm",
	"mmmmmddijiiiddijiiiddmmmmm",
	"mmmmmddiiaiiddiiiiiddmmmmm",
	"mmmmmddeeoeedddeoaoedmmmmm",
	"mmmmmddeleedddddeleedmmmmm",
	"mmmmmdddddddeeedddddddmmmm",
	"mmmmmdddddddeeedddddddmmmm",
	"mmmmmddddhhghhghhhddddmmmm",
	"mmmmmdddgggggggggggdddmmmm",
	"mmmmmdddgggggggggggddmmmmm",
	"mmddmdddgggggggggggmmmddmm",
	"ddddmmldgggggggggggmmmdddd",
	"ddddmmmdgggggggggggmmmdddd",
	"ddddmmmdddddddddddmmmaoddd",
	"dddobmmmdddddmmmmmmmmmdddd",
	"ddddmmmmmddddmmmmmmmmmdddd",
	"ddddmmmmmmdddmmmmmmbmmdddd",
	"ddddmmcmmaldmlmammmmmmdddd",
	"ooodmmmmbbblaabbbmmmmmdooo",
	"bcbmmmmmbcbbcbbcbmmmmmmbcb",
};

static const char* const szKillaBurn2[] =
{
	"mmmmmmmmmkkkkkkkkmmmmmmmmm",
	"mmmmmmmkkkkkkkkkkkkmmmmmmm",
	"mmmmmmnnddddddddnnmmmmmmmm",
	"mmmmmddddddddddddddddmmmmm",
	"mmmmmdffffddddddffffdmmmmm",
	"mmmmmddijiiiddijiiiddmmmmm",
	"mmmmmddiiaiiddiiiiiddmmmmm",
	"mmmmmddeeoeedddeoaoedmmmmm",
	"mmmmmddeleedddddeleedmmmmm",
	"mmammdddldddeeeddlddddmamm",
	"mmammdddddddeeeddddddkmamm",
	"mbbbmdddhhghhghhghhddobbbm",
	"abcbmdddgggggggggggddkbcba",
	"ammmmdddgggggggggggddmmmam",
	"aaokmdddgggggggggggmmmkoaa",
	"ooddmmldgggggggggggmmmddoo",
	"ddddmmmdgggggggggggmmmdddd",
	"ddddmmmdggaaaaaaaggmmmdddd",
	"ddddmmmmggaaaaaaaggmmmdddd",
	"dddoammmmdooooooodmmmmdddd",
	"ddddmmammmddommmmmmmaboddd",
	"ddddmamammloabmlmmamamdddd",
	"oodobbbbbmmbbbmmbbbbbbodoo",
	"cbmmbcbbcbmbcbmmbcbbcbmmbc",
};

static const char* const szKillaBurn3[] =
{
	"mmmmmmmmmkkkkkkkkmmmmmmmmm",
	"mmmmmmmkkkkkkkkkkkkmmmmmmm",
	"mmmmmmbkkkkkkkkkkkmmmmmmmm",
	"ammmmdoddddddddddddddmmmma",
	"ammmmdffffddddddffffdmmmma",
	"bbmamddijiiiddijiiiddmambb",
	"cbammddiiaiiddiiiiiddmambc",
	"maaamddeeoeedddeoaoeoaaamm",
	"mbbbmddeleedddddeleeobbbmm",
	"mbbbmdddldddeeeddlddobbbmm",
	"abcbmdddldddeeeddldddkbcba",
	"ammmmdddhhghhghhghhddkcmam",
	"aambmdddgggggggggggddkmaaa",
	"bbmmmdddgggggggggggddmmbbb",
	"bbokmdddgggggggggggmmmkobb",
	"ooddmmldgggggggggggmmmddoo",
	"ddddmmmdgggggggggggmmmdddd",
	"ddddmmmdggaaaaaaaggmmmdddd",
	"ddddmmmmggaaaaaaaggmmmdddd",
	"ddddmammmdooooooodmmamdddd",
	"dddoacmmamdddmbmmmamamdddd",
	"dddobbamablolmlmlambbboddd",
	"oodobbbbbbbbbmlmbbbbbbodoo",
	"cbmbcbcbcblbcblmbcbcbcbmbc",
};

static const char* const szKillaBurn4[] =
{
	"mmmmmmmmmkkkkkkkkmmmmmmmmm",
	"mammmmmkkkkkkkkkkkkmmmmmam",
	"mammmmnkkkkkkkkkkkmmmmmmam",
	"bbbamddddddddddddddddmabbb",
	"bcammdffffddddddffffdmabcb",
	"maaamddijiiiddijiiidoaaamm",
	"mbbbmddiiaiiddiiiiidobbbmm",
	"mbbbmddeeoeedddeoaoeobbbmm",
	"abbbmddeleedddddeleeobbbma",
	"abcbmdddldddeeeddldddkbcam",
	"aammmdddldddeeeddldddkmaaa",
	"aammmdddlhhghhghhhdddkcaaa",
	"bbmbmdddgggggggggggddkmbbb",
	"bammaoddgggggggggggddmmbba",
	"abokmdddgggggggggggmmmkoba",
	"ooddmmldgggggggggggmmmddoo",
	"ddddmmmdgggggggggggmmmdddd",
	"ddddmmmdddddddddddmmmmdddd",
	"ddddmammdddddmmmmmmmamdddd",
	"dddoammamddddmammmmaamdddd",
	"dddoaamammoddmammmaaaaoddd",
	"dddobbbbbbbolbbblbbbbboddd",
	"oodobbbbbbbblbbblbbbbbodoo",
	"cbmbcbbcbmbcbbcbmbcbbcbmbc",
};

static const char* const szKillaBurn5[] =
{
	"acbmmmmmmkkkkkkkkmmmmmmbca",
	"bbmammmkkkkkkkkkkkkmmmambb",
	"bbammmakkkkkkkkkkkmmmmambb",
	"caaamkodddddddddddddkaaabc",
	"maaamkffffddddddffffkaaamm",
	"mbbbmkeeeeeiddieeeeekbbbmm",
	"abbbmkfffffiddifffffkbbbma",
	"abbbmkdeeeeedddeeeeekbbbam",
	"aacbmkdeleedddddeleekmbaaa",
	"aammmkddldddeeeddlddkkcaaa",
	"bbmbmkddldddeeeddlddkkmbbb",
	"bammmkddhhghhghhghhdkkmbba",
	"abmmmkddgggggggggggdkkmbba",
	"aaammkddgggggggggggdkmmbaa",
	"aaokmkddgggggggggggmmmkoaa",
	"ooddmmldgggggggggggmmmddoo",
	"ddddmmmdgggggggggggmmcoddd",
	"dddoaamoggaaaaaaaggmamdddd",
	"dddoammaggaaaaaaaggaamdddd",
	"dddoaamamoooooooooaaaaoddd",
	"dddobbaaaaodoaaaaaabbboddd",
	"dddobbbbbbbobbbbbbbbbboddd",
	"oodobbbbbbblbbbbbbbbbbodoo",
	"cbmbcbbcbbcbbcbbcbcbbcbmbc",
};

static const char* const* const szKillaBurnF[] = { szKillaBurn0, szKillaBurn1, szKillaBurn2, szKillaBurn3, szKillaBurn4, szKillaBurn5 };

static const Color_t colKillaStickyman[] = { { 194, 59, 48, 255 }, { 240, 232, 216, 255 }, { 212, 87, 74, 255 }, { 147, 40, 31, 255 }, { 64, 64, 74, 255 }, { 255, 179, 64, 255 }, { 77, 92, 52, 255 }, { 55, 70, 31, 255 }, { 58, 58, 64, 255 }, { 44, 47, 51, 255 }, { 87, 55, 40, 255 }, { 25, 25, 28, 255 }, { 120, 120, 124, 255 } };

static const char* const szKillaStickyman0[] =
{
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..............kkk.........",
	"..............lll.........",
	"..............kkb.........",
	"..............kkk.........",
	"..............ma..........",
	".............aaaaaa.......",
	"............aama..aa......",
	"...........aa.aa...aa.....",
	"..........aa...mm...aa....",
	".........aa....gm...b.....",
	"..........b...hmm.........",
	".............llllm........",
	"............ll..lll.......",
	"...........ll.....ll......",
	"..........ll.......ll.....",
	".........ll........ll.....",
	"........ll.........ll.....",
	"........ll..........ll....",
	".......ll...........ll....",
	".......ll............ll...",
};

static const char* const szKillaStickyman1[] =
{
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..............kkk.........",
	"..............lll.........",
	"..............kkb.........",
	"..............kkk.........",
	"..............ma..........",
	"............aaaaaaaa......",
	"...........aa.ma...aa.....",
	"..........aa..aa...b......",
	"...........b...mm.........",
	"...............gm.........",
	"..............hmm.........",
	".............lllmm........",
	"............llll..........",
	"............llll..........",
	"............ll.ll.........",
	"............ll.ll.........",
	"...........ll..ll.........",
	"...........ll...ll........",
	"...........ll...ll........",
};

static const char* const szKillaStickyman2[] =
{
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..............kkk.........",
	"..............lll.........",
	"..............kkb.........",
	"..............kkk.........",
	"..............ma..........",
	"............aaaaaaaaa.....",
	"...........aa.ma....aa....",
	"..........aa..aa....b.....",
	".........aa....mm.........",
	"..........b....gm.........",
	"..............hmm.........",
	"............llllmm........",
	"...........ll.ll..........",
	"..........ll...ll.........",
	".........ll.....ll........",
	"........ll.......ll.......",
	"........ll........ll......",
	".......ll..........ll.....",
	".......ll...........ll....",
	".......ll.................",
};

static const char* const szKillaStickyman3[] =
{
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..................aaa.....",
	".................aa.......",
	"....kkk........aaa........",
	"....klb....m..aa..........",
	"....klk..ghaaaa...........",
	"....kkkaaaa..mmmmmmmm.....",
	"......aa...llll.....mm....",
	".....ll.ll.ll.ll..........",
	"....ll..ll..ll.ll.........",
	"...ll...ll...ll.ll........",
	"...ll....ll..ll..ll.......",
	"..ll.....ll...ll..........",
	"..ll.....ll....ll.........",
	"..........ll..............",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
};

static const char* const* const szKillaStickymanF[] = { szKillaStickyman0, szKillaStickyman1, szKillaStickyman2, szKillaStickyman3 };

static const Color_t colKillaSkull[] = { { 236, 236, 232, 255 }, { 198, 194, 188, 255 }, { 152, 148, 142, 255 }, { 22, 22, 26, 255 }, { 90, 88, 86, 255 }, { 120, 60, 50, 255 } };

static const char* const szKillaSkull0[] =
{
	"..........................",
	".......aaabbaaaa..........",
	".....aaabbaaaabbaa........",
	"....aabbaaaaaaaabba.......",
	"...aabbaaaaaaaaaabba......",
	"...abaaaaaaaaaaaaaba......",
	"..aabaaaaaaaaaaaaabaab....",
	"..abaaaaaaaaaaaaaaaba.....",
	"..abaeeeeaaaaaeeeeaba.....",
	"..abaddddaaaaaddddaba.....",
	"..abaddddaaaaaddddaba.....",
	"..abaddddaaaaaddddaba.....",
	"..abaedddaaaaadddaba......",
	"..abaaaaaaaaaaaaaba.......",
	"..abaaaaaaeddaaaaba.......",
	"..abaaaaaaeddaaaaba.......",
	"..abaaaaaaaeaaaaaba.......",
	"..abaaaaaaaaaaaaaba.......",
	"...abaeacaeaacaeaba.......",
	"...abaeacaeaacaeaba.......",
	"....aabbaaaaaabba.........",
	".....aabbbbbbbaa..........",
	".......aaaaaaa............",
	"..........................",
};

static const char* const szKillaSkull1[] =
{
	"..........................",
	".......aaabbaaaa..........",
	".....aaabbaaaabbaa........",
	"....aabbacddcaaabba.......",
	"...aabbacddddcaaabba......",
	"...abaaaacdfcaaaaaba......",
	"..aabaaaaafcaaaaaabaab....",
	"..abaaaaaaaaaaaaaaaba.....",
	"..abaeeeeaaaaaeeeeaba.....",
	"..abaddddaaaaaddddaba.....",
	"..abaddddaaaaaddddaba.....",
	"..abaddddaaaaaddddaba.....",
	"..abaedddaaaaadddaba......",
	"..abaaaaaaaaaaaaaba.......",
	"..abaaaaaaeddaaaaba.......",
	"..abaaaaaaeddaaaaba.......",
	"..abaaaaaaaeaaaaaba.......",
	"..abaaaaaaaaaaaaaba.......",
	"...abaeacaeaacaeaba.......",
	"...abaeacaeaacaeaba.......",
	"....aabbaaaaaabba.........",
	".....aabbbbbbbaa..........",
	".......aaaaaaa............",
	"..........................",
};

static const char* const szKillaSkull2[] =
{
	"..........................",
	".......aaabbaaaa..........",
	".....aaacbaaaccbaa........",
	"....aabcacddcfcabba.......",
	"...aabbacddddccaabba......",
	"...abaacfcdfcccaaaba......",
	"..aabaaccafccafaaabaab....",
	"..abaacaaaaaaaacaaaba.....",
	"..abaeeeeaaaaaeeeeaba.....",
	"..abaddddaaaaaddddaba.....",
	"..abaddddaaaaaddddaba.....",
	"..abaddddaaaaaddddaba.....",
	"..abaedddaaaaadddaba......",
	"..abaaaaaaaaaaaaaba.......",
	"..abaaaaaaeddaaaaba.......",
	"..abaaaaaaeddaaaaba.......",
	"..abaaaaaaaeaaaaaba.......",
	"..abaaaaaaaaaaaaaba.......",
	"...abaeacaeaacaeaba.......",
	"...abaeacaeaacaeaba.......",
	"....aabbaaaaaabba.........",
	".....aabbbbbbbaa..........",
	".......aaaaaaa............",
	"..........................",
};

static const char* const* const szKillaSkullF[] = { szKillaSkull0, szKillaSkull1, szKillaSkull2 };

static const Color_t colKillaArrow[] = { { 122, 126, 134, 255 }, { 198, 202, 210, 255 }, { 146, 108, 62, 255 }, { 100, 72, 42, 255 }, { 236, 234, 228, 255 }, { 170, 168, 162, 255 } };

static const char* const szKillaArrow[] =
{
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..fff.....................",
	".eeeee.......aabb.........",
	"d..dcccccccccaabbb........",
	".eeeee.......aabb.........",
	"..fff.....................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
};

static const Color_t colKillaFlaregun[] = { { 38, 38, 42, 255 }, { 84, 84, 94, 255 }, { 120, 120, 132, 255 }, { 160, 86, 44, 255 }, { 20, 16, 14, 255 } };

static const char* const szKillaFlaregun[] =
{
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"...............aaa........",
	"...aa..........aaa........",
	"...aabb.........bbcc......",
	"...aabbcccccccccbbbb......",
	"...aaeebbbbbbbbbbbbb......",
	"...aabbbbbbbbbbbbbbb......",
	"...aabb.......bbbbbb......",
	"...aa.........bccbbb......",
	"..............bbadd.......",
	"................add.......",
	"................add.......",
	"................add.......",
	"................add.......",
	"................aaa.......",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
};

static const Color_t colKillaFirepool[] = { { 255, 170, 60, 255 }, { 255, 224, 110, 255 }, { 200, 60, 20, 255 }, { 255, 240, 190, 255 } };

static const char* const szKillaFirepool0[] =
{
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	".............c............",
	"........c....c............",
	"......c.cd...c....c.......",
	".....c..c...ca.d.c..c.....",
	".....a.ca..c.ac..a..c.....",
	"..c..a.ca..a.ac..a.ca.....",
	"..a.da.aa..a.aa..a.aa.....",
	"..a..a.aa..a.aa..a.aa.....",
	"...b.b.b.b.b.b.b.b.b.b....",
	"..ca.aca.aca.aca.aca.aca..",
	"..........................",
	"..........................",
};

static const char* const szKillaFirepool1[] =
{
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........c...............",
	"........cdc...............",
	"...c...c..c.d..c..........",
	"...c...c.ca....c....c.....",
	"...ac..a.ca...ca...c......",
	"...ac..a.aa..c.ac..a.c....",
	"...aa..a.aa..a.aa..a.a....",
	"...aa..a.aa..a.aa..a.a....",
	"...b.b.b.b.b.b.b.b.b.b....",
	"..c.cac.cac.cac.cac.cac...",
	"..........................",
	"..........................",
};

static const char* const szKillaFirepool2[] =
{
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........c...............",
	".....c...c..d.............",
	".....c...c..c.......d.c...",
	".....cc..a..c....c...c....",
	"....cac..a.ca....c...a....",
	"...c.aa..a.ca..dcac..a....",
	"...a.aa..a.aa..a.aa..a....",
	"...a.aa..a.aa..a.aa..a....",
	"...b.b.b.b.b.b.b.b.b.b....",
	"...aca.aca.aca.aca.aca.a..",
	"..........................",
	"..........................",
};

static const char* const szKillaFirepool3[] =
{
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	"..........................",
	".......c....d.............",
	"..c....c..................",
	"..c....c....c......c......",
	"..c.d.ca.d.c..c....c......",
	"..a..c.ac..a..cd...ac.....",
	"..a..a.ac..a.ca...cac.....",
	"..a..a.aa..a.aa..a.aa.....",
	"..a..a.aa..a.aa..a.aa.....",
	"...b.b.b.b.b.b.b.b.b.b....",
	"..cac.cac.cac.cac.cac.ca..",
	"..........................",
	"..........................",
};

static const char* const* const szKillaFirepoolF[] = { szKillaFirepool0, szKillaFirepool1, szKillaFirepool2, szKillaFirepool3 };

static const Color_t colKillaShotgun[] = { { 63, 66, 70, 255 }, { 57, 61, 65, 255 }, { 166, 175, 185, 255 }, { 152, 163, 173, 255 }, { 26, 24, 23, 255 }, { 47, 48, 50, 255 }, { 124, 128, 134, 255 }, { 91, 93, 96, 255 }, { 71, 65, 61, 255 }, { 80, 81, 84, 255 }, { 44, 39, 36, 255 }, { 34, 35, 36, 255 }, { 16, 14, 13, 255 }, { 30, 30, 31, 255 } };

static const char* const szKillaShotgun[] =
{
	"..........................",
	"..........................",
	"....................aab...",
	"..................aaccdbe.",
	"................facddghiie",
	"...............fgddgjhjkki",
	"..............lhjhhihhmkki",
	"............llhjjijhjn.mkm",
	"...........lhjjjkihjn...m.",
	"..........njjhiiikmn......",
	"........nnjjjkiiem........",
	".......njjhiiikm..........",
	".....lnjjjiijjn...........",
	"....lhjhiiijjn............",
	"..nnjjhiiijiie............",
	".njjhjiiiiikm.............",
	"njjhiiiiikmm..............",
	"kkjkiiiikm................",
	"kkkijkkmm.................",
	"mmkkmmm...................",
	"..mm......................",
	"..........................",
	"..........................",
	"..........................",
};

static const Color_t colKillaScatter[] = { { 49, 49, 49, 255 }, { 36, 36, 36, 255 }, { 129, 131, 130, 255 }, { 96, 97, 96, 255 }, { 59, 59, 58, 255 }, { 52, 44, 41, 255 }, { 22, 22, 22, 255 }, { 76, 78, 77, 255 }, { 8, 7, 7, 255 }, { 23, 21, 21, 255 }, { 25, 26, 25, 255 }, { 68, 69, 68, 255 }, { 28, 29, 29, 255 }, { 19, 16, 15, 255 } };

static const char* const szKillaScatter[] =
{
	"..........................",
	"..........................",
	"..........................",
	"...................aaabb..",
	"...............abbacccddb.",
	".............aacddddefffeg",
	"............acdddccfefffff",
	"...........geehcccchffijji",
	"..........klfjjdccdhjiiji.",
	"........bmhffelehdhhlk.i..",
	".......bdlllehddfhhhm.....",
	"......mhlhlhhdhejlhlk.....",
	"....bmhlllhddlefjeeg......",
	"...bdllllhdhefffjig.......",
	".bmhllllddlefffji.........",
	"bdllllhddeffffni..........",
	"jffelddhefffnn............",
	"jjjjddlefffn..............",
	"ijjjjefffnn...............",
	".iijjfjin.................",
	"...iini...................",
	"..........................",
	"..........................",
	"..........................",
};

static const Color_t colKillaPistol[] = { { 62, 68, 76, 255 }, { 165, 180, 202, 255 }, { 144, 157, 173, 255 }, { 54, 59, 65, 255 }, { 116, 124, 135, 255 }, { 44, 47, 51, 255 }, { 97, 98, 105, 255 }, { 36, 37, 39, 255 }, { 72, 68, 67, 255 }, { 27, 25, 25, 255 }, { 17, 17, 18, 255 }, { 46, 45, 48, 255 }, { 21, 19, 24, 255 }, { 7, 7, 9, 255 } };

static const char* const szKillaPistol[] =
{
	"...............abcdd......",
	".............aabbcccd.....",
	"............abbbbbccef....",
	"...........abbbbbcceegh...",
	".........aabbbbbbeggggh...",
	"........abbbbceccggggij...",
	"......fdcbcceeeeeggggh....",
	".....fecccceeegeeggggh....",
	"....dccccccegggegggggh....",
	"...hggeccccggeegggiiij....",
	"..hggecccceeeeiiiiiigh....",
	".klligcceeeggiliiiiiigh...",
	".klmmlceggggillliiiiigh...",
	"..nmmlggiiiimlmlliiiiij...",
	"..nmmmiiiiilmlmlliiiiiij..",
	"...nnmlilmllllllliiiiiij..",
	".....nknmmmllllmlliiiiij..",
	"........nmmiiimmmliiiij...",
	".........nklinmmmliiiij...",
	"...........kjnmmmlliij....",
	".............nmmmllij.....",
	".............nmmmllk......",
	".............nmmmlk.......",
	"..............nnmlk.......",
};

static const Color_t colKillaSmg[] = { { 28, 28, 25, 255 }, { 76, 74, 68, 255 }, { 86, 84, 78, 255 }, { 32, 31, 29, 255 }, { 19, 17, 17, 255 }, { 50, 47, 45, 255 }, { 64, 62, 57, 255 }, { 27, 23, 22, 255 }, { 24, 23, 21, 255 }, { 39, 37, 33, 255 }, { 103, 99, 89, 255 }, { 87, 71, 58, 255 }, { 33, 26, 22, 255 }, { 10, 8, 8, 255 } };

static const char* const szKillaSmg[] =
{
	"...................abcd...",
	"..................efggcd..",
	"..................efghgcd.",
	"..................igbggcd.",
	".................jkccbbgi.",
	"...............jjkkcbgba..",
	"..............jkkcccgfllm.",
	".............jkkcgggffllm.",
	"............jkkkbbbbghgllm",
	"..........jjkkcbbbbanhhlm.",
	"....dj...jkkkbfhbbbanhhlm.",
	"...dckj.jkkkbanhfba..nnm..",
	"..efgbkjkkkgi.nhfgi.......",
	"..efffbkkcdi..nhhgi.......",
	"...nhfckba....nhfgi.......",
	"..jkcbkba.....nhhgi.......",
	".dckkkba......nhhgi.......",
	"nhhbcgi.......nhhgi.......",
	"nhhfgi........nhhfe.......",
	".nhni.........nhhfe.......",
	"..n...........nhhfe.......",
	"..............nhhfe.......",
	"..............nhffe.......",
	"..............nhfe........",
};

void CStatusWindow::DrawKillAnim(int iAnim, int ax, int ay, int aw, int ah, float t)
{
	const int cx = ax + (aw / 2);
	const int cy = ay + (ah / 2);

	auto PXR = [&](const char* const* pszRows, const Color_t* pPal, int nOffX, int nOffY)
	{
		for (int y = 0; y < 24; y++)
		{
			const char* pszRow = pszRows[y];
			int x = 0;
			while (x < 26)
			{
				const char c = pszRow[x];
				if (c == '.')
				{
					x++;
					continue;
				}
				int w = 1;
				while (x + w < 26 && pszRow[x + w] == c)
					w++;
				H::Draw->Rect(ax + 16 + nOffX + x * 2, ay + 6 + nOffY + y * 2, w * 2, 2, pPal[c - 'a']);
				x += w;
			}
		}
	};

	auto FRM = [](const char* const* const* pFrames, int nCount, float flFps, float flT, bool bLoop) -> const char* const*
	{
		int nF = static_cast<int>(flT * flFps);
		if (nF < 0)
			nF = 0;
		nF = bLoop ? (nF % nCount) : std::min(nF, nCount - 1);
		return pFrames[nF];
	};

	auto Shell = [&](float flT, int nSX, int nSY, int nVX)
	{
		if (flT <= 0.0f || flT >= 1.0f)
			return;
		const int nX = nSX + static_cast<int>(flT * nVX);
		const int nY = nSY + static_cast<int>(flT * flT * 34.0f);
		H::Draw->Rect(nX, nY, 3, 2, Color_t{ 225, 195, 85, static_cast<byte>(220 * (1.0f - flT)) });
	};

	auto Puff = [&](float flT, int nX, int nY, int nDrift)
	{
		if (flT <= 0.0f || flT >= 1.0f)
			return;
		H::Draw->FilledCircle(nX + static_cast<int>(flT * nDrift), nY - static_cast<int>(flT * 12.0f), 2 + static_cast<int>(flT * 6.0f), 10, Color_t{ 160, 160, 166, static_cast<byte>(140 * (1.0f - flT)) });
	};

	auto Bullet = [&](float flP, int nMX, int nMY, int nVX, int nVY)
	{
		if (flP <= 0.0f || flP >= 1.0f)
			return;
		const int nX = nMX + static_cast<int>(flP * nVX);
		const int nY = nMY + static_cast<int>(flP * nVY);
		H::Draw->Rect(nX, nY, 5, 2, Color_t{ 255, 240, 160, 235 });
		H::Draw->Rect(nX + 5, nY, 3, 2, Color_t{ 255, 200, 80, 130 });
	};

	auto Debris = [&](float flT, int nX, int nY, Color_t tCol)
	{
		if (flT <= 0.0f || flT >= 1.0f)
			return;
		for (int k = 0; k < 4; k++)
		{
			const int nDX = nX + static_cast<int>(flT * (6 + k * 5));
			const int nDY = nY - static_cast<int>(std::sin(flT * 3.14159f) * (10 + k * 3));
			H::Draw->Rect(nDX, nDY, 2, 2, Color_t{ tCol.r, tCol.g, tCol.b, static_cast<byte>(210 * (1.0f - flT)) });
		}
	};

	switch (iAnim)
	{
	case KILLA_SHOTGUN:
	{
		const float flCycle = fmodf(t, 1.1f);
		PXR(szKillaShotgun, colKillaShotgun, flCycle < 0.12f ? 2 : 0, flCycle < 0.12f ? 1 : 0);
		if (flCycle < 0.12f)
			DrawMuzzleStar(ax + 13, ay + 42, 9, Color_t{ 255, 255, 210, 255 }, Color_t{ 255, 210, 70, 255 });
		Bullet((flCycle - 0.02f) / 0.24f, ax + 11, ay + 42, -36, -8);
		Shell(flCycle * 2.0f - 0.2f, ax + 44, ay + 34, 9);
		Puff(flCycle * 1.4f - 0.18f, ax + 12, ay + 40, 8);
		break;
	}
	case KILLA_SCATTER:
	{
		const float flCycle = fmodf(t, 1.1f);
		PXR(szKillaScatter, colKillaScatter, flCycle < 0.12f ? 2 : 0, flCycle < 0.12f ? 1 : 0);
		if (flCycle < 0.12f)
			DrawMuzzleStar(ax + 16, ay + 45, 9, Color_t{ 255, 255, 210, 255 }, Color_t{ 255, 210, 70, 255 });
		Bullet((flCycle - 0.02f) / 0.24f, ax + 14, ay + 45, -36, -8);
		Shell(flCycle * 2.0f - 0.2f, ax + 44, ay + 30, 10);
		Shell(flCycle * 2.0f - 0.32f, ax + 42, ay + 36, 8);
		Puff(flCycle * 1.4f - 0.18f, ax + 12, ay + 43, 9);
		break;
	}
	case KILLA_PISTOL:
	{
		const float flCycle = fmodf(t, 0.8f);
		PXR(szKillaPistol, colKillaPistol, flCycle < 0.1f ? 2 : 0, 0);
		if (flCycle < 0.1f)
			DrawMuzzleStar(ax + 15, ay + 28, 7, Color_t{ 255, 255, 210, 255 }, Color_t{ 255, 210, 70, 255 });
		Bullet((flCycle - 0.02f) / 0.22f, ax + 13, ay + 28, -36, -8);
		Shell(flCycle * 2.4f - 0.18f, ax + 40, ay + 28, 12);
		Puff(flCycle * 1.6f - 0.15f, ax + 12, ay + 30, 7);
		break;
	}
	case KILLA_BAT:
	{
		const float flCycle = fmodf(t, 0.57f);
		PXR(FRM(szKillaBatF, 4, 7.0f, flCycle, false), colKillaBat, 0, 0);
		if (flCycle > 0.3f && flCycle < 0.52f)
		{
			DrawMuzzleStar(ax + 8, ay + 50, 7, Color_t{ 255, 255, 240, 255 }, Color_t{ 255, 230, 120, 255 });
			H::Draw->Line(ax + 6, ay + 12, ax + 12, ay + 18, Color_t{ 255, 255, 255, 190 });
			H::Draw->Line(ax + 4, ay + 19, ax + 10, ay + 21, Color_t{ 255, 255, 255, 190 });
		}
		break;
	}
	case KILLA_ROCKET:
	{
		const float flCycle = fmodf(t, 2.0f);
		const auto RocPt = [&](float flP, int& nX, int& nY)
		{
			nX = ax + 8 + static_cast<int>(flP * 58.0f);
			nY = ay + 50 - static_cast<int>(flP * 30.0f);
		};
		if (flCycle < 0.55f)
		{
			const float flP = flCycle / 0.55f;
			int nX, nY;
			RocPt(flP, nX, nY);
			for (int k = 1; k <= 5; k++)
			{
				const float flT = flP - k * 0.055f;
				if (flT <= 0.0f)
					continue;
				int nTX, nTY;
				RocPt(flT, nTX, nTY);
				H::Draw->FilledCircle(nTX - 5, nTY + 1, 2 + (5 - k) / 2, 8, Color_t{ 165, 165, 170, static_cast<byte>(std::max(0, 150 - static_cast<int>((flP - flT) * 500.0f))) });
			}
			PXR(FRM(szKillaRocketF, 2, 12.0f, t, true), colKillaRocket, nX - ax - 42, nY - ay - 30);
		}
		else if (flCycle < 1.25f)
			DrawExplosion(ax + 66, ay + 20, flCycle - 0.55f, 255);
		break;
	}
	case KILLA_SHOVEL:
	{
		const float flCycle = fmodf(t, 0.57f);
		PXR(FRM(szKillaShovelF, 4, 7.0f, flCycle, false), colKillaShovel, 0, 0);
		if (flCycle > 0.3f && flCycle < 0.52f)
			Debris((flCycle - 0.3f) * 4.5f, ax + 26, ay + 44, Color_t{ 122, 104, 78, 255 });
		break;
	}
	case KILLA_BURN:
		PXR(FRM(szKillaBurnF, 6, 5.0f, t, true), colKillaBurn, 0, 0);
		break;
	case KILLA_AXE:
	{
		const float flCycle = fmodf(t, 0.57f);
		PXR(FRM(szKillaAxeF, 4, 7.0f, flCycle, false), colKillaAxe, 0, 0);
		if (flCycle > 0.3f && flCycle < 0.52f)
		{
			Debris((flCycle - 0.3f) * 4.5f, ax + 26, ay + 44, Color_t{ 110, 74, 56, 255 });
			H::Draw->Line(ax + 16, ay + 40, ax + 26, ay + 48, Color_t{ 255, 240, 200, 160 });
		}
		break;
	}
	case KILLA_STICKYARC:
	{
		const float flCycle = fmodf(t, 2.3f);
		const auto ArcPt = [&](float flK, int& nX, int& nY)
		{
			nX = ax + 8 + static_cast<int>(flK * 44.0f);
			nY = ay + 50 - static_cast<int>(59.0f * flK) + static_cast<int>(45.0f * flK * flK);
		};
		H::Draw->Rect(ax + 18, ay + 50, 52, 2, Color_t{ 44, 47, 51, 255 });
		if (flCycle < 1.35f)
		{
			PXR(FRM(szKillaStickymanF, 3, 8.0f, t, true), colKillaStickyman, 0, 0);
			for (int k = 0; k < 9; k++)
			{
				const float flK = (k + 0.5f) / 9.0f;
				int nDX, nDY;
				ArcPt(flK, nDX, nDY);
				H::Draw->Rect(nDX - 1, nDY - 1, 2, 2, Color_t{ 200, 210, 190, static_cast<byte>(190 - flK * 150) });
			}
			const float flP = flCycle / 1.35f;
			int nGX, nGY;
			ArcPt(flP, nGX, nGY);
			H::Draw->FilledCircle(nGX, nGY, 4, 10, Color_t{ 77, 92, 52, 255 });
			H::Draw->OutlinedCircle(nGX, nGY, 4, 10, Color_t{ 55, 70, 31, 255 });
			H::Draw->Rect(nGX - 1, nGY - 3, 2, 2, Color_t{ 180, 190, 170, 220 });
		}
		else if (flCycle < 1.55f)
			DrawExplosion(ax + 52, ay + 38, (flCycle - 1.35f) * 3.0f, 255);
		else
		{
			const float flP = (flCycle - 1.55f) / 0.75f;
			const int nRX = static_cast<int>(flP * 22.0f);
			const int nRY = -static_cast<int>(std::sin(flP * 3.14159f) * 12.0f) + static_cast<int>(flP * 14.0f);
			const int nJit = (static_cast<int>(t * 24.0f) % 2) ? 1 : -1;
			PXR(szKillaStickyman3, colKillaStickyman, nRX, nRY + nJit);
		}
		break;
	}
	case KILLA_BOTTLE:
	{
		const float flCycle = fmodf(t, 1.7f);
		PXR(FRM(szKillaBottleF, 5, 7.0f, flCycle, false), colKillaBottle, 0, 0);
		if (flCycle > 0.57f && flCycle < 1.5f)
		{
			const float flP = (flCycle - 0.57f) / 0.93f;
			for (int k = 0; k < 2; k++)
			{
				const int nLX = ax + 30 + k * 9 + static_cast<int>(flP * 8.0f);
				const int nLY = ay + 44 + static_cast<int>(flP * flP * 10.0f);
				H::Draw->Rect(nLX, nLY, 2, 3, Color_t{ 82, 58, 31, static_cast<byte>(200 * (1.0f - flP)) });
			}
		}
		break;
	}
	case KILLA_MINIGUN:
	{
		const int nJx = static_cast<int>(t * 53.0f) % 3 - 1;
		const int nJy = static_cast<int>(t * 71.0f) % 3 - 1;
		PXR(FRM(szKillaMinigunF, 3, 16.0f, t, true), colKillaMinigun, nJx, nJy);
		if (fmodf(t, 0.14f) < 0.055f)
		{
			const float flF = fmodf(t, 0.14f) / 0.055f;
			const int nS = 9 - static_cast<int>(flF * 3.0f);
			H::Draw->FilledCircle(ax + 18, ay + 46, std::max(4, nS / 2), 10, Color_t{ 255, 245, 190, 255 });
			H::Draw->FilledTriangle({ Vec2(ax + 18, ay + 46), Vec2(ax + 13 - nS, ay + 49), Vec2(ax + 18 - nS, ay + 53) }, Color_t{ 255, 210, 80, 220 });
			H::Draw->FilledTriangle({ Vec2(ax + 18, ay + 46), Vec2(ax + 16 - nS, ay + 42), Vec2(ax + 22 - nS, ay + 48) }, Color_t{ 255, 210, 80, 220 });
			Bullet((fmodf(t, 0.14f) + 0.02f) / 0.14f, ax + 18, ay + 46, -32, 26);
		}
		Shell(fmodf(t * 1.4f, 1.0f), ax + 52, ay + 20, 12);
		Shell(fmodf(t * 1.4f + 0.45f, 1.0f), ax + 54, ay + 24, 10);
		Shell(fmodf(t * 1.4f + 0.8f, 1.0f), ax + 50, ay + 22, 13);
		break;
	}
	case KILLA_WRENCH:
	{
		const float flCycle = fmodf(t, 0.57f);
		PXR(FRM(szKillaWrenchF, 4, 7.0f, flCycle, false), colKillaWrench, 0, 0);
		if (flCycle > 0.3f && flCycle < 0.52f)
		{
			const float flP = (flCycle - 0.3f) * 4.5f;
			for (int k = 0; k < 5; k++)
			{
				const float flAng = -0.4f - k * 0.28f;
				H::Draw->Line(ax + 24, ay + 44, ax + 24 + static_cast<int>(std::cos(flAng) * (4 + flP * 10.0f)), ay + 44 + static_cast<int>(std::sin(flAng) * (4 + flP * 10.0f)), Color_t{ 255, 230, 120, static_cast<byte>(220 * (1.0f - flP)) });
			}
		}
		break;
	}
	case KILLA_SENTRY:
	{
		const bool bRed = m_bSpreeTeamRed;
		PXR(FRM(bRed ? szKillaSentryF : szKillaSentryBlueF, 2, 3.0f, t, true), bRed ? colKillaSentry : colKillaSentryBlue, 0, 0);
		if (fmodf(t, 1.1f) < 0.1f)
			H::Draw->FilledCircle(ax + 50, ay + 27, 2, 8, bRed ? Color_t{ 255, 70, 60, 255 } : Color_t{ 100, 170, 255, 255 });
		if (fmodf(t, 0.16f) < 0.09f)
		{
			DrawMuzzleStar(ax + 66, ay + 27, 8, Color_t{ 255, 255, 215, 255 }, Color_t{ 255, 205, 70, 255 });
			H::Draw->Line(ax + 71, ay + 27, ax + aw, ay + 24 + (rand() % 5), Color_t{ 255, 225, 100, 190 });
		}
		break;
	}
	case KILLA_SKULLBOW:
	{
		const float flCycle = fmodf(t, 1.6f);
		if (flCycle < 0.22f)
		{
			PXR(szKillaSkull0, colKillaSkull, 0, 0);
			const float flP = flCycle / 0.22f;
			PXR(szKillaArrow, colKillaArrow, -46 + static_cast<int>(flP * 32.0f), -20 + static_cast<int>(flP * 4.0f));
		}
		else
		{
			const float flHT = flCycle - 0.22f;
			const int nShake = (flHT < 0.25f && (static_cast<int>(t * 30.0f) % 2)) ? 1 : -1;
			PXR(flHT > 0.25f ? szKillaSkull2 : szKillaSkull1, colKillaSkull, nShake, 0);
			PXR(szKillaArrow, colKillaArrow, -14, -16);
			if (flHT < 0.1f)
				H::Draw->OutlinedCircle(ax + 36, ay + 14, 3 + static_cast<int>(flHT * 45.0f), 14, Color_t{ 255, 255, 255, static_cast<byte>(210 * (1.0f - flHT / 0.1f)) });
			if (flHT < 0.35f)
			{
				for (int n = 0; n < 5; n++)
				{
					const float flAng = 2.1f + (n * 0.9f);
					const int nD = 4 + static_cast<int>(flHT * 42.0f);
					H::Draw->Rect(ax + 36 + static_cast<int>(std::cos(flAng) * nD), ay + 14 + static_cast<int>(std::sin(flAng) * nD), 2, 2, Color_t{ 190, 40, 40, static_cast<byte>(220 * (1.0f - flHT / 0.35f)) });
				}
			}
		}
		break;
	}
	case KILLA_SMG:
	{
		const float flCycle = fmodf(t, 0.34f);
		const bool bFire = flCycle < 0.12f;
		PXR(szKillaSmg, colKillaSmg, bFire ? 2 : 0, 0);
		if (bFire)
			DrawMuzzleStar(ax + 13, ay + 43, 7, Color_t{ 255, 255, 210, 255 }, Color_t{ 255, 210, 70, 255 });
		Bullet((flCycle - 0.02f) / 0.18f, ax + 11, ay + 43, -36, -8);
		Shell(flCycle * 2.6f - 0.15f, ax + 42, ay + 34, 12);
		break;
	}
	case KILLA_KUKRI:
	{
		const float flCycle = fmodf(t, 0.57f);
		PXR(FRM(szKillaKukriF, 4, 7.0f, flCycle, false), colKillaKukri, 0, 0);
		if (flCycle > 0.2f && flCycle < 0.44f)
			H::Draw->Arc(ax + 22, ay + 36, 14, 2, -1.3f, 0.6f, Color_t{ 220, 60, 55, static_cast<byte>(220 * (1.0f - (flCycle - 0.2f) / 0.24f)) });
		break;
	}
	case KILLA_REVOLVER:
	{
		const float flCycle = fmodf(t, 0.95f);
		const int nRx = flCycle < 0.22f ? static_cast<int>(6.0f * (1.0f - flCycle / 0.22f)) : 0;
		const int nRy = flCycle < 0.22f ? static_cast<int>(-3.0f * (1.0f - flCycle / 0.22f)) : 0;
		PXR(FRM(szKillaRevolverF, 3, 10.0f, t, true), colKillaRevolver, nRx, nRy);
		if (flCycle < 0.1f)
		{
			const int nS = 9 - static_cast<int>(flCycle / 0.1f * 3.0f);
			H::Draw->FilledCircle(ax + 18 + nRx, ay + 46 + nRy, std::max(4, nS / 2), 10, Color_t{ 255, 245, 190, 255 });
			H::Draw->FilledTriangle({ Vec2(ax + 18 + nRx, ay + 46 + nRy), Vec2(ax + 13 + nRx - nS, ay + 49 + nRy), Vec2(ax + 18 + nRx - nS, ay + 53 + nRy) }, Color_t{ 255, 210, 80, 220 });
			H::Draw->FilledTriangle({ Vec2(ax + 18 + nRx, ay + 46 + nRy), Vec2(ax + 16 + nRx - nS, ay + 42 + nRy), Vec2(ax + 22 + nRx - nS, ay + 48 + nRy) }, Color_t{ 255, 210, 80, 220 });
		}
		Bullet((flCycle - 0.02f) / 0.24f, ax + 18 + nRx, ay + 46 + nRy, -32, 26);
		Shell(flCycle * 2.2f - 0.15f, ax + 40, ay + 26, 11);
		Puff(flCycle * 1.5f - 0.12f, ax + 16, ay + 42, 7);
		break;
	}
	case KILLA_KNIFE:
	{
		const float flCycle = fmodf(t, 1.1f);
		if (flCycle < 0.32f)
			PXR(FRM(szKillaKnifeF, 4, 12.5f, flCycle, false), colKillaKnife, 0, 0);
		else
			PXR(szKillaKnife3, colKillaKnife, 0, static_cast<int>(std::sin(t * 6.0f) * 2.0f));
		if (flCycle > 0.26f && flCycle < 0.48f)
			H::Draw->Arc(ax + 18, ay + 34, 15, 2, -0.9f, 1.1f, Color_t{ 225, 230, 240, static_cast<byte>(200 * (1.0f - (flCycle - 0.26f) / 0.22f)) });
		break;
	}
	case KILLA_FLARE:
	{
		const float flCycle = fmodf(t, 2.0f);
		const int nRx = flCycle < 0.15f ? static_cast<int>(3.0f * (1.0f - flCycle / 0.15f)) : 0;
		PXR(szKillaFlaregun, colKillaFlaregun, nRx, 0);
		if (flCycle < 0.12f)
			DrawMuzzleStar(ax + 24, ay + 26, 7, Color_t{ 255, 255, 215, 255 }, Color_t{ 255, 200, 60, 255 });
		const float flT = flCycle - 0.05f;
		if (flT > 0.0f && flT < 0.85f)
		{
			const float flP = flT / 0.85f;
			const auto FlPt = [&](float flQ, int& nQX, int& nQY)
			{
				nQX = ax + 24 + static_cast<int>(std::sin(flQ * 3.14159f) * 10.0f) - static_cast<int>(flQ * 14.0f);
				nQY = ay + 24 - static_cast<int>(std::sin(flQ * 3.14159f) * 22.0f) + static_cast<int>(flQ * 24.0f);
			};
			for (int k = 4; k >= 1; k--)
			{
				const float flT2 = flP - k * 0.07f;
				if (flT2 <= 0.0f)
					continue;
				int nTX, nTY;
				FlPt(flT2, nTX, nTY);
				H::Draw->FilledCircle(nTX, nTY, 3 - k / 2, 6, Color_t{ 255, 140, 50, static_cast<byte>(150 - k * 30) });
			}
			int nFX, nFY;
			FlPt(flP, nFX, nFY);
			H::Draw->FilledCircle(nFX, nFY, 4, 8, Color_t{ 255, 160, 60, 255 });
			H::Draw->FilledCircle(nFX, nFY, 2, 6, Color_t{ 255, 240, 170, 255 });
		}
		if (flCycle > 0.9f)
		{
			const int nRise = flCycle < 1.1f ? static_cast<int>((1.1f - flCycle) * 40.0f) : 0;
			PXR(FRM(szKillaFirepoolF, 4, 9.0f, t, true), colKillaFirepool, -20, 2 + nRise);
		}
		break;
	}
	case KILLA_GLOVES:
	{
		const bool bAlt = (static_cast<int>(t * 3.0f) % 2) == 0;
		const int nFX = cx - 6 + (bAlt ? 8 : 0);
		const int nFY = cy - 4 + (bAlt ? -2 : 4);
		H::Draw->FillRectRounded(cx - 30 - (bAlt ? 0 : 6), cy - 2 + (bAlt ? 4 : -4), 18, 15, 6, Color_t{ 150, 42, 38, 255 });
		H::Draw->FilledCircle(cx - 30 - (bAlt ? 0 : 6) + 14, cy - 2 + (bAlt ? 4 : -4) + 12, 4, 8, Color_t{ 150, 42, 38, 255 });
		H::Draw->Rect(cx - 36 - (bAlt ? 0 : 6), cy - 2 + (bAlt ? 4 : -4) + 2, 5, 11, Color_t{ 228, 224, 214, 255 });
		H::Draw->FillRectRounded(nFX - 10, nFY - 5, 20, 16, 6, Color_t{ 205, 56, 49, 255 });
		H::Draw->FilledCircle(nFX + 8, nFY + 8, 5, 8, Color_t{ 205, 56, 49, 255 });
		H::Draw->Rect(nFX - 15, nFY - 2, 6, 12, Color_t{ 238, 234, 224, 255 });
		if (bAlt)
		{
			H::Draw->Line(nFX + 12, nFY - 2, nFX + 20, nFY - 6, Color_t{ 255, 255, 255, 200 });
			H::Draw->Line(nFX + 12, nFY + 2, nFX + 21, nFY + 2, Color_t{ 255, 255, 255, 200 });
			H::Draw->Line(nFX + 12, nFY + 6, nFX + 20, nFY + 10, Color_t{ 255, 255, 255, 200 });
		}
		break;
	}
	case KILLA_MEDIC:
	{
		const int nCX = ax + (aw / 2) - 4;
		const int nCY = ay + (ah / 2);
		const float flPulse = (std::sin(t * 4.0f) + 1.0f) * 0.5f;
		H::Draw->OutlinedCircle(nCX, nCY, 15 + static_cast<int>(flPulse * 5.0f), 20, Color_t{ 255, 255, 255, static_cast<byte>(70 + (flPulse * 80.0f)) });
		H::Draw->FillRectRounded(nCX - 11, nCY - 11, 22, 22, 4, Color_t{ 240, 240, 240, 245 });
		H::Draw->Rect(nCX - 2, nCY - 8, 4, 16, Color_t{ 205, 60, 60, 255 });
		H::Draw->Rect(nCX - 8, nCY - 2, 16, 4, Color_t{ 205, 60, 60, 255 });
		for (int k = 0; k < 3; k++)
		{
			const float flPT = fmodf(t * 1.2f + (k * 0.33f), 1.0f);
			const int nPX = nCX + static_cast<int>(std::sin(k * 2.1f + t) * 11.0f);
			const int nPY = nCY + 12 - static_cast<int>(flPT * 26.0f);
			H::Draw->Rect(nPX - 1, nPY - 3, 2, 6, Color_t{ 255, 255, 255, static_cast<byte>(210 * (1.0f - flPT)) });
			H::Draw->Rect(nPX - 3, nPY - 1, 6, 2, Color_t{ 255, 255, 255, static_cast<byte>(210 * (1.0f - flPT)) });
		}
		break;
	}
	case KILLA_SKULL:
	{
		constexpr float flHit = 0.45f;
		const bool bHS = m_bSpreeHeadshot;
		if (!bHS)
		{
			PXR(szKillaSkull0, colKillaSkull, 0, static_cast<int>(std::sin(t * 3.0f) * 2.0f));
			break;
		}
		if (t < flHit)
		{
			PXR(szKillaSkull0, colKillaSkull, 0, 0);
			const int nGap = std::max(2, 16 - static_cast<int>(t * 34.0f));
			H::Draw->Line(ax + 36 - nGap - 8, ay + 14, ax + 36 - nGap, ay + 14, Color_t{ 235, 70, 70, 255 });
			H::Draw->Line(ax + 36 + nGap + 8, ay + 14, ax + 36 + nGap, ay + 14, Color_t{ 235, 70, 70, 255 });
			H::Draw->Line(ax + 36, ay + 14 - nGap - 5, ax + 36, ay + 14 - nGap, Color_t{ 235, 70, 70, 255 });
			H::Draw->Line(ax + 36, ay + 14 + nGap + 5, ax + 36, ay + 14 + nGap, Color_t{ 235, 70, 70, 255 });
		}
		else
		{
			const float flHT = t - flHit;
			const int nShake = (flHT < 0.3f && (static_cast<int>(t * 30.0f) % 2)) ? 1 : -1;
			PXR(flHT > 0.3f ? szKillaSkull2 : szKillaSkull1, colKillaSkull, nShake, 0);
			if (flHT < 0.12f)
				H::Draw->OutlinedCircle(ax + 36, ay + 14, 4 + static_cast<int>(flHT * 40.0f), 16, Color_t{ 255, 255, 255, static_cast<byte>(220 * (1.0f - flHT / 0.12f)) });
			if (flHT < 0.45f)
			{
				for (int n = 0; n < 6; n++)
				{
					const float flAng = 0.5f + (n * 1.04f);
					const int nD = 3 + static_cast<int>(flHT * 34.0f);
					H::Draw->Rect(ax + 36 + static_cast<int>(std::cos(flAng) * nD), ay + 14 + static_cast<int>(std::sin(flAng) * nD), 2, 2, Color_t{ 190, 40, 40, static_cast<byte>(230 * (1.0f - flHT / 0.45f)) });
				}
				for (int n = 0; n < 4; n++)
				{
					const float flAng = 1.2f + (n * 0.95f);
					const int nD = 5 + static_cast<int>(flHT * 26.0f);
					H::Draw->Line(ax + 36, ay + 14, ax + 36 + static_cast<int>(std::cos(flAng) * nD), ay + 14 + static_cast<int>(std::sin(flAng) * nD), Color_t{ 120, 120, 118, static_cast<byte>(210 * (1.0f - flHT / 0.45f)) });
				}
			}
			const int nDrop = std::min(12, static_cast<int>((flHT - 0.4f) * 14.0f));
			if (nDrop > 0)
			{
				H::Draw->Rect(ax + 35, ay + 16, 1, nDrop, Color_t{ 150, 40, 40, 220 });
				H::Draw->Rect(ax + 39, ay + 16, 1, std::max(0, nDrop - 4), Color_t{ 150, 40, 40, 170 });
			}
		}
		break;
	}
	default:
		break;
	}
}

void CStatusWindow::DrawScrambledText(const CFont& font, int nX, int nY, Color_t tColor, const char* pszText, int nAlign, float flPhase)
{
	static constexpr char szGlitch[] = "!<>?/\\*+=~#%&@$0123456789ABCDEFXYZ";

	const size_t nLen = strlen(pszText);
	if (!nLen || nLen >= 64)
		return;

	wchar_t wszFull[64] = {};
	for (size_t n = 0; n < nLen; n++)
		wszFull[n] = static_cast<wchar_t>(static_cast<unsigned char>(pszText[n]));

	int w = 0, h = 0;
	I::MatSystemSurface->GetTextSize(font.m_dwFont, wszFull, w, h);

	if (nAlign == 1)
		nX -= w / 2;
	else if (nAlign == 2)
		nX -= w;

	nY -= h / 2;

	const int nJitterX = static_cast<int>(std::sin(I::GlobalVars->realtime * 31.0f + flPhase) * 2.0f);
	const int nJitterY = static_cast<int>(std::cos(I::GlobalVars->realtime * 43.0f + flPhase) * 2.0f);

	for (size_t n = 0; n < nLen; n++)
	{
		const char c = (rand() % 4 == 0) ? szGlitch[rand() % (sizeof(szGlitch) - 1)] : pszText[n];
		H::Draw->String(font, nX + nJitterX, nY + nJitterY, tColor, POS_DEFAULT, L"%lc", static_cast<wchar_t>(static_cast<unsigned char>(c)));

		const wchar_t wszOne[2] = { static_cast<wchar_t>(static_cast<unsigned char>(pszText[n])), L'\0' };
		int nCharW = 0, nCharH = 0;
		I::MatSystemSurface->GetTextSize(font.m_dwFont, wszOne, nCharW, nCharH);
		nX += nCharW;
	}
}

void CStatusWindow::DrawRainbowText(const CFont& font, int nX, int nY, const char* pszText, int nAlign, float flNow)
{
	const size_t nLen = strlen(pszText);
	if (!nLen || nLen >= 64)
		return;

	wchar_t wszFull[64] = {};
	for (size_t n = 0; n < nLen; n++)
		wszFull[n] = static_cast<wchar_t>(static_cast<unsigned char>(pszText[n]));

	int w = 0, h = 0;
	I::MatSystemSurface->GetTextSize(font.m_dwFont, wszFull, w, h);

	if (nAlign == 1)
		nX -= w / 2;
	else if (nAlign == 2)
		nX -= w;

	nY -= h / 2;

	for (size_t n = 0; n < nLen; n++)
	{
		const Color_t tLetter = HueColor(flNow * 2.0f + n * 0.35f, 255);
		H::Draw->String(font, nX, nY, tLetter, POS_DEFAULT, L"%lc", wszFull[n]);

		const wchar_t wszOne[2] = { wszFull[n], L'\0' };
		int nCharW = 0, nCharH = 0;
		I::MatSystemSurface->GetTextSize(font.m_dwFont, wszOne, nCharW, nCharH);
		nX += nCharW;
	}
}
