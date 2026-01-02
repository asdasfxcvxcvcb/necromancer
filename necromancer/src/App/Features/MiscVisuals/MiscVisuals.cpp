#include "MiscVisuals.h"

#include "../CFG.h"
#include "../RapidFire/RapidFire.h"
#include "../Crits/Crits.h"
#include "../Menu/Menu.h"
#include "../FakeLag/FakeLag.h"

#include "../VisualUtils/VisualUtils.h"
#include "../SpyCamera/SpyCamera.h"

#pragma warning (disable : 4244) //possible loss of data (int to float)

// Initialize bloom rendering system (same as Paint)
void CMiscVisuals::InitializeBloom()
{
	if (m_bBloomInitialized)
		return;

	if (!m_pMatGlowColor)
		m_pMatGlowColor = I::MaterialSystem->FindMaterial("dev/glow_color", TEXTURE_GROUP_OTHER);

	if (!m_pRtFullFrame)
		m_pRtFullFrame = I::MaterialSystem->FindTexture("_rt_FullFrameFB", TEXTURE_GROUP_RENDER_TARGET);

	if (!m_pRenderBuffer0)
	{
		m_pRenderBuffer0 = I::MaterialSystem->CreateNamedRenderTargetTextureEx(
			"seo_fov_buffer0",
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

	if (!m_pRenderBuffer1)
	{
		m_pRenderBuffer1 = I::MaterialSystem->CreateNamedRenderTargetTextureEx(
			"seo_fov_buffer1",
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
		kv->SetString("$basetexture", "seo_fov_buffer0");
		kv->SetString("$additive", "1");
		m_pMatHaloAddToScreen = I::MaterialSystem->CreateMaterial("seo_fov_material", kv);
	}

	if (!m_pMatBlurX)
	{
		const auto kv = new KeyValues("BlurFilterX");
		kv->SetString("$basetexture", "seo_fov_buffer0");
		m_pMatBlurX = I::MaterialSystem->CreateMaterial("seo_fov_material_blurx", kv);
	}

	if (!m_pMatBlurY)
	{
		const auto kv = new KeyValues("BlurFilterY");
		kv->SetString("$basetexture", "seo_fov_buffer1");
		m_pMatBlurY = I::MaterialSystem->CreateMaterial("seo_fov_material_blury", kv);
		if (m_pMatBlurY)
			m_pBloomAmount = m_pMatBlurY->FindVar("$bloomamount", nullptr);
	}

	m_bBloomInitialized = true;
}

void CMiscVisuals::CleanUpBloom()
{
	if (m_pMatHaloAddToScreen)
	{
		m_pMatHaloAddToScreen->DecrementReferenceCount();
		m_pMatHaloAddToScreen->DeleteIfUnreferenced();
		m_pMatHaloAddToScreen = nullptr;
	}

	if (m_pRenderBuffer0)
	{
		m_pRenderBuffer0->DecrementReferenceCount();
		m_pRenderBuffer0->DeleteIfUnreferenced();
		m_pRenderBuffer0 = nullptr;
	}

	if (m_pRenderBuffer1)
	{
		m_pRenderBuffer1->DecrementReferenceCount();
		m_pRenderBuffer1->DeleteIfUnreferenced();
		m_pRenderBuffer1 = nullptr;
	}

	if (m_pMatBlurX)
	{
		m_pMatBlurX->DecrementReferenceCount();
		m_pMatBlurX->DeleteIfUnreferenced();
		m_pMatBlurX = nullptr;
	}

	if (m_pMatBlurY)
	{
		m_pMatBlurY->DecrementReferenceCount();
		m_pMatBlurY->DeleteIfUnreferenced();
		m_pMatBlurY = nullptr;
	}

	m_bBloomInitialized = false;
}

// Helper lambda for Paint-style rainbow color (position-based like Paint.cpp)
static Color_t GetRainbowColor(int segment, float rate)
{
	const float t = segment * 0.1f;  // Position offset for multi-color effect
	const int r = std::lround(std::cosf(I::GlobalVars->realtime * rate + t + 0.0f) * 127.5f + 127.5f);
	const int g = std::lround(std::cosf(I::GlobalVars->realtime * rate + t + 2.0f) * 127.5f + 127.5f);
	const int b = std::lround(std::cosf(I::GlobalVars->realtime * rate + t + 4.0f) * 127.5f + 127.5f);
	return { static_cast<byte>(r), static_cast<byte>(g), static_cast<byte>(b), 255 };
}

void CMiscVisuals::AimbotFOVCircle()
{
	if (I::EngineClient->IsTakingScreenshot())
		return;

	if (!CFG::Visuals_Aimbot_FOV_Circle
		|| I::EngineVGui->IsGameUIVisible()
		|| I::Input->CAM_IsThirdPerson())
		return;

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal)
		return;

	const auto flAimFOV = G::flAimbotFOV;
	if (!flAimFOV)
		return;

	const float flRadius = tanf(DEG2RAD(flAimFOV) / 2.0f) / tanf(DEG2RAD(static_cast<float>(pLocal->m_iFOV())) / 2.0f) * H::Draw->GetScreenW();
	const int centerX = H::Draw->GetScreenW() / 2;
	const int centerY = H::Draw->GetScreenH() / 2;
	const int segments = 70;
	const float step = 2.0f * 3.14159f / segments;

	// Use shader-based bloom if glow is enabled
	if (CFG::Visuals_Aimbot_FOV_Circle_Glow && CFG::Visuals_Aimbot_FOV_Circle_RGB)
	{
		AimbotFOVCircleBloom();
		return;
	}

	if (CFG::Visuals_Aimbot_FOV_Circle_RGB)
	{
		const float rate = CFG::Visuals_Aimbot_FOV_Circle_RGB_Rate;
		
		// Draw main circle with per-segment rainbow colors
		for (int i = 0; i < segments; i++)
		{
			const float angle1 = i * step;
			const float angle2 = (i + 1) * step;
			
			const int x1 = centerX + static_cast<int>(flRadius * std::cosf(angle1));
			const int y1 = centerY + static_cast<int>(flRadius * std::sinf(angle1));
			const int x2 = centerX + static_cast<int>(flRadius * std::cosf(angle2));
			const int y2 = centerY + static_cast<int>(flRadius * std::sinf(angle2));
			
			Color_t color = GetRainbowColor(i, rate);
			color.a = static_cast<byte>(255.0f * CFG::Visuals_Aimbot_FOV_Circle_Alpha);
			H::Draw->Line(x1, y1, x2, y2, color);
		}
	}
	else
	{
		// Non-RGB mode - single color
		Color_t circleColor = CFG::Visuals_Aimbot_FOV_Circle_Color;
		circleColor.a = static_cast<byte>(255.0f * CFG::Visuals_Aimbot_FOV_Circle_Alpha);
		H::Draw->OutlinedCircle(centerX, centerY, static_cast<int>(flRadius), segments, circleColor);
	}
}

// Shader-based bloom FOV circle (same technique as Paint)
void CMiscVisuals::AimbotFOVCircleBloom()
{
	int w = H::Draw->GetScreenW(), h = H::Draw->GetScreenH();
	if (w < 1 || h < 1 || w > 4096 || h > 2160)
		return;

	InitializeBloom();

	if (!m_pMatGlowColor || !m_pRenderBuffer0 || !m_pRenderBuffer1 || !m_pMatBlurX || !m_pMatBlurY || !m_pMatHaloAddToScreen)
		return;

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal)
		return;

	const auto flAimFOV = G::flAimbotFOV;
	if (!flAimFOV)
		return;

	auto pRenderContext = I::MaterialSystem->GetRenderContext();
	if (!pRenderContext)
		return;

	if (m_pBloomAmount)
		m_pBloomAmount->SetIntValue(CFG::Visuals_Aimbot_FOV_Circle_Bloom_Amount);

	const float flRadius = tanf(DEG2RAD(flAimFOV) / 2.0f) / tanf(DEG2RAD(static_cast<float>(pLocal->m_iFOV())) / 2.0f) * w;
	const float centerX = w / 2.0f;
	const float centerY = h / 2.0f;
	const int segments = 70;
	const float step = 2.0f * 3.14159f / segments;
	const float rate = CFG::Visuals_Aimbot_FOV_Circle_RGB_Rate;

	// Render to buffer with glow material
	pRenderContext->PushRenderTargetAndViewport();
	{
		pRenderContext->SetRenderTarget(m_pRenderBuffer0);
		pRenderContext->Viewport(0, 0, w, h);
		pRenderContext->ClearColor4ub(0, 0, 0, 0);
		pRenderContext->ClearBuffers(true, false, false);

		I::ModelRender->ForcedMaterialOverride(m_pMatGlowColor);

		// Draw circle segments as 3D lines for the glow effect
		for (int i = 0; i < segments; i++)
		{
			const float angle1 = i * step;
			const float angle2 = (i + 1) * step;
			
			// Convert screen coords to world coords for RenderLine
			// We'll use screen-space rendering instead
			Vec3 v1 = { centerX + flRadius * std::cosf(angle1), centerY + flRadius * std::sinf(angle1), 0 };
			Vec3 v2 = { centerX + flRadius * std::cosf(angle2), centerY + flRadius * std::sinf(angle2), 0 };
			
			Color_t color = GetRainbowColor(i, rate);
			
			// Draw using surface (2D) since we're in screen space
			I::MatSystemSurface->DrawSetColor(color.r, color.g, color.b, 255);
			I::MatSystemSurface->DrawLine(static_cast<int>(v1.x), static_cast<int>(v1.y), 
			                               static_cast<int>(v2.x), static_cast<int>(v2.y));
		}

		I::ModelRender->ForcedMaterialOverride(nullptr);
	}
	pRenderContext->PopRenderTargetAndViewport();

	// Apply blur passes
	pRenderContext->PushRenderTargetAndViewport();
	{
		pRenderContext->Viewport(0, 0, w, h);
		pRenderContext->SetRenderTarget(m_pRenderBuffer1);
		pRenderContext->DrawScreenSpaceRectangle(m_pMatBlurX, 0, 0, w, h, 0.0f, 0.0f, w - 1, h - 1, w, h);
		pRenderContext->SetRenderTarget(m_pRenderBuffer0);
		pRenderContext->DrawScreenSpaceRectangle(m_pMatBlurY, 0, 0, w, h, 0.0f, 0.0f, w - 1, h - 1, w, h);
	}
	pRenderContext->PopRenderTargetAndViewport();

	// Draw the blurred result to screen with additive blending
	ShaderStencilState_t sEffect = {};
	sEffect.m_bEnable = true;
	sEffect.m_nWriteMask = 0x0;
	sEffect.m_nTestMask = 0xFF;
	sEffect.m_nReferenceValue = 0;
	sEffect.m_CompareFunc = STENCILCOMPARISONFUNCTION_EQUAL;
	sEffect.m_PassOp = STENCILOPERATION_KEEP;
	sEffect.m_FailOp = STENCILOPERATION_KEEP;
	sEffect.m_ZFailOp = STENCILOPERATION_KEEP;
	sEffect.SetStencilState(pRenderContext);

	pRenderContext->DrawScreenSpaceRectangle(m_pMatHaloAddToScreen, 0, 0, w, h, 0.0f, 0.0f, w - 1, h - 1, w, h);

	ShaderStencilState_t stencilStateDisable = {};
	stencilStateDisable.m_bEnable = false;
	stencilStateDisable.SetStencilState(pRenderContext);

	// Also draw the main circle on top (non-bloomed) for crisp edges
	const float alpha = CFG::Visuals_Aimbot_FOV_Circle_Alpha;
	for (int i = 0; i < segments; i++)
	{
		const float angle1 = i * step;
		const float angle2 = (i + 1) * step;
		
		const int x1 = static_cast<int>(centerX + flRadius * std::cosf(angle1));
		const int y1 = static_cast<int>(centerY + flRadius * std::sinf(angle1));
		const int x2 = static_cast<int>(centerX + flRadius * std::cosf(angle2));
		const int y2 = static_cast<int>(centerY + flRadius * std::sinf(angle2));
		
		Color_t color = GetRainbowColor(i, rate);
		color.a = static_cast<byte>(255.0f * alpha);
		H::Draw->Line(x1, y1, x2, y2, color);
	}
}

void CMiscVisuals::ViewModelSway()
{
	static ConVar* cl_wpn_sway_interp = I::CVar->FindVar("cl_wpn_sway_interp");

	if (!cl_wpn_sway_interp)
		return;

	const auto pLocal = H::Entities->GetLocal();

	if (!pLocal)
		return;

	if (CFG::Visuals_ViewModel_Active && CFG::Visuals_ViewModel_Sway && !pLocal->deadflag())
	{
		if (const auto pWeapon = H::Entities->GetWeapon())
		{
			const float flBaseValue = pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW ? 0.02f : 0.05f;

			cl_wpn_sway_interp->SetValue(flBaseValue * CFG::Visuals_ViewModel_Sway_Scale);
		}
	}
	else
	{
		if (cl_wpn_sway_interp->GetFloat() != 0.f)
		{
			cl_wpn_sway_interp->SetValue(0.0f);
		}
	}
	
	// Apply viewmodel convars
	static ConVar* tf_use_min_viewmodels = I::CVar->FindVar("tf_use_min_viewmodels");
	static ConVar* cl_flipviewmodels = I::CVar->FindVar("cl_flipviewmodels");
	static ConVar* cl_first_person_uses_world_model = I::CVar->FindVar("cl_first_person_uses_world_model");
	
	if (tf_use_min_viewmodels)
		tf_use_min_viewmodels->SetValue(CFG::Visuals_ViewModel_Minimal ? 1 : 0);
	
	if (cl_flipviewmodels)
		cl_flipviewmodels->SetValue(CFG::Visuals_Viewmodel_Flip ? 1 : 0);
	
	if (cl_first_person_uses_world_model)
		cl_first_person_uses_world_model->SetValue(CFG::Visuals_ViewModel_WorldModel ? 1 : 0);
}

void CMiscVisuals::DetailProps()
{
	if (!CFG::Visuals_Disable_Detail_Props)
		return;

	static ConVar* r_drawdetailprops = I::CVar->FindVar("r_drawdetailprops");

	if (r_drawdetailprops && r_drawdetailprops->GetInt())
		r_drawdetailprops->SetValue(0);
}

void CMiscVisuals::ShiftBar()
{
	if (!CFG::Exploits_Shifting_Draw_Indicator)
		return;

	if (CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot())
	{
		return;
	}

	if (I::EngineVGui->IsGameUIVisible() || SDKUtils::BInEndOfMatch())
		return;

	const auto pLocal = H::Entities->GetLocal();

	if (!pLocal || pLocal->deadflag())
		return;

	const auto pWeapon = H::Entities->GetWeapon();

	if (!pWeapon)
		return;

	static int nBarW = 80;
	static int nBarH = 4;

	const int nBarX = (H::Draw->GetScreenW() / 2) - (nBarW / 2);
	const int nBarY = (H::Draw->GetScreenH() / 2) + 100;
	const int circleX = H::Draw->GetScreenW() / 2;

	if (CFG::Exploits_Shifting_Indicator_Style == 0)
	{
		H::Draw->Rect(nBarX - 1, nBarY - 1, nBarW + 2, nBarH + 2, CFG::Menu_Background);

		// Calculate max ticks budget (shared between DT and fakelag)
		static auto sv_maxusrcmdprocessticks = I::CVar->FindVar("sv_maxusrcmdprocessticks");
		int nMaxTicksBudget = sv_maxusrcmdprocessticks ? sv_maxusrcmdprocessticks->GetInt() : 24;
		if (CFG::Misc_AntiCheat_Enabled)
			nMaxTicksBudget = std::min(nMaxTicksBudget, 8);
		
		// Get current usage
		int nSavedDTTicks = Shifting::nAvailableTicks;
		int nChokedCommands = I::ClientState->chokedcommands;
		int nTotalUsed = nSavedDTTicks + nChokedCommands;
		
		if (nTotalUsed > 0)
		{
			// Calculate fill widths based on the actual max budget
			const int nDTFillWidth = static_cast<int>(Math::RemapValClamped(
				static_cast<float>(nSavedDTTicks),
				0.0f, static_cast<float>(nMaxTicksBudget),
				0.0f, static_cast<float>(nBarW)
			));
			
			const int nChokeFillWidth = static_cast<int>(Math::RemapValClamped(
				static_cast<float>(nChokedCommands),
				0.0f, static_cast<float>(nMaxTicksBudget),
				0.0f, static_cast<float>(nBarW)
			));

			// Use RGB with bloom if enabled
			if (CFG::Menu_Accent_Secondary_RGB)
			{
				const float rate = CFG::Menu_Accent_Secondary_RGB_Rate;
				
				// Draw DT ticks (rainbow)
				for (int i = 0; i < nDTFillWidth; i++)
				{
					Color_t color = GetRainbowColor(i, rate);
					const Color_t colorDim = {color.r, color.g, color.b, static_cast<byte>(25 + (230 * i / nBarW))};
					H::Draw->Line(nBarX + i, nBarY, nBarX + i, nBarY + nBarH - 1, colorDim);
				}
				// Outline with rainbow
				for (int i = 0; i < nDTFillWidth; i++)
				{
					Color_t color = GetRainbowColor(i, rate);
					H::Draw->Line(nBarX + i, nBarY, nBarX + i, nBarY, color);
					H::Draw->Line(nBarX + i, nBarY + nBarH - 1, nBarX + i, nBarY + nBarH - 1, color);
				}
				
				// Draw choked ticks using FakeLag color from config
				if (nChokedCommands > 0)
				{
					const Color_t chokeColor = CFG::Color_FakeLag;
					const Color_t chokeColorDim = {chokeColor.r, chokeColor.g, chokeColor.b, 100};
					int nActualChokeWidth = std::min(nChokeFillWidth, nBarW - nDTFillWidth);
					for (int i = 0; i < nActualChokeWidth; i++)
					{
						H::Draw->Line(nBarX + nDTFillWidth + i, nBarY, nBarX + nDTFillWidth + i, nBarY + nBarH - 1, chokeColorDim);
					}
					if (nActualChokeWidth > 0)
						H::Draw->OutlinedRect(nBarX + nDTFillWidth, nBarY, nActualChokeWidth, nBarH, chokeColor);
				}
				
				// Left and right edges for DT portion
				if (nDTFillWidth > 0)
				{
					Color_t leftColor = GetRainbowColor(0, rate);
					Color_t rightColor = GetRainbowColor(nDTFillWidth, rate);
					H::Draw->Line(nBarX, nBarY, nBarX, nBarY + nBarH - 1, leftColor);
					H::Draw->Line(nBarX + nDTFillWidth - 1, nBarY, nBarX + nDTFillWidth - 1, nBarY + nBarH - 1, rightColor);
				}
			}
			else
			{
				const Color_t color = F::VisualUtils->GetAccentSecondary();
				const Color_t colorDim = {color.r, color.g, color.b, 25};
				
				// Draw DT ticks
				if (nDTFillWidth > 0)
				{
					H::Draw->GradientRect(nBarX, nBarY, nDTFillWidth, nBarH, colorDim, color, false);
					H::Draw->OutlinedRect(nBarX, nBarY, nDTFillWidth, nBarH, color);
				}
				
				// Draw choked ticks using FakeLag color from config
				if (nChokedCommands > 0)
				{
					const Color_t chokeColor = CFG::Color_FakeLag;
					const Color_t chokeColorDim = {chokeColor.r, chokeColor.g, chokeColor.b, 50};
					int nChokeStartX = nBarX + nDTFillWidth;
					int nActualChokeWidth = std::min(nChokeFillWidth, nBarW - nDTFillWidth);
					if (nActualChokeWidth > 0)
					{
						H::Draw->GradientRect(nChokeStartX, nBarY, nActualChokeWidth, nBarH, chokeColorDim, chokeColor, false);
						H::Draw->OutlinedRect(nChokeStartX, nBarY, nActualChokeWidth, nBarH, chokeColor);
					}
				}
			}
		}
	}

	if (CFG::Exploits_Shifting_Indicator_Style == 1)
	{
		const float end{Math::RemapValClamped(static_cast<float>(Shifting::nAvailableTicks), 0.0f, MAX_COMMANDS, -90.0f, 359.0f)};

		H::Draw->Arc(circleX, nBarY, 21, 6.0f, -90.0f, 359.0f, CFG::Menu_Background);
		H::Draw->Arc(circleX, nBarY, 20, 4.0f, -90.0f, end, F::VisualUtils->GetAccentSecondary());
	}

	if (G::nTicksSinceCanFire < 30 && F::RapidFire->IsWeaponSupported(pWeapon))
	{
		if (CFG::Exploits_Shifting_Indicator_Style == 0)
		{
			H::Draw->Rect(nBarX - 1, (nBarY + nBarH + 4) - 1, nBarW + 2, nBarH + 2, CFG::Menu_Background);

			if (G::nTicksSinceCanFire > 0)
			{
				constexpr Color_t color = {241, 196, 15, 255};
				constexpr Color_t colorDim = {color.r, color.g, color.b, 25};

				const int nFillWidth = static_cast<int>(Math::RemapValClamped(
					static_cast<float>(G::nTicksSinceCanFire),
					0.0f, 24.0f,
					0.0f, static_cast<float>(nBarW)
				));

				H::Draw->GradientRect(nBarX, nBarY + nBarH + 4, nFillWidth, nBarH, colorDim, color, false);
				H::Draw->OutlinedRect(nBarX, nBarY + nBarH + 4, nFillWidth, nBarH, color);
			}
		}

		if (CFG::Exploits_Shifting_Indicator_Style == 1)
		{
			const float end{Math::RemapValClamped(static_cast<float>(G::nTicksSinceCanFire), 0.0f, 24.0f, -90.0f, 359.0f)};

			H::Draw->Arc(circleX, nBarY, 24, 2.0f, -90.0f, 359.0f, CFG::Menu_Background);
			H::Draw->Arc(circleX, nBarY, 24, 2.0f, -90.0f, end, {241, 196, 15, 255});
		}
	}
}

void CMiscVisuals::SniperLines()
{
	if (CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot())
	{
		return;
	}

	auto getMaxViewOffsetZ = [](C_TFPlayer* pPlayer)
	{
		if (pPlayer->m_fFlags() & FL_DUCKING)
			return 45.0f;

		switch (pPlayer->m_iClass())
		{
			case TF_CLASS_SCOUT: return 65.0f;
			case TF_CLASS_SOLDIER: return 68.0f;
			case TF_CLASS_PYRO: return 68.0f;
			case TF_CLASS_DEMOMAN: return 68.0f;
			case TF_CLASS_HEAVYWEAPONS: return 75.0f;
			case TF_CLASS_ENGINEER: return 68.0f;
			case TF_CLASS_MEDIC: return 75.0f;
			case TF_CLASS_SNIPER: return 75.0f;
			case TF_CLASS_SPY: return 75.0f;
			default: return 0.0f;
		}
	};

	if (!CFG::ESP_Active || !CFG::ESP_Players_Active || !CFG::ESP_Players_Sniper_Lines
		|| I::EngineVGui->IsGameUIVisible() || SDKUtils::BInEndOfMatch() || F::SpyCamera->IsRendering())
		return;

	const auto pLocal = H::Entities->GetLocal();

	if (!pLocal)
		return;

	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ALL))
	{
		if (!pEntity)
			continue;

		const auto pPlayer = pEntity->As<C_TFPlayer>();

		if (!pPlayer || pPlayer == pLocal || pPlayer->deadflag() || pPlayer->m_iClass() != TF_CLASS_SNIPER)
			continue;

		const auto pWeapon = pPlayer->m_hActiveWeapon().Get();

		if (!pWeapon)
		{
			continue;
		}

		const bool classicCharging = pWeapon->As<C_TFWeaponBase>()->m_iItemDefinitionIndex() == Sniper_m_TheClassic && pWeapon->As<C_TFSniperRifleClassic>()->m_bCharging();

		if (!pPlayer->InCond(TF_COND_ZOOMED) && !classicCharging)
		{
			continue;
		}

		const bool bIsFriend = pPlayer->IsPlayerOnSteamFriendsList();

		if (CFG::ESP_Players_Ignore_Friends && bIsFriend)
			continue;

		if (!bIsFriend)
		{
			if (CFG::ESP_Players_Ignore_Teammates && pPlayer->m_iTeamNum() == pLocal->m_iTeamNum())
				continue;

			if (CFG::ESP_Players_Ignore_Enemies && pPlayer->m_iTeamNum() != pLocal->m_iTeamNum())
				continue;
		}

		Vec3 vForward = {};
		Math::AngleVectors(pPlayer->GetEyeAngles(), &vForward);

		Vec3 vStart = pPlayer->m_vecOrigin() + Vec3(0.0f, 0.0f, getMaxViewOffsetZ(pPlayer));
		Vec3 vEnd = vStart + (vForward * 8192.0f);

		CTraceFilterWorldCustom traceFilter = {};
		trace_t trace = {};

		H::AimUtils->Trace(vStart, vEnd, MASK_SOLID, &traceFilter, &trace);

		vEnd = trace.endpos;

		RenderUtils::RenderLine(vStart, vEnd, F::VisualUtils->GetEntityColor(pLocal, pPlayer), true);
	}
}

void CMiscVisuals::CustomFOV(CViewSetup* pSetup)
{
	if (CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot())
	{
		return;
	}

	const auto pLocal = H::Entities->GetLocal();

	if (!pLocal)
		return;

	if (CFG::Visuals_Removals_Mode == 1 && pLocal->m_iObserverMode() == OBS_MODE_IN_EYE)
		return;

	if (!CFG::Visuals_Remove_Zoom && pLocal->IsZoomed())
		return;

	if (!pLocal->deadflag())
		pLocal->m_iFOV() = static_cast<int>(CFG::Visuals_FOV_Override);

	pSetup->fov = CFG::Visuals_FOV_Override;
}

void CMiscVisuals::Thirdperson(CViewSetup* pSetup)
{
	const auto pLocal = H::Entities->GetLocal();

	if (!pLocal || pLocal->deadflag())
		return;

	if (!I::MatSystemSurface->IsCursorVisible() && !I::EngineVGui->IsGameUIVisible() && !SDKUtils::BInEndOfMatch() && !G::bStartedFakeTaunt)
	{
		if (H::Input->IsPressed(CFG::Visuals_Thirdperson_Key))
			CFG::Visuals_Thirdperson_Active = !CFG::Visuals_Thirdperson_Active;
	}

	// Clean screenshot - force first person view
	const bool bCleanScreenshot = CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot();

	const bool bShouldDoTP = !bCleanScreenshot && (CFG::Visuals_Thirdperson_Active
		|| pLocal->InCond(TF_COND_TAUNTING)
		|| pLocal->InCond(TF_COND_HALLOWEEN_KART)
		|| pLocal->InCond(TF_COND_HALLOWEEN_THRILLER)
		|| pLocal->InCond(TF_COND_HALLOWEEN_GHOST_MODE)
		|| G::bStartedFakeTaunt);

	if (bShouldDoTP)
	{
		I::Input->CAM_ToThirdPerson();
	}

	else
	{
		I::Input->CAM_ToFirstPerson();
	}

	pLocal->ThirdPersonSwitch();

	if (bShouldDoTP)
	{
		Vec3 vForward = {}, vRight = {}, vUp = {};
		Math::AngleVectors(pSetup->angles, &vForward, &vRight, &vUp);

		const Vec3 vOffset = (vForward * CFG::Visuals_Thirdperson_Offset_Forward)
			- (vRight * CFG::Visuals_Thirdperson_Offset_Right)
			- (vUp * CFG::Visuals_Thirdperson_Offset_Up);

		const Vec3 vDesiredOrigin = pSetup->origin - vOffset;

		Ray_t ray = {};
		ray.Init(pSetup->origin, vDesiredOrigin, { -10.0f, -10.0f, -10.0f }, { 10.0f, 10.0f, 10.0f });
		CTraceFilterWorldCustom traceFilter = {};
		trace_t trace = {};
		I::EngineTrace->TraceRay(ray, MASK_SOLID, &traceFilter, &trace);

		pSetup->origin -= vOffset * trace.fraction;
	}
}

void CMiscVisuals::CritIndicator()
{
	// Delegate to the advanced crit tracking system
	F::CritHack->Draw();
}


