#include "VisualUtils.h"

#include "Icons.h"
#include "../CFG.h"

#include "../Players/Players.h"

bool CVisualUtils::IsEntityOwnedBy(C_BaseEntity* pEntity, C_BaseEntity* pWho)
{
	switch (pEntity->GetClassId())
	{
	case ETFClassIds::CTFGrenadePipebombProjectile:
	case ETFClassIds::CTFProjectile_Jar:
	case ETFClassIds::CTFProjectile_JarGas:
	case ETFClassIds::CTFProjectile_JarMilk:
	case ETFClassIds::CTFProjectile_Cleaver:
		{
			return pEntity->As<C_BaseGrenade>()->m_hThrower().Get() == pWho;
		}

	case ETFClassIds::CTFProjectile_SentryRocket:
		{
			if (const auto pOwner = pEntity->m_hOwnerEntity().Get())
				return pOwner->As<C_BaseObject>()->m_hBuilder() == pWho;

			break;
		}

	case ETFClassIds::CObjectSentrygun:
	case ETFClassIds::CObjectDispenser:
	case ETFClassIds::CObjectTeleporter:
		{
			return pEntity->As<C_BaseObject>()->m_hBuilder() == pWho;
		}

	default: return pEntity->m_hOwnerEntity().Get() == pWho;
	}

	return false;
}

Color_t CVisualUtils::GetAlphaColor(Color_t base, float alpha)
{
	base.a = static_cast<byte>(alpha * 255.f);
	return base;
}

Color_t CVisualUtils::GetEntityColor(C_TFPlayer* pLocal, C_BaseEntity* pEntity)
{
	if (!pLocal || !pEntity)
		return { 255, 255, 255, 255 };

	if (pEntity->entindex() == G::nTargetIndex)
		return CFG::Color_Target;

	const auto nClassId = pEntity->GetClassId();
	
	if (nClassId == ETFClassIds::CTFPlayer)
	{
		const auto pPlayer = pEntity->As<C_TFPlayer>();

		if (pPlayer->IsInvulnerable())
			return CFG::Color_Invulnerable;

		if (pPlayer->IsInvisible())
			return CFG::Color_Invisible;

		if (pPlayer != pLocal && pPlayer->IsPlayerOnSteamFriendsList())
			return CFG::Color_Friend;

		if (pPlayer != pLocal)
		{
			PlayerPriority info{};
			if (F::Players->GetInfo(pPlayer->entindex(), info))
			{
				if (info.Cheater)
					return CFG::Color_Cheater;
				if (info.Targeted)
					return CFG::Color_Targeted;
				if (info.Nigger)
					return CFG::Color_Nigger;
				if (info.RetardLegit)
					return CFG::Color_RetardLegit;
				if (info.Streamer)
					return CFG::Color_Streamer;
			}
		}
	}

	if (pEntity == pLocal || IsEntityOwnedBy(pEntity, pLocal))
		return CFG::Color_Local;

	const int nEntityTeam = pEntity->m_iTeamNum();
	const int nLocalTeam = pLocal->m_iTeamNum();
	
	if (nEntityTeam == nLocalTeam)
		return CFG::Color_Teammate;

	return CFG::Color_Enemy;
}

Color_t CVisualUtils::GetEntityColorForOutlines(C_TFPlayer* pLocal, C_BaseEntity* pEntity)
{
	if (!pLocal || !pEntity)
		return { 255, 255, 255, 255 };

	// Check if HP-based coloring is enabled for enemy outlines
	if (CFG::Visuals_Enemy_Outline_HP_Based && pEntity->GetClassId() == ETFClassIds::CTFPlayer)
	{
		const auto pPlayer = pEntity->As<C_TFPlayer>();
		
		// Only apply HP-based color to enemies (not local, not friends, not teammates)
		// Check team first (cheapest), then other conditions
		if (pPlayer != pLocal && 
			pPlayer->m_iTeamNum() != pLocal->m_iTeamNum() &&
			!pPlayer->IsInvulnerable() &&
			!pPlayer->IsInvisible() &&
			!pPlayer->IsPlayerOnSteamFriendsList())
		{
			// Check if not a tagged player (cheater/targeted/nigger/retardlegit/streamer)
			PlayerPriority info{};
			if (!F::Players->GetInfo(pPlayer->entindex(), info) || (!info.Cheater && !info.Targeted && !info.Nigger && !info.RetardLegit && !info.Streamer))
			{
				return GetHealthColor(pPlayer->m_iHealth(), pPlayer->GetMaxHealth());
			}
		}
	}

	// Fall back to default entity color
	return GetEntityColor(pLocal, pEntity);
}

Color_t CVisualUtils::GetEntityColorForMaterials(C_TFPlayer* pLocal, C_BaseEntity* pEntity)
{
	if (!pLocal || !pEntity)
		return { 255, 255, 255, 255 };

	// Check if HP-based coloring is enabled for enemy materials
	if (CFG::Visuals_Enemy_Materials_HP_Based && pEntity->GetClassId() == ETFClassIds::CTFPlayer)
	{
		const auto pPlayer = pEntity->As<C_TFPlayer>();
		
		// Only apply HP-based color to enemies (not local, not friends, not teammates)
		// Check team first (cheapest), then other conditions
		if (pPlayer != pLocal && 
			pPlayer->m_iTeamNum() != pLocal->m_iTeamNum() &&
			!pPlayer->IsInvulnerable() &&
			!pPlayer->IsInvisible() &&
			!pPlayer->IsPlayerOnSteamFriendsList())
		{
			// Check if not a tagged player (cheater/targeted/nigger/retardlegit/streamer)
			PlayerPriority info{};
			if (!F::Players->GetInfo(pPlayer->entindex(), info) || (!info.Cheater && !info.Targeted && !info.Nigger && !info.RetardLegit && !info.Streamer))
			{
				return GetHealthColor(pPlayer->m_iHealth(), pPlayer->GetMaxHealth());
			}
		}
	}

	// Fall back to default entity color
	return GetEntityColor(pLocal, pEntity);
}

Color_t CVisualUtils::GetHealthColor(int nHealth, int nMaxHealth)
{
	if (nHealth > nMaxHealth)
		return CFG::Color_OverHeal;

	// Clamp health to valid range
	if (nHealth < 0) nHealth = 0;
	else if (nHealth > nMaxHealth) nHealth = nMaxHealth;
	
	// Pre-calculate to avoid repeated division
	const int nHealthDiff = nMaxHealth - nHealth;
	const int r = std::min((510 * nHealthDiff) / nMaxHealth, 210);
	const int g = std::min((510 * nHealth) / nMaxHealth, 230);
	return { static_cast<byte>(r), static_cast<byte>(g), 50, 255 };
}

Color_t CVisualUtils::GetHealthColorAlt(int nHealth, int nMaxHealth)
{
	nHealth = std::max(0, std::min(nHealth, nMaxHealth));
	const int r = std::min((510 * (nMaxHealth - nHealth)) / nMaxHealth, 255);
	const int g = std::min((510 * nHealth) / nMaxHealth, 230);
	return { static_cast<byte>(r), static_cast<byte>(g), 50, 255 };
}

int CVisualUtils::CreateTextureFromArray(const unsigned char* rgba, int w, int h)
{
	const int nTextureIdOut = I::MatSystemSurface->CreateNewTextureID(true);
	I::MatSystemSurface->DrawSetTextureRGBAEx(nTextureIdOut, rgba, w, h, IMAGE_FORMAT_BGRA8888);
	return nTextureIdOut;
}

int CVisualUtils::CreateTextureFromVTF(const char* name)
{
	const int nTextureIdOut = I::MatSystemSurface->CreateNewTextureID(false);
	I::MatSystemSurface->DrawSetTextureFile(nTextureIdOut, name, 0, true);
	return nTextureIdOut;
}

int CVisualUtils::GetClassIcon(int nClassNum)
{
	// Use static array for O(1) lookup instead of switch statement
	static int nClassIcons[10] = { 0 };
	static bool bInitialized = false;
	
	if (!bInitialized)
	{
		nClassIcons[TF_CLASS_SCOUT] = CreateTextureFromVTF("hud/leaderboard_class_scout.vtf");
		nClassIcons[TF_CLASS_SOLDIER] = CreateTextureFromVTF("hud/leaderboard_class_soldier.vtf");
		nClassIcons[TF_CLASS_PYRO] = CreateTextureFromVTF("hud/leaderboard_class_pyro.vtf");
		nClassIcons[TF_CLASS_DEMOMAN] = CreateTextureFromVTF("hud/leaderboard_class_demo.vtf");
		nClassIcons[TF_CLASS_HEAVYWEAPONS] = CreateTextureFromVTF("hud/leaderboard_class_heavy.vtf");
		nClassIcons[TF_CLASS_ENGINEER] = CreateTextureFromVTF("hud/leaderboard_class_engineer.vtf");
		nClassIcons[TF_CLASS_MEDIC] = CreateTextureFromVTF("hud/leaderboard_class_medic.vtf");
		nClassIcons[TF_CLASS_SNIPER] = CreateTextureFromVTF("hud/leaderboard_class_sniper.vtf");
		nClassIcons[TF_CLASS_SPY] = CreateTextureFromVTF("hud/leaderboard_class_spy.vtf");
		bInitialized = true;
	}
	
	if (nClassNum >= 1 && nClassNum <= 9)
		return nClassIcons[nClassNum];
	
	return 0;
}

int CVisualUtils::GetBuildingTextureId(C_BaseObject* pObject)
{
	static int nSentryGunLvl1 = CreateTextureFromVTF("hud/hud_obj_status_sentry_1.vtf");
	static int nSentryGunLvl2 = CreateTextureFromVTF("hud/hud_obj_status_sentry_2.vtf");
	static int nSentryGunLvl3 = CreateTextureFromVTF("hud/hud_obj_status_sentry_3.vtf");
	static int nDispenser = CreateTextureFromVTF("hud/hud_obj_status_dispenser.vtf");
	static int nTeleporter = CreateTextureFromVTF("hud/hud_obj_status_tele_entrance.vtf");

	if (!pObject)
		return 0;

	switch (pObject->GetClassId())
	{
	case ETFClassIds::CObjectSentrygun:
		{
			switch (pObject->m_iUpgradeLevel())
			{
			case 1: return nSentryGunLvl1;
			case 2: return nSentryGunLvl2;
			case 3: return nSentryGunLvl3;
			default: break;
			}

			break;
		}

	case ETFClassIds::CObjectDispenser: return nDispenser;
	case ETFClassIds::CObjectTeleporter: return nTeleporter;

	default: break;
	}

	return 0;
}

int CVisualUtils::GetHealthIconTextureId()
{
	static int nOut = CreateTextureFromVTF("sprites/healbeam.vtf");
	return nOut;
}

int CVisualUtils::GetAmmoIconTextureId()
{
	static int nOut = CreateTextureFromVTF("hud/hud_obj_status_ammo_64");
	return nOut;
}

int CVisualUtils::GetHalloweenGiftTextureId()
{
	static int nOut = CreateTextureFromVTF("models/props_halloween/halloween_gift.vtf");
	return nOut;
}

bool CVisualUtils::IsOnScreen(const C_TFPlayer* pLocal, const C_BaseEntity* pEntity)
{
	const Vec3& vPos = pEntity->GetAbsOrigin();
	const Vec3& vLocalPos = pLocal->GetAbsOrigin();
	
	// Quick distance check using squared distance (avoids sqrt)
	const float dx = vPos.x - vLocalPos.x;
	const float dy = vPos.y - vLocalPos.y;
	const float dz = vPos.z - vLocalPos.z;
	const float flDistSq = dx*dx + dy*dy + dz*dz;
	
	// 300^2 = 90000
	if (flDistSq > 90000.0f)
	{
		Vec3 vScreen = {};

		if (H::Draw->W2S(vPos, vScreen))
		{
			const int nScreenW = H::Draw->GetScreenW();
			const int nScreenH = H::Draw->GetScreenH();
			
			if (vScreen.x < -400
				|| vScreen.x > nScreenW + 400
				|| vScreen.y < -400
				|| vScreen.y > nScreenH + 400)
				return false;
		}
		else
		{
			return false;
		}
	}

	return true;
}

bool CVisualUtils::IsOnScreenNoEntity(const C_TFPlayer* pLocal, const Vec3& vAbsOrigin)
{
	const Vec3& vLocalPos = pLocal->GetAbsOrigin();
	
	// Quick distance check using squared distance (avoids sqrt)
	const float dx = vAbsOrigin.x - vLocalPos.x;
	const float dy = vAbsOrigin.y - vLocalPos.y;
	const float dz = vAbsOrigin.z - vLocalPos.z;
	const float flDistSq = dx*dx + dy*dy + dz*dz;
	
	// 300^2 = 90000
	if (flDistSq > 90000.0f)
	{
		Vec3 vScreen = {};

		if (H::Draw->W2S(vAbsOrigin, vScreen))
		{
			const int nScreenW = H::Draw->GetScreenW();
			const int nScreenH = H::Draw->GetScreenH();
			
			if (vScreen.x < -400
				|| vScreen.x > nScreenW + 400
				|| vScreen.y < -400
				|| vScreen.y > nScreenH + 400)
				return false;
		}
		else
		{
			return false;
		}
	}

	return true;
}

int CVisualUtils::GetCat(int nFrame)
{
	if (nFrame < 0 || nFrame > 3)
		return 0;

	static const int arrFrames[4] =
	{
		CreateTextureFromArray(Icons::cat_0, 12, 12),
		CreateTextureFromArray(Icons::cat_1, 12, 12),
		CreateTextureFromArray(Icons::cat_2, 12, 12),
		CreateTextureFromArray(Icons::cat_3, 12, 12)
	};

	return arrFrames[nFrame];
}

int CVisualUtils::GetCat2(int nFrame)
{
	if (nFrame < 0 || nFrame > 3)
		return 0;

	static const int arrFrames[4] =
	{
		CreateTextureFromArray(Icons::cat2_0, 12, 12),
		CreateTextureFromArray(Icons::cat2_1, 12, 12),
		CreateTextureFromArray(Icons::cat2_2, 12, 12),
		CreateTextureFromArray(Icons::cat2_3, 12, 12)
	};

	return arrFrames[nFrame];
}

int CVisualUtils::GetCatSleep(int nFrame)
{
	if (nFrame < 0 || nFrame > 3)
		return 0;

	static const int arrFrames[4] =
	{
		CreateTextureFromArray(Icons::cat_sleep0, 16, 8),
		CreateTextureFromArray(Icons::cat_sleep1, 16, 8),
		CreateTextureFromArray(Icons::cat_sleep2, 16, 8),
		CreateTextureFromArray(Icons::cat_sleep3, 16, 8)
	};

	return arrFrames[nFrame];
}

int CVisualUtils::GetCatRun(int nFrame)
{
	if (nFrame < 0 || nFrame > 7)
		return 0;

	static const int arrFrames[8] =
	{
		CreateTextureFromArray(Icons::cat_run0, 20, 13),
		CreateTextureFromArray(Icons::cat_run1, 20, 13),
		CreateTextureFromArray(Icons::cat_run2, 20, 13),
		CreateTextureFromArray(Icons::cat_run3, 20, 13),
		CreateTextureFromArray(Icons::cat_run4, 20, 13),
		CreateTextureFromArray(Icons::cat_run5, 20, 13),
		CreateTextureFromArray(Icons::cat_run6, 20, 13),
		CreateTextureFromArray(Icons::cat_run7, 20, 13)
	};

	return arrFrames[nFrame];
}

Color_t CVisualUtils::Rainbow()
{
	const float t = TICKS_TO_TIME(I::GlobalVars->tickcount);

	const int r = static_cast<int>(std::round(std::cos(I::GlobalVars->realtime + t + 0.0f) * 127.5f + 127.5f));
	const int g = static_cast<int>(std::round(std::cos(I::GlobalVars->realtime + t + 2.0f) * 127.5f + 127.5f));
	const int b = static_cast<int>(std::round(std::cos(I::GlobalVars->realtime + t + 4.0f) * 127.5f + 127.5f));

	return Color_t{ static_cast<byte>(r), static_cast<byte>(g), static_cast<byte>(b), 255 };
}

Color_t CVisualUtils::RainbowTickOffset(int nTick)
{
	const float t = TICKS_TO_TIME(nTick);

	const int r = static_cast<int>(std::lround(std::cos((I::GlobalVars->realtime * 2.0f) + t + 0.0f) * 127.5f + 127.5f));
	const int g = static_cast<int>(std::lround(std::cos((I::GlobalVars->realtime * 2.0f) + t + 2.0f) * 127.5f + 127.5f));
	const int b = static_cast<int>(std::lround(std::cos((I::GlobalVars->realtime * 2.0f) + t + 4.0f) * 127.5f + 127.5f));

	return Color_t{ static_cast<byte>(r), static_cast<byte>(g), static_cast<byte>(b), 255 };
}

Color_t CVisualUtils::GetAccentSecondary()
{
	if (CFG::Menu_Accent_Secondary_RGB)
	{
		const float rate = CFG::Menu_Accent_Secondary_RGB_Rate;
		const int r = static_cast<int>(std::lround(std::cosf(I::GlobalVars->realtime * rate + 0.0f) * 127.5f + 127.5f));
		const int g = static_cast<int>(std::lround(std::cosf(I::GlobalVars->realtime * rate + 2.0f) * 127.5f + 127.5f));
		const int b = static_cast<int>(std::lround(std::cosf(I::GlobalVars->realtime * rate + 4.0f) * 127.5f + 127.5f));
		return Color_t{ static_cast<byte>(r), static_cast<byte>(g), static_cast<byte>(b), 255 };
	}
	return CFG::Menu_Accent_Secondary;
}

Color_t CVisualUtils::GetAccentSecondaryWithAlpha(float alpha)
{
	Color_t color = GetAccentSecondary();
	color.a = static_cast<byte>(255.0f * alpha);
	return color;
}
