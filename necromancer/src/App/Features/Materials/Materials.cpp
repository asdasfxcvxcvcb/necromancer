#include "Materials.h"

#include "../CFG.h"
#include "../VisualUtils/VisualUtils.h"
#include "../LagRecords/LagRecords.h"
#include "../SpyCamera/SpyCamera.h"
#include "../FakeAngle/FakeAngle.h"
#include "../Players/Players.h"

void SetModelStencilForOutlines(C_BaseEntity* pEntity)
{
	IMatRenderContext* pRenderContext = I::MaterialSystem->GetRenderContext();

	if (!pRenderContext)
		return;

	const auto pLocal = H::Entities->GetLocal();

	if (!pLocal)
		return;

	auto IsEntGoingToBeGlowed = [&]()
	{
		if (pEntity->GetClassId() == ETFClassIds::CTFPlayer)
		{
			if (!CFG::Outlines_Players_Active)
				return false;

			const auto pPlayer = pEntity->As<C_TFPlayer>();

			const bool bIsLocal = pPlayer == pLocal;
			const bool bIsFriend = pPlayer->IsPlayerOnSteamFriendsList();

			if (CFG::Outlines_Players_Ignore_Local && bIsLocal)
				return false;

			if (CFG::Outlines_Players_Ignore_Friends && bIsFriend)
				return false;

			if (!bIsLocal && !bIsFriend)
			{
				if (CFG::Outlines_Players_Ignore_Teammates && pPlayer->m_iTeamNum() == pLocal->m_iTeamNum())
				{
					if (CFG::Outlines_Players_Show_Teammate_Medics)
					{
						if (pPlayer->m_iClass() != TF_CLASS_MEDIC)
							return false;
					}
					else
					{
						return false;
					}
				}

				if (CFG::Outlines_Players_Ignore_Enemies && pPlayer->m_iTeamNum() != pLocal->m_iTeamNum())
					return false;
			}
		}

		if (pEntity->GetClassId() == ETFClassIds::CObjectSentrygun
			|| pEntity->GetClassId() == ETFClassIds::CObjectDispenser
			|| pEntity->GetClassId() == ETFClassIds::CObjectTeleporter)
		{
			if (!CFG::Outlines_Buildings_Active)
				return false;

			auto pBuilding = pEntity->As<C_BaseObject>();

			if (pBuilding->m_bPlacing())
				return false;

			bool bIsLocal = F::VisualUtils->IsEntityOwnedBy(pBuilding, pLocal);

			if (CFG::Outlines_Buildings_Ignore_Local && bIsLocal)
				return false;

			if (!bIsLocal)
			{
				if (CFG::Outlines_Buildings_Ignore_Teammates && pBuilding->m_iTeamNum() == pLocal->m_iTeamNum())
				{
					if (CFG::Outlines_Buildings_Show_Teammate_Dispensers)
					{
						if (pBuilding->GetClassId() != ETFClassIds::CObjectDispenser)
							return false;
					}

					else
					{
						return false;
					}
				}

				if (CFG::Outlines_Buildings_Ignore_Enemies && pBuilding->m_iTeamNum() != pLocal->m_iTeamNum())
					return false;
			}
		}

		//fuck rest

		return true;
	};

	if (!IsEntGoingToBeGlowed())
	{
		pRenderContext->SetStencilEnable(false);
	}

	else
	{
		ShaderStencilState_t state = {};
		state.m_bEnable = true;
		state.m_nReferenceValue = 1;
		state.m_CompareFunc = STENCILCOMPARISONFUNCTION_ALWAYS;
		state.m_PassOp = STENCILOPERATION_REPLACE;
		state.m_FailOp = STENCILOPERATION_KEEP;
		state.m_ZFailOp = STENCILOPERATION_REPLACE;
		state.SetStencilState(pRenderContext);
	}
}

void CMaterials::Initialize()
{
	static ConVar* mat_hdr_level = I::CVar->FindVar("mat_hdr_level");

	if (!m_pFlat)
	{
		auto* kv = new KeyValues("VertexLitGeneric");
		kv->SetString("$dummy", "dummy");
		kv->SetString("$basetexture", "vgui/white_additive");
		kv->SetString("$bumpmap", "vgui/white_additive");
		kv->SetString("$selfillum", "1");
		kv->SetString("$selfillumFresnel", "1");
		kv->SetString("$selfillumFresnelMinMaxExp", "[0.4999 0.5 1]");
		kv->SetString("$cloakPassEnabled", "1");
		kv->SetString("$nodecal", "1");
		kv->SetString("$model", "1");
		if (const auto proxies = kv->FindKey("Proxies", true)) { proxies->FindKey("invis", true); }

		m_pFlat = I::MaterialSystem->CreateMaterial("seo_material_flat", kv);
	}

	if (!m_pShaded)
	{
		auto* kv = new KeyValues("VertexLitGeneric");
		kv->SetString("$basetexture", "vgui/white_additive");
		kv->SetString("$bumpmap", "models/player/shared/shared_normal");
		kv->SetString("$selfillum", "1");
		kv->SetString("$selfillumFresnel", "1");
		kv->SetString("$selfillumFresnelMinMaxExp", "[0.1 0.5 2]");
		kv->SetString("$cloakPassEnabled", "1");
		kv->SetString("$nodecal", "1");
		kv->SetString("$model", "1");
		if (const auto proxies = kv->FindKey("Proxies", true)) { proxies->FindKey("invis", true); }

		m_pShaded = I::MaterialSystem->CreateMaterial("seo_material_shaded", kv);
	}

	if (!m_pGlossy)
	{
		auto* kv = new KeyValues("VertexLitGeneric");
		kv->SetString("$basetexture", "vgui/white_additive");
		kv->SetString("$bumpmap", "models/player/shared/shared_normal");
		kv->SetString("$envmap", "cubemaps/cubemap_sheen002");
		kv->SetString("$envmapfresnel", "1");
		kv->SetString("$phong", "1");
		kv->SetString("$phongfresnelranges", "[0 1 2]");
		kv->SetString("$selfillum", "1");
		kv->SetString("$selfillumFresnel", "1");
		kv->SetString("$selfillumFresnelMinMaxExp", "[0 0.1 1]");
		kv->SetString("$cloakPassEnabled", "1");
		kv->SetString("$nodecal", "1");
		kv->SetString("$model", "1");
		if (const auto proxies = kv->FindKey("Proxies", true)) { proxies->FindKey("invis", true); }

		m_pGlossy = I::MaterialSystem->CreateMaterial("seo_material_glossy", kv);
	}

	if (!m_pGlow)
	{
		auto* kv = new KeyValues("VertexLitGeneric");
		kv->SetString("$basetexture", "vgui/white_additive");
		kv->SetString("$bumpmap", "models/player/shared/shared_normal");
		kv->SetString("$envmap", "effects/saxxy_gold");
		//kv->SetString("$envmap", "skybox/sky_dustbowl_01");
		kv->SetString("$envmapfresnel", "1");
		kv->SetString("$phong", "1");
		kv->SetString("$phongfresnelranges", mat_hdr_level->GetInt() > 1 ? "[0 0.05 0.1]" : "[0 1 2]");
		kv->SetString("$selfillum", "1");
		kv->SetString("$selfillumFresnel", "1");
		kv->SetString("$selfillumFresnelMinMaxExp", "[0.4999 0.5 0]");
		kv->SetString("$envmaptint", "[0 0 0]");
		kv->SetString("$selfillumtint", "[0.03 0.03 0.03]");
		kv->SetString("$cloakPassEnabled", "1");
		kv->SetString("$nodecal", "1");
		kv->SetString("$model", "1");
		if (const auto proxies = kv->FindKey("Proxies", true)) { proxies->FindKey("invis", true); }

		m_pGlow = I::MaterialSystem->CreateMaterial("seo_material_glow", kv);
		m_pGlowEnvmapTint = m_pGlow->FindVar("$envmaptint", nullptr);
		m_pGlowSelfillumTint = m_pGlow->FindVar("$selfillumtint", nullptr);
	}

	if (!m_pPlastic)
	{
		auto* kv = new KeyValues("VertexLitGeneric");
		kv->SetString("$basetexture", "models/player/shared/ice_player");
		kv->SetString("$bumpmap", "models/player/shared/shared_normal");
		kv->SetString("$phong", "1");
		kv->SetString("$phongexponent", "10");
		kv->SetString("$phongboost", "1");
		kv->SetString("$phongfresnelranges", "[0 0 0]");
		kv->SetString("$basemapalphaphongmask", "1");
		kv->SetString("$phongwarptexture", "models/player/shared/ice_player_warp");
		kv->SetString("$cloakPassEnabled", "1");
		kv->SetString("$nodecal", "1");
		kv->SetString("$model", "1");
		if (const auto proxies = kv->FindKey("Proxies", true)) { proxies->FindKey("invis", true); }

		m_pPlastic = I::MaterialSystem->CreateMaterial("seo_material_plastic", kv);
	}

	if (!m_pFlatNoInvis)
	{
		auto* kv = new KeyValues("VertexLitGeneric");
		kv->SetString("$basetexture", "vgui/white_additive");
		kv->SetString("$bumpmap", "vgui/white_additive");
		kv->SetString("$selfillum", "1");
		kv->SetString("$selfillumFresnel", "1");
		kv->SetString("$selfillumFresnelMinMaxExp", "[0.4999 0.5 1]");
		kv->SetString("$nodecal", "1");
		kv->SetString("$model", "1");

		m_pFlatNoInvis = I::MaterialSystem->CreateMaterial("seo_material_flat_no_invis", kv);
	}

	if (!m_pShadedNoInvis)
	{
		auto* kv = new KeyValues("VertexLitGeneric");
		kv->SetString("$basetexture", "vgui/white_additive");
		kv->SetString("$bumpmap", "models/player/shared/shared_normal");
		kv->SetString("$selfillum", "1");
		kv->SetString("$selfillumFresnel", "1");
		kv->SetString("$selfillumFresnelMinMaxExp", "[0.1 0.5 2]");
		kv->SetString("$nodecal", "1");
		kv->SetString("$model", "1");

		m_pShadedNoInvis = I::MaterialSystem->CreateMaterial("seo_material_shaded_no_invis", kv);
	}
}

void CMaterials::DrawEntity(C_BaseEntity* pEntity)
{
	if (!pEntity)
		return;

	SetModelStencilForOutlines(pEntity);

	m_bRendering = true;

	const float flOldInvisibility = pEntity->GetClassId() == ETFClassIds::CTFPlayer ? pEntity->As<C_TFPlayer>()->m_flInvisibility() : -1.0f;

	if (flOldInvisibility > 0.99f)
	{
		pEntity->As<C_TFPlayer>()->m_flInvisibility() = 0.0f;
		I::RenderView->SetBlend(0.0f);
	}

	pEntity->DrawModel(STUDIO_RENDER);

	if (flOldInvisibility > 0.99f)
	{
		pEntity->As<C_TFPlayer>()->m_flInvisibility() = flOldInvisibility;
		I::RenderView->SetBlend(1.0f);
	}

	m_mapDrawnEntities[pEntity] = true;

	m_bRendering = false;
}

void CMaterials::RunLagRecords()
{
	const auto pRenderContext = I::MaterialSystem->GetRenderContext();

	if (!pRenderContext || !CFG::Materials_LagRecords_Active)
		return;

	const auto pLocal = H::Entities->GetLocal();

	if (!pLocal)
		return;

	const auto pWeapon = H::Entities->GetWeapon();

	if (!pWeapon)
		return;

	const auto weaponType = H::AimUtils->GetWeaponType(pWeapon);

	if (weaponType == EWeaponType::HITSCAN)
	{
		if (!CFG::Aimbot_Active
			|| !CFG::Aimbot_Hitscan_Active
			|| !CFG::Aimbot_Target_Players
			|| !CFG::Aimbot_Hitscan_Target_LagRecords)
			return;
	}
	else if (weaponType == EWeaponType::MELEE)
	{
		if (!CFG::Aimbot_Active
			|| !CFG::Aimbot_Melee_Active
			|| !CFG::Aimbot_Target_Players
			|| !CFG::Aimbot_Melee_Target_LagRecords)
			return;
	}
	else
	{
		return;
	}

	// Get material based on setting
	auto GetMaterial = [&](int nIndex) -> IMaterial* {
		switch (nIndex)
		{
		case 0: return nullptr;
		case 1: return m_pFlat;
		case 2: return m_pShaded;
		case 3: return m_pGlossy;
		case 4: return m_pGlow;
		case 5: return m_pPlastic;
		default: return m_pFlat;
		}
	};

	m_bRenderingOriginalMat = CFG::Materials_LagRecords_Material == 0;

	const auto pMaterial = GetMaterial(CFG::Materials_LagRecords_Material);
	const auto& color = CFG::Color_LagRecord;

	if (pMaterial)
	{
		I::ModelRender->ForcedMaterialOverride(pMaterial);
		if (pMaterial != m_pGlow)
			I::RenderView->SetColorModulation(color);
		else if (m_pGlowEnvmapTint)
			m_pGlowEnvmapTint->SetVecValue(ColorUtils::ToFloat(color.r), ColorUtils::ToFloat(color.g), ColorUtils::ToFloat(color.b));
	}

	if (CFG::Materials_Players_No_Depth)
		pRenderContext->DepthRange(0.0f, 0.2f);

	// Cache style and alpha for the loop
	const int nStyle = CFG::Materials_LagRecords_Style;
	const float flAlpha = CFG::Materials_LagRecords_Alpha;

	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
	{
		if (!pEntity)
			continue;

		const auto pPlayer = pEntity->As<C_TFPlayer>();

		if (pPlayer->deadflag())
			continue;

		int nRecords = 0;

		if (!F::LagRecords->HasRecords(pPlayer, &nRecords))
			continue;

		if (nRecords <= 0)
			continue;

		if (nStyle == 0)
		{
			for (int n = 1; n < nRecords; n++)
			{
				const auto pRecord = F::LagRecords->GetRecord(pPlayer, n, true);

				if (!pRecord || !F::VisualUtils->IsOnScreenNoEntity(pLocal, pRecord->AbsOrigin) || !F::LagRecords->DiffersFromCurrent(pRecord))
					continue;

				I::RenderView->SetBlend(Math::RemapValClamped(static_cast<float>(n), 1.0f, static_cast<float>(nRecords), flAlpha, 0.01f));

				F::LagRecordMatrixHelper->Set(pRecord);
				m_bRendering = true;
				const float flOldInvisibility = pPlayer->m_flInvisibility();
				pPlayer->m_flInvisibility() = 0.0f;
				pPlayer->DrawModel(STUDIO_RENDER | STUDIO_NOSHADOWS);
				pPlayer->m_flInvisibility() = flOldInvisibility;
				m_bRendering = false;
				F::LagRecordMatrixHelper->Restore();
			}
		}
		else
		{
			const auto pRecord = F::LagRecords->GetRecord(pPlayer, nRecords - 1, true);

			if (!pRecord || !F::VisualUtils->IsOnScreenNoEntity(pLocal, pRecord->AbsOrigin) || !F::LagRecords->DiffersFromCurrent(pRecord))
				continue;

			I::RenderView->SetBlend(flAlpha);

			F::LagRecordMatrixHelper->Set(pRecord);
			m_bRendering = true;
			const float flOldInvisibility = pPlayer->m_flInvisibility();
			pPlayer->m_flInvisibility() = 0.0f;
			pPlayer->DrawModel(STUDIO_RENDER | STUDIO_NOSHADOWS);
			pPlayer->m_flInvisibility() = flOldInvisibility;
			m_bRendering = false;
			F::LagRecordMatrixHelper->Restore();
		}
	}

	I::ModelRender->ForcedMaterialOverride(nullptr);

	if (CFG::Materials_Players_No_Depth)
		pRenderContext->DepthRange(0.0f, 1.0f);

	I::RenderView->SetBlend(1.0f);
	I::RenderView->SetColorModulation(1.0f, 1.0f, 1.0f);
}

void CMaterials::RunFakeAngle()
{
	const auto pRenderContext = I::MaterialSystem->GetRenderContext();
	if (!pRenderContext)
		return;

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || !pLocal->IsAlive())
		return;

	// Check if bones are setup (SetupFakeModel handles all the logic)
	if (!F::FakeAngle->m_bBonesSetup)
		return;

	// Check if fake model materials are enabled
	if (!CFG::Materials_FakeModel_Active)
		return;

	// Get cached bone data
	auto pCachedBoneData = pLocal->GetCachedBoneData();
	if (!pCachedBoneData || pCachedBoneData->Count() <= 0)
		return;

	// Save original bones BEFORE any rendering
	matrix3x4_t originalBones[128];
	int boneCount = std::min(pCachedBoneData->Count(), 128);
	memcpy(originalBones, pCachedBoneData->Base(), sizeof(matrix3x4_t) * boneCount);

	// Set fake bones for rendering
	memcpy(pCachedBoneData->Base(), F::FakeAngle->m_aBones, sizeof(matrix3x4_t) * boneCount);

	// Get material based on fake model material setting
	auto GetMaterial = [&](int nIndex) -> IMaterial* {
		switch (nIndex)
		{
		case 0: return nullptr;
		case 1: return m_pFlat;
		case 2: return m_pShaded;
		case 3: return m_pGlossy;
		case 4: return m_pGlow;
		case 5: return m_pPlastic;
		default: return m_pFlat;
		}
	};

	m_bRenderingOriginalMat = CFG::Materials_FakeModel_Material == 0;

	const auto pMaterial = GetMaterial(CFG::Materials_FakeModel_Material);
	const auto& color = CFG::Color_FakeModel;

	if (pMaterial)
	{
		I::ModelRender->ForcedMaterialOverride(pMaterial);
		if (pMaterial != m_pGlow)
			I::RenderView->SetColorModulation(color);
		else if (m_pGlowEnvmapTint)
			m_pGlowEnvmapTint->SetVecValue(ColorUtils::ToFloat(color.r), ColorUtils::ToFloat(color.g), ColorUtils::ToFloat(color.b));
	}
	else
	{
		I::RenderView->SetColorModulation(color);
	}

	I::RenderView->SetBlend(CFG::Materials_FakeModel_Alpha);

	if (CFG::Materials_Players_No_Depth)
		pRenderContext->DepthRange(0.0f, 0.2f);

	// Draw the fake model
	m_bRendering = true;
	const float flOldInvisibility = pLocal->m_flInvisibility();
	pLocal->m_flInvisibility() = 0.0f;
	pLocal->DrawModel(STUDIO_RENDER | STUDIO_NOSHADOWS);
	pLocal->m_flInvisibility() = flOldInvisibility;
	m_bRendering = false;

	// Restore rendering state
	I::ModelRender->ForcedMaterialOverride(nullptr);
	I::RenderView->SetBlend(1.0f);
	I::RenderView->SetColorModulation(1.0f, 1.0f, 1.0f);

	if (CFG::Materials_Players_No_Depth)
		pRenderContext->DepthRange(0.0f, 1.0f);

	// Restore original bones AFTER rendering is complete
	pCachedBoneData = pLocal->GetCachedBoneData();
	if (pCachedBoneData && pCachedBoneData->Count() > 0)
	{
		memcpy(pCachedBoneData->Base(), originalBones, sizeof(matrix3x4_t) * std::min(pCachedBoneData->Count(), 128));
	}
	
	// Invalidate bone cache to force recalculation with real angles next frame
	pLocal->InvalidateBoneCache();
}

void CMaterials::Run()
{
	// Don't run if we're cleaning up or during level transitions
	if (m_bCleaningUp || G::bLevelTransition)
		return;


	Initialize();

	if (!m_mapDrawnEntities.empty())
		m_mapDrawnEntities.clear();

	if (!CFG::Materials_Active || I::EngineVGui->IsGameUIVisible() || F::SpyCamera->IsRendering())
		return;

	if (CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot())
	{
		return;
	}
	
	// Early exit if nothing is enabled - massive performance gain
	if (!CFG::Materials_Players_Active && !CFG::Materials_Buildings_Active && !CFG::Materials_World_Active)
		return;

	const auto pRenderContext = I::MaterialSystem->GetRenderContext();

	if (!pRenderContext)
		return;

	const auto pLocal = H::Entities->GetLocal();

	if (!pLocal)
		return;

	if (m_pGlowSelfillumTint)
		m_pGlowSelfillumTint->SetVecValue(0.03f, 0.03f, 0.03f);

	RunLagRecords();
	RunFakeAngle();

	auto GetMaterial = [&](int nIndex) -> IMaterial* {
		//don't forget to change me if more materials are added!
		m_bRenderingOriginalMat = nIndex == 0 || nIndex > 5;

		switch (nIndex)
		{
		case 0: return nullptr;
		case 1: return m_pFlat;
		case 2: return m_pShaded;
		case 3: return m_pGlossy;
		case 4: return m_pGlow;
		case 5: return m_pPlastic;
		default: return nullptr;
		}
	};

	if (CFG::Materials_Players_Active)
	{
		I::RenderView->SetColorModulation(1.0f, 1.0f, 1.0f);

		const auto pMaterial = GetMaterial(CFG::Materials_Players_Material);

		if (pMaterial)
			I::ModelRender->ForcedMaterialOverride(pMaterial);

		const float flPlayersAlpha = CFG::Materials_Players_Alpha;
		if (flPlayersAlpha < 1.0f)
			I::RenderView->SetBlend(flPlayersAlpha);

		const bool bNoDepth = CFG::Materials_Players_No_Depth;
		if (bNoDepth)
			pRenderContext->DepthRange(0.0f, 0.2f);

		// Cache local team and config values for faster comparisons
		const int nLocalTeam = pLocal->m_iTeamNum();
		const bool bIgnoreLocal = CFG::Materials_Players_Ignore_Local;
		const bool bIgnoreFriends = CFG::Materials_Players_Ignore_Friends;
		const bool bIgnoreTeammates = CFG::Materials_Players_Ignore_Teammates;
		const bool bIgnoreEnemies = CFG::Materials_Players_Ignore_Enemies;
		const bool bIgnoreTagged = CFG::Materials_Players_Ignore_Tagged;
		const bool bShowTeammateMedics = CFG::Materials_Players_Show_Teammate_Medics;
		
		// Get entity group once
		const auto& vPlayers = H::Entities->GetGroup(EEntGroup::PLAYERS_ALL);
		
		for (const auto pEntity : vPlayers)
		{
			if (!pEntity)
				continue;

			const auto pPlayer = pEntity->As<C_TFPlayer>();

			if (pPlayer->deadflag())
				continue;

			const bool bIsLocal = pPlayer == pLocal;
			
			if (bIgnoreLocal && bIsLocal)
				continue;
			
			// Early screen check before expensive friend check
			if (!F::VisualUtils->IsOnScreen(pLocal, pPlayer))
				continue;

			const bool bIsFriend = pPlayer->IsPlayerOnSteamFriendsList();

			if (bIgnoreFriends && bIsFriend)
				continue;

			// Check if player is tagged (Cheater/RetardLegit/Ignored)
			bool bIsTagged = false;
			if (!bIgnoreTagged)
			{
				PlayerPriority playerPriority = {};
				if (F::Players->GetInfo(pPlayer->entindex(), playerPriority))
				{
					bIsTagged = playerPriority.Cheater || playerPriority.RetardLegit || playerPriority.Ignored;
				}
			}

			// Skip team/enemy filtering if player is tagged (and not ignoring tagged)
			if (!bIsLocal && !bIsFriend && !bIsTagged)
			{
				const int nPlayerTeam = pPlayer->m_iTeamNum();
				
				if (bIgnoreTeammates && nPlayerTeam == nLocalTeam)
				{
					if (!bShowTeammateMedics || pPlayer->m_iClass() != TF_CLASS_MEDIC)
						continue;
				}

				if (bIgnoreEnemies && nPlayerTeam != nLocalTeam)
					continue;
			}

			const auto entColor = F::VisualUtils->GetEntityColorForMaterials(pLocal, pPlayer);

			if (pMaterial && pMaterial != m_pGlow)
				I::RenderView->SetColorModulation(entColor);

			if (pMaterial == m_pGlow && m_pGlowEnvmapTint)
				m_pGlowEnvmapTint->SetVecValue(ColorUtils::ToFloat(entColor.r), ColorUtils::ToFloat(entColor.g), ColorUtils::ToFloat(entColor.b));

			DrawEntity(pPlayer);

			C_BaseEntity* pAttach = pPlayer->FirstMoveChild();

			for (int n = 0; n < 32; n++)
			{
				if (!pAttach)
					break;

				if (pAttach->ShouldDraw())
					DrawEntity(pAttach);

				pAttach = pAttach->NextMovePeer();
			}
		}

		if (pMaterial)
			I::ModelRender->ForcedMaterialOverride(nullptr);

		if (flPlayersAlpha < 1.0f)
			I::RenderView->SetBlend(1.0f);

		if (bNoDepth)
			pRenderContext->DepthRange(0.0f, 1.0f);
	}

	if (CFG::Materials_Buildings_Active)
	{
		I::RenderView->SetColorModulation(1.0f, 1.0f, 1.0f);

		const auto pMaterial = GetMaterial(CFG::Materials_Buildings_Material);

		if (pMaterial)
			I::ModelRender->ForcedMaterialOverride(pMaterial);

		const float flBuildingsAlpha = CFG::Materials_Buildings_Alpha;
		if (flBuildingsAlpha < 1.0f)
			I::RenderView->SetBlend(flBuildingsAlpha);

		const bool bNoDepth = CFG::Materials_Buildings_No_Depth;
		if (bNoDepth)
			pRenderContext->DepthRange(0.0f, 0.2f);

		// Cache config values
		const bool bIgnoreLocal = CFG::Materials_Buildings_Ignore_Local;
		const bool bIgnoreTeammates = CFG::Materials_Buildings_Ignore_Teammates;
		const bool bIgnoreEnemies = CFG::Materials_Buildings_Ignore_Enemies;
		const bool bShowTeammateDispensers = CFG::Materials_Buildings_Show_Teammate_Dispensers;
		const int nLocalTeam = pLocal->m_iTeamNum();

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

			if (pMaterial && pMaterial != m_pGlow)
				I::RenderView->SetColorModulation(entColor);

			if (pMaterial == m_pGlow && m_pGlowEnvmapTint)
				m_pGlowEnvmapTint->SetVecValue(ColorUtils::ToFloat(entColor.r), ColorUtils::ToFloat(entColor.g), ColorUtils::ToFloat(entColor.b));

			DrawEntity(pBuilding);
		}

		if (pMaterial)
			I::ModelRender->ForcedMaterialOverride(nullptr);

		if (flBuildingsAlpha < 1.0f)
			I::RenderView->SetBlend(1.0f);

		if (bNoDepth)
			pRenderContext->DepthRange(0.0f, 1.0f);
	}

	if (CFG::Materials_World_Active)
	{
		I::RenderView->SetColorModulation(1.0f, 1.0f, 1.0f);

		const auto pMaterial = GetMaterial(CFG::Materials_World_Material);

		if (pMaterial)
			I::ModelRender->ForcedMaterialOverride(pMaterial);

		if (CFG::Materials_World_Alpha < 1.0f)
			I::RenderView->SetBlend(CFG::Materials_World_Alpha);

		if (CFG::Materials_World_No_Depth)
			pRenderContext->DepthRange(0.0f, 0.2f);

		if (!CFG::Materials_World_Ignore_HealthPacks)
		{
			const auto color = CFG::Color_HealthPack;

			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::HEALTHPACKS))
			{
				if (!pEntity || !F::VisualUtils->IsOnScreen(pLocal, pEntity))
					continue;

				if (pMaterial && pMaterial != m_pGlow)
					I::RenderView->SetColorModulation(color);

				if (pMaterial == m_pGlow && m_pGlowEnvmapTint)
					m_pGlowEnvmapTint->SetVecValue(ColorUtils::ToFloat(color.r), ColorUtils::ToFloat(color.g), ColorUtils::ToFloat(color.b));

				DrawEntity(pEntity);
			}
		}

		if (!CFG::Materials_World_Ignore_AmmoPacks)
		{
			const auto color = CFG::Color_AmmoPack;

			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::AMMOPACKS))
			{
				if (!pEntity || !F::VisualUtils->IsOnScreen(pLocal, pEntity))
					continue;

				if (pMaterial && pMaterial != m_pGlow)
					I::RenderView->SetColorModulation(color);

				if (pMaterial == m_pGlow && m_pGlowEnvmapTint)
					m_pGlowEnvmapTint->SetVecValue(ColorUtils::ToFloat(color.r), ColorUtils::ToFloat(color.g), ColorUtils::ToFloat(color.b));

				DrawEntity(pEntity);
			}
		}

		if (!CFG::Materials_World_Ignore_Halloween_Gift)
		{
			const auto color = CFG::Color_Halloween_Gift;

			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::HALLOWEEN_GIFT))
			{
				if (!pEntity || !pEntity->ShouldDraw() || !F::VisualUtils->IsOnScreen(pLocal, pEntity))
					continue;

				if (pMaterial && pMaterial != m_pGlow)
					I::RenderView->SetColorModulation(color);

				if (pMaterial == m_pGlow && m_pGlowEnvmapTint)
					m_pGlowEnvmapTint->SetVecValue(ColorUtils::ToFloat(color.r), ColorUtils::ToFloat(color.g), ColorUtils::ToFloat(color.b));

				DrawEntity(pEntity);
			}
		}

		if (!CFG::Materials_World_Ignore_MVM_Money)
		{
			const auto color = CFG::Color_MVM_Money;

			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::MVM_MONEY))
			{
				if (!pEntity || !pEntity->ShouldDraw() || !F::VisualUtils->IsOnScreen(pLocal, pEntity))
					continue;

				if (pMaterial && pMaterial != m_pGlow)
					I::RenderView->SetColorModulation(color);

				if (pMaterial == m_pGlow && m_pGlowEnvmapTint)
					m_pGlowEnvmapTint->SetVecValue(ColorUtils::ToFloat(color.r), ColorUtils::ToFloat(color.g), ColorUtils::ToFloat(color.b));

				DrawEntity(pEntity);
			}
		}

		const bool bIgnoringAllProjectiles = CFG::Materials_World_Ignore_LocalProjectiles
			&& CFG::Materials_World_Ignore_EnemyProjectiles
			&& CFG::Materials_World_Ignore_TeammateProjectiles;

		if (!bIgnoringAllProjectiles)
		{
			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PROJECTILES_ALL))
			{
				if (!pEntity || !pEntity->ShouldDraw())
					continue;

				const bool bIsLocal = F::VisualUtils->IsEntityOwnedBy(pEntity, pLocal);

				if (CFG::Materials_World_Ignore_LocalProjectiles && bIsLocal)
					continue;

				if (!bIsLocal)
				{
					if (CFG::Materials_World_Ignore_EnemyProjectiles && pEntity->m_iTeamNum() != pLocal->m_iTeamNum())
						continue;

					if (CFG::Materials_World_Ignore_TeammateProjectiles && pEntity->m_iTeamNum() == pLocal->m_iTeamNum())
						continue;
				}

				if (!F::VisualUtils->IsOnScreen(pLocal, pEntity))
					continue;

				const auto color = F::VisualUtils->GetEntityColor(pLocal, pEntity);

				if (pMaterial && pMaterial != m_pGlow)
					I::RenderView->SetColorModulation(color);

				if (pMaterial == m_pGlow && m_pGlowEnvmapTint)
					m_pGlowEnvmapTint->SetVecValue(ColorUtils::ToFloat(color.r), ColorUtils::ToFloat(color.g), ColorUtils::ToFloat(color.b));

				DrawEntity(pEntity);
			}
		}

		if (pMaterial)
			I::ModelRender->ForcedMaterialOverride(nullptr);

		if (CFG::Materials_World_Alpha < 1.0f)
			I::RenderView->SetBlend(1.0f);

		if (CFG::Materials_World_No_Depth)
			pRenderContext->DepthRange(0.0f, 1.0f);
	}
}

void CMaterials::CleanUp()
{
	// Set flag first to prevent any rendering from using materials
	m_bCleaningUp = true;

	// Clear entity list to prevent any pending renders
	m_mapDrawnEntities.clear();

	// Null out pointers first before decrementing references
	// This prevents race conditions where render thread checks pointer validity
	auto pFlat = m_pFlat;
	auto pShaded = m_pShaded;
	auto pGlossy = m_pGlossy;
	auto pGlow = m_pGlow;
	auto pPlastic = m_pPlastic;
	auto pFlatNoInvis = m_pFlatNoInvis;
	auto pShadedNoInvis = m_pShadedNoInvis;

	m_pFlat = nullptr;
	m_pShaded = nullptr;
	m_pGlossy = nullptr;
	m_pGlow = nullptr;
	m_pPlastic = nullptr;
	m_pFlatNoInvis = nullptr;
	m_pShadedNoInvis = nullptr;
	m_pGlowEnvmapTint = nullptr;
	m_pGlowSelfillumTint = nullptr;

	// Now safe to decrement references
	if (pFlat)
	{
		pFlat->DecrementReferenceCount();
		pFlat->DeleteIfUnreferenced();
	}

	if (pShaded)
	{
		pShaded->DecrementReferenceCount();
		pShaded->DeleteIfUnreferenced();
	}

	if (pGlossy)
	{
		pGlossy->DecrementReferenceCount();
		pGlossy->DeleteIfUnreferenced();
	}

	if (pGlow)
	{
		pGlow->DecrementReferenceCount();
		pGlow->DeleteIfUnreferenced();
	}

	if (pPlastic)
	{
		pPlastic->DecrementReferenceCount();
		pPlastic->DeleteIfUnreferenced();
	}

	if (pFlatNoInvis)
	{
		pFlatNoInvis->DecrementReferenceCount();
		pFlatNoInvis->DeleteIfUnreferenced();
	}

	if (pShadedNoInvis)
	{
		pShadedNoInvis->DecrementReferenceCount();
		pShadedNoInvis->DeleteIfUnreferenced();
	}

	m_bCleaningUp = false;
}
