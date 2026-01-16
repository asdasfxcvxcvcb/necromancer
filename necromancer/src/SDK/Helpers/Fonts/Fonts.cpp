#include "Fonts.h"

void CFontManager::Reload()
{
	m_mapFonts[EFonts::Menu] = { "Verdana", 12, FONTFLAG_ANTIALIAS, 0 };
	m_mapFonts[EFonts::ESP] = { "Verdana", 12, FONTFLAG_OUTLINE, 0 };
	m_mapFonts[EFonts::ESP_CONDS] = { "Small Fonts", 9, FONTFLAG_OUTLINE, 0 };
	m_mapFonts[EFonts::ESP_SMALL] = { "Small Fonts", 11, FONTFLAG_OUTLINE, 0 };
	m_mapFonts[EFonts::CritIndicator] = { "Small Fonts", 11, FONTFLAG_OUTLINE, 0 }; // Default size, will be updated dynamically
	m_mapFonts[EFonts::ChatESP_Large] = { "Small Fonts", 11, FONTFLAG_OUTLINE, 0 };  // Close range
	m_mapFonts[EFonts::ChatESP_Medium] = { "Small Fonts", 9, FONTFLAG_OUTLINE, 0 };  // Medium range
	m_mapFonts[EFonts::ChatESP_Small] = { "Small Fonts", 7, FONTFLAG_OUTLINE, 0 };   // Far range

	for (auto &v : m_mapFonts)
	{
		I::MatSystemSurface->SetFontGlyphSet
		(
			v.second.m_dwFont = I::MatSystemSurface->CreateFont(),
			v.second.m_szName,	//name
			v.second.m_nTall,	//tall
			v.second.m_nWeight,	//weight
			0,					//blur
			0,					//scanlines
			v.second.m_nFlags	//flags
		);
	}
}

void CFontManager::UpdateCritIndicatorFont(int nSizePercent)
{
	// Calculate font size: 11 at 100%, up to 22 at 200%
	int nTall = 11 * nSizePercent / 100;
	
	auto& font = m_mapFonts[EFonts::CritIndicator];
	if (font.m_nTall != nTall)
	{
		font.m_nTall = nTall;
		I::MatSystemSurface->SetFontGlyphSet
		(
			font.m_dwFont,
			font.m_szName,
			font.m_nTall,
			font.m_nWeight,
			0,
			0,
			font.m_nFlags
		);
	}
}

const CFont &CFontManager::Get(EFonts eFont)
{
	return m_mapFonts[eFont];
}