#include "MiscVisuals.h"

#include "../CFG.h"
#include "../RapidFire/RapidFire.h"
#include "../TickbaseManip/TickbaseManip.h"
#include "../Crits/Crits.h"
#include "../Menu/Menu.h"
#include "../FakeLag/FakeLag.h"
#include "../FakeAngle/FakeAngle.h"

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
// Pre-calculate phase offsets to avoid repeated addition
static Color_t GetRainbowColor(int segment, float rate)
{
	const float t = segment * 0.1f + I::GlobalVars->realtime * rate;
	const int r = std::lround(std::cosf(t) * 127.5f + 127.5f);
	const int g = std::lround(std::cosf(t + 2.0f) * 127.5f + 127.5f);
	const int b = std::lround(std::cosf(t + 4.0f) * 127.5f + 127.5f);
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

	// Cache screen dimensions
	const int screenW = H::Draw->GetScreenW();
	const int screenH = H::Draw->GetScreenH();
	const float flRadius = tanf(DEG2RAD(flAimFOV) / 2.0f) / tanf(DEG2RAD(static_cast<float>(pLocal->m_iFOV())) / 2.0f) * screenW;
	const int centerX = screenW / 2;
	const int centerY = screenH / 2;
	constexpr int segments = 70;
	constexpr float step = 2.0f * 3.14159f / segments;

	// Use shader-based bloom if glow is enabled
	if (CFG::Visuals_Aimbot_FOV_Circle_Glow && CFG::Visuals_Aimbot_FOV_Circle_RGB)
	{
		AimbotFOVCircleBloom();
		return;
	}

	if (CFG::Visuals_Aimbot_FOV_Circle_RGB)
	{
		const float rate = CFG::Visuals_Aimbot_FOV_Circle_RGB_Rate;
		const byte alpha = static_cast<byte>(255.0f * CFG::Visuals_Aimbot_FOV_Circle_Alpha);
		
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
			color.a = alpha;
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
			const float flDesiredValue = flBaseValue * CFG::Visuals_ViewModel_Sway_Scale;
			
			// Only update if value changed
			if (cl_wpn_sway_interp->GetFloat() != flDesiredValue)
				cl_wpn_sway_interp->SetValue(flDesiredValue);
		}
	}
	else
	{
		if (cl_wpn_sway_interp->GetFloat() != 0.f)
		{
			cl_wpn_sway_interp->SetValue(0.0f);
		}
	}
	
	// Apply viewmodel convars - cache previous values to avoid redundant SetValue calls
	static ConVar* tf_use_min_viewmodels = I::CVar->FindVar("tf_use_min_viewmodels");
	static ConVar* cl_flipviewmodels = I::CVar->FindVar("cl_flipviewmodels");
	static ConVar* cl_first_person_uses_world_model = I::CVar->FindVar("cl_first_person_uses_world_model");
	
	static bool bLastMinimal = false;
	static bool bLastFlip = false;
	static bool bLastWorldModel = false;
	
	if (tf_use_min_viewmodels && CFG::Visuals_ViewModel_Minimal != bLastMinimal)
	{
		tf_use_min_viewmodels->SetValue(CFG::Visuals_ViewModel_Minimal ? 1 : 0);
		bLastMinimal = CFG::Visuals_ViewModel_Minimal;
	}
	
	if (cl_flipviewmodels && CFG::Visuals_Viewmodel_Flip != bLastFlip)
	{
		cl_flipviewmodels->SetValue(CFG::Visuals_Viewmodel_Flip ? 1 : 0);
		bLastFlip = CFG::Visuals_Viewmodel_Flip;
	}
	
	if (cl_first_person_uses_world_model && CFG::Visuals_ViewModel_WorldModel != bLastWorldModel)
	{
		cl_first_person_uses_world_model->SetValue(CFG::Visuals_ViewModel_WorldModel ? 1 : 0);
		bLastWorldModel = CFG::Visuals_ViewModel_WorldModel;
	}
}

void CMiscVisuals::DetailProps()
{
	if (!CFG::Visuals_Disable_Detail_Props)
		return;

	static ConVar* r_drawdetailprops = I::CVar->FindVar("r_drawdetailprops");
	static bool bAlreadyDisabled = false;

	// Only set once - no need to check/set every frame
	if (r_drawdetailprops && !bAlreadyDisabled)
	{
		r_drawdetailprops->SetValue(0);
		bAlreadyDisabled = true;
	}
}

void CMiscVisuals::ShiftBar()
{
	if (!CFG::Exploits_Shifting_Draw_Indicator)
		return;

	if (CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot())
		return;

	if (I::EngineVGui->IsGameUIVisible() || SDKUtils::BInEndOfMatch())
		return;

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pLocal->deadflag())
		return;

	const auto pWeapon = H::Entities->GetWeapon();
	if (!pWeapon)
		return;

	// Handle dragging when menu is open
	if (F::Menu->IsOpen())
		ShiftBarDrag();

	int x = CFG::Exploits_Shifting_Indicator_Pos_X;
	int y = CFG::Exploits_Shifting_Indicator_Pos_Y;

	// Layout dimensions
	const int nWidth = CFG::Exploits_Shifting_Indicator_Width;
	const int nBarHeight = CFG::Exploits_Shifting_Indicator_Height;
	const int nPadding = 6;
	const int nRowHeight = 12;
	const auto& font = H::Fonts->Get(EFonts::ESP_SMALL);

	// Colors
	const Color_t clrOutline = {60, 60, 60, 255};
	const Color_t clrBarBg = {40, 40, 40, 180};
	const Color_t clrText = {220, 220, 220, 255};
	const Color_t clrTextGreen = {80, 255, 120, 255};
	const Color_t clrTextRed = {255, 80, 80, 255};
	const Color_t clrTextYellow = {255, 180, 60, 255};

	// Get accent secondary color (with RGB support)
	Color_t clrAccent = CFG::Menu_Accent_Secondary;
	if (CFG::Menu_Accent_Secondary_RGB)
	{
		const float rate = CFG::Menu_Accent_Secondary_RGB_Rate;
		clrAccent.r = static_cast<byte>(std::lround(std::cosf(I::GlobalVars->realtime * rate + 0.0f) * 127.5f + 127.5f));
		clrAccent.g = static_cast<byte>(std::lround(std::cosf(I::GlobalVars->realtime * rate + 2.0f) * 127.5f + 127.5f));
		clrAccent.b = static_cast<byte>(std::lround(std::cosf(I::GlobalVars->realtime * rate + 4.0f) * 127.5f + 127.5f));
	}

	// Calculate max ticks budget
	static auto sv_maxusrcmdprocessticks = I::CVar->FindVar("sv_maxusrcmdprocessticks");
	int nMaxTicksBudget = sv_maxusrcmdprocessticks ? sv_maxusrcmdprocessticks->GetInt() : 24;
	if (CFG::Misc_AntiCheat_Enabled && !CFG::Misc_AntiCheat_IgnoreTickLimit)
		nMaxTicksBudget = std::min(nMaxTicksBudget, 8);

	int nChokedCommands = I::ClientState->chokedcommands;
	int nProcessableTicks = F::Ticks->GetProcessableTicks();
	int nServerBudget = Shifting::GetServerProcessBudget();

	bool bCanDT = F::RapidFire->CanDoubleTapNow(pLocal, pWeapon);
	bool bShifting = Shifting::bShifting || Shifting::bShiftingWarp;
	bool bRecharging = Shifting::bRecharging;

	int nLeftX = x + nPadding;
	int nBarEndX = x + nWidth - nPadding;
	int nTextRightX = nBarEndX - 20;
	int nDrawY = y;

	// === ROW 1: Progress bar ===
	int nBarX = nLeftX;
	int nBarY = nDrawY;
	int nActualBarWidth = nWidth - nPadding * 2;

	// Bar background + outline
	H::Draw->Rect(nBarX, nBarY, nActualBarWidth, nBarHeight, clrBarBg);
	H::Draw->OutlinedRect(nBarX, nBarY, nActualBarWidth, nBarHeight, clrOutline);

	// Calculate fill ratios (no interpolation - instant)
	float flDTRatio = static_cast<float>(nProcessableTicks) / static_cast<float>((std::max)(nServerBudget, 1));
	float flChokeRatio = static_cast<float>(nChokedCommands) / static_cast<float>(nMaxTicksBudget);

	int nInnerWidth = nActualBarWidth - 2;
	int nFillX = nBarX + 1;
	int nFillY = nBarY + 1;
	int nFillHeight = nBarHeight - 2;

	// Draw DT fill with 3D gradient (same style as crit bar)
	int nFillWidth = static_cast<int>(flDTRatio * nInnerWidth);
	if (nFillWidth > 0)
	{
		for (int row = 0; row < nFillHeight; row++)
		{
			float flVertRatio = static_cast<float>(row) / static_cast<float>(std::max(nFillHeight - 1, 1));

			float flBrightness;
			if (flVertRatio < 0.3f)
				flBrightness = 1.4f - (flVertRatio / 0.3f) * 0.4f;
			else if (flVertRatio < 0.7f)
				flBrightness = 1.0f;
			else
				flBrightness = 1.0f - ((flVertRatio - 0.7f) / 0.3f) * 0.35f;

			Color_t clrLeft = {
				static_cast<byte>(std::clamp(static_cast<int>(clrAccent.r * flBrightness * 0.9f), 0, 255)),
				static_cast<byte>(std::clamp(static_cast<int>(clrAccent.g * flBrightness * 0.9f), 0, 255)),
				static_cast<byte>(std::clamp(static_cast<int>(clrAccent.b * flBrightness * 0.9f), 0, 255)),
				255
			};

			Color_t clrRight = {
				static_cast<byte>(std::clamp(static_cast<int>(clrAccent.r * flBrightness), 0, 255)),
				static_cast<byte>(std::clamp(static_cast<int>(clrAccent.g * flBrightness), 0, 255)),
				static_cast<byte>(std::clamp(static_cast<int>(clrAccent.b * flBrightness), 0, 255)),
				255
			};

			H::Draw->GradientRect(nFillX, nFillY + row, nFillWidth, 1, clrLeft, clrRight, true);
		}
	}

	// Draw DT tick indicator (transparent overlay showing DT cost)
	{
		const int nDTNeededTicks = F::Ticks->GetOptimalDTTicks();
		float flDTCostRatio = static_cast<float>(nDTNeededTicks) / static_cast<float>(nMaxTicksBudget);
		int nDTIndicatorWidth = static_cast<int>(flDTCostRatio * nInnerWidth);

		// Clamp to bar bounds
		nDTIndicatorWidth = std::min(nDTIndicatorWidth, nInnerWidth);

		if (nDTIndicatorWidth > 0)
		{
			// Gradient fill based on DT readiness
			Color_t clrDTLeft, clrDTRight, clrDTOutline;
			if (bRecharging)
			{
				// Yellow/amber - recharging
				clrDTLeft = {255, 200, 60, 70};
				clrDTRight = {255, 160, 30, 100};
				clrDTOutline = {255, 200, 60, 200};
			}
			else if (bCanDT)
			{
				// Bright green - ready
				clrDTLeft = {40, 255, 100, 70};
				clrDTRight = {20, 220, 80, 100};
				clrDTOutline = {60, 255, 120, 220};
			}
			else
			{
				// Red - not enough ticks
				clrDTLeft = {255, 60, 60, 70};
				clrDTRight = {200, 30, 30, 100};
				clrDTOutline = {255, 80, 80, 200};
			}

			// Draw gradient overlay
			H::Draw->GradientRect(nFillX, nFillY, nDTIndicatorWidth, nFillHeight, clrDTLeft, clrDTRight, true);

			// Full outline around the indicator
			H::Draw->OutlinedRect(nFillX, nFillY, nDTIndicatorWidth, nFillHeight, clrDTOutline);

			// Diagonal hatch lines for extra visual distinction when recharging
			if (bRecharging && nDTIndicatorWidth > 4)
			{
				Color_t clrHatch = {255, 200, 60, 50};
				for (int hx = -nFillHeight; hx < nDTIndicatorWidth; hx += 6)
				{
					int x1 = nFillX + std::max(hx, 0);
					int y1 = nFillY + std::max(-hx, 0);
					int x2 = nFillX + std::min(hx + nFillHeight, nDTIndicatorWidth);
					int y2 = nFillY + std::min(nFillHeight, nFillHeight + hx);
					if (x1 < x2 && y1 < y2)
						H::Draw->Line(x1, y1, x2, y2, clrHatch);
				}
			}
		}
	}

	// Draw choked ticks as a secondary color overlay
	int nChokeWidth = static_cast<int>(flChokeRatio * nInnerWidth);
	if (nChokeWidth > 0)
	{
		const Color_t chokeColor = CFG::Color_FakeLag;
		int nChokeStartX = nFillX + nFillWidth;
		int nActualChokeWidth = std::min(nChokeWidth, nInnerWidth - nFillWidth);

		if (nActualChokeWidth > 0)
		{
			for (int row = 0; row < nFillHeight; row++)
			{
				float flVertRatio = static_cast<float>(row) / static_cast<float>(std::max(nFillHeight - 1, 1));

				float flBrightness;
				if (flVertRatio < 0.3f)
					flBrightness = 1.4f - (flVertRatio / 0.3f) * 0.4f;
				else if (flVertRatio < 0.7f)
					flBrightness = 1.0f;
				else
					flBrightness = 1.0f - ((flVertRatio - 0.7f) / 0.3f) * 0.35f;

				Color_t clrLeft = {
					static_cast<byte>(std::clamp(static_cast<int>(chokeColor.r * flBrightness * 0.9f), 0, 255)),
					static_cast<byte>(std::clamp(static_cast<int>(chokeColor.g * flBrightness * 0.9f), 0, 255)),
					static_cast<byte>(std::clamp(static_cast<int>(chokeColor.b * flBrightness * 0.9f), 0, 255)),
					200
				};

				Color_t clrRight = {
					static_cast<byte>(std::clamp(static_cast<int>(chokeColor.r * flBrightness), 0, 255)),
					static_cast<byte>(std::clamp(static_cast<int>(chokeColor.g * flBrightness), 0, 255)),
					static_cast<byte>(std::clamp(static_cast<int>(chokeColor.b * flBrightness), 0, 255)),
					200
				};

				H::Draw->GradientRect(nChokeStartX, nFillY + row, nActualChokeWidth, 1, clrLeft, clrRight, true);
			}

			// Outline the choke portion
			H::Draw->OutlinedRect(nChokeStartX, nBarY, nActualChokeWidth, nBarHeight, chokeColor);
		}
	}

	nDrawY += nBarHeight + 2;

	// === ROW 2: TICKS label (left) and STATUS (right) ===
	// Left: TICKS: X/MAX
	H::Draw->String(font, nLeftX, nDrawY, clrText, POS_DEFAULT,
		std::format("TICKS: {}/{}", nProcessableTicks, nServerBudget).c_str());

	// Right: Status
	if (bShifting)
		H::Draw->String(font, nTextRightX, nDrawY, clrTextYellow, POS_CENTERX, "SHIFTING");
	else if (bRecharging)
		H::Draw->String(font, nTextRightX, nDrawY, clrTextYellow, POS_CENTERX, "RECHARGE");
	else if (bCanDT)
		H::Draw->String(font, nTextRightX, nDrawY, clrTextGreen, POS_CENTERX, "READY");
	else
		H::Draw->String(font, nTextRightX, nDrawY, clrTextRed, POS_CENTERX, "WAIT");

	nDrawY += nRowHeight;

	// === ROW 3: Choked info (right-aligned) ===
	if (nChokedCommands > 0 || Shifting::nDeficit > 0)
	{
		H::Draw->String(font, nTextRightX, nDrawY, {200, 150, 255, 255}, POS_CENTERX,
			std::format("USED: {} DROP: {}", nChokedCommands, Shifting::nDeficit).c_str());
	}
}

void CMiscVisuals::ShiftBarDrag()
{
	const int nMouseX = H::Input->GetMouseX();
	const int nMouseY = H::Input->GetMouseY();

	static bool bDragging = false;
	static int nDeltaX = 0;
	static int nDeltaY = 0;

	if (!bDragging && F::Menu->IsMenuWindowHovered())
		return;

	const int x = CFG::Exploits_Shifting_Indicator_Pos_X;
	const int y = CFG::Exploits_Shifting_Indicator_Pos_Y;

	// Hitbox matches the indicator size
	const int nWidth = CFG::Exploits_Shifting_Indicator_Width;
	const int nHeight = CFG::Exploits_Shifting_Indicator_Height + 40; // Bar height + text rows
	const bool bHovered = nMouseX >= x && nMouseX <= x + nWidth && nMouseY >= y && nMouseY <= y + nHeight;

	if (bHovered && H::Input->IsPressed(VK_LBUTTON))
	{
		nDeltaX = nMouseX - x;
		nDeltaY = nMouseY - y;
		bDragging = true;
	}

	if (!H::Input->IsPressed(VK_LBUTTON) && !H::Input->IsHeld(VK_LBUTTON))
		bDragging = false;

	if (bDragging)
	{
		CFG::Exploits_Shifting_Indicator_Pos_X = nMouseX - nDeltaX;
		CFG::Exploits_Shifting_Indicator_Pos_Y = nMouseY - nDeltaY;
	}
}

void CMiscVisuals::SniperLines()
{
	if (CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot())
		return;

	// Cache lambda as static to avoid recreation each call
	static auto getMaxViewOffsetZ = [](C_TFPlayer* pPlayer) -> float
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

	// Cache local team for comparison
	const int nLocalTeam = pLocal->m_iTeamNum();

	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ALL))
	{
		if (!pEntity)
			continue;

		const auto pPlayer = pEntity->As<C_TFPlayer>();
		if (!pPlayer || pPlayer == pLocal || pPlayer->deadflag() || pPlayer->m_iClass() != TF_CLASS_SNIPER)
			continue;

		const auto pWeapon = pPlayer->m_hActiveWeapon().Get();
		if (!pWeapon)
			continue;

		const bool classicCharging = pWeapon->As<C_TFWeaponBase>()->m_iItemDefinitionIndex() == Sniper_m_TheClassic && pWeapon->As<C_TFSniperRifleClassic>()->m_bCharging();

		if (!pPlayer->InCond(TF_COND_ZOOMED) && !classicCharging)
			continue;

		const bool bIsFriend = pPlayer->IsPlayerOnSteamFriendsList();

		if (CFG::ESP_Players_Ignore_Friends && bIsFriend)
			continue;

		if (!bIsFriend)
		{
			if (CFG::ESP_Players_Ignore_Teammates && pPlayer->m_iTeamNum() == nLocalTeam)
				continue;

			if (CFG::ESP_Players_Ignore_Enemies && pPlayer->m_iTeamNum() != nLocalTeam)
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

void CMiscVisuals::AspectRatio()
{
	static auto r_aspectratio = I::CVar->FindVar("r_aspectratio");
	if (!r_aspectratio)
		return;

	static float flStaticRatio = -1.f; // Start at -1 to force initial apply
	float flOldRatio = flStaticRatio;
	float flNewRatio = flStaticRatio = CFG::Visuals_Freecam_AspectRatio;
	
	// Only update when value changes (0 = no change/disabled)
	if (flNewRatio != flOldRatio)
		r_aspectratio->SetValue(flNewRatio);
}

void CMiscVisuals::ReapplyAspectRatio()
{
	// Force reapply aspect ratio on level change by resetting the static cache
	static auto r_aspectratio = I::CVar->FindVar("r_aspectratio");
	if (r_aspectratio && CFG::Visuals_Freecam_AspectRatio > 0.0f)
		r_aspectratio->SetValue(CFG::Visuals_Freecam_AspectRatio);
}

void CMiscVisuals::Freecam(CViewSetup* pSetup)
{
	// Toggle freecam with key
	if (!I::MatSystemSurface->IsCursorVisible() && !I::EngineVGui->IsGameUIVisible())
	{
		if (H::Input->IsPressed(CFG::Visuals_Freecam_Key))
		{
			m_bFreecamActive = !m_bFreecamActive;
			
			if (m_bFreecamActive)
			{
				// Save player angles, thirdperson state, and initialize freecam position when enabling
				m_vSavedPlayerAngles = I::EngineClient->GetViewAngles();
				m_bWasThirdpersonBeforeFreecam = CFG::Visuals_Thirdperson_Active;
				CFG::Visuals_Thirdperson_Active = true;  // Force thirdperson so camera detaches from player
				m_vFreecamPos = pSetup->origin;
				m_vFreecamAngles = pSetup->angles;
			}
			else
			{
				// Restore saved player angles and thirdperson state when disabling
				I::EngineClient->SetViewAngles(m_vSavedPlayerAngles);
				CFG::Visuals_Thirdperson_Active = m_bWasThirdpersonBeforeFreecam;
			}
		}
	}
	
	if (!m_bFreecamActive || !CFG::Visuals_Freecam_Key)
		return;
	
	// Update angles from mouse input
	m_vFreecamAngles = I::EngineClient->GetViewAngles();
	
	// Calculate movement direction
	Vec3 vForward = {}, vRight = {}, vUp = {};
	Math::AngleVectors(m_vFreecamAngles, &vForward, &vRight, &vUp);
	
	// Get movement input
	float flSpeed = CFG::Visuals_Freecam_Speed * I::GlobalVars->frametime;
	
	// Speed boost with shift
	if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
		flSpeed *= 2.0f;
	
	// WASD movement
	if (GetAsyncKeyState('W') & 0x8000)
		m_vFreecamPos = m_vFreecamPos + vForward * flSpeed;
	if (GetAsyncKeyState('S') & 0x8000)
		m_vFreecamPos = m_vFreecamPos - vForward * flSpeed;
	if (GetAsyncKeyState('A') & 0x8000)
		m_vFreecamPos = m_vFreecamPos - vRight * flSpeed;
	if (GetAsyncKeyState('D') & 0x8000)
		m_vFreecamPos = m_vFreecamPos + vRight * flSpeed;
	
	// Up/Down with space/ctrl
	if (GetAsyncKeyState(VK_SPACE) & 0x8000)
		m_vFreecamPos = m_vFreecamPos + Vec3(0, 0, flSpeed);
	if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
		m_vFreecamPos = m_vFreecamPos - Vec3(0, 0, flSpeed);
	
	// Apply freecam view
	pSetup->origin = m_vFreecamPos;
	pSetup->angles = m_vFreecamAngles;
}


