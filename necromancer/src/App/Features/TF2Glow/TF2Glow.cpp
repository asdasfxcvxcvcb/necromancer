#include "TF2Glow.h"
#include "../CFG.h"
#include "../VisualUtils/VisualUtils.h"
#include "../Players/Players.h"

void CTF2Glow::Run()
{
	// Only run if TF2 Glow style is selected
	if (!CFG::Outlines_Active || CFG::Outlines_Style != 4)
	{
		CleanUp();
		return;
	}

	// Get the TF2 glow manager
	auto pGlowManager = SDKUtils::GetGlowObjectManager();
	if (!pGlowManager)
		return;

	// Make sure the glow ConVar is enabled
	static auto glow_outline_enable = I::CVar->FindVar("glow_outline_effect_enable");
	if (glow_outline_enable && !glow_outline_enable->GetBool())
	{
		glow_outline_enable->SetValue(1);
	}

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal)
		return;

	// Clear the vector - like Outlines does
	if (!m_vecGlowEntities.empty())
		m_vecGlowEntities.clear();

	// Players
	if (CFG::Outlines_Players_Active)
	{
		const int nLocalTeam = pLocal->m_iTeamNum();
		const bool bIgnoreLocal = CFG::Outlines_Players_Ignore_Local;
		const bool bIgnoreFriends = CFG::Outlines_Players_Ignore_Friends;
		const bool bIgnoreTeammates = CFG::Outlines_Players_Ignore_Teammates;
		const bool bIgnoreEnemies = CFG::Outlines_Players_Ignore_Enemies;
		const bool bIgnoreTagged = CFG::Outlines_Players_Ignore_Tagged;
		const bool bIgnoreTaggedTeammates = CFG::Outlines_Players_Ignore_Tagged_Teammates;
		const bool bShowTeammateMedics = CFG::Outlines_Players_Show_Teammate_Medics;
		const float flAlpha = CFG::Outlines_Players_Alpha;

		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ALL))
		{
			if (!pEntity)
				continue;

			const auto pPlayer = pEntity->As<C_TFPlayer>();
			if (pPlayer->deadflag())
				continue;

			const bool bIsLocal = pPlayer == pLocal;
			if (bIgnoreLocal && bIsLocal)
				continue;

			if (!F::VisualUtils->IsOnScreen(pLocal, pPlayer))
				continue;

			const bool bIsFriend = pPlayer->IsPlayerOnSteamFriendsList();
			if (bIgnoreFriends && bIsFriend)
				continue;

			bool bIsTagged = false;
			if (!bIgnoreTagged)
			{
				PlayerPriority playerPriority = {};
				if (F::Players->GetInfo(pPlayer->entindex(), playerPriority))
				{
					bIsTagged = playerPriority.Cheater || playerPriority.RetardLegit || playerPriority.Ignored || playerPriority.Targeted || playerPriority.Streamer || playerPriority.Nigger;
				}
			}

			const int nPlayerTeam = pPlayer->m_iTeamNum();
			const bool bIsTeammate = nPlayerTeam == nLocalTeam;

			if (bIsTagged && bIsTeammate && bIgnoreTaggedTeammates)
			{
				if (!bShowTeammateMedics || pPlayer->m_iClass() != TF_CLASS_MEDIC)
					continue;
			}

			if (!bIsLocal && !bIsFriend && !bIsTagged)
			{
				if (bIgnoreTeammates && bIsTeammate)
				{
					if (!bShowTeammateMedics || pPlayer->m_iClass() != TF_CLASS_MEDIC)
						continue;
				}

				if (bIgnoreEnemies && !bIsTeammate)
					continue;
			}

			const auto entColor = F::VisualUtils->GetEntityColorForOutlines(pLocal, pPlayer);
			Vec3 glowColor = { entColor.r / 255.0f, entColor.g / 255.0f, entColor.b / 255.0f };

			// Register with glow manager and store in vector
			int nGlowIndex = pGlowManager->RegisterGlowObject(pEntity, glowColor, flAlpha, true, true);
			if (nGlowIndex >= 0)
			{
				m_vecGlowEntities.push_back({pEntity, nGlowIndex, entColor, flAlpha});
			}
		}
	}

	// Buildings
	if (CFG::Outlines_Buildings_Active)
	{
		const bool bIgnoreLocal = CFG::Outlines_Buildings_Ignore_Local;
		const bool bIgnoreTeammates = CFG::Outlines_Buildings_Ignore_Teammates;
		const bool bIgnoreEnemies = CFG::Outlines_Buildings_Ignore_Enemies;
		const bool bShowTeammateDispensers = CFG::Outlines_Buildings_Show_Teammate_Dispensers;
		const int nLocalTeam = pLocal->m_iTeamNum();
		const float flAlpha = CFG::Outlines_Buildings_Alpha;

		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_ALL))
		{
			if (!pEntity)
				continue;

			const auto pBuilding = pEntity->As<C_BaseObject>();
			if (pBuilding->m_bPlacing())
				continue;

			const bool bIsLocal = F::VisualUtils->IsEntityOwnedBy(pBuilding, pLocal);
			if (bIgnoreLocal && bIsLocal)
				continue;

			if (!bIsLocal)
			{
				const int nBuildingTeam = pBuilding->m_iTeamNum();

				if (bIgnoreTeammates && nBuildingTeam == nLocalTeam)
				{
					if (!bShowTeammateDispensers || pBuilding->GetClassId() != ETFClassIds::CObjectDispenser)
						continue;
				}

				if (bIgnoreEnemies && nBuildingTeam != nLocalTeam)
					continue;
			}

			if (!F::VisualUtils->IsOnScreen(pLocal, pBuilding))
				continue;

			const auto entColor = F::VisualUtils->GetEntityColor(pLocal, pBuilding);
			Vec3 glowColor = { entColor.r / 255.0f, entColor.g / 255.0f, entColor.b / 255.0f };

			int nGlowIndex = pGlowManager->RegisterGlowObject(pEntity, glowColor, flAlpha, true, true);
			if (nGlowIndex >= 0)
			{
				m_vecGlowEntities.push_back({pEntity, nGlowIndex, entColor, flAlpha});
			}
		}
	}

	// World items
	if (CFG::Outlines_World_Active)
	{
		const bool bIgnoreHealthPacks = CFG::Outlines_World_Ignore_HealthPacks;
		const bool bIgnoreAmmoPacks = CFG::Outlines_World_Ignore_AmmoPacks;
		const bool bIgnoreHalloweenGift = CFG::Outlines_World_Ignore_Halloween_Gift;
		const bool bIgnoreMVMMoney = CFG::Outlines_World_Ignore_MVM_Money;
		const bool bIgnoreLocalProj = CFG::Outlines_World_Ignore_LocalProjectiles;
		const bool bIgnoreEnemyProj = CFG::Outlines_World_Ignore_EnemyProjectiles;
		const bool bIgnoreTeammateProj = CFG::Outlines_World_Ignore_TeammateProjectiles;
		const int nLocalTeam = pLocal->m_iTeamNum();
		const float flAlpha = CFG::Outlines_World_Alpha;

		if (!bIgnoreHealthPacks)
		{
			const auto color = CFG::Color_HealthPack;
			Vec3 glowColor = { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f };

			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::HEALTHPACKS))
			{
				if (!pEntity || !F::VisualUtils->IsOnScreen(pLocal, pEntity))
					continue;

				int nGlowIndex = pGlowManager->RegisterGlowObject(pEntity, glowColor, flAlpha, true, true);
				if (nGlowIndex >= 0)
				{
					m_vecGlowEntities.push_back({pEntity, nGlowIndex, color, flAlpha});
				}
			}
		}

		if (!bIgnoreAmmoPacks)
		{
			const auto color = CFG::Color_AmmoPack;
			Vec3 glowColor = { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f };

			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::AMMOPACKS))
			{
				if (!pEntity || !F::VisualUtils->IsOnScreen(pLocal, pEntity))
					continue;

				int nGlowIndex = pGlowManager->RegisterGlowObject(pEntity, glowColor, flAlpha, true, true);
				if (nGlowIndex >= 0)
				{
					m_vecGlowEntities.push_back({pEntity, nGlowIndex, color, flAlpha});
				}
			}
		}

		const bool bIgnoringAllProjectiles = bIgnoreLocalProj && bIgnoreEnemyProj && bIgnoreTeammateProj;
		if (!bIgnoringAllProjectiles)
		{
			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PROJECTILES_ALL))
			{
				if (!pEntity || !pEntity->ShouldDraw())
					continue;

				const bool bIsLocal = F::VisualUtils->IsEntityOwnedBy(pEntity, pLocal);
				if (bIgnoreLocalProj && bIsLocal)
					continue;

				if (!bIsLocal)
				{
					const int nProjTeam = pEntity->m_iTeamNum();

					if (bIgnoreEnemyProj && nProjTeam != nLocalTeam)
						continue;

					if (bIgnoreTeammateProj && nProjTeam == nLocalTeam)
						continue;
				}

				if (!F::VisualUtils->IsOnScreen(pLocal, pEntity))
					continue;

				const auto color = F::VisualUtils->GetEntityColor(pLocal, pEntity);
				Vec3 glowColor = { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f };

				int nGlowIndex = pGlowManager->RegisterGlowObject(pEntity, glowColor, flAlpha, true, true);
				if (nGlowIndex >= 0)
				{
					m_vecGlowEntities.push_back({pEntity, nGlowIndex, color, flAlpha});
				}
			}
		}
	}
}

void CTF2Glow::Render(const CViewSetup* pViewSetup)
{
	if (!pViewSetup)
		return;

	auto pGlowManager = SDKUtils::GetGlowObjectManager();
	if (!pGlowManager)
		return;

	// Call TF2's native RenderGlowEffects function FIRST (while objects are still registered)
	using RenderGlowEffectsFn = void(__fastcall*)(CGlowObjectManager*, const CViewSetup*, int);
	static auto fnRenderGlowEffects = reinterpret_cast<RenderGlowEffectsFn>(Signatures::RenderGlowEffects.Get());
	
	if (fnRenderGlowEffects)
	{
		fnRenderGlowEffects(pGlowManager, pViewSetup, 0); // 0 = split screen slot
	}

	// AFTER rendering, unregister all glow objects from this frame
	for (const auto& glowEntity : m_vecGlowEntities)
	{
		if (glowEntity.m_nGlowIndex >= 0 && glowEntity.m_nGlowIndex < pGlowManager->m_GlowObjectDefinitions.Count())
		{
			if (!pGlowManager->m_GlowObjectDefinitions[glowEntity.m_nGlowIndex].IsUnused())
			{
				pGlowManager->UnregisterGlowObject(glowEntity.m_nGlowIndex);
			}
		}
	}
}

void CTF2Glow::CleanUp()
{
	auto pGlowManager = SDKUtils::GetGlowObjectManager();
	if (pGlowManager)
	{
		// Unregister all our glow objects
		for (const auto& glowEntity : m_vecGlowEntities)
		{
			if (glowEntity.m_nGlowIndex >= 0 && glowEntity.m_nGlowIndex < pGlowManager->m_GlowObjectDefinitions.Count())
			{
				if (!pGlowManager->m_GlowObjectDefinitions[glowEntity.m_nGlowIndex].IsUnused())
				{
					pGlowManager->UnregisterGlowObject(glowEntity.m_nGlowIndex);
				}
			}
		}
	}
	
	m_vecGlowEntities.clear();
}
