#include "Fonts.h"

#include "../Draw/Draw.h"
#include "../../../App/Features/Menu/MenuScale.h"
#include <cmath>
#include <algorithm>

void CFontManager::Rebuild()
{
	if (!m_bInitialized)
	{
		m_mapFonts[EFonts::Menu] = { "Verdana", 12, FONTFLAG_ANTIALIAS, 0, 0, 0 };
		m_mapFonts[EFonts::ESP] = { "Verdana", 12, FONTFLAG_OUTLINE, 0, 0, 0 };
		m_mapFonts[EFonts::ESP_CONDS] = { "Small Fonts", 9, FONTFLAG_OUTLINE, 0, 0, 0 };
		m_mapFonts[EFonts::ESP_SMALL] = { "Small Fonts", 11, FONTFLAG_OUTLINE, 0, 0, 0 };
		m_mapFonts[EFonts::CritIndicator] = { "Small Fonts", 11, FONTFLAG_OUTLINE, 0, 0, 0 };
		m_mapFonts[EFonts::ChatESP_Large] = { "Small Fonts", 14, FONTFLAG_OUTLINE, 0, 0, 0 };
		m_mapFonts[EFonts::ChatESP_Medium] = { "Small Fonts", 12, FONTFLAG_OUTLINE, 0, 0, 0 };
		m_mapFonts[EFonts::ChatESP_Small] = { "Small Fonts", 10, FONTFLAG_OUTLINE, 0, 0, 0 };
		m_mapFonts[EFonts::StatusAnnounce] = { "Verdana", 15, FONTFLAG_OUTLINE | FONTFLAG_ANTIALIAS, 0, 0, 0 };
		m_mapFonts[EFonts::NecroNet] = { "Verdana", 14, FONTFLAG_ANTIALIAS, 0, 0, 0 };
		m_bInitialized = true;
	}

	const int nScreenW = H::Draw ? H::Draw->GetScreenW() : 0;
	const int nScreenH = H::Draw ? H::Draw->GetScreenH() : 0;
	const float flScale = MenuScale::Calculate(nScreenW, nScreenH);

	for (auto &v : m_mapFonts)
	{
		if (!v.second.m_dwFont)
			v.second.m_dwFont = I::MatSystemSurface->CreateFont();
		if (!v.second.m_dwScaledFont)
			v.second.m_dwScaledFont = I::MatSystemSurface->CreateFont();

		const bool bBase = I::MatSystemSurface->SetFontGlyphSet
		(
			v.second.m_dwFont,
			v.second.m_szName,
			v.second.m_nTall,
			v.second.m_nWeight,
			0,
			0,
			v.second.m_nFlags
		);
		const int nScaledTall = std::max(1, static_cast<int>(std::lround(v.second.m_nTall * flScale)));
		const bool bScaled = I::MatSystemSurface->SetFontGlyphSet
		(
			v.second.m_dwScaledFont,
			v.second.m_szName,
			nScaledTall,
			v.second.m_nWeight,
			0,
			0,
			v.second.m_nFlags
		);
		if (!bBase || !bScaled)
		{
			char szMsg[160] = {};
			sprintf_s(szMsg, sizeof(szMsg), "[Necromancer] font rebuild failed: %s tall=%d scaled=%d base=%d\n", v.second.m_szName, v.second.m_nTall, nScaledTall, bBase);
			OutputDebugStringA(szMsg);
			m_bDirty = true;
		}
	}
	m_bDirty = false;
	m_nLastScreenW = nScreenW;
	m_nLastScreenH = nScreenH;
}

void CFontManager::Reload()
{
	m_bDirty = true;
	if (H::Draw && H::Draw->GetScreenW() > 0)
		Rebuild();
}

void CFontManager::EnsureValid()
{
	const int nScreenW = H::Draw ? H::Draw->GetScreenW() : 0;
	const int nScreenH = H::Draw ? H::Draw->GetScreenH() : 0;
	if (m_nLastScreenW != nScreenW || m_nLastScreenH != nScreenH)
		m_bDirty = true;
	if (m_bDirty)
		Rebuild();
}

void CFontManager::Invalidate()
{
	m_bDirty = true;
}

void CFontManager::UpdateCritIndicatorFont(int nSizePercent)
{
	int nTall = 11 * nSizePercent / 100;

	auto& font = m_mapFonts[EFonts::CritIndicator];
	if (font.m_nTall != nTall)
	{
		font.m_nTall = nTall;
		m_bDirty = true;
	}
}

const CFont &CFontManager::Get(EFonts eFont)
{
	return m_mapFonts[eFont];
}
