#pragma once

#include "../../TF2/IMatSystemSurface.h"

enum class EFonts
{
	Menu,
	ESP, ESP_CONDS, ESP_SMALL,
	CritIndicator, // Dynamic font for crit indicator text mode
	ChatESP_Large,   // ChatESP fonts at different sizes for distance scaling
	ChatESP_Medium,
	ChatESP_Small
};

class CFont
{
public:
	const char *m_szName;
	int m_nTall, m_nFlags, m_nWeight;
	DWORD m_dwFont;
};

class CFontManager
{
private:
	std::map<EFonts, CFont> m_mapFonts = {};

public:
	void Reload();
	void UpdateCritIndicatorFont(int nSizePercent);
	const CFont &Get(EFonts eFont);
};

MAKE_SINGLETON_SCOPED(CFontManager, Fonts, H);