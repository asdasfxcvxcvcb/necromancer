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

	// Mark all existing glow entities as "not seen this frame"
	for (auto& glowEntity : m_vecGlowEntities)
	{
		glowEntity.m_bSeenThisFrame = false;
	}

	// Helper to find or create glow entry for an entity
	auto FindOrCreateGlow = [&](C_BaseEntity* pEntity, const Color_t& color, float flAlpha) -> bool
	{
		Vec3 glowColor = { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f };

		// Check if we already have a glow for this entity
		for (auto& glowEntity : m_vecGlowEntities)
		{
			if (glowEntity.m_pEntity == pEntity)
			{
				// Update existing glow
				glowEntity.m_bSeenThisFrame = true;
				glowEntity.m_Color = color;
				glowEntity.m_flAlpha = flAlpha;

				// Update the glow manager entry if valid
				if (glowEntity.m_nGlowIndex >= 0 && glowEntity.m_nGlowIndex < pGlowManager->m_GlowObjectDefinitions.Count())
				{
					if (!pGlowManager->m_GlowObjectDefinitions[glowEntity.m_nGlowIndex].IsUnused())
					{
						pGlowManager->SetColor(glowEntity.m_nGlowIndex, glowColor);
						pGlowManager->SetAlpha(glowEntity.m_nGlowIndex, flAlpha);
						return true;
					}
				}

				// Glow index was invalid, re-register
				int nGlowIndex = pGlowManager->RegisterGlowObject(pEntity, glowColor, flAlpha, true, true);
				if (nGlowIndex >= 0)
				{
					glowEntity.m_nGlowIndex = nGlowIndex;
					return true;
				}
				return false;
			}
		}

		// New entity, register it
		int nGlowIndex = pGlowManager->RegisterGlowObject(pEntity, glowColor, flAlpha, true, true);
		if (nGlowIndex >= 0)
		{
			m_vecGlowEntities.push_back({pEntity, nGlowIndex, color, flAlpha, true});
			return true;
		}
		return false;
	};

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
			FindOrCreateGlow(pEntity, entColor, flAlpha);
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
			FindOrCreateGlow(pEntity, entColor, flAlpha);
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

			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::HEALTHPACKS))
			{
				if (!pEntity || !F::VisualUtils->IsOnScreen(pLocal, pEntity))
					continue;

				FindOrCreateGlow(pEntity, color, flAlpha);
			}
		}

		if (!bIgnoreAmmoPacks)
		{
			const auto color = CFG::Color_AmmoPack;

			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::AMMOPACKS))
			{
				if (!pEntity || !F::VisualUtils->IsOnScreen(pLocal, pEntity))
					continue;

				FindOrCreateGlow(pEntity, color, flAlpha);
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
				FindOrCreateGlow(pEntity, color, flAlpha);
			}
		}
	}

	// Remove glow objects for entities that weren't seen this frame
	// (they went off screen, died, etc.)
	for (auto it = m_vecGlowEntities.begin(); it != m_vecGlowEntities.end();)
	{
		if (!it->m_bSeenThisFrame)
		{
			// Unregister from glow manager
			if (it->m_nGlowIndex >= 0 && it->m_nGlowIndex < pGlowManager->m_GlowObjectDefinitions.Count())
			{
				if (!pGlowManager->m_GlowObjectDefinitions[it->m_nGlowIndex].IsUnused())
				{
					pGlowManager->UnregisterGlowObject(it->m_nGlowIndex);
				}
			}
			it = m_vecGlowEntities.erase(it);
		}
		else
		{
			++it;
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

	// Call TF2's native RenderGlowEffects function
	// Our glow objects are already registered and will be rendered
	using RenderGlowEffectsFn = void(__fastcall*)(CGlowObjectManager*, const CViewSetup*, int);
	static auto fnRenderGlowEffects = reinterpret_cast<RenderGlowEffectsFn>(Signatures::RenderGlowEffects.Get());
	
	if (fnRenderGlowEffects)
	{
		fnRenderGlowEffects(pGlowManager, pViewSetup, 0); // 0 = split screen slot
	}

	// NOTE: We no longer unregister here - glow objects persist until
	// the entity goes off screen or is no longer valid
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
