#include "Outlines.h"

#include "../CFG.h"
#include "../Materials/Materials.h"
#include "../SpyCamera/SpyCamera.h"
#include "../VisualUtils/VisualUtils.h"
#include "../Players/Players.h"

#pragma warning (disable : 4244) //possible loss of data (int to float)

void COutlines::Initialize()
{
	if (!m_pMatGlowColor)
	{
		m_pMatGlowColor = I::MaterialSystem->FindMaterial("dev/glow_color", TEXTURE_GROUP_OTHER);
		if (m_pMatGlowColor && m_pMatGlowColor->IsErrorMaterial())
			m_pMatGlowColor = nullptr;
	}

	if (!m_pRtFullFrame)
	{
		m_pRtFullFrame = I::MaterialSystem->FindTexture("_rt_FullFrameFB", TEXTURE_GROUP_RENDER_TARGET);
		if (m_pRtFullFrame && m_pRtFullFrame->IsError())
			m_pRtFullFrame = nullptr;
	}

	if (!m_pRenderBuffer0 && m_pRtFullFrame)
	{
		m_pRenderBuffer0 = I::MaterialSystem->CreateNamedRenderTargetTextureEx(
			"seo_outline_buffer0",
			m_pRtFullFrame->GetActualWidth(),
			m_pRtFullFrame->GetActualHeight(),
			RT_SIZE_LITERAL,
			IMAGE_FORMAT_RGB888,
			MATERIAL_RT_DEPTH_SHARED,
			TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_EIGHTBITALPHA,
			CREATERENDERTARGETFLAGS_HDR
		);

		if (m_pRenderBuffer0)
			m_pRenderBuffer0->IncrementReferenceCount();
	}

	if (!m_pRenderBuffer1 && m_pRtFullFrame)
	{
		m_pRenderBuffer1 = I::MaterialSystem->CreateNamedRenderTargetTextureEx(
			"seo_outline_buffer1",
			m_pRtFullFrame->GetActualWidth(),
			m_pRtFullFrame->GetActualHeight(),
			RT_SIZE_LITERAL,
			IMAGE_FORMAT_RGB888,
			MATERIAL_RT_DEPTH_SHARED,
			TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_EIGHTBITALPHA,
			CREATERENDERTARGETFLAGS_HDR
		);

		if (m_pRenderBuffer1)
			m_pRenderBuffer1->IncrementReferenceCount();
	}

	if (!m_pMatHaloAddToScreen)
	{
		const auto kv = new KeyValues("UnlitGeneric");
		kv->SetString("$dummy", "dummy");
		kv->SetString("$basetexture", "seo_outline_buffer0");
		kv->SetString("$additive", "1");
		m_pMatHaloAddToScreen = I::MaterialSystem->CreateMaterial("seo_outline_material", kv);
		if (m_pMatHaloAddToScreen && m_pMatHaloAddToScreen->IsErrorMaterial())
			m_pMatHaloAddToScreen = nullptr;
	}

	if (!m_pMatBlurX)
	{
		const auto kv = new KeyValues("BlurFilterX");
		kv->SetString("$dummy", "dummy");
		kv->SetString("$basetexture", "seo_outline_buffer0");
		m_pMatBlurX = I::MaterialSystem->CreateMaterial("seo_outline_material_blurx", kv);
		if (m_pMatBlurX && m_pMatBlurX->IsErrorMaterial())
			m_pMatBlurX = nullptr;
	}

	if (!m_pMatBlurY)
	{
		const auto kv = new KeyValues("BlurFilterY");
		kv->SetString("$dummy", "dummy");
		kv->SetString("$basetexture", "seo_outline_buffer1");
		m_pMatBlurY = I::MaterialSystem->CreateMaterial("seo_outline_material_blury", kv);
		if (m_pMatBlurY && m_pMatBlurY->IsErrorMaterial())
			m_pMatBlurY = nullptr;
		
		if (m_pMatBlurY)
			m_pBloomAmount = m_pMatBlurY->FindVar("$bloomamount", nullptr);
	}
}

void COutlines::DrawEntity(C_BaseEntity* pEntity, bool bModel)
{
	if (!pEntity)
		return;

	m_bRendering = true;

	if (!bModel)
		m_bRenderingOutlines = true;

	const float flOldInvisibility = pEntity->GetClassId() == ETFClassIds::CTFPlayer ? pEntity->As<C_TFPlayer>()->m_flInvisibility() : -1.0f;

	if (flOldInvisibility > 0.99f)
	{
		pEntity->As<C_TFPlayer>()->m_flInvisibility() = 0.0f;

		if (!m_bRenderingOutlines)
			I::RenderView->SetBlend(0.0f);
	}

	pEntity->DrawModel(bModel ? STUDIO_RENDER : (STUDIO_RENDER | STUDIO_NOSHADOWS));

	if (flOldInvisibility > 0.99f)
	{
		pEntity->As<C_TFPlayer>()->m_flInvisibility() = flOldInvisibility;

		if (!m_bRenderingOutlines)
			I::RenderView->SetBlend(1.0f);
	}

	if (bModel)
		m_mapDrawnEntities[pEntity] = true;

	if (!bModel)
		m_bRenderingOutlines = false;

	m_bRendering = false;
}

void COutlines::RunModels()
{
	// Don't run if we're cleaning up
	if (m_bCleaningUp)
		return;

	Initialize();

	if (!m_mapDrawnEntities.empty())
		m_mapDrawnEntities.clear();

	if (!m_vecOutlineEntities.empty())
		m_vecOutlineEntities.clear();

	if (!CFG::Outlines_Active || I::EngineVGui->IsGameUIVisible() || F::SpyCamera->IsRendering())
		return;

	// Early exit if nothing is enabled - massive performance gain
	if (!CFG::Outlines_Players_Active && !CFG::Outlines_Buildings_Active && !CFG::Outlines_World_Active)
		return;

	// Validate required materials exist
	if (!m_pMatGlowColor || !m_pRenderBuffer0 || !m_pRenderBuffer1 || !m_pMatHaloAddToScreen)
		return;

	const int w = H::Draw->GetScreenW();
	const int h = H::Draw->GetScreenH();

	if (w < 1 || h < 1 || w > 4096 || h > 2160)
		return;

	const auto pRC = I::MaterialSystem->GetRenderContext();

	if (!pRC)
		return;

	const auto pLocal = H::Entities->GetLocal();

	if (!pLocal)
		return;

	ShaderStencilState_t stencilStateDisable = {};
	stencilStateDisable.m_bEnable = false;

	float flOriginalColor[3] = {};
	I::RenderView->GetColorModulation(flOriginalColor);
	const float flOriginalBlend = I::RenderView->GetBlend();

	SetModelStencil(pRC);

	I::RenderView->SetBlend(1.0f);
	I::RenderView->SetColorModulation(1.0f, 1.0f, 1.0f);

	// Cache frequently accessed config values
	const float flPlayersAlpha = CFG::Outlines_Players_Alpha;
	const float flBuildingsAlpha = CFG::Outlines_Buildings_Alpha;
	const float flWorldAlpha = CFG::Outlines_World_Alpha;

	if (CFG::Outlines_Players_Active)
	{
		// Cache local team and config values for faster comparisons
		const int nLocalTeam = pLocal->m_iTeamNum();
		const bool bIgnoreLocal = CFG::Outlines_Players_Ignore_Local;
		const bool bIgnoreFriends = CFG::Outlines_Players_Ignore_Friends;
		const bool bIgnoreTeammates = CFG::Outlines_Players_Ignore_Teammates;
		const bool bIgnoreEnemies = CFG::Outlines_Players_Ignore_Enemies;
		const bool bIgnoreTagged = CFG::Outlines_Players_Ignore_Tagged;
		const bool bShowTeammateMedics = CFG::Outlines_Players_Show_Teammate_Medics;
		
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

			const auto entColor = F::VisualUtils->GetEntityColorForOutlines(pLocal, pPlayer);

			m_vecOutlineEntities.emplace_back(OutlineEntity_t{pPlayer, entColor, flPlayersAlpha});

			if (!F::Materials->HasDrawn(pPlayer))
				DrawEntity(pPlayer, true);

			C_BaseEntity* pAttach = pPlayer->FirstMoveChild();

			for (int n = 0; n < 32; n++)
			{
				if (!pAttach)
					break;

				if (pAttach->ShouldDraw())
				{
					m_vecOutlineEntities.emplace_back(OutlineEntity_t{pAttach, entColor, flPlayersAlpha});

					if (!F::Materials->HasDrawn(pAttach))
						DrawEntity(pAttach, true);
				}

				pAttach = pAttach->NextMovePeer();
			}
		}
	}

	if (CFG::Outlines_Buildings_Active)
	{
		// Cache config values
		const bool bIgnoreLocal = CFG::Outlines_Buildings_Ignore_Local;
		const bool bIgnoreTeammates = CFG::Outlines_Buildings_Ignore_Teammates;
		const bool bIgnoreEnemies = CFG::Outlines_Buildings_Ignore_Enemies;
		const bool bShowTeammateDispensers = CFG::Outlines_Buildings_Show_Teammate_Dispensers;
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

			m_vecOutlineEntities.emplace_back(OutlineEntity_t{pBuilding, entColor, flBuildingsAlpha});

			if (!F::Materials->HasDrawn(pBuilding))
				DrawEntity(pBuilding, true);
		}
	}

	if (CFG::Outlines_World_Active)
	{
		// Cache config values for world items
		const bool bIgnoreHealthPacks = CFG::Outlines_World_Ignore_HealthPacks;
		const bool bIgnoreAmmoPacks = CFG::Outlines_World_Ignore_AmmoPacks;
		const bool bIgnoreHalloweenGift = CFG::Outlines_World_Ignore_Halloween_Gift;
		const bool bIgnoreMVMMoney = CFG::Outlines_World_Ignore_MVM_Money;
		const bool bIgnoreLocalProj = CFG::Outlines_World_Ignore_LocalProjectiles;
		const bool bIgnoreEnemyProj = CFG::Outlines_World_Ignore_EnemyProjectiles;
		const bool bIgnoreTeammateProj = CFG::Outlines_World_Ignore_TeammateProjectiles;
		const int nLocalTeam = pLocal->m_iTeamNum();
		
		if (!bIgnoreHealthPacks)
		{
			const auto color = CFG::Color_HealthPack;

			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::HEALTHPACKS))
			{
				if (!pEntity || !F::VisualUtils->IsOnScreen(pLocal, pEntity))
					continue;

				m_vecOutlineEntities.emplace_back(OutlineEntity_t{pEntity, color, flWorldAlpha});

				if (!F::Materials->HasDrawn(pEntity))
					DrawEntity(pEntity, true);
			}
		}

		if (!bIgnoreAmmoPacks)
		{
			const auto color = CFG::Color_AmmoPack;

			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::AMMOPACKS))
			{
				if (!pEntity || !F::VisualUtils->IsOnScreen(pLocal, pEntity))
					continue;

				m_vecOutlineEntities.emplace_back(OutlineEntity_t{pEntity, color, flWorldAlpha});

				if (!F::Materials->HasDrawn(pEntity))
					DrawEntity(pEntity, true);
			}
		}

		if (!bIgnoreHalloweenGift)
		{
			const auto color = CFG::Color_Halloween_Gift;

			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::HALLOWEEN_GIFT))
			{
				if (!pEntity || !pEntity->ShouldDraw() || !F::VisualUtils->IsOnScreen(pLocal, pEntity))
					continue;

				m_vecOutlineEntities.emplace_back(OutlineEntity_t{pEntity, color, flWorldAlpha});

				if (!F::Materials->HasDrawn(pEntity))
					DrawEntity(pEntity, true);
			}
		}

		if (!bIgnoreMVMMoney)
		{
			const auto color = CFG::Color_MVM_Money;

			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::MVM_MONEY))
			{
				if (!pEntity || !pEntity->ShouldDraw() || !F::VisualUtils->IsOnScreen(pLocal, pEntity))
					continue;

				m_vecOutlineEntities.emplace_back(OutlineEntity_t{pEntity, color, flWorldAlpha});

				if (!F::Materials->HasDrawn(pEntity))
					DrawEntity(pEntity, true);
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

				m_vecOutlineEntities.emplace_back(OutlineEntity_t{pEntity, color, flWorldAlpha});

				if (!F::Materials->HasDrawn(pEntity))
					DrawEntity(pEntity, true);
			}
		}
	}

	stencilStateDisable.SetStencilState(pRC);

	I::ModelRender->ForcedMaterialOverride(nullptr);
	I::RenderView->SetColorModulation(flOriginalColor);
	I::RenderView->SetBlend(flOriginalBlend);
}

void COutlines::Run()
{
	// Don't run if we're cleaning up
	if (m_bCleaningUp)
		return;

	if (!CFG::Outlines_Active || I::EngineVGui->IsGameUIVisible() || F::SpyCamera->IsRendering())
		return;

	if (CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot())
	{
		return;
	}

	const int w = H::Draw->GetScreenW();
	const int h = H::Draw->GetScreenH();

	if (w < 1 || h < 1 || w > 4096 || h > 2160)
		return;

	const auto pRC = I::MaterialSystem->GetRenderContext();

	if (!pRC)
		return;

	// Validate required materials exist
	if (!m_pMatGlowColor || !m_pRenderBuffer0 || !m_pRenderBuffer1 || !m_pMatHaloAddToScreen)
		return;

	if (CFG::Outlines_Style == 0 && m_pBloomAmount)
		m_pBloomAmount->SetIntValue(CFG::Outlines_Bloom_Amount);

	ShaderStencilState_t stencilStateDisable = {};
	stencilStateDisable.m_bEnable = false;

	float flOriginalColor[3] = {};
	I::RenderView->GetColorModulation(flOriginalColor);
	const float flOriginalBlend = I::RenderView->GetBlend();

	if (m_vecOutlineEntities.empty())
		return;

	I::ModelRender->ForcedMaterialOverride(m_pMatGlowColor);

	pRC->PushRenderTargetAndViewport();
	{
		pRC->SetRenderTarget(m_pRenderBuffer0);
		pRC->Viewport(0, 0, w, h);
		pRC->ClearColor4ub(0, 0, 0, 0);
		pRC->ClearBuffers(true, false, false);

		for (const auto& entity : m_vecOutlineEntities)
		{
			I::RenderView->SetBlend(entity.m_flAlpha);
			I::RenderView->SetColorModulation(ColorUtils::ToFloat(entity.m_Color.r), ColorUtils::ToFloat(entity.m_Color.g), ColorUtils::ToFloat(entity.m_Color.b));
			DrawEntity(entity.m_pEntity, false);
		}

		stencilStateDisable.SetStencilState(pRC);
	}
	pRC->PopRenderTargetAndViewport();

	if (CFG::Outlines_Style == 0 && m_pMatBlurX && m_pMatBlurY)
	{
		pRC->PushRenderTargetAndViewport();
		{
			pRC->Viewport(0, 0, w, h);
			pRC->SetRenderTarget(m_pRenderBuffer1);
			pRC->DrawScreenSpaceRectangle(m_pMatBlurX, 0, 0, w, h, 0.0f, 0.0f, w - 1, h - 1, w, h);
			pRC->SetRenderTarget(m_pRenderBuffer0);
			pRC->DrawScreenSpaceRectangle(m_pMatBlurY, 0, 0, w, h, 0.0f, 0.0f, w - 1, h - 1, w, h);
		}
		pRC->PopRenderTargetAndViewport();
	}

	ShaderStencilState_t sEffect = {};
	sEffect.m_bEnable = true;
	sEffect.m_nWriteMask = 0x0;
	sEffect.m_nTestMask = 0xFF;
	sEffect.m_nReferenceValue = 0;
	sEffect.m_CompareFunc = STENCILCOMPARISONFUNCTION_EQUAL;
	sEffect.m_PassOp = STENCILOPERATION_KEEP;
	sEffect.m_FailOp = STENCILOPERATION_KEEP;
	sEffect.m_ZFailOp = STENCILOPERATION_KEEP;
	sEffect.SetStencilState(pRC);

	switch (CFG::Outlines_Style)
	{
	case 0:
		{
			pRC->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen, 0, 0, w, h, 0.0f, 0.0f, w - 1, h - 1, w, h);
			break;
		}

	case 1:
		{
			pRC->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen, -1, -1, w, h, 0.0f, 0.0f, w - 1, h - 1, w, h);
			pRC->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen, -1, 1, w, h, 0.0f, 0.0f, w - 1, h - 1, w, h);
			pRC->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen, 1, -1, w, h, 0.0f, 0.0f, w - 1, h - 1, w, h);
			pRC->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen, 1, 1, w, h, 0.0f, 0.0f, w - 1, h - 1, w, h);
			break;
		}

	case 2:
		{
			pRC->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen, -2, -1, w, h, 0.0f, 0.0f, w - 1, h - 1, w, h);
			pRC->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen, 2, 1, w, h, 0.0f, 0.0f, w - 1, h - 1, w, h);
			pRC->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen, -2, -1, w, h, 0.0f, 0.0f, w - 1, h - 1, w, h);
			pRC->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen, 2, 1, w, h, 0.0f, 0.0f, w - 1, h - 1, w, h);
			break;
		}

	case 3:
		{
			pRC->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen, -2, -2, w, h, 0.0f, 0.0f, w - 1, h - 1, w, h);
			pRC->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen, -2, 2, w, h, 0.0f, 0.0f, w - 1, h - 1, w, h);
			pRC->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen, 2, -2, w, h, 0.0f, 0.0f, w - 1, h - 1, w, h);
			pRC->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen, 2, 2, w, h, 0.0f, 0.0f, w - 1, h - 1, w, h);
			break;
		}

	default: break;
	}

	stencilStateDisable.SetStencilState(pRC);

	I::ModelRender->ForcedMaterialOverride(nullptr);
	I::RenderView->SetColorModulation(flOriginalColor);
	I::RenderView->SetBlend(flOriginalBlend);
}

void COutlines::CleanUp()
{
	// Set flag first to prevent any rendering from using materials
	m_bCleaningUp = true;

	// Clear entity lists to prevent any pending renders
	m_mapDrawnEntities.clear();
	m_vecOutlineEntities.clear();

	// Null out pointers first before decrementing references
	// This prevents race conditions where render thread checks pointer validity
	auto pMatHalo = m_pMatHaloAddToScreen;
	auto pBuffer0 = m_pRenderBuffer0;
	auto pBuffer1 = m_pRenderBuffer1;
	auto pBlurX = m_pMatBlurX;
	auto pBlurY = m_pMatBlurY;

	m_pMatHaloAddToScreen = nullptr;
	m_pRenderBuffer0 = nullptr;
	m_pRenderBuffer1 = nullptr;
	m_pMatBlurX = nullptr;
	m_pMatBlurY = nullptr;
	m_pBloomAmount = nullptr;
	m_pMatGlowColor = nullptr;
	m_pRtFullFrame = nullptr;

	// Now safe to decrement references
	if (pMatHalo)
	{
		pMatHalo->DecrementReferenceCount();
		pMatHalo->DeleteIfUnreferenced();
	}

	if (pBuffer0)
	{
		pBuffer0->DecrementReferenceCount();
		pBuffer0->DeleteIfUnreferenced();
	}

	if (pBuffer1)
	{
		pBuffer1->DecrementReferenceCount();
		pBuffer1->DeleteIfUnreferenced();
	}

	if (pBlurX)
	{
		pBlurX->DecrementReferenceCount();
		pBlurX->DeleteIfUnreferenced();
	}

	if (pBlurY)
	{
		pBlurY->DecrementReferenceCount();
		pBlurY->DeleteIfUnreferenced();
	}

	m_bCleaningUp = false;
}

void COutlines::SetModelStencil(IMatRenderContext* pRenderContext)
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
