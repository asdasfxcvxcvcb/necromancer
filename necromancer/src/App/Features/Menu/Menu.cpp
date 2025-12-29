#include "Menu.h"

#include "LateRenderer/LateRenderer.h"

#include "../CFG.h"
#include "../VisualUtils/VisualUtils.h"
#include "../Players/Players.h"
#include "../../CheaterDatabase/CheaterDatabase.h"

#define multiselect(label, unique, ...) static std::vector<std::pair<const char *, bool &>> unique##multiselect = __VA_ARGS__; \
SelectMulti(label, unique##multiselect)

void CMenu::Drag(int &x, int &y, int w, int h, int offset_y)
{
	static POINT delta = {};
	static bool drag = false;
	static bool move = false;

	static bool held = false;

	if (!H::Input->IsPressed(VK_LBUTTON) && !H::Input->IsHeld(VK_LBUTTON))
		held = false;

	int mousex = H::Input->GetMouseX();
	int mousey = H::Input->GetMouseY();

	if ((mousex > x && mousex < x + w && mousey > y - offset_y && mousey < y - offset_y + h) && (held || H::Input->IsPressed(VK_LBUTTON)))
	{
		held = true;
		drag = true;

		if (!move)
		{
			delta.x = mousex - x;
			delta.y = mousey - y;
			move = true;
		}
	}

	if (drag)
	{
		x = mousex - delta.x;
		y = mousey - delta.y;
	}

	if (!held)
	{
		drag = false;
		move = false;
	}
}

bool CMenu::IsHovered(int x, int y, int w, int h, void *pVar, bool bStrict)
{
	//this is pretty ok to use but like.. it can have annoying visual bugs with clicks..
	/*if (H::Input->IsHeld(VK_LBUTTON))
		return false;*/

	if (pVar == nullptr)
	{
		for (const auto &State : m_mapStates)
		{
			if (State.second)
				return false;
		}
	}

	else
	{
		for (const auto &State : m_mapStates)
		{
			if (State.second && State.first != pVar)
				return false;
		}
	}

	int mx = H::Input->GetMouseX();
	int my = H::Input->GetMouseY();

	if (bStrict)
	{
		bool bLeft = mx >= x;
		bool bRight = mx <= x + w;
		bool bTop = my >= y;
		bool bBottom = my <= y + h;

		return bLeft && bRight && bTop && bBottom;
	}

	else
	{
		bool bLeft = mx > x;
		bool bRight = mx < x + w;
		bool bTop = my > y;
		bool bBottom = my < y + h;

		return bLeft && bRight && bTop && bBottom;
	}
}

bool CMenu::IsHoveredSimple(int x, int y, int w, int h)
{
	int mx = H::Input->GetMouseX();
	int my = H::Input->GetMouseY();

	bool bLeft = mx >= x;
	bool bRight = mx <= x + w;
	bool bTop = my >= y;
	bool bBottom = my <= y + h;

	return bLeft && bRight && bTop && bBottom;
}

void CMenu::GroupBoxStart(const char *szLabel, int nWidth)
{
	m_nCursorY += CFG::Menu_Spacing_Y * 2; //hmm

	m_nLastGroupBoxY = m_nCursorY;
	m_nLastGroupBoxW = nWidth;

	int x = m_nCursorX;
	int y = m_nCursorY;
	int w = nWidth;

	int nTextW = [&]() -> int {
		int w_out = 0, h_out = 0;
		I::MatSystemSurface->GetTextSize(H::Fonts->Get(EFonts::Menu).m_dwFont, Utils::ConvertUtf8ToWide(szLabel).c_str(), w_out, h_out);
		return w_out;
	}();

	int nWidthRemaining = w - (nTextW + (CFG::Menu_Spacing_X * 4));
	int nSideWidth = nWidthRemaining / 2;

	H::Draw->Line(x, y, x + nSideWidth, y, CFG::Menu_Accent_Primary);
	H::Draw->Line(x + w, y, x + (w - nSideWidth), y, CFG::Menu_Accent_Primary);

	H::Draw->String(
		H::Fonts->Get(EFonts::Menu),
		x + (w / 2), y - (CFG::Menu_Spacing_Y - 1), CFG::Menu_Text_Inactive, POS_CENTERXY, szLabel
	);

	m_nCursorX += CFG::Menu_Spacing_X * 2;
	m_nCursorY += CFG::Menu_Spacing_Y * 2;
}

void CMenu::GroupBoxEnd()
{
	m_nCursorX -= (CFG::Menu_Spacing_X * 2);
	m_nCursorY += 2;

	Color_t clr = CFG::Menu_Accent_Primary;

	H::Draw->Line(m_nCursorX, m_nLastGroupBoxY, m_nCursorX, m_nCursorY, clr);
	H::Draw->Line(m_nCursorX + m_nLastGroupBoxW, m_nLastGroupBoxY, m_nCursorX + m_nLastGroupBoxW, m_nCursorY, clr);
	H::Draw->Line(m_nCursorX, m_nCursorY, m_nCursorX + m_nLastGroupBoxW, m_nCursorY, clr);

	m_nCursorY += CFG::Menu_Spacing_Y;
}

bool CMenu::CheckBox(const char *szLabel, bool &bVar)
{
	bool bCallback = false;

	int x = m_nCursorX;
	int y = m_nCursorY;
	int w = CFG::Menu_CheckBox_Width;
	int h = CFG::Menu_CheckBox_Height;

	int w_with_text = [&]() -> int {
		int w_out = 0, h_out = 0;
		I::MatSystemSurface->GetTextSize(H::Fonts->Get(EFonts::Menu).m_dwFont, Utils::ConvertUtf8ToWide(szLabel).c_str(), w_out, h_out);
		return w + w_out + 1;
	}();

	bool bHovered = IsHovered(x, y, w_with_text, h, &bVar);
	
	// Animation state tracking
	std::string checkboxId = std::string("chk_") + szLabel + "_" + std::to_string(x) + "_" + std::to_string(y);
	
	// Hover animation
	if (m_buttonHoverStates.find(checkboxId) == m_buttonHoverStates.end())
		m_buttonHoverStates[checkboxId] = 0.0f;
	
	float& hoverState = m_buttonHoverStates[checkboxId];
	float targetHover = bHovered ? 1.0f : 0.0f;
	hoverState += (targetHover - hoverState) * 8.0f * I::GlobalVars->frametime;
	
	// Toggle animation
	if (m_buttonPressStates.find(checkboxId) == m_buttonPressStates.end())
		m_buttonPressStates[checkboxId] = bVar ? 1.0f : 0.0f;
	
	float& toggleState = m_buttonPressStates[checkboxId];
	float targetToggle = bVar ? 1.0f : 0.0f;
	toggleState += (targetToggle - toggleState) * 10.0f * I::GlobalVars->frametime;

	if (bHovered && H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed) {
		bCallback = m_bClickConsumed = true;
		bVar = !bVar;
	}

	Color_t clr = CFG::Menu_Accent_Primary;
	
	// Animated fill
	if (toggleState > 0.01f) {
		byte fillAlpha = static_cast<byte>(25 + toggleState * 55);
		Color_t fillColor = { clr.r, clr.g, clr.b, fillAlpha };
		Color_t fillColorBright = { clr.r, clr.g, clr.b, static_cast<byte>(fillAlpha * 0.5f) };
		H::Draw->GradientRect(x, y, w, h, fillColorBright, fillColor, false);
		
		// Animated checkmark
		int checkSize = static_cast<int>(w * 0.6f * toggleState);
		int checkX = x + (w - checkSize) / 2;
		int checkY = y + (h - checkSize) / 2;
		H::Draw->Rect(checkX, checkY, checkSize, checkSize, clr);
	}
	
	// Hover glow
	if (hoverState > 0.01f)
	{
		byte glowAlpha = static_cast<byte>(hoverState * 30);
		Color_t glowColor = { clr.r, clr.g, clr.b, glowAlpha };
		H::Draw->Rect(x - 1, y - 1, w + 2, h + 2, glowColor);
	}

	H::Draw->OutlinedRect(x, y, w, h, clr);
	
	// Text color transition
	Color_t textColor = CFG::Menu_Text_Inactive;
	if (hoverState > 0.01f)
	{
		Color_t activeColor = CFG::Menu_Text_Active;
		textColor.r = static_cast<byte>(textColor.r + (activeColor.r - textColor.r) * hoverState);
		textColor.g = static_cast<byte>(textColor.g + (activeColor.g - textColor.g) * hoverState);
		textColor.b = static_cast<byte>(textColor.b + (activeColor.b - textColor.b) * hoverState);
	}

	H::Draw->String(
		H::Fonts->Get(EFonts::Menu),
		x + w + CFG::Menu_Spacing_X,
		y + (h / 2),
		textColor,
		POS_CENTERY,
		szLabel
	);

	m_nCursorY += h + CFG::Menu_Spacing_Y;

	return bCallback;
}

bool CMenu::SliderFloat(const char *szLabel, float &flVar, float flMin, float flMax, float flStep, const char *szFormat)
{
	bool bCallback = false;

	int x = m_nCursorX;
	int y = m_nCursorY;
	int w = CFG::Menu_Slider_Width;
	int h = CFG::Menu_Slider_Height;

	int nTextH = H::Fonts->Get(EFonts::Menu).m_nTall;

	bool bHovered = IsHovered(x, y + (nTextH + CFG::Menu_Spacing_Y), w, h, &flVar, true);

	bool bAcceptsInput = [&]() -> bool
	{
		for (const auto &State : m_mapStates)
		{
			if (State.second && State.first != &flVar)
				return false;
		}

		return true;
	}();

	if (!m_bClickConsumed && bAcceptsInput)
	{
		if (H::Input->IsPressed(VK_RBUTTON))
		{
			if (bHovered)
			{
				bool bLeftSideHovered = IsHovered(x, y + (nTextH + CFG::Menu_Spacing_Y), w / 2, h, &flVar, true);

				if (bLeftSideHovered)
					flVar -= flStep;

				else flVar += flStep;
			}
		}

		if (H::Input->IsPressed(VK_LBUTTON))
		{
			if (bHovered) {
				m_bClickConsumed = true;
				m_mapStates[&flVar] = true;
			}
		}

		else
		{
			if (!H::Input->IsHeld(VK_LBUTTON))
				m_mapStates[&flVar] = false;
		}
	}

	if (m_mapStates[&flVar])
	{
		flVar = Math::RemapValClamped(
			static_cast<float>(H::Input->GetMouseX()),
			static_cast<float>(x), static_cast<float>(x + w),
			flMin, flMax
		);
	}

	// Round to step - fixed for negative numbers
	if (flStep > 0.f)
	{
		flVar = roundf(flVar / flStep) * flStep;
	}

	// Round to 2 decimal places - fixed for negative numbers
	flVar = roundf(flVar * 100.0f) / 100.0f;
	flVar = std::clamp(flVar, flMin, flMax);

	int nFillWidth = static_cast<int>(Math::RemapValClamped(
		flVar,
		flMin, flMax,
		0.0f, static_cast<float>(w)
	));

	H::Draw->String(
		H::Fonts->Get(EFonts::Menu),
		x, y,
		(bHovered || m_mapStates[&flVar]) ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive,
		POS_DEFAULT,
		szLabel
	);

	Color_t clr = CFG::Menu_Accent_Primary;
	Color_t clr_dim = { clr.r, clr.g, clr.b, 25 };

	H::Draw->Rect(x, y + (nTextH + CFG::Menu_Spacing_Y), w, h, clr_dim);
	H::Draw->GradientRect(x, y + (nTextH + CFG::Menu_Spacing_Y), nFillWidth, h, clr_dim, clr, false);
	H::Draw->OutlinedRect(x, y + (nTextH + CFG::Menu_Spacing_Y), nFillWidth, h, clr);
	H::Draw->Rect(x + (nFillWidth - 1), y + (nTextH + CFG::Menu_Spacing_Y) - 1, 2, h + 2, CFG::Menu_Text_Active);

	H::Draw->String(
		H::Fonts->Get(EFonts::Menu),
		x + (w + CFG::Menu_Spacing_X),
		y + (nTextH - 1),
		(bHovered || m_mapStates[&flVar]) ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive,
		POS_DEFAULT,
		szFormat, flVar
	);

	m_nCursorY += h + nTextH + CFG::Menu_Spacing_Y + CFG::Menu_Spacing_Y + 2; //+ 2 for the little white thingy

	return bCallback;
}

bool CMenu::SliderInt(const char *szLabel, int &nVar, int nMin, int nMax, int nStep)
{
	bool bCallback = false;

	int x = m_nCursorX;
	int y = m_nCursorY;
	int w = CFG::Menu_Slider_Width;
	int h = CFG::Menu_Slider_Height;

	int nTextH = H::Fonts->Get(EFonts::Menu).m_nTall;

	bool bHovered = IsHovered(x, y + (nTextH + CFG::Menu_Spacing_Y), w, h, &nVar, true);

	bool bAcceptsInput = [&]() -> bool
	{
		for (const auto &State : m_mapStates)
		{
			if (State.second && State.first != &nVar)
				return false;
		}

		return true;
	}();

	if (!m_bClickConsumed && bAcceptsInput)
	{
		if (H::Input->IsPressed(VK_RBUTTON))
		{
			if (bHovered)
			{
				bool bLeftSideHovered = IsHovered(x, y + (nTextH + CFG::Menu_Spacing_Y), w / 2, h, &nVar, true);

				if (bLeftSideHovered)
					nVar -= nStep;

				else nVar += nStep;
			}
		}

		if (H::Input->IsPressed(VK_LBUTTON))
		{
			if (bHovered) {
				m_bClickConsumed = true;
				m_mapStates[&nVar] = true;
			}
		}

		else
		{
			if (!H::Input->IsHeld(VK_LBUTTON))
				m_mapStates[&nVar] = false;
		}
	}

	if (m_mapStates[&nVar])
	{
		nVar = static_cast<int>(Math::RemapValClamped(
			static_cast<float>(H::Input->GetMouseX()),
			static_cast<float>(x), static_cast<float>(x + w),
			static_cast<float>(nMin), static_cast<float>(nMax)
		));
	}

	if (nVar < 0)
		nVar = nVar - (nVar - nStep / 2) % nStep - nStep / 2;

	else nVar = nVar - (nVar + nStep / 2) % nStep + nStep / 2;

	nVar = std::clamp(nVar, nMin, nMax);

	int nFillWidth = static_cast<int>(Math::RemapValClamped(
		static_cast<float>(nVar),
		static_cast<float>(nMin), static_cast<float>(nMax),
		0.0f, static_cast<float>(w)
	));

	H::Draw->String(
		H::Fonts->Get(EFonts::Menu),
		x, y,
		(bHovered || m_mapStates[&nVar]) ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive,
		POS_DEFAULT,
		szLabel
	);

	Color_t clr = CFG::Menu_Accent_Primary;
	Color_t clr_dim = { clr.r, clr.g, clr.b, 25 };

	H::Draw->Rect(x, y + (nTextH + CFG::Menu_Spacing_Y), w, h, clr_dim);
	H::Draw->GradientRect(x, y + (nTextH + CFG::Menu_Spacing_Y), nFillWidth, h, clr_dim, clr, false);
	H::Draw->OutlinedRect(x, y + (nTextH + CFG::Menu_Spacing_Y), nFillWidth, h, clr);
	H::Draw->Rect(x + (nFillWidth - 1), y + (nTextH + CFG::Menu_Spacing_Y) - 1, 2, h + 2, CFG::Menu_Text_Active);

	H::Draw->String(
		H::Fonts->Get(EFonts::Menu),
		x + (w + CFG::Menu_Spacing_X),
		y + (nTextH - 1),
		(bHovered || m_mapStates[&nVar]) ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive,
		POS_DEFAULT,
		"%d", nVar
	);

	m_nCursorY += h + nTextH + CFG::Menu_Spacing_Y + CFG::Menu_Spacing_Y + 2;

	return bCallback;
}

bool CMenu::InputKey(const char *szLabel, int &nKeyOut)
{
	auto VK2STR = [&](const short key) -> std::string
	{
		switch (key)
		{
			case VK_LBUTTON: return "LButton";
			case VK_RBUTTON: return "RButton";
			case VK_MBUTTON: return "MButton";
			case VK_XBUTTON1: return "XButton1";
			case VK_XBUTTON2: return "XButton2";
			case VK_NUMPAD0: return "NumPad0";
			case VK_NUMPAD1: return "NumPad1";
			case VK_NUMPAD2: return "NumPad2";
			case VK_NUMPAD3: return "NumPad3";
			case VK_NUMPAD4: return "NumPad4";
			case VK_NUMPAD5: return "NumPad5";
			case VK_NUMPAD6: return "NumPad6";
			case VK_NUMPAD7: return "NumPad7";
			case VK_NUMPAD8: return "NumPad8";
			case VK_NUMPAD9: return "NumPad9";
			case VK_MENU: return "Alt";
			case VK_CAPITAL: return "Caps Lock";
			case 0x0: return "None";
			default: break;
		}

		CHAR output[16] = { "\0" };

		if (const int result = GetKeyNameTextA(MapVirtualKeyW(key, MAPVK_VK_TO_VSC) << 16, output, 16))
			return output;

		return "VK2STR_FAILED";
	};

	bool bCallback = false;

	int x = m_nCursorX;
	int y = m_nCursorY;
	int w = CFG::Menu_InputKey_Width;
	int h = CFG::Menu_InputKey_Height;

	int w_with_text = [&]() -> int {
		int w_out = 0, h_out = 0;
		I::MatSystemSurface->GetTextSize(H::Fonts->Get(EFonts::Menu).m_dwFont, Utils::ConvertUtf8ToWide(szLabel).c_str(), w_out, h_out);
		return w + w_out + 1;
	}();

	bool bHovered = IsHovered(x, y, w_with_text, h, &nKeyOut);
	bool bActive = m_mapStates[&nKeyOut] || bHovered;

	if (!m_mapStates[&nKeyOut] && bHovered && H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed)
		m_mapStates[&nKeyOut] = m_bClickConsumed = true;

	m_bInKeybind = false;
	if (m_mapStates[&nKeyOut])
	{
		m_bInKeybind = true;

		for (int n = 0; n < 256; n++)
		{
			bool bMouse = (n > 0x0 && n < 0x7);
			bool bLetter = (n > L'A' - 1 && n < L'Z' + 1);
			bool bAllowed = (n == VK_LSHIFT || n == VK_RSHIFT || n == VK_SHIFT || n == VK_ESCAPE || n == VK_INSERT || n == VK_F3 || n == VK_MENU || n == VK_CAPITAL || n == VK_SPACE || n == VK_CONTROL);
			bool bNumPad = n > (VK_NUMPAD0 - 1) && n < (VK_NUMPAD9)+1;

			if (bMouse || bLetter || bAllowed || bNumPad)
			{
				if (H::Input->IsPressed(n))
				{
					if (n == VK_INSERT || n == VK_F3) {
						m_mapStates[&nKeyOut] = false;
						break;
					}

					else if (n == VK_ESCAPE) {
						nKeyOut = 0x0;
						m_mapStates[&nKeyOut] = false;
						break;
					}

					else
					{
						if (n == VK_LBUTTON)
						{
							if (m_bClickConsumed)
								continue;

							m_bClickConsumed = true;
						}

						nKeyOut = n;
						m_mapStates[&nKeyOut] = false;
					}

					break;
				}
			}
		}
	}

	Color_t clr = CFG::Menu_Accent_Primary;

	if (bActive)
		H::Draw->Rect(x, y, w, h, { clr.r, clr.g, clr.b, 25 });

	H::Draw->OutlinedRect(x, y, w, h, clr);

	if (m_mapStates[&nKeyOut])
	{
		H::Draw->String(
			H::Fonts->Get(EFonts::Menu),
			x + (w / 2),
			y + (h / 2),
			bActive ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive,
			POS_CENTERXY,
			"...");
	}

	else
	{
		H::Draw->String(
			H::Fonts->Get(EFonts::Menu),
			x + (w / 2),
			y + (h / 2),
			bActive ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive,
			POS_CENTERXY,
			VK2STR(nKeyOut).c_str());
	}

	H::Draw->String(
		H::Fonts->Get(EFonts::Menu),
		x + (w + CFG::Menu_Spacing_X),
		y + (h / 2),
		bActive ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive,
		POS_CENTERY,
		szLabel);

	m_nCursorY += h + CFG::Menu_Spacing_Y;

	return bCallback;
}

bool CMenu::Button(const char *szLabel, bool bActive, int nCustomWidth)
{
	bool bCallback = false;

	int x = m_nCursorX;
	int y = m_nCursorY;
	int w = 0;
	int h = 0;

	I::MatSystemSurface->GetTextSize(H::Fonts->Get(EFonts::Menu).m_dwFont, Utils::ConvertUtf8ToWide(szLabel).c_str(), w, h);

	if (!w || !h)
		return false;

	if (nCustomWidth > 0)
		w = nCustomWidth;

	w += CFG::Menu_Spacing_X * 2;
	h += CFG::Menu_Spacing_Y - 1;

	bool bHovered = IsHovered(x, y, w, h, nullptr);
	
	// Animation state tracking
	std::string buttonId = std::string("btn_") + szLabel + "_" + std::to_string(x) + "_" + std::to_string(y);
	
	// Hover animation
	if (m_buttonHoverStates.find(buttonId) == m_buttonHoverStates.end())
		m_buttonHoverStates[buttonId] = 0.0f;
	
	float targetHover = (bHovered || bActive) ? 1.0f : 0.0f;
	float& currentHover = m_buttonHoverStates[buttonId];
	
	// Smooth interpolation
	float hoverSpeed = 8.0f;
	currentHover += (targetHover - currentHover) * hoverSpeed * I::GlobalVars->frametime;
	currentHover = std::max(0.0f, std::min(1.0f, currentHover));
	
	// Press animation
	if (m_buttonPressStates.find(buttonId) == m_buttonPressStates.end())
		m_buttonPressStates[buttonId] = 0.0f;
	
	float& pressState = m_buttonPressStates[buttonId];
	
	if (bHovered && H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed)
	{
		bCallback = m_bClickConsumed = true;
		pressState = 1.0f;
	}
	
	// Decay press state
	pressState *= 0.85f;
	if (pressState < 0.01f) pressState = 0.0f;
	
	// Calculate animated values
	float hoverAlpha = 50.0f + currentHover * 30.0f;
	float pressScale = 1.0f - pressState * 0.05f;
	float glowAlpha = currentHover * 40.0f;
	
	// Apply press scale
	int scaledW = static_cast<int>(w * pressScale);
	int scaledH = static_cast<int>(h * pressScale);
	int offsetX = (w - scaledW) / 2;
	int offsetY = (h - scaledH) / 2;
	
	Color_t clr = CFG::Menu_Accent_Primary;
	
	// Glow effect (outer)
	if (glowAlpha > 1.0f)
	{
		Color_t glowColor = { clr.r, clr.g, clr.b, static_cast<byte>(glowAlpha) };
		H::Draw->Rect(x + offsetX - 1, y + offsetY - 1, scaledW + 2, scaledH + 2, glowColor);
	}
	
	// Gradient background
	Color_t clr_dim = { clr.r, clr.g, clr.b, static_cast<byte>(hoverAlpha) };
	Color_t clr_bright = { clr.r, clr.g, clr.b, static_cast<byte>(hoverAlpha * 0.5f) };
	H::Draw->GradientRect(x + offsetX, y + offsetY, scaledW, scaledH, clr_bright, clr_dim, false);
	
	// Border
	H::Draw->OutlinedRect(x + offsetX, y + offsetY, scaledW, scaledH, clr);
	
	// Text with hover color transition
	Color_t textColor = CFG::Menu_Text_Inactive;
	if (currentHover > 0.01f)
	{
		Color_t activeColor = CFG::Menu_Text_Active;
		textColor.r = static_cast<byte>(textColor.r + (activeColor.r - textColor.r) * currentHover);
		textColor.g = static_cast<byte>(textColor.g + (activeColor.g - textColor.g) * currentHover);
		textColor.b = static_cast<byte>(textColor.b + (activeColor.b - textColor.b) * currentHover);
	}
	
	H::Draw->String(
		H::Fonts->Get(EFonts::Menu),
		x + (w / 2), y + (h / 2) - 1,
		textColor,
		POS_CENTERXY, szLabel
	);

	m_nCursorY += h + CFG::Menu_Spacing_Y;
	m_nLastButtonW = w;

	return bCallback;
}

bool CMenu::playerListButton(const wchar_t *label, int nCustomWidth, Color_t clr, bool center_txt)
{
	bool bCallback = false;

	int x = m_nCursorX;
	int y = m_nCursorY;
	int w = nCustomWidth;
	int h = H::Fonts->Get(EFonts::Menu).m_nTall;

	w += CFG::Menu_Spacing_X * 2;
	h += CFG::Menu_Spacing_Y - 1;

	bool bHovered = IsHovered(x, y, w, h, nullptr);

	if (bHovered && H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed)
		bCallback = m_bClickConsumed = true;

	Color_t clrA = CFG::Menu_Accent_Primary;
	Color_t clr_dim = { clrA.r, clrA.g, clrA.b, bHovered ? static_cast<byte>(50) : static_cast<byte>(0) };

	H::Draw->Rect(x, y, w, h, clr_dim);
	H::Draw->OutlinedRect(x, y, w, h, clrA);

	H::Draw->StartClipping(x, y, w, h);

	if (center_txt)
	{
		H::Draw->String
		(
			H::Fonts->Get(EFonts::Menu),
			x + (w / 2), y + (h / 2) - 1,
			clr,
			POS_CENTERXY, label
		);
	}

	else
	{
		H::Draw->String
		(
			H::Fonts->Get(EFonts::Menu),
			x + CFG::Menu_Spacing_X, y + (h / 2) - 1,
			clr,
			POS_CENTERY, label
		);
	}

	H::Draw->EndClipping();

	m_nCursorY += h + CFG::Menu_Spacing_Y;
	m_nLastButtonW = w;

	return bCallback;
}

bool CMenu::InputText(const char *szLabel, const char *szLabel2, std::string &strOutput)
{
	bool bCallback = false;

	int x = m_nCursorX;
	int y = m_nCursorY;
	int w = 0;
	int h = 0;

	I::MatSystemSurface->GetTextSize(H::Fonts->Get(EFonts::Menu).m_dwFont, Utils::ConvertUtf8ToWide(szLabel).c_str(), w, h);

	if (!w || !h)
		return false;

	w += CFG::Menu_Spacing_X * 2;
	h += CFG::Menu_Spacing_Y - 1;

	bool bHovered = IsHovered(x, y, w, h, nullptr);

	Color_t clr = CFG::Menu_Accent_Primary;
	Color_t clr_dim = { clr.r, clr.g, clr.b, bHovered ? static_cast<byte>(50) : static_cast<byte>(0) };

	if (!m_mapStates[&strOutput])
	{
		H::Draw->Rect(x, y, w, h, clr_dim);
		H::Draw->OutlinedRect(x, y, w, h, clr);
		H::Draw->String(
			H::Fonts->Get(EFonts::Menu),
			x + (w / 2), y + (h / 2) - 1,
			bHovered ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive,
			POS_CENTERXY, szLabel
		);
	}

	bool bCanOpen = [&]() -> bool
	{
		for (const auto &State : m_mapStates)
		{
			if (State.second && State.first != &strOutput)
				return false;
		}

		return true;
	}();

	// Use a map to store temp strings per input field
	static std::map<void*, std::string> mapTempStrings;
	std::string& strTemp = mapTempStrings[&strOutput];

	if (bHovered && H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed && bCanOpen) {
		m_bClickConsumed = m_mapStates[&strOutput] = true;
		strTemp = strOutput; // Load existing text for editing
	}

	if (H::Input->IsPressed(VK_ESCAPE) || H::Input->IsPressed(VK_INSERT) || H::Input->IsPressed(VK_F3)) {
		m_mapStates[&strOutput] = false;
		strTemp.clear();
	}

	m_bWantTextInput = false;
	if (m_mapStates[&strOutput])
	{
		m_bWantTextInput = true;

		y += CFG::Menu_Spacing_Y;

		int w = CFG::Menu_InputText_Width;
		int h = CFG::Menu_InputText_Height;

		H::LateRender->Rect(x, y, w, h, CFG::Menu_Background);
		H::LateRender->OutlinedRect(x, y, w, h, clr);
		H::LateRender->String(
			H::Fonts->Get(EFonts::Menu),
			x + CFG::Menu_Spacing_X,
			y + (CFG::Menu_Spacing_Y * 3),
			CFG::Menu_Text_Inactive,
			POS_CENTERY, szLabel2, {}
		);

		// Allow up to 128 characters
		if (strTemp.length() < 128)
		{
			bool bShift = H::Input->IsHeld(VK_SHIFT);
			bool bCaps = (GetKeyState(VK_CAPITAL) & 1) != 0;
			
			// Letters A-Z
			for (int n = 'A'; n <= 'Z'; n++)
			{
				if (H::Input->IsPressedAndHeld(n))
				{
					char ch = (bCaps != bShift) ? static_cast<char>(n) : static_cast<char>(std::tolower(n));
					strTemp += ch;
				}
			}
			
			// Numbers 0-9 and their shift variants
			const char* numShift = ")!@#$%^&*(";
			for (int n = '0'; n <= '9'; n++)
			{
				if (H::Input->IsPressedAndHeld(n))
				{
					char ch = bShift ? numShift[n - '0'] : static_cast<char>(n);
					strTemp += ch;
				}
			}
			
			// Space
			if (H::Input->IsPressedAndHeld(VK_SPACE))
				strTemp += ' ';
			
			// Special characters
			if (H::Input->IsPressedAndHeld(VK_OEM_PERIOD)) // . >
				strTemp += bShift ? '>' : '.';
			if (H::Input->IsPressedAndHeld(VK_OEM_COMMA)) // , <
				strTemp += bShift ? '<' : ',';
			if (H::Input->IsPressedAndHeld(VK_OEM_2)) // / ?
				strTemp += bShift ? '?' : '/';
			if (H::Input->IsPressedAndHeld(VK_OEM_1)) // ; :
				strTemp += bShift ? ':' : ';';
			if (H::Input->IsPressedAndHeld(VK_OEM_7)) // ' "
				strTemp += bShift ? '"' : '\'';
			if (H::Input->IsPressedAndHeld(VK_OEM_4)) // [ {
				strTemp += bShift ? '{' : '[';
			if (H::Input->IsPressedAndHeld(VK_OEM_6)) // ] }
				strTemp += bShift ? '}' : ']';
			if (H::Input->IsPressedAndHeld(VK_OEM_5)) // \ |
				strTemp += bShift ? '|' : '\\';
			if (H::Input->IsPressedAndHeld(VK_OEM_MINUS)) // - _
				strTemp += bShift ? '_' : '-';
			if (H::Input->IsPressedAndHeld(VK_OEM_PLUS)) // = +
				strTemp += bShift ? '+' : '=';
			if (H::Input->IsPressedAndHeld(VK_OEM_3)) // ` ~
				strTemp += bShift ? '~' : '`';
		}

		if (strTemp.length() > 0)
		{
			if (H::Input->IsPressedAndHeld(VK_BACK))
				strTemp.erase(strTemp.end() - 1);
		}

		if (H::Input->IsPressed(VK_RETURN)) {
			bCallback = true;
			strOutput = strTemp;
			m_mapStates[&strOutput] = false;
			strTemp.clear();
		}

		// Display text with overflow handling
		// Use a separate static map for display strings to handle truncation
		static std::map<void*, std::string> mapDisplayStrings;
		std::string& strDisplay = mapDisplayStrings[&strOutput];
		
		int maxDisplayWidth = w - (CFG::Menu_Spacing_X * 2);
		strDisplay = strTemp;
		
		// If text is too wide, show only the end portion (so user sees what they're typing)
		int textW = 0, textH = 0;
		I::MatSystemSurface->GetTextSize(H::Fonts->Get(EFonts::Menu).m_dwFont, Utils::ConvertUtf8ToWide(strDisplay).c_str(), textW, textH);
		
		while (textW > maxDisplayWidth && strDisplay.length() > 1)
		{
			strDisplay = strDisplay.substr(1);
			I::MatSystemSurface->GetTextSize(H::Fonts->Get(EFonts::Menu).m_dwFont, Utils::ConvertUtf8ToWide(strDisplay).c_str(), textW, textH);
		}

		// Position near bottom of input box (h=30), below the label
		int textY = y + h - H::Fonts->Get(EFonts::Menu).m_nTall - 2;
		H::LateRender->String(
			H::Fonts->Get(EFonts::Menu),
			x + CFG::Menu_Spacing_X,
			textY,
			CFG::Menu_Text_Active,
			POS_DEFAULT, strDisplay.c_str(), {}
		);
	}

	m_nCursorY += h + CFG::Menu_Spacing_Y;

	m_nLastButtonW = w;

	return bCallback;
}

bool CMenu::SelectSingle(const char *szLabel, int &nVar, const std::vector<std::pair<const char *, int>> &vecSelects)
{
	bool bCallback = false;

	int x = m_nCursorX;
	int y = m_nCursorY;
	int w = CFG::Menu_Select_Width;
	int h = CFG::Menu_Select_Height;

	int nTextH = H::Fonts->Get(EFonts::Menu).m_nTall;

	bool bHovered = IsHovered(x, y + (nTextH + CFG::Menu_Spacing_Y), w, h, &nVar);
	bool bActive = bHovered || m_mapStates[&nVar];

	if (!m_bClickConsumed && bHovered && H::Input->IsPressed(VK_LBUTTON)) {
		m_mapStates[&nVar] = !m_mapStates[&nVar];
		m_bClickConsumed = true;
	}

	auto pszCurSelected = [&]() -> const char *
	{
		for (const auto &Select : vecSelects)
		{
			if (Select.second == nVar)
				return Select.first;
		}

		return "Unknown";
	}();

	Color_t clr = CFG::Menu_Accent_Primary;
	Color_t bg{ CFG::Menu_Background };
	Color_t clr_dim = { bg.r, bg.g, bg.b, 253 };

	H::Draw->Rect(x, y + (nTextH + CFG::Menu_Spacing_Y), w, h, clr_dim);

	if (!m_mapStates[&nVar])
		H::Draw->OutlinedRect(x, y + (nTextH + CFG::Menu_Spacing_Y), w, h, clr);

	H::Draw->String(
		H::Fonts->Get(EFonts::Menu),
		x + (w / 2), y + (h / 2) + (nTextH + CFG::Menu_Spacing_Y) - 1,
		bActive ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive,
		POS_CENTERXY,
		pszCurSelected
	);

	H::Draw->String(
		H::Fonts->Get(EFonts::Menu),
		x, y,
		(bHovered || m_mapStates[&nVar]) ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive,
		POS_DEFAULT,
		szLabel
	);

	if (m_mapStates[&nVar])
	{
		bool bSelectRegionHovered = IsHovered(x, y + (nTextH + CFG::Menu_Spacing_Y), w, h * static_cast<int>(vecSelects.size()), &nVar);

		if (H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed && !bSelectRegionHovered) {
			m_bClickConsumed = true;
			m_mapStates[&nVar] = false;
		}
	}

	if (m_mapStates[&nVar])
	{
		H::LateRender->OutlinedRect(x, y + (nTextH + CFG::Menu_Spacing_Y), w, h * static_cast<int>(vecSelects.size()), clr);

		int real_n{ 0 };

		for (int n = 0; n < static_cast<int>(vecSelects.size()); n++)
		{
			const auto &Select = vecSelects[n];

			if (Select.second == nVar)
			{
				continue;
			}

			int nSelectY = (y + h + (nTextH + CFG::Menu_Spacing_Y)) + (h * real_n);
			bool bSelectHovered = IsHovered(x, nSelectY, w, h, &nVar);

			H::LateRender->Rect(x, nSelectY, w, h, clr_dim);

			H::LateRender->String(
				H::Fonts->Get(EFonts::Menu),
				x + (w / 2), nSelectY,
				bSelectHovered ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive,
				POS_CENTERX,
				Select.first, {}
			);

			if (H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed && bSelectHovered) {
				nVar = Select.second;
				m_mapStates[&nVar] = false;
				m_bClickConsumed = true;
				break;
			}

			real_n++;
		}
	}

	m_nCursorY += h + nTextH + CFG::Menu_Spacing_Y + CFG::Menu_Spacing_Y;

	return bCallback;
}

bool CMenu::SelectMulti(const char *szLabel, std::vector<std::pair<const char *, bool &>> &vecSelects)
{
	bool bCallback = false;

	int x = m_nCursorX;
	int y = m_nCursorY;
	int w = CFG::Menu_Select_Width;
	int h = CFG::Menu_Select_Height;

	int nTextH = H::Fonts->Get(EFonts::Menu).m_nTall;

	bool bHovered = IsHovered(x, y + (nTextH + CFG::Menu_Spacing_Y), w, h, &vecSelects);
	bool bActive = bHovered || m_mapStates[&vecSelects];

	if (!m_bClickConsumed && bHovered && H::Input->IsPressed(VK_LBUTTON)) {
		m_mapStates[&vecSelects] = !m_mapStates[&vecSelects];
		m_bClickConsumed = true;
	}

	Color_t clr = CFG::Menu_Accent_Primary;
	Color_t bg{ CFG::Menu_Background };
	Color_t clr_dim = { bg.r, bg.g, bg.b, 253 };

	H::Draw->Rect(x, y + (nTextH + CFG::Menu_Spacing_Y), w, h, clr_dim);

	if (!m_mapStates[&vecSelects])
		H::Draw->OutlinedRect(x, y + (nTextH + CFG::Menu_Spacing_Y), w, h, clr);

	H::Draw->String(
		H::Fonts->Get(EFonts::Menu),
		x, y,
		(bHovered || m_mapStates[&vecSelects]) ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive,
		POS_DEFAULT,
		szLabel
	);

	std::string strSelected = {};

	for (const auto &Select : vecSelects)
	{
		if (Select.second)
		{
			if (!strSelected.empty())
				strSelected += ", ";

			strSelected += Select.first;
		}
	}

	I::MatSystemSurface->DisableClipping(false);
	I::MatSystemSurface->SetClippingRect(x, y + (nTextH + CFG::Menu_Spacing_Y), x + (w - CFG::Menu_Spacing_X), y + (nTextH + CFG::Menu_Spacing_Y) + h);

	H::Draw->String(
		H::Fonts->Get(EFonts::Menu),
		x + CFG::Menu_Spacing_X, y + (nTextH + CFG::Menu_Spacing_Y) + (h / 2) - 1,
		bActive ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive,
		POS_CENTERY,
		strSelected.empty() ? "None" : strSelected.c_str()
	);

	I::MatSystemSurface->DisableClipping(true);

	if (m_mapStates[&vecSelects])
	{
		bool bSelectRegionHovered = IsHovered(x, y + (nTextH + CFG::Menu_Spacing_Y), w, h * static_cast<int>(vecSelects.size() + 1), &vecSelects);

		if (H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed && !bSelectRegionHovered) {
			m_bClickConsumed = true;
			m_mapStates[&vecSelects] = false;
		}
	}

	if (m_mapStates[&vecSelects])
	{
		H::LateRender->OutlinedRect(x, y + (nTextH + CFG::Menu_Spacing_Y), w, h * static_cast<int>(vecSelects.size() + 1), clr);

		for (int n = 0; n < static_cast<int>(vecSelects.size()); n++)
		{
			const auto &Select = vecSelects[n];

			int nSelectY = (y + (nTextH + CFG::Menu_Spacing_Y) + h) + (h * n);
			bool bSelectHovered = IsHovered(x, nSelectY, w, h, &vecSelects);

			H::LateRender->Rect(x, nSelectY, w, h, clr_dim);

			static int nYesWidth = []() -> int {
				int w = 0, h = 0;
				I::MatSystemSurface->GetTextSize(H::Fonts->Get(EFonts::Menu).m_dwFont, Utils::ConvertUtf8ToWide("Yess").c_str(), w, h);
				return w;
			}();

			H::LateRender->String(
				H::Fonts->Get(EFonts::Menu),
				x + CFG::Menu_Spacing_X, nSelectY,
				bSelectHovered ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive,
				POS_DEFAULT,
				Select.first, { x, nSelectY, w - nYesWidth, nTextH }
			);

			H::LateRender->String(
				H::Fonts->Get(EFonts::Menu),
				x + w - CFG::Menu_Spacing_X, nSelectY,
				bSelectHovered ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive,
				POS_LEFT,
				Select.second ? "Yes" : "No", {}
			);

			if (H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed && bSelectHovered) {
				Select.second = !Select.second;
				m_bClickConsumed = true;
			}
		}
	}

	m_nCursorY += h + nTextH + CFG::Menu_Spacing_Y + CFG::Menu_Spacing_Y;

	return bCallback;
}

bool CMenu::ColorPicker(const char *szLabel, Color_t &colVar)
{
	bool bCallback = false;

	int x = m_nCursorX;
	int y = m_nCursorY;
	int w = CFG::Menu_ColorPicker_Preview_Width;
	int h = CFG::Menu_ColorPicker_Preview_Height;

	int w_with_text = [&]() -> int {
		int w_out = 0, h_out = 0;
		I::MatSystemSurface->GetTextSize(H::Fonts->Get(EFonts::Menu).m_dwFont, Utils::ConvertUtf8ToWide(szLabel).c_str(), w_out, h_out);
		return w + w_out + 1;
	}();

	bool bHovered = IsHovered(x, y, w_with_text, h, &colVar);

	if (bHovered && H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed) {
		m_mapStates[&colVar] = !m_mapStates[&colVar];
		m_bClickConsumed = true;
	}

	if (H::Input->IsPressed(VK_ESCAPE) || H::Input->IsPressed(VK_INSERT) || H::Input->IsPressed(VK_F3))
		m_mapStates[&colVar] = false;

	H::Draw->Rect(x, y, w, h, colVar);
	H::Draw->OutlinedRect(x, y, w, h, CFG::Menu_Accent_Primary);
	H::Draw->String(
		H::Fonts->Get(EFonts::Menu),
		x + w + CFG::Menu_Spacing_X,
		y + (h / 2),
		(bHovered || m_mapStates[&colVar]) ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive,
		POS_CENTERY, szLabel
	);

	if (m_mapStates[&colVar])
	{
		int y = m_nCursorY + h + CFG::Menu_Spacing_Y;
		int w = 200;
		int h = 200;

		bool bHovered = IsHovered(x, y, w, h, &colVar);

		if (H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed && !bHovered)
			m_mapStates[&colVar] = false;

		if (H::Input->IsHeld(VK_LBUTTON) && bHovered)
		{
			int x_rel = (H::Input->GetMouseX() - x);
			int y_rel = (H::Input->GetMouseY() - y);

			colVar = *reinterpret_cast<Color_t *>(m_pGradient.get() + (x_rel + y_rel * 200));
		}

		H::LateRender->Texture(m_nColorPickerTextureId, x, y, w, h);
		H::LateRender->OutlinedRect(x, y, w, h, CFG::Menu_Accent_Primary);
	}

	m_nCursorY += h + CFG::Menu_Spacing_Y;

	return bCallback;
}

void CMenu::MainWindow()
{
	Drag(
		CFG::Menu_Pos_X,
		CFG::Menu_Pos_Y,
		CFG::Menu_Width,
		CFG::Menu_Drag_Bar_Height,
		0
	);

	m_bMenuWindowHovered = IsHoveredSimple(
		CFG::Menu_Pos_X,
		CFG::Menu_Pos_Y,
		CFG::Menu_Width,
		CFG::Menu_Height
	);
	
	// Apply menu open animation (scale and fade)
	float scale = 0.9f + m_flMenuOpenProgress * 0.1f;
	byte alpha = static_cast<byte>(m_flMenuOpenProgress * 255);
	
	// Calculate scaled dimensions
	int scaledW = static_cast<int>(CFG::Menu_Width * scale);
	int scaledH = static_cast<int>(CFG::Menu_Height * scale);
	int offsetX = (CFG::Menu_Width - scaledW) / 2;
	int offsetY = (CFG::Menu_Height - scaledH) / 2;
	
	// Background with fade
	Color_t bgColor = CFG::Menu_Background;
	bgColor.a = static_cast<byte>((bgColor.a / 255.0f) * alpha);
	H::Draw->Rect(
		CFG::Menu_Pos_X + offsetX,
		CFG::Menu_Pos_Y + offsetY,
		scaledW,
		scaledH,
		bgColor
	);
	
	// Border with accent primary color
	Color_t borderColor = CFG::Menu_Accent_Primary;
	borderColor.a = alpha;
	
	H::Draw->OutlinedRect(
		CFG::Menu_Pos_X + offsetX,
		CFG::Menu_Pos_Y + offsetY,
		scaledW,
		scaledH,
		borderColor
	);
	
	// Drag bar line
	int dragBarY = static_cast<int>((CFG::Menu_Drag_Bar_Height - 1) * scale);
	H::Draw->Line(
		CFG::Menu_Pos_X + offsetX,
		CFG::Menu_Pos_Y + offsetY + dragBarY,
		CFG::Menu_Pos_X + offsetX + scaledW - 1,
		CFG::Menu_Pos_Y + offsetY + dragBarY,
		borderColor
	);
	
	// Only render content if menu is mostly open
	if (m_flMenuOpenProgress < 0.3f)
		return;

	m_nCursorX = CFG::Menu_Pos_X + CFG::Menu_Spacing_X;
	m_nCursorY = CFG::Menu_Pos_Y + CFG::Menu_Drag_Bar_Height + CFG::Menu_Spacing_Y;

	enum class EMainTabs { AIM, VISUALS, EXPLOITS, MISC, PLAYERS, CONFIGS, PLAYER_DETAILS };
	static EMainTabs MainTab = EMainTabs::AIM;
	static int nSelectedPlayerIndex = -1; // For player details view
	
	// Enhanced tab box with detailed pixel art icons
	auto DrawTabBox = [&](const char* label, int type, bool active) -> bool {
		static float animTime = 0.0f;
		animTime += I::GlobalVars->frametime * 0.8f; // Slower animation speed
		
		int x = m_nCursorX;
		int y = m_nCursorY;
		int boxW = CFG::Menu_Tab_Button_Width;
		int boxH = 50; // Taller box for icon + text
		
		bool bHovered = IsHovered(x, y, boxW, boxH, nullptr);
		bool bCallback = false;
		
		if (bHovered && H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed)
			bCallback = m_bClickConsumed = true;
		
		// Animation states
		std::string tabId = std::string("tab_") + label;
		if (m_buttonHoverStates.find(tabId) == m_buttonHoverStates.end())
			m_buttonHoverStates[tabId] = 0.0f;
		
		float& hoverState = m_buttonHoverStates[tabId];
		float targetHover = (bHovered || active) ? 1.0f : 0.0f;
		hoverState += (targetHover - hoverState) * 8.0f * I::GlobalVars->frametime;
		
		// Colors
		Color_t accentColor = CFG::Menu_Accent_Primary;
		Color_t bgColor = { accentColor.r, accentColor.g, accentColor.b, static_cast<byte>(20 + hoverState * 40) };
		Color_t glowColor = { accentColor.r, accentColor.g, accentColor.b, static_cast<byte>(hoverState * 50) };
		Color_t iconColor = active ? accentColor : CFG::Menu_Text_Inactive;
		
		// Glow effect
		if (hoverState > 0.01f) {
			H::Draw->Rect(x - 1, y - 1, boxW + 2, boxH + 2, glowColor);
		}
		
		// Box background
		H::Draw->GradientRect(x, y, boxW, boxH, bgColor, { bgColor.r, bgColor.g, bgColor.b, static_cast<byte>(bgColor.a * 0.5f) }, false);
		H::Draw->OutlinedRect(x, y, boxW, boxH, accentColor);
		
		// Icon area (top part of box) - larger canvas for more detailed pixel art
		int iconSize = 36;
		int iconX = x + (boxW - iconSize) / 2;
		int iconY = y + 4;
		int centerX = iconX + iconSize / 2;
		int centerY = iconY + iconSize / 2;
		
		int frame = static_cast<int>(animTime) % 2;
		float pulse = active ? (1.0f + std::sin(animTime * 1.5f) * 0.12f) : 1.0f; // Slower, gentler pulse
		
		// Draw detailed pixel art icons
		switch(type) {
			case 0: // Aim - Colored Sniper Scope
			{
				// Define colors
				Color_t scopeGray = { 120, 120, 130, 255 };
				Color_t scopeDark = { 60, 60, 70, 255 };
				Color_t targetRed = { 220, 50, 50, 255 };
				Color_t centerDot = { 255, 80, 80, 255 };
				Color_t glintWhite = { 255, 255, 255, 255 };
				
				if (!active) {
					scopeGray.a = 200;
					targetRed.a = 200;
					centerDot.a = 200;
				}
				
				// Outer scope circles (gray/dark gray)
				H::Draw->OutlinedCircle(centerX, centerY, static_cast<int>(11 * pulse), 20, scopeDark);
				H::Draw->OutlinedCircle(centerX, centerY, static_cast<int>(9 * pulse), 20, scopeGray);
				H::Draw->OutlinedCircle(centerX, centerY, static_cast<int>(7 * pulse), 16, scopeGray);
				
				// Crosshair lines (red targeting lines)
				H::Draw->Rect(centerX - 12, centerY - 1, 3, 2, targetRed);
				H::Draw->Rect(centerX + 10, centerY - 1, 3, 2, targetRed);
				H::Draw->Rect(centerX - 1, centerY - 12, 2, 3, targetRed);
				H::Draw->Rect(centerX - 1, centerY + 10, 2, 3, targetRed);
				
				// Center dot (bright red, blinks) - perfectly centered
				if (frame == 0 || active) {
					H::Draw->Rect(centerX - 1, centerY - 1, 2, 2, centerDot);
				}
				
				// Mil-dots (range markers - gray)
				for (int i = 1; i <= 3; i++) {
					H::Draw->Rect(centerX - 1, centerY - 3 * i - 4, 2, 2, scopeGray);
					H::Draw->Rect(centerX - 1, centerY + 3 * i + 2, 2, 2, scopeGray);
					H::Draw->Rect(centerX - 3 * i - 4, centerY - 1, 2, 2, scopeGray);
					H::Draw->Rect(centerX + 3 * i + 2, centerY - 1, 2, 2, scopeGray);
				}
				

				break;
			}
			case 1: // Visuals - Color-Changing Blinking Eye (Almond Shape)
			{
				// Define base colors
				Color_t eyeWhite = { 240, 240, 250, 255 };
				Color_t eyeOutline = { 80, 80, 100, 255 };
				Color_t pupilBlack = { 20, 20, 30, 255 };
				Color_t lashBrown = { 60, 50, 40, 255 };
				
				// 6 different iris colors that cycle with each blink
				Color_t irisColors[6] = {
					{ 100, 150, 220, 255 }, // Blue
					{ 100, 200, 120, 255 }, // Green
					{ 180, 100, 200, 255 }, // Purple
					{ 220, 150, 100, 255 }, // Amber/Orange
					{ 200, 100, 120, 255 }, // Red/Pink
					{ 100, 180, 180, 255 }  // Cyan/Teal
				};
				
				// Long animation cycle: 6 blinks × 4.3s = 25.8 seconds total
				float fullCycle = active ? std::fmod(animTime * 0.25f, 25.8f) : 0.0f;
				
				// Determine which blink we're on (0-5)
				int blinkNumber = static_cast<int>(fullCycle / 4.3f);
				float blinkCycle = std::fmod(fullCycle, 4.3f);
				
				int eyeCenterX = centerX;
				int eyeCenterY = iconY + 16;
				
				// Calculate eye openness (0 = closed, 1 = fully open)
				float eyeOpenness = 1.0f;
				float colorTransition = 1.0f;
				
				if (active) {
					if (blinkCycle >= 2.0f && blinkCycle < 3.0f) {
						float closeProgress = (blinkCycle - 2.0f) / 1.0f;
						eyeOpenness = 1.0f - closeProgress;
						colorTransition = closeProgress * 0.5f;
					}
					else if (blinkCycle >= 3.0f && blinkCycle < 3.1f) {
						eyeOpenness = 0.0f;
						colorTransition = 0.5f;
					}
					else if (blinkCycle >= 3.1f && blinkCycle < 4.1f) {
						float openProgress = (blinkCycle - 3.1f) / 1.0f;
						eyeOpenness = openProgress;
						colorTransition = 0.5f + (openProgress * 0.5f);
					}
					else if (blinkCycle >= 0.0f && blinkCycle < 2.0f) {
						eyeOpenness = 1.0f;
						colorTransition = 0.0f;
					}
					else {
						eyeOpenness = 1.0f;
						colorTransition = 1.0f;
					}
				}
				
				// Interpolate iris color
				int currentColorIndex = blinkNumber % 6;
				int nextColorIndex = (blinkNumber + 1) % 6;
				Color_t currentColor = irisColors[currentColorIndex];
				Color_t nextColor = irisColors[nextColorIndex];
				
				Color_t irisColor;
				irisColor.r = static_cast<byte>(currentColor.r + (nextColor.r - currentColor.r) * colorTransition);
				irisColor.g = static_cast<byte>(currentColor.g + (nextColor.g - currentColor.g) * colorTransition);
				irisColor.b = static_cast<byte>(currentColor.b + (nextColor.b - currentColor.b) * colorTransition);
				irisColor.a = 255;
				
				if (!active) {
					eyeWhite.a = 200;
					irisColor.a = 200;
					eyeOutline.a = 200;
				}
				
				// Draw almond-shaped eye using pixel rows
				if (eyeOpenness > 0.02f) {
					// Almond eye shape - widths for each row from center outward
					// Full eye is 11 rows tall (5 above center, 5 below, 1 center)
					// Use even widths to ensure symmetric centering
					int almondWidths[11] = { 4, 10, 16, 20, 22, 24, 22, 20, 16, 10, 4 };
					
					int maxHeight = 11;
					int visibleHeight = static_cast<int>(maxHeight * eyeOpenness);
					if (visibleHeight < 1) visibleHeight = 1;
					
					// Calculate which rows to draw based on openness (from center outward)
					int halfVisible = visibleHeight / 2;
					int startRow = 5 - halfVisible;
					int endRow = 5 + halfVisible;
					if (visibleHeight % 2 == 0 && visibleHeight > 1) startRow++;
					
					// Draw almond eye white fill - use exact positions to avoid stray pixels
					for (int row = startRow; row <= endRow && row < 11; row++) {
						if (row < 0) continue;
						int rowWidth = almondWidths[row];
						int rowY = eyeCenterY - 5 + row;
						// Center properly by using integer division that rounds down
						int halfWidth = rowWidth / 2;
						int rowX = eyeCenterX - halfWidth;
						// Adjust width to not exceed outline bounds
						H::Draw->Rect(rowX, rowY, rowWidth - 1, 1, eyeWhite);
					}
					
					// Draw almond outline (top and bottom curves)
					// Top curve
					if (startRow <= 0) H::Draw->Line(eyeCenterX - 2, eyeCenterY - 5, eyeCenterX + 1, eyeCenterY - 5, eyeOutline);
					if (startRow <= 1) H::Draw->Line(eyeCenterX - 5, eyeCenterY - 4, eyeCenterX + 4, eyeCenterY - 4, eyeOutline);
					if (startRow <= 2) {
						H::Draw->Rect(eyeCenterX - 8, eyeCenterY - 3, 1, 1, eyeOutline);
						H::Draw->Rect(eyeCenterX + 7, eyeCenterY - 3, 1, 1, eyeOutline);
					}
					if (startRow <= 3) {
						H::Draw->Rect(eyeCenterX - 10, eyeCenterY - 2, 1, 1, eyeOutline);
						H::Draw->Rect(eyeCenterX + 9, eyeCenterY - 2, 1, 1, eyeOutline);
					}
					if (startRow <= 4) {
						H::Draw->Rect(eyeCenterX - 11, eyeCenterY - 1, 1, 1, eyeOutline);
						H::Draw->Rect(eyeCenterX + 10, eyeCenterY - 1, 1, 1, eyeOutline);
					}
					// Center row corners (pointed almond tips)
					H::Draw->Rect(eyeCenterX - 12, eyeCenterY, 1, 1, eyeOutline);
					H::Draw->Rect(eyeCenterX + 11, eyeCenterY, 1, 1, eyeOutline);
					// Bottom curve
					if (endRow >= 6) {
						H::Draw->Rect(eyeCenterX - 11, eyeCenterY + 1, 1, 1, eyeOutline);
						H::Draw->Rect(eyeCenterX + 10, eyeCenterY + 1, 1, 1, eyeOutline);
					}
					if (endRow >= 7) {
						H::Draw->Rect(eyeCenterX - 10, eyeCenterY + 2, 1, 1, eyeOutline);
						H::Draw->Rect(eyeCenterX + 9, eyeCenterY + 2, 1, 1, eyeOutline);
					}
					if (endRow >= 8) {
						H::Draw->Rect(eyeCenterX - 8, eyeCenterY + 3, 1, 1, eyeOutline);
						H::Draw->Rect(eyeCenterX + 7, eyeCenterY + 3, 1, 1, eyeOutline);
					}
					if (endRow >= 9) H::Draw->Line(eyeCenterX - 5, eyeCenterY + 4, eyeCenterX + 4, eyeCenterY + 4, eyeOutline);
					if (endRow >= 10) H::Draw->Line(eyeCenterX - 2, eyeCenterY + 5, eyeCenterX + 1, eyeCenterY + 5, eyeOutline);
					
					// Draw iris (circular) when eye is open enough
					if (eyeOpenness > 0.3f) {
						int irisRadius = static_cast<int>(5.0f * eyeOpenness);
						if (irisRadius > 0) {
							H::Draw->FilledCircle(eyeCenterX, eyeCenterY, irisRadius, 16, irisColor);
							H::Draw->OutlinedCircle(eyeCenterX, eyeCenterY, irisRadius, 16, eyeOutline);
						}
						
						// Draw pupil (circular) when eye is more open
						if (eyeOpenness > 0.5f) {
							int pupilRadius = static_cast<int>(2.0f * eyeOpenness);
							if (pupilRadius > 0) {
								H::Draw->FilledCircle(eyeCenterX, eyeCenterY, pupilRadius, 12, pupilBlack);
							}
							// Highlight (shine)
							H::Draw->Rect(eyeCenterX - 2, eyeCenterY - 2, 2, 2, eyeWhite);
						}
					}
				}
				
				// Eyelashes - only when eye is mostly open
				if (eyeOpenness > 0.6f) {
					// Top lashes (curved outward from almond shape)
					H::Draw->Line(eyeCenterX - 10, eyeCenterY - 4, eyeCenterX - 13, eyeCenterY - 8, lashBrown);
					H::Draw->Line(eyeCenterX - 5, eyeCenterY - 5, eyeCenterX - 6, eyeCenterY - 9, lashBrown);
					H::Draw->Line(eyeCenterX, eyeCenterY - 5, eyeCenterX, eyeCenterY - 10, lashBrown);
					H::Draw->Line(eyeCenterX + 5, eyeCenterY - 5, eyeCenterX + 6, eyeCenterY - 9, lashBrown);
					H::Draw->Line(eyeCenterX + 10, eyeCenterY - 4, eyeCenterX + 13, eyeCenterY - 8, lashBrown);
					
					// Bottom lashes (smaller)
					H::Draw->Line(eyeCenterX - 6, eyeCenterY + 4, eyeCenterX - 7, eyeCenterY + 6, lashBrown);
					H::Draw->Line(eyeCenterX, eyeCenterY + 5, eyeCenterX, eyeCenterY + 8, lashBrown);
					H::Draw->Line(eyeCenterX + 6, eyeCenterY + 4, eyeCenterX + 7, eyeCenterY + 6, lashBrown);
				}
				
				break;
			}
			case 2: // Misc - Enhanced Gear/Wrench with Colors
			{
				// Define colors
				Color_t wrenchSilver = { 180, 180, 190, 255 };
				Color_t wrenchDark = { 100, 100, 110, 255 };
				Color_t gearGold = { 220, 180, 80, 255 };
				Color_t gearDark = { 160, 130, 50, 255 };
				Color_t boltGray = { 140, 140, 150, 255 };
				
				if (!active) {
					wrenchSilver.a = 200;
					gearGold.a = 200;
				}
				
				// Wrench (silver/gray) - shifted right and down
				H::Draw->Rect(iconX + 20, iconY + 6, 4, 17, wrenchSilver);
				H::Draw->OutlinedRect(iconX + 20, iconY + 6, 4, 17, wrenchDark);
				// Wrench head (adjustable)
				H::Draw->OutlinedRect(iconX + 18, iconY + 4, 8, 6, wrenchDark);
				H::Draw->Rect(iconX + 20, iconY + 5, 4, 4, wrenchSilver);
				// Adjustment lines
				H::Draw->Line(iconX + 19, iconY + 6, iconX + 19, iconY + 8, wrenchDark);
				H::Draw->Line(iconX + 25, iconY + 6, iconX + 25, iconY + 8, wrenchDark);
				
				// Rotating gear (gold) - ONLY ANIMATES WHEN ACTIVE
				float gearAngle = active ? (animTime * 15.0f) : 0.0f; // Only rotate when active
				for (int i = 0; i < 8; i++) {
					float angle = (i * 45.0f + gearAngle) * (3.14159f / 180.0f);
					int x1 = iconX + 11 + static_cast<int>(std::cos(angle) * 6);
					int y1 = iconY + 17 + static_cast<int>(std::sin(angle) * 6);
					H::Draw->Rect(x1 - 1, y1 - 1, 3, 3, gearGold);
				}
				// Gear center (detailed)
				H::Draw->OutlinedCircle(iconX + 11, iconY + 17, 4, 12, gearDark);
				H::Draw->OutlinedCircle(iconX + 11, iconY + 17, 2, 8, gearDark);
				H::Draw->Rect(iconX + 10, iconY + 16, 3, 3, gearGold);
				
				// Bolts (gray)
				H::Draw->Rect(iconX + 9, iconY + 15, 2, 2, boltGray);
				H::Draw->Rect(iconX + 12, iconY + 15, 2, 2, boltGray);
				H::Draw->Rect(iconX + 9, iconY + 18, 2, 2, boltGray);
				H::Draw->Rect(iconX + 12, iconY + 18, 2, 2, boltGray);
				break;
			}
			case 3: // Players - TF2 Spy Icon (Based on Reference)
			{
				// Color palette matching reference image
				Color_t maskBlue = { 70, 80, 110, 255 };       // Blue-gray balaclava
				Color_t skinPeach = { 240, 180, 140, 255 };    // Peach skin tone
				Color_t eyeWhite = { 255, 255, 255, 255 };     // White of eyes
				Color_t eyeDark = { 40, 50, 70, 255 };         // Dark pupils/brows
				Color_t mouthDark = { 20, 20, 20, 255 };       // Black mouth
				Color_t cigBrown = { 100, 70, 50, 255 };       // Brown cigarette
				Color_t cigEmber = { 255, 140, 40, 255 };      // Orange ember
				Color_t collarBeige = { 220, 210, 180, 255 };  // Beige collar
				Color_t suitDark = { 50, 60, 80, 255 };        // Dark blue suit
				Color_t tieDark = { 30, 35, 50, 255 };         // Dark tie
				
				// Adjust opacity when inactive
				if (!active) {
					maskBlue.a = 200;
					skinPeach.a = 200;
					cigEmber.a = 200;
				}
				
				// ROUNDED HEAD - Balaclava mask
				// Row 0-1: Top of head
				H::Draw->Line(iconX + 15, iconY + 0, iconX + 21, iconY + 0, maskBlue);
				H::Draw->Line(iconX + 13, iconY + 1, iconX + 23, iconY + 1, maskBlue);
				
				// Row 2-3: Upper head (widest)
				H::Draw->Line(iconX + 11, iconY + 2, iconX + 25, iconY + 2, maskBlue);
				H::Draw->Line(iconX + 10, iconY + 3, iconX + 26, iconY + 3, maskBlue);
				
				// Row 4-6: Forehead area
				H::Draw->Line(iconX + 9, iconY + 4, iconX + 27, iconY + 4, maskBlue);
				H::Draw->Line(iconX + 8, iconY + 5, iconX + 28, iconY + 5, maskBlue);
				H::Draw->Line(iconX + 8, iconY + 6, iconX + 28, iconY + 6, maskBlue);
				
				// Row 7: Upper eye area
				H::Draw->Line(iconX + 8, iconY + 7, iconX + 28, iconY + 7, maskBlue);
				
				// Row 8: Top of eye area with brows - FILL ENTIRE ROW
				H::Draw->Line(iconX + 8, iconY + 8, iconX + 28, iconY + 8, maskBlue); // Fill entire row
				H::Draw->Line(iconX + 11, iconY + 8, iconX + 14, iconY + 8, eyeDark); // Left brow
				H::Draw->Line(iconX + 15, iconY + 8, iconX + 16, iconY + 8, skinPeach); // Nose bridge
				H::Draw->Line(iconX + 17, iconY + 8, iconX + 20, iconY + 8, eyeDark); // Right brow
				
				// Row 9: Eyes - white with dark pupils - FILL ENTIRE ROW
				H::Draw->Line(iconX + 8, iconY + 9, iconX + 28, iconY + 9, maskBlue); // Fill entire row
				H::Draw->Rect(iconX + 11, iconY + 9, 4, 1, eyeWhite); // Left eye white
				H::Draw->Rect(iconX + 12, iconY + 9, 2, 1, eyeDark); // Left pupil
				H::Draw->Rect(iconX + 15, iconY + 9, 2, 1, skinPeach); // Nose
				H::Draw->Rect(iconX + 17, iconY + 9, 4, 1, eyeWhite); // Right eye white
				H::Draw->Rect(iconX + 18, iconY + 9, 2, 1, eyeDark); // Right pupil
				
				// Row 10: Below eyes - FILL ENTIRE ROW
				H::Draw->Line(iconX + 8, iconY + 10, iconX + 28, iconY + 10, maskBlue); // Fill entire row
				H::Draw->Line(iconX + 12, iconY + 10, iconX + 20, iconY + 10, skinPeach); // Skin on top
				
				// Row 11: Nose cutout area - FILL ENTIRE ROW
				H::Draw->Line(iconX + 9, iconY + 11, iconX + 27, iconY + 11, maskBlue); // Fill entire row
				H::Draw->Rect(iconX + 14, iconY + 11, 1, 1, skinPeach); // Nose tip
				H::Draw->Rect(iconX + 15, iconY + 11, 1, 1, maskBlue); // Nose cutout
				H::Draw->Rect(iconX + 16, iconY + 11, 1, 1, skinPeach);
				
				// Row 12: Lower face opening - FILL ENTIRE ROW
				H::Draw->Line(iconX + 10, iconY + 12, iconX + 26, iconY + 12, maskBlue); // Fill entire row
				H::Draw->Line(iconX + 14, iconY + 12, iconX + 20, iconY + 12, skinPeach); // Skin on top
				
				// Row 13: Mouth area with cigarette - FILL ENTIRE ROW
				H::Draw->Line(iconX + 10, iconY + 13, iconX + 25, iconY + 13, maskBlue); // Fill entire row
				H::Draw->Line(iconX + 14, iconY + 13, iconX + 15, iconY + 13, skinPeach);
				H::Draw->Rect(iconX + 16, iconY + 13, 4, 1, mouthDark); // Mouth
				H::Draw->Line(iconX + 20, iconY + 13, iconX + 21, iconY + 13, cigBrown); // Cigarette
				H::Draw->Rect(iconX + 22, iconY + 13, 1, 1, cigEmber); // Ember
				
				// Animated smoke when active
				if (active) {
					Color_t smoke = { 200, 200, 210, 150 };
					if (frame == 0) {
						H::Draw->Rect(iconX + 23, iconY + 11, 1, 1, smoke);
						H::Draw->Rect(iconX + 24, iconY + 10, 1, 1, smoke);
					} else {
						H::Draw->Rect(iconX + 24, iconY + 11, 1, 1, smoke);
						H::Draw->Rect(iconX + 25, iconY + 9, 1, 1, smoke);
					}
				}
				
				// Row 14: Lower chin - FILL ENTIRE ROW
				H::Draw->Line(iconX + 11, iconY + 14, iconX + 24, iconY + 14, maskBlue); // Fill entire row
				H::Draw->Line(iconX + 15, iconY + 14, iconX + 19, iconY + 14, skinPeach); // Skin on top
				
				// Row 15-16: Bottom of mask
				H::Draw->Line(iconX + 12, iconY + 15, iconX + 22, iconY + 15, maskBlue);
				H::Draw->Line(iconX + 14, iconY + 16, iconX + 20, iconY + 16, maskBlue);
				
				// Row 17-18: Neck area
				H::Draw->Line(iconX + 15, iconY + 17, iconX + 19, iconY + 17, maskBlue);
				H::Draw->Line(iconX + 16, iconY + 18, iconX + 18, iconY + 18, maskBlue);
				
				// Row 19-20: Lower neck/collar transition
				H::Draw->Line(iconX + 15, iconY + 19, iconX + 19, iconY + 19, suitDark);
				H::Draw->Line(iconX + 14, iconY + 20, iconX + 20, iconY + 20, suitDark);
				
				// SHOULDERS & SUIT - Rows 21-32
				// Row 21-22: Upper shoulders
				H::Draw->Line(iconX + 8, iconY + 21, iconX + 28, iconY + 21, suitDark);
				H::Draw->Line(iconX + 6, iconY + 22, iconX + 30, iconY + 22, suitDark);
				
				// Row 23: Collar starts showing - FILL ENTIRE ROW FIRST
				H::Draw->Line(iconX + 5, iconY + 23, iconX + 31, iconY + 23, suitDark); // Fill entire row
				H::Draw->Line(iconX + 11, iconY + 23, iconX + 14, iconY + 23, collarBeige); // Left collar
				H::Draw->Line(iconX + 15, iconY + 23, iconX + 19, iconY + 23, tieDark); // Tie
				H::Draw->Line(iconX + 20, iconY + 23, iconX + 23, iconY + 23, collarBeige); // Right collar
				
				// Rows 24-32: Suit body with visible collar and tie - FILL ALL GAPS
				for (int row = 24; row <= 32; row++) {
					// Fill entire row first with suit color to avoid gaps
					H::Draw->Line(iconX + 5, iconY + row, iconX + 31, iconY + row, suitDark);
					
					// Then draw collar on top (V-shape getting narrower)
					int collarWidth = std::max(0, 5 - (row - 24) / 2);
					if (collarWidth > 0) {
						H::Draw->Line(iconX + 10, iconY + row, iconX + 10 + collarWidth, iconY + row, collarBeige);
						H::Draw->Line(iconX + 24 - collarWidth, iconY + row, iconX + 24, iconY + row, collarBeige);
					}
					
					// Dark tie in center
					H::Draw->Rect(iconX + 17, iconY + row, 2, 1, tieDark);
				}
				
				break;
			}
			case 4: // Configs - Yellow Folder with Animated Paper
			{
				// Define colors
				Color_t folderYellow = { 240, 200, 80, 255 };
				Color_t folderDark = { 200, 160, 60, 255 };
				Color_t paperWhite = { 250, 250, 250, 255 };
				Color_t textDark = { 40, 40, 40, 255 };
				
				if (!active) {
					folderYellow.a = 200;
					paperWhite.a = 200;
				}
				
				// Paper sliding out from inside folder when active
				if (active) {
					// Animation: paper slides up from inside the folder
					int slideAmount = (frame == 0 ? 4 : 8); // How much paper is visible
					
					// Paper starts from inside folder (iconY + 9) and slides up
					int paperY = iconY + 9 - slideAmount; // Starts inside, slides up
					int paperX = iconX + 10; // Centered in folder (moved right by 2)
					int paperHeight = 18; // Total paper height
					
					// Draw the visible part of the paper (the part that's outside the folder)
					int visibleHeight = std::min(slideAmount, paperHeight);
					
					// Paper body
					H::Draw->Rect(paperX, paperY, 20, visibleHeight, paperWhite);
					H::Draw->OutlinedRect(paperX, paperY, 20, visibleHeight, textDark);
					
					// Text lines on visible part of paper
					if (visibleHeight > 2) H::Draw->Line(paperX + 2, paperY + 2, paperX + 17, paperY + 2, textDark);
					if (visibleHeight > 4) H::Draw->Line(paperX + 2, paperY + 4, paperX + 16, paperY + 4, textDark);
					if (visibleHeight > 6) H::Draw->Line(paperX + 2, paperY + 6, paperX + 18, paperY + 6, textDark);
				}
				
				// Folder body (ALL YELLOW) - 26px (reduced from 28) - drawn AFTER paper so it covers the bottom part
				H::Draw->Rect(iconX + 4, iconY + 9, 26, 13, folderYellow);
				H::Draw->OutlinedRect(iconX + 4, iconY + 9, 26, 13, folderDark);
				
				// Folder tab (ALL YELLOW) - 14px
				H::Draw->Rect(iconX + 4, iconY + 6, 14, 4, folderYellow);
				H::Draw->OutlinedRect(iconX + 4, iconY + 6, 14, 4, folderDark);
				
				break;
			}
			case 5: // Exploits - Matrix Falling Numbers
			{
				// Matrix color palette
				Color_t matrixBright = { 0, 255, 70, 255 };    // Bright green (foreground)
				Color_t matrixMid = { 0, 200, 50, 255 };       // Medium green
				Color_t matrixDim = { 0, 150, 40, 200 };       // Dim green (background)
				Color_t matrixDark = { 0, 80, 20, 150 };       // Dark green (trail)
				
				if (!active) {
					matrixBright.a = 180;
					matrixMid.a = 180;
					matrixDim.a = 150;
					matrixDark.a = 100;
				}
				
				// Static state for animation persistence
				static float matrixOffsets[6] = { 0.0f, 0.3f, 0.6f, 0.1f, 0.5f, 0.8f };
				static int matrixDigits[6][6] = {
					{1, 4, 7, 2, 9, 3},
					{8, 0, 5, 6, 1, 4},
					{3, 9, 2, 8, 0, 7},
					{6, 1, 4, 3, 5, 2},
					{0, 7, 8, 1, 6, 9},
					{5, 2, 3, 4, 8, 0}
				};
				static float matrixSpeeds[6] = { 0.9f, 1.1f, 0.8f, 1.2f, 1.0f, 0.85f };
				
				// Update animation when active
				if (active) {
					for (int col = 0; col < 6; col++) {
						matrixOffsets[col] += matrixSpeeds[col] * I::GlobalVars->frametime * 2.0f;
						if (matrixOffsets[col] >= 1.0f) {
							matrixOffsets[col] = 0.0f;
							// Shift digits down and generate new top digit
							for (int row = 5; row > 0; row--) {
								matrixDigits[col][row] = matrixDigits[col][row - 1];
							}
							matrixDigits[col][0] = rand() % 10;
						}
					}
				}
				
				// Draw 6 columns of falling numbers
				int colWidth = 5;
				int rowHeight = 5;
				int startX = iconX + 3;
				int startY = iconY + 2;
				
				for (int col = 0; col < 6; col++) {
					int colX = startX + col * colWidth;
					float offset = matrixOffsets[col];
					
					for (int row = 0; row < 6; row++) {
						int digit = matrixDigits[col][row];
						int rowY = startY + row * rowHeight + static_cast<int>(offset * rowHeight);
						
						// Skip if outside icon bounds
						if (rowY < iconY || rowY > iconY + iconSize - 4)
							continue;
						
						// Determine brightness based on row (top = brightest when active)
						Color_t digitColor;
						if (active) {
							if (row == 0) digitColor = matrixBright;
							else if (row == 1) digitColor = matrixMid;
							else if (row < 4) digitColor = matrixDim;
							else digitColor = matrixDark;
						} else {
							// Static display when inactive - all same brightness
							digitColor = matrixDim;
						}
						
						// Draw digit as simple pixel representation
						// Using a minimal 3x4 pixel font for digits 0-9
						int dx = colX;
						int dy = rowY;
						
						switch (digit) {
							case 0:
								H::Draw->Rect(dx, dy, 3, 1, digitColor);
								H::Draw->Rect(dx, dy + 3, 3, 1, digitColor);
								H::Draw->Rect(dx, dy + 1, 1, 2, digitColor);
								H::Draw->Rect(dx + 2, dy + 1, 1, 2, digitColor);
								break;
							case 1:
								H::Draw->Rect(dx + 1, dy, 1, 4, digitColor);
								break;
							case 2:
								H::Draw->Rect(dx, dy, 3, 1, digitColor);
								H::Draw->Rect(dx + 2, dy + 1, 1, 1, digitColor);
								H::Draw->Rect(dx, dy + 2, 3, 1, digitColor);
								H::Draw->Rect(dx, dy + 3, 3, 1, digitColor);
								break;
							case 3:
								H::Draw->Rect(dx, dy, 3, 1, digitColor);
								H::Draw->Rect(dx + 2, dy + 1, 1, 1, digitColor);
								H::Draw->Rect(dx, dy + 2, 3, 1, digitColor);
								H::Draw->Rect(dx + 2, dy + 3, 1, 1, digitColor);
								H::Draw->Rect(dx, dy + 3, 3, 1, digitColor);
								break;
							case 4:
								H::Draw->Rect(dx, dy, 1, 2, digitColor);
								H::Draw->Rect(dx + 2, dy, 1, 4, digitColor);
								H::Draw->Rect(dx, dy + 2, 3, 1, digitColor);
								break;
							case 5:
								H::Draw->Rect(dx, dy, 3, 1, digitColor);
								H::Draw->Rect(dx, dy + 1, 1, 1, digitColor);
								H::Draw->Rect(dx, dy + 2, 3, 1, digitColor);
								H::Draw->Rect(dx + 2, dy + 3, 1, 1, digitColor);
								H::Draw->Rect(dx, dy + 3, 3, 1, digitColor);
								break;
							case 6:
								H::Draw->Rect(dx, dy, 3, 1, digitColor);
								H::Draw->Rect(dx, dy + 1, 1, 2, digitColor);
								H::Draw->Rect(dx, dy + 2, 3, 1, digitColor);
								H::Draw->Rect(dx + 2, dy + 3, 1, 1, digitColor);
								H::Draw->Rect(dx, dy + 3, 3, 1, digitColor);
								break;
							case 7:
								H::Draw->Rect(dx, dy, 3, 1, digitColor);
								H::Draw->Rect(dx + 2, dy + 1, 1, 3, digitColor);
								break;
							case 8:
								H::Draw->Rect(dx, dy, 3, 1, digitColor);
								H::Draw->Rect(dx, dy + 1, 1, 1, digitColor);
								H::Draw->Rect(dx + 2, dy + 1, 1, 1, digitColor);
								H::Draw->Rect(dx, dy + 2, 3, 1, digitColor);
								H::Draw->Rect(dx, dy + 3, 1, 1, digitColor);
								H::Draw->Rect(dx + 2, dy + 3, 1, 1, digitColor);
								H::Draw->Rect(dx, dy + 3, 3, 1, digitColor);
								break;
							case 9:
								H::Draw->Rect(dx, dy, 3, 1, digitColor);
								H::Draw->Rect(dx, dy + 1, 1, 1, digitColor);
								H::Draw->Rect(dx + 2, dy + 1, 1, 1, digitColor);
								H::Draw->Rect(dx, dy + 2, 3, 1, digitColor);
								H::Draw->Rect(dx + 2, dy + 3, 1, 1, digitColor);
								H::Draw->Rect(dx, dy + 3, 3, 1, digitColor);
								break;
						}
					}
				}
				
				break;
			}
		}
		
		// Text label (bottom part of box)
		Color_t textColor = active ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive;
		if (hoverState > 0.01f && !active) {
			textColor.r = static_cast<byte>(textColor.r + (CFG::Menu_Text_Active.r - textColor.r) * hoverState);
			textColor.g = static_cast<byte>(textColor.g + (CFG::Menu_Text_Active.g - textColor.g) * hoverState);
			textColor.b = static_cast<byte>(textColor.b + (CFG::Menu_Text_Active.b - textColor.b) * hoverState);
		}
		
		H::Draw->String(
			H::Fonts->Get(EFonts::Menu),
			x + boxW / 2,
			y + boxH - 12,
			textColor,
			POS_CENTERXY,
			label
		);
		
		m_nCursorY += boxH + CFG::Menu_Spacing_Y;
		m_nLastButtonW = boxW;
		
		return bCallback;
	};

	if (DrawTabBox("Aim", 0, MainTab == EMainTabs::AIM))
		MainTab = EMainTabs::AIM;

	if (DrawTabBox("Visuals", 1, MainTab == EMainTabs::VISUALS))
		MainTab = EMainTabs::VISUALS;

	if (DrawTabBox("Exploits", 5, MainTab == EMainTabs::EXPLOITS))
		MainTab = EMainTabs::EXPLOITS;

	if (DrawTabBox("Misc", 2, MainTab == EMainTabs::MISC))
		MainTab = EMainTabs::MISC;

	if (DrawTabBox("Players", 3, MainTab == EMainTabs::PLAYERS))
		MainTab = EMainTabs::PLAYERS;

	if (DrawTabBox("Configs", 4, MainTab == EMainTabs::CONFIGS))
		MainTab = EMainTabs::CONFIGS;

	H::Draw->Line(
		CFG::Menu_Pos_X + m_nLastButtonW + (CFG::Menu_Spacing_X * 2) - 1,
		CFG::Menu_Pos_Y + CFG::Menu_Drag_Bar_Height,
		CFG::Menu_Pos_X + m_nLastButtonW + (CFG::Menu_Spacing_X * 2) - 1,
		CFG::Menu_Pos_Y + CFG::Menu_Height - 1,
		CFG::Menu_Accent_Primary
	);

	m_nCursorX = CFG::Menu_Pos_X + m_nLastButtonW + (CFG::Menu_Spacing_X * 3) - 1;
	m_nCursorY = CFG::Menu_Pos_Y + CFG::Menu_Drag_Bar_Height + CFG::Menu_Spacing_Y;

	if (MainTab == EMainTabs::AIM)
	{
		enum class EAimTabs { AIMBOT, TRIGGERBOT };
		static EAimTabs AimTab = EAimTabs::AIMBOT;

		int anchor_x = m_nCursorX;
		int anchor_y = m_nCursorY;

		if (Button("Aimbot", AimTab == EAimTabs::AIMBOT))
			AimTab = EAimTabs::AIMBOT;

		m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
		m_nCursorY = anchor_y;

		if (Button("Triggerbot", AimTab == EAimTabs::TRIGGERBOT))
			AimTab = EAimTabs::TRIGGERBOT;

		H::Draw->Line(
			anchor_x - CFG::Menu_Spacing_X,
			m_nCursorY,
			CFG::Menu_Pos_X + CFG::Menu_Width - 1,
			m_nCursorY,
			CFG::Menu_Accent_Primary
		);

		m_nCursorX = anchor_x + CFG::Menu_Spacing_X;
		m_nCursorY += CFG::Menu_Spacing_Y;

		if (AimTab == EAimTabs::AIMBOT)
		{
			anchor_y = m_nCursorY;

			GroupBoxStart("Global", 150);
			{
				CheckBox("Active", CFG::Aimbot_Active);
				CheckBox("Auto Shoot", CFG::Aimbot_AutoShoot);
				InputKey("Key", CFG::Aimbot_Key);

				multiselect("Targets", AimbotTargets, {
					{ "Players", CFG::Aimbot_Target_Players },
					{ "Buildings", CFG::Aimbot_Target_Buildings }
					});

				multiselect("Ignore", AimbotIgnores, {
					{ "Friends", CFG::Aimbot_Ignore_Friends },
					{ "Invisible", CFG::Aimbot_Ignore_Invisible },
					{ "Invulnerable", CFG::Aimbot_Ignore_Invulnerable },
					{ "Taunting", CFG::Aimbot_Ignore_Taunting }
					});
			}
			GroupBoxEnd();

			GroupBoxStart("Melee", 150);
			{
				CheckBox("Active", CFG::Aimbot_Melee_Active);
				CheckBox("Always Active", CFG::Aimbot_Melee_Always_Active);
				CheckBox("Target Lag Records", CFG::Aimbot_Melee_Target_LagRecords);

				CheckBox("Predict Swing", CFG::Aimbot_Melee_Predict_Swing);
				CheckBox("Walk To Target", CFG::Aimbot_Melee_Walk_To_Target);
				CheckBox("Whip Teammates", CFG::Aimbot_Melee_Whip_Teammates);
				CheckBox("Auto Repair Buildings", CFG::Aimbot_Melee_Auto_Repair);

				SelectSingle("Aim Type", CFG::Aimbot_Melee_Aim_Type, {
					{ "Normal", 0 },
					{ "Silent", 1 },
					{ "Smooth", 2 }
					});

				SelectSingle("Sort", CFG::Aimbot_Melee_Sort, {
					{ "FOV", 0 },
					{ "Distance", 1 }
					});

				SliderFloat("FOV", CFG::Aimbot_Melee_FOV, 1.0f, 180.0f, 1.0f, "%.0f");
				SliderFloat("Smoothing", CFG::Aimbot_Melee_Smoothing, 0.0f, 20.0f, 0.5f, "%.1f");
			}
			GroupBoxEnd();

			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;

			GroupBoxStart("Hitscan", 150);
			{
				CheckBox("Active", CFG::Aimbot_Hitscan_Active);
				CheckBox("Target Lag Records", CFG::Aimbot_Hitscan_Target_LagRecords);
				CheckBox("Target Stickies", CFG::Aimbot_Hitscan_Target_Stickies);

				CheckBox("Smooth Auto Shoot", CFG::Aimbot_Hitscan_Advanced_Smooth_AutoShoot);
				CheckBox("Auto Scope", CFG::Aimbot_Hitscan_Auto_Scope);
				CheckBox("Wait For Headshot", CFG::Aimbot_Hitscan_Wait_For_Headshot);
				CheckBox("Wait For Charge", CFG::Aimbot_Hitscan_Wait_For_Charge);
				CheckBox("Minigun Tapfire", CFG::Aimbot_Hitscan_Minigun_TapFire);
				CheckBox("Smart Shotgun (Beta)", CFG::Aimbot_Hitscan_Smart_Shotgun);
				CheckBox("FakeLag Fix", CFG::Aimbot_Hitscan_FakeLagFix);
				if (CFG::Aimbot_Hitscan_FakeLagFix)
					CheckBox("FakeLag Fix Indicator", CFG::Aimbot_Hitscan_FakeLagFix_Indicator);

				SelectSingle("Aim Type", CFG::Aimbot_Hitscan_Aim_Type, {
					{ "Normal", 0 },
					{ "Silent", 1 },
					{ "Smooth", 2 }
				});

				SelectSingle("Hitbox", CFG::Aimbot_Hitscan_Hitbox, {
					{ "Head", 0 },
					{ "Body", 1 },
					{ "Auto", 2 },
					{ "Switch", 3 }
					});

				if (CFG::Aimbot_Hitscan_Hitbox == 3)
				{
					InputKey("Switch Key", CFG::Aimbot_Hitscan_Switch_Key);
				}

				SelectSingle("Sort", CFG::Aimbot_Hitscan_Sort, {
					{ "FOV", 0 },
					{ "Distance", 1 }
					});

				multiselect("Scan", HitscanScan, {
					{ "Head", CFG::Aimbot_Hitscan_Scan_Head },
					{ "Body", CFG::Aimbot_Hitscan_Scan_Body },
					{ "Arms", CFG::Aimbot_Hitscan_Scan_Arms },
					{ "Legs", CFG::Aimbot_Hitscan_Scan_Legs },
					{ "Buildings", CFG::Aimbot_Hitscan_Scan_Buildings }
					});

				SliderFloat("FOV", CFG::Aimbot_Hitscan_FOV, 1.0f, 180.0f, 1.0f, "%.0f");
				SliderFloat("Smoothing", CFG::Aimbot_Hitscan_Smoothing, 0.0f, 20.0f, 0.5f, "%.1f");
				SliderFloat("Fake Latency", CFG::Aimbot_Hitscan_Fake_Latency, 0.0f, 600.0f, 10.0f, "%.0f ms");
				SliderInt("Hitchance", CFG::Aimbot_Hitscan_Hitchance, 0, 100, 1);
			}
			GroupBoxEnd();

			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;

			GroupBoxStart("Projectile", 150);
			{
				CheckBox("Active", CFG::Aimbot_Amalgam_Projectile_Active);
				CheckBox("Use Prime Time", CFG::Aimbot_Amalgam_Projectile_Mod_PrimeTime);
				CheckBox("Midpoint Aim", CFG::Aimbot_Projectile_Midpoint_Aim);
				CheckBox("Timed Double Donk", CFG::Aimbot_Projectile_Timed_Double_Donk);
				CheckBox("Cancel Charge", CFG::Aimbot_Projectile_Cannon_Cancel_Charge);
				
				SelectSingle("Aim Type", CFG::Aimbot_Projectile_Aim_Type, {
					{ "Plain", 0 },
					{ "Silent", 1 }
					});

				SelectSingle("Splash", CFG::Aimbot_Amalgam_Projectile_Splash, {
					{ "Off", 0 },
					{ "Include", 1 },
					{ "Prefer", 2 },
					{ "Only", 3 }
					});

				multiselect("Hitbox", AmalgamProjHitbox, {
					{ "Auto", CFG::Aimbot_Amalgam_Projectile_Hitbox_Auto },
					{ "Head", CFG::Aimbot_Amalgam_Projectile_Hitbox_Head },
					{ "Body", CFG::Aimbot_Amalgam_Projectile_Hitbox_Body },
					{ "Feet", CFG::Aimbot_Amalgam_Projectile_Hitbox_Feet },
					{ "Bodyaim If Lethal", CFG::Aimbot_Amalgam_Projectile_Hitbox_BodyaimLethal },
					{ "Prioritize Feet", CFG::Aimbot_Amalgam_Projectile_Hitbox_PrioritizeFeet }
					});

				SliderFloat("FOV", CFG::Aimbot_Projectile_FOV, 1.0f, 180.0f, 1.0f, "%.0f");
				SliderInt("Max Targets", CFG::Aimbot_Projectile_Max_Targets, 1, 5, 1);
				SliderInt("Splash Points", CFG::Aimbot_Amalgam_Projectile_SplashPoints, 50, 400, 25);
				SliderFloat("Max Sim Time", CFG::Aimbot_Projectile_Max_Simulation_Time, 0.5f, 5.0f, 0.1f, "%.1fs");
				CheckBox("Neckbreaker", CFG::Aimbot_Projectile_Neckbreaker);
				SelectSingle("Neckbreaker Step ZAngle", CFG::Aimbot_Projectile_NeckbreakerStep, {
					{ "20z", 20 },
					{ "45z", 45 },
					{ "90z", 90 },
					{ "180z", 180 }
					});
			}
			GroupBoxEnd();

		}

		if (AimTab == EAimTabs::TRIGGERBOT)
		{
			anchor_y = m_nCursorY;

			GroupBoxStart("Global", 150);
			{
				CheckBox("Active", CFG::Triggerbot_Active);
				InputKey("Key", CFG::Triggerbot_Key);
			}
			GroupBoxEnd();

			GroupBoxStart("Auto Airblast", 150);
			{
				CheckBox("Active", CFG::Triggerbot_AutoAirblast_Active);
				CheckBox("Aim Assist", CFG::Triggerbot_AutoAirblast_Aim_Assist);
				CheckBox("Aimbot Support (BETA)", CFG::Triggerbot_AutoAirblast_Aimbot_Support);

				SelectSingle("Mode", CFG::Triggerbot_AutoAirblast_Mode,
				{
					{ "Legit", 0 },
					{ "Rage", 1 }
				});

				SelectSingle("Aim Mode", CFG::Triggerbot_AutoAirblast_Aim_Mode,
				{
					{ "Normal", 0 },
					{ "Silent", 1 }
				});

				multiselect("Ignore", TriggerbotAirblastIgnore,
				{
					{ "Rocket", CFG::Triggerbot_AutoAirblast_Ignore_Rocket },
					{ "Sentry Rocket", CFG::Triggerbot_AutoAirblast_Ignore_SentryRocket },
					{ "Jarate", CFG::Triggerbot_AutoAirblast_Ignore_Jar },
					{ "Gas", CFG::Triggerbot_AutoAirblast_Ignore_JarGas },
					{ "Milk", CFG::Triggerbot_AutoAirblast_Ignore_JarMilk },
					{ "Arrow", CFG::Triggerbot_AutoAirblast_Ignore_Arrow },
					{ "Flare", CFG::Triggerbot_AutoAirblast_Ignore_Flare },
					{ "Cleaver", CFG::Triggerbot_AutoAirblast_Ignore_Cleaver },
					{ "Healing Bolt", CFG::Triggerbot_AutoAirblast_Ignore_HealingBolt },
					{ "Pipebomb", CFG::Triggerbot_AutoAirblast_Ignore_PipebombProjectile },
					{ "Ball of Fire", CFG::Triggerbot_AutoAirblast_Ignore_BallOfFire },
					{ "Energy Ring", CFG::Triggerbot_AutoAirblast_Ignore_EnergyRing },
					{ "Energy Ball", CFG::Triggerbot_AutoAirblast_Ignore_EnergyBall },
				});
			}
			GroupBoxEnd();

			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;

			GroupBoxStart("Auto Detonate", 150);
			{
				CheckBox("Active", CFG::Triggerbot_AutoDetonate_Active);
				CheckBox("Always On (No Keybind)", CFG::Triggerbot_AutoDetonate_Always_On);
				CheckBox("Detonate Timer", CFG::Triggerbot_AutoDetonate_Timer_Enabled);

				if (CFG::Triggerbot_AutoDetonate_Timer_Enabled)
				{
					SliderFloat("Timer", CFG::Triggerbot_AutoDetonate_Timer_Value, 0.1f, 0.5f, 0.1f, "%.1fs");
					CheckBox("Danger Zone", CFG::Triggerbot_AutoDetonate_DangerZone_Enabled);
				}

				multiselect("Targets", DetonateTargets, {
					{ "Players", CFG::Triggerbot_AutoDetonate_Target_Players },
					{ "Buildings", CFG::Triggerbot_AutoDetonate_Target_Buildings }
					});

				multiselect("Ignore", DetonateIgnores, {
					{ "Friends", CFG::Triggerbot_AutoDetonate_Ignore_Friends },
					{ "Invisible", CFG::Triggerbot_AutoDetonate_Ignore_Invisible },
					{ "Invulnerable", CFG::Triggerbot_AutoDetonate_Ignore_Invulnerable }
					});
			}
			GroupBoxEnd();

			GroupBoxStart("Auto Uber", 150);
			{
				CheckBox("Active", CFG::AutoUber_Active);
				CheckBox("Always On (No Keybind)", CFG::AutoUber_Always_On);
				if (CFG::AutoUber_Active)
				{
					CheckBox("Crit Detection", CFG::AutoUber_CritProjectile_Check);
					CheckBox("Sniper Sightline", CFG::AutoUber_SniperSightline_Check);
				}
				CheckBox("Auto Heal", CFG::AutoUber_AutoHeal_Active);
				if (CFG::AutoUber_AutoHeal_Active)
				{
					CheckBox("Prioritize Friends", CFG::AutoUber_AutoHeal_Prioritize_Friends);
				}
			}
			GroupBoxEnd();

			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;

			GroupBoxStart("Auto Backstab", 150);
			{
				CheckBox("Active", CFG::Triggerbot_AutoBackstab_Active);
				CheckBox("Always On (No Keybind)", CFG::Triggerbot_AutoBackstab_Always_On);
				CheckBox("Knife If Lethal", CFG::Triggerbot_AutoBackstab_Knife_If_Lethal);

				SelectSingle("Mode", CFG::Triggerbot_AutoBacktab_Mode,
				{
					{ "Legit", 0 },
					{ "Rage", 1 }
				});

				SelectSingle("Aim Mode", CFG::Triggerbot_AutoBacktab_Aim_Mode,
				{
					{ "Normal", 0 },
					{ "Silent", 1 }
				});

				multiselect("Ignore", AutoBackstabIgnores,
				{
					{ "Friends", CFG::Triggerbot_AutoBackstab_Ignore_Friends },
					{ "Invisible", CFG::Triggerbot_AutoBackstab_Ignore_Invisible },
					{ "Invulnerable", CFG::Triggerbot_AutoBackstab_Ignore_Invulnerable }
				});
			}
			GroupBoxEnd();

			GroupBoxStart("Auto Vaccinator", 150);
			{
				CheckBox("Active", CFG::Triggerbot_AutoVaccinator_Active);
				CheckBox("Always On (No Keybind)", CFG::Triggerbot_AutoVaccinator_Always_On);
				if (CFG::Triggerbot_AutoVaccinator_Always_On)
					CheckBox("No Pop (Cycle Only)", CFG::Triggerbot_AutoVaccinator_NoPop);
				SelectSingle("Pop For", CFG::Triggerbot_AutoVaccinator_Pop, {
					{ "Everyone", 0 },
					{ "Friends Only", 1 }
				});
			}
			GroupBoxEnd();

			GroupBoxStart("Auto Sapper", 150);
			{
				CheckBox("Active", CFG::Triggerbot_AutoSapper_Active);
				CheckBox("Always On (No Keybind)", CFG::Triggerbot_AutoSapper_Always_On);
				CheckBox("Range ESP", CFG::Triggerbot_AutoSapper_ESP);

				SelectSingle("Mode", CFG::Triggerbot_AutoSapper_Mode,
				{
					{ "Legit", 0 },
					{ "Rage", 1 }
				});

				SelectSingle("Aim Mode", CFG::Triggerbot_AutoSapper_Aim_Mode,
				{
					{ "Normal", 0 },
					{ "Silent", 1 }
				});
			}
			GroupBoxEnd();

			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;
		}
	}

	if (MainTab == EMainTabs::VISUALS)
	{
		enum class EVisualsTabs { ESP, RADAR, MATERIALS, OUTLINES, OTHER, OTHER2, COLORS };
		static EVisualsTabs VisualsTab = EVisualsTabs::ESP;

		int anchor_x = m_nCursorX;
		int anchor_y = m_nCursorY;

		if (Button("ESP", VisualsTab == EVisualsTabs::ESP))
			VisualsTab = EVisualsTabs::ESP;

		m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
		m_nCursorY = anchor_y;

		if (Button("Radar", VisualsTab == EVisualsTabs::RADAR))
			VisualsTab = EVisualsTabs::RADAR;

		m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
		m_nCursorY = anchor_y;

		if (Button("Materials", VisualsTab == EVisualsTabs::MATERIALS))
			VisualsTab = EVisualsTabs::MATERIALS;

		m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
		m_nCursorY = anchor_y;

		if (Button("Outlines", VisualsTab == EVisualsTabs::OUTLINES))
			VisualsTab = EVisualsTabs::OUTLINES;

		m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
		m_nCursorY = anchor_y;

		if (Button("Other", VisualsTab == EVisualsTabs::OTHER))
			VisualsTab = EVisualsTabs::OTHER;

		m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
		m_nCursorY = anchor_y;

		if (Button("Other2", VisualsTab == EVisualsTabs::OTHER2))
			VisualsTab = EVisualsTabs::OTHER2;

		m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
		m_nCursorY = anchor_y;

		if (Button("Colors", VisualsTab == EVisualsTabs::COLORS))
			VisualsTab = EVisualsTabs::COLORS;

		H::Draw->Line(
			anchor_x - CFG::Menu_Spacing_X,
			m_nCursorY,
			CFG::Menu_Pos_X + CFG::Menu_Width - 1,
			m_nCursorY,
			CFG::Menu_Accent_Primary
		);

		m_nCursorX = anchor_x + CFG::Menu_Spacing_X;
		m_nCursorY += CFG::Menu_Spacing_Y;

		if (VisualsTab == EVisualsTabs::ESP)
		{
			anchor_y = m_nCursorY;

			GroupBoxStart("Global", 150);
			{
				CheckBox("Active", CFG::ESP_Active);
				SelectSingle("Tracer From", CFG::ESP_Tracer_From, { { "Top", 0 }, { "Center", 1 }, { "Bottom", 2 } });
				SelectSingle("Tracer To", CFG::ESP_Tracer_To, { { "Top", 0 }, { "Center", 1 }, { "Bottom", 2 } });
				SelectSingle("Text Color", CFG::ESP_Text_Color, { { "Default", 0 }, { "White", 1 } });
			}
			GroupBoxEnd();

			GroupBoxStart("World", 150);
			{
				CheckBox("Active", CFG::ESP_World_Active);
				SliderFloat("Alpha", CFG::ESP_World_Alpha, 0.1f, 1.0f, 0.1f, "%.1f");

				multiselect("Ignore", WorldIgnore, {
					{ "Health Packs", CFG::ESP_World_Ignore_HealthPacks },
					{ "Ammo Packs", CFG::ESP_World_Ignore_AmmoPacks },
					{ "Local Projectiles", CFG::ESP_World_Ignore_LocalProjectiles },
					{ "Enemy Projectiles", CFG::ESP_World_Ignore_EnemyProjectiles },
					{ "Teammate Projectiles", CFG::ESP_World_Ignore_TeammateProjectiles },
					{ "Halloween Gifts", CFG::ESP_World_Ignore_Halloween_Gift },
					{ "MVM Money", CFG::ESP_World_Ignore_MVM_Money }
				});

				multiselect("Draw", WorldDraw, {
					{ "Name", CFG::ESP_World_Name },
					{ "Box", CFG::ESP_World_Box },
					{ "Tracer", CFG::ESP_World_Tracer }
				});
			}
			GroupBoxEnd();

			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;

			GroupBoxStart("Players", 150);
			{
				CheckBox("Active", CFG::ESP_Players_Active);
				SliderFloat("Alpha", CFG::ESP_Players_Alpha, 0.1f, 1.0f, 0.1f, "%.1f");
				SliderFloat("Arrow Radius", CFG::ESP_Players_Arrows_Radius, 50.0f, 400.0f, 50.0f, "%.0f");
				SliderFloat("Arrow Max Distance", CFG::ESP_Players_Arrows_Max_Distance, 100.0f, 1000.0f, 100.0f, "%.0f");
				SelectSingle("Bones Color", CFG::ESP_Players_Bones_Color, { { "Default", 0 }, { "White", 1 } });

				multiselect("Ignore", PlayerIgnore, {
					{ "Local", CFG::ESP_Players_Ignore_Local },
					{ "Friends", CFG::ESP_Players_Ignore_Friends },
					{ "Enemies", CFG::ESP_Players_Ignore_Enemies },
					{ "Teammates", CFG::ESP_Players_Ignore_Teammates },
					{ "Invisible", CFG::ESP_Players_Ignore_Invisible },
					{ "Tagged", CFG::ESP_Players_Ignore_Tagged }
					});

				multiselect("Draw", PlayerDraw, {
					{ "Name", CFG::ESP_Players_Name },
					{ "Class", CFG::ESP_Players_Class },
					{ "Class Icon", CFG::ESP_Players_Class_Icon },
					{ "Health", CFG::ESP_Players_Health },
					{ "Health Bar", CFG::ESP_Players_HealthBar },
					{ "Uber", CFG::ESP_Players_Uber },
					{ "Uber Bar", CFG::ESP_Players_UberBar },
					{ "Box", CFG::ESP_Players_Box },
					{ "Tracer", CFG::ESP_Players_Tracer },
					{ "Bones", CFG::ESP_Players_Bones },
					{ "Arrows", CFG::ESP_Players_Arrows },
					{ "Conds", CFG::ESP_Players_Conds },
					{ "Sniper Lines", CFG::ESP_Players_Sniper_Lines },
					{ "F2P Tag", CFG::ESP_Players_Show_F2P },
					{ "Party Tag", CFG::ESP_Players_Show_Party }
					});

				CheckBox("Show Team Medics", CFG::ESP_Players_Show_Teammate_Medics);
			}
			GroupBoxEnd();

			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;

			GroupBoxStart("Buildings", 150);
			{
				CheckBox("Active", CFG::ESP_Buildings_Active);
				SliderFloat("Alpha", CFG::ESP_Buildings_Alpha, 0.1f, 1.0f, 0.1f, "%.1f");

				multiselect("Ignore", BuildingIgnore, {
					{ "Local", CFG::ESP_Buildings_Ignore_Local },
					{ "Enemies", CFG::ESP_Buildings_Ignore_Enemies },
					{ "Teammates", CFG::ESP_Buildings_Ignore_Teammates }
					});

				multiselect("Draw", BuildingDraw, {
					{ "Name", CFG::ESP_Buildings_Name },
					{ "Health", CFG::ESP_Buildings_Health },
					{ "Health Bar", CFG::ESP_Buildings_HealthBar },
					{ "Level", CFG::ESP_Buildings_Level },
					{ "Level Bar", CFG::ESP_Buildings_LevelBar },
					{ "Box", CFG::ESP_Buildings_Box },
					{ "Tracer", CFG::ESP_Buildings_Tracer },
					{ "Conds", CFG::ESP_Buildings_Conds }
					});

				CheckBox("Show Team Dispensers", CFG::ESP_Buildings_Show_Teammate_Dispensers);
			}
			GroupBoxEnd();
		}

		if (VisualsTab == EVisualsTabs::RADAR)
		{
			anchor_x = m_nCursorX;
			anchor_y = m_nCursorY;

			GroupBoxStart("Global", 150);
			{
				CheckBox("Active", CFG::Radar_Active);
				SelectSingle("Style", CFG::Radar_Style, { { "Rectangle", 0 }, { "Circle", 1 } });
				SliderInt("Size", CFG::Radar_Size, 100, 1000, 25);
				SliderInt("Icon Size", CFG::Radar_Icon_Size, 18, 36, 2);
				SliderFloat("Radius", CFG::Radar_Radius, 100.0f, 3000.0f, 50.0f, "%.0f");
				SliderFloat("Cross Alpha", CFG::Radar_Cross_Alpha, 0.0f, 1.0f, 0.1f, "%.1f");
				SliderFloat("Outline Alpha", CFG::Radar_Outline_Alpha, 0.0f, 1.0f, 0.1f, "%.1f");
				SliderFloat("Background Alpha", CFG::Radar_Background_Alpha, 0.0f, 1.0f, 0.1f, "%.1f");
			}
			GroupBoxEnd();

			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;

			GroupBoxStart("Players", 150);
			{
				CheckBox("Active", CFG::Radar_Players_Active);

				multiselect("Ignore", PlayerIgnore, {
					{ "Local", CFG::Radar_Players_Ignore_Local },
					{ "Friends", CFG::Radar_Players_Ignore_Friends },
					{ "Enemies", CFG::Radar_Players_Ignore_Enemies },
					{ "Teammates", CFG::Radar_Players_Ignore_Teammates },
					{ "Invisible", CFG::Radar_Players_Ignore_Invisible }
					});

				CheckBox("Show Team Medics", CFG::Radar_Players_Show_Teammate_Medics);
			}
			GroupBoxEnd();

			GroupBoxStart("Buildings", 150);
			{
				CheckBox("Active", CFG::Radar_Buildings_Active);

				multiselect("Ignore", BuildingIgnore, {
					{ "Local", CFG::Radar_Buildings_Ignore_Local },
					{ "Enemies", CFG::Radar_Buildings_Ignore_Enemies },
					{ "Teammates", CFG::Radar_Buildings_Ignore_Teammates }
					});

				CheckBox("Show Team Dispensers", CFG::Radar_Buildings_Show_Teammate_Dispensers);
			}
			GroupBoxEnd();

			GroupBoxStart("World", 150);
			{
				CheckBox("Active", CFG::Radar_World_Active);

				multiselect("Ignore", BuildingIgnore, {
					{ "Health Packs", CFG::Radar_World_Ignore_HealthPacks },
					{ "Ammo Packs", CFG::Radar_World_Ignore_AmmoPacks },
					{ "Halloween Gifts", CFG::Radar_World_Ignore_Halloween_Gift },
					{ "MVM Money", CFG::Radar_World_Ignore_MVM_Money }
				});
			}
			GroupBoxEnd();
		}

		if (VisualsTab == EVisualsTabs::MATERIALS)
		{
			anchor_x = m_nCursorX;
			anchor_y = m_nCursorY;

			GroupBoxStart("Global", 150);
			{
				CheckBox("Active", CFG::Materials_Active);
			}
			GroupBoxEnd();

			GroupBoxStart("World", 150);
			{
				CheckBox("Active", CFG::Materials_World_Active);
				CheckBox("No Depth", CFG::Materials_World_No_Depth);
				SliderFloat("Alpha", CFG::Materials_World_Alpha, 0.0f, 1.0f, 0.1f, "%.1f");

				SelectSingle("Material", CFG::Materials_World_Material, {
					{ "Original", 0 },
					{ "Flat", 1 },
					{ "Shaded", 2 },
					{ "Glossy", 3 },
					{ "Glow", 4 },
					{ "Plastic", 5 }
					});

				multiselect("Ignore", WorldIgnore, {
					{ "Health Packs", CFG::Materials_World_Ignore_HealthPacks },
					{ "Ammo Packs", CFG::Materials_World_Ignore_AmmoPacks },
					{ "Local Projectiles", CFG::Materials_World_Ignore_LocalProjectiles },
					{ "Enemy Projectiles", CFG::Materials_World_Ignore_EnemyProjectiles },
					{ "Teammate Projectiles", CFG::Materials_World_Ignore_TeammateProjectiles },
					{ "Halloween Gifts", CFG::Materials_World_Ignore_Halloween_Gift },
					{ "MVM Money", CFG::Materials_World_Ignore_MVM_Money }
					});
			}
			GroupBoxEnd();

			GroupBoxStart("View Model", 150);
			{
				CheckBox("Active", CFG::Materials_ViewModel_Active);

				SliderFloat("Hands Alpha", CFG::Materials_ViewModel_Hands_Alpha, 0.0f, 1.0f, 0.1f, "%.1f");

				SelectSingle("Hands Material", CFG::Materials_ViewModel_Hands_Material, {
					{ "Original", 0 },
					{ "Flat", 1 },
					{ "Shaded", 2 },
					{ "Glossy", 3 },
					{ "Glow", 4 },
					{ "Plastic", 5 }
				});

				SliderFloat("Weapon Alpha", CFG::Materials_ViewModel_Weapon_Alpha, 0.0f, 1.0f, 0.1f, "%.1f");

				SelectSingle("Weapon Material", CFG::Materials_ViewModel_Weapon_Material, {
					{ "Original", 0 },
					{ "Flat", 1 },
					{ "Shaded", 2 },
					{ "Glossy", 3 },
					{ "Glow", 4 },
					{ "Plastic", 5 }
				});
			}
			GroupBoxEnd();

			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;

			GroupBoxStart("Players", 150);
			{
				CheckBox("Active", CFG::Materials_Players_Active);
				CheckBox("No Depth", CFG::Materials_Players_No_Depth);
				SliderFloat("Alpha", CFG::Materials_Players_Alpha, 0.0f, 1.0f, 0.1f, "%.1f");

				SelectSingle("Material", CFG::Materials_Players_Material, {
					{ "Original", 0 },
					{ "Flat", 1 },
					{ "Shaded", 2 },
					{ "Glossy", 3 },
					{ "Glow", 4 },
					{ "Plastic", 5 }
					});

				multiselect("Ignore", PlayerIgnore, {
					{ "Local", CFG::Materials_Players_Ignore_Local },
					{ "Friends", CFG::Materials_Players_Ignore_Friends },
					{ "Enemies", CFG::Materials_Players_Ignore_Enemies },
					{ "Teammates", CFG::Materials_Players_Ignore_Teammates },
					{ "Tagged", CFG::Materials_Players_Ignore_Tagged }
					});

				CheckBox("Show Team Medics", CFG::Materials_Players_Show_Teammate_Medics);
			}
			GroupBoxEnd();

			// Fake Model group box (under Players)
			GroupBoxStart("Fake Model", 150);
			{
				CheckBox("Active", CFG::Materials_FakeModel_Active);
				SliderFloat("Alpha", CFG::Materials_FakeModel_Alpha, 0.0f, 1.0f, 0.1f, "%.1f");
				SelectSingle("Material", CFG::Materials_FakeModel_Material, {
					{ "Original", 0 },
					{ "Flat", 1 },
					{ "Shaded", 2 },
					{ "Glossy", 3 },
					{ "Glow", 4 },
					{ "Plastic", 5 }
					});
			}
			GroupBoxEnd();

			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;

			GroupBoxStart("Buildings", 150);
			{
				CheckBox("Active", CFG::Materials_Buildings_Active);
				CheckBox("No Depth", CFG::Materials_Buildings_No_Depth);
				SliderFloat("Alpha", CFG::Materials_Buildings_Alpha, 0.0f, 1.0f, 0.1f, "%.1f");

				SelectSingle("Material", CFG::Materials_Buildings_Material, {
					{ "Original", 0 },
					{ "Flat", 1 },
					{ "Shaded", 2 },
					{ "Glossy", 3 },
					{ "Glow", 4 },
					{ "Plastic", 5 }
					});

				multiselect("Ignore", BuildingIgnore, {
					{ "Local", CFG::Materials_Buildings_Ignore_Local },
					{ "Enemies", CFG::Materials_Buildings_Ignore_Enemies },
					{ "Teammates", CFG::Materials_Buildings_Ignore_Teammates }
					});

				CheckBox("Show Team Dispensers", CFG::Materials_Buildings_Show_Teammate_Dispensers);
			}
			GroupBoxEnd();

			// Lag Records group box (under Buildings)
			GroupBoxStart("Lag Records", 150);
			{
				CheckBox("Active", CFG::Materials_LagRecords_Active);
				SliderFloat("Alpha", CFG::Materials_LagRecords_Alpha, 0.0f, 1.0f, 0.1f, "%.1f");
				SelectSingle("Material", CFG::Materials_LagRecords_Material, {
					{ "Original", 0 },
					{ "Flat", 1 },
					{ "Shaded", 2 },
					{ "Glossy", 3 },
					{ "Glow", 4 },
					{ "Plastic", 5 }
					});
				SelectSingle("Style", CFG::Materials_LagRecords_Style, {
					{ "All", 0 },
					{ "Last Only", 1 }
					});
			}
			GroupBoxEnd();
		}

		if (VisualsTab == EVisualsTabs::OUTLINES)
		{
			anchor_x = m_nCursorX;
			anchor_y = m_nCursorY;

			GroupBoxStart("Global", 150);
			{
				CheckBox("Active", CFG::Outlines_Active);

				SelectSingle("Style", CFG::Outlines_Style, {
					{ "Bloom", 0 },
					{ "Crisp", 1 },
					{ "Cartoony", 2 },
					{ "Cartoony Alt", 3 }
				});

				SliderInt("Bloom Amount", CFG::Outlines_Bloom_Amount, 1, 10, 1);
			}
			GroupBoxEnd();

			GroupBoxStart("World", 150);
			{
				CheckBox("Active", CFG::Outlines_World_Active);
				SliderFloat("Alpha", CFG::Outlines_World_Alpha, 0.0f, 1.0f, 0.1f, "%.1f");

				multiselect("Ignore", WorldIgnore, {
					{ "Health Packs", CFG::Outlines_World_Ignore_HealthPacks },
					{ "Ammo Packs", CFG::Outlines_World_Ignore_AmmoPacks },
					{ "Local Projectiles", CFG::Outlines_World_Ignore_LocalProjectiles },
					{ "Enemy Projectiles", CFG::Outlines_World_Ignore_EnemyProjectiles },
					{ "Teammate Projectiles", CFG::Outlines_World_Ignore_TeammateProjectiles },
					{ "Halloween Gifts", CFG::Outlines_World_Ignore_Halloween_Gift },
					{ "MVM Money", CFG::Outlines_World_Ignore_MVM_Money }
					});
			}
			GroupBoxEnd();

			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;

			GroupBoxStart("Players", 150);
			{
				CheckBox("Active", CFG::Outlines_Players_Active);
				SliderFloat("Alpha", CFG::Outlines_Players_Alpha, 0.0f, 1.0f, 0.1f, "%.1f");

				multiselect("Ignore", PlayerIgnore, {
					{ "Local", CFG::Outlines_Players_Ignore_Local },
					{ "Friends", CFG::Outlines_Players_Ignore_Friends },
					{ "Enemies", CFG::Outlines_Players_Ignore_Enemies },
					{ "Teammates", CFG::Outlines_Players_Ignore_Teammates },
					{ "Tagged", CFG::Outlines_Players_Ignore_Tagged }
					});

				CheckBox("Show Team Medics", CFG::Outlines_Players_Show_Teammate_Medics);
			}
			GroupBoxEnd();

			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;

			GroupBoxStart("Buildings", 150);
			{
				CheckBox("Active", CFG::Outlines_Buildings_Active);
				SliderFloat("Alpha", CFG::Outlines_Buildings_Alpha, 0.0f, 1.0f, 0.1f, "%.1f");

				multiselect("Ignore", BuildingIgnore, {
					{ "Local", CFG::Outlines_Buildings_Ignore_Local },
					{ "Enemies", CFG::Outlines_Buildings_Ignore_Enemies },
					{ "Teammates", CFG::Outlines_Buildings_Ignore_Teammates }
					});

				CheckBox("Show Team Dispensers", CFG::Outlines_Buildings_Show_Teammate_Dispensers);
			}
			GroupBoxEnd();
		}

		if (VisualsTab == EVisualsTabs::OTHER)
		{
			anchor_x = m_nCursorX;
			anchor_y = m_nCursorY;

			GroupBoxStart("Local", 150);
			{
				SliderFloat("Player FOV", CFG::Visuals_FOV_Override, 70.0f, 140.0f, 1.0f, "%.0f");
				
				// FOV Circle section
				CheckBox("Aimbot FOV Circle", CFG::Visuals_Aimbot_FOV_Circle);
				if (CFG::Visuals_Aimbot_FOV_Circle)
				{
					SliderFloat("FOV Circle Alpha", CFG::Visuals_Aimbot_FOV_Circle_Alpha, 0.01f, 1.0f, 0.01f, "%.2f");
					ColorPicker("FOV Circle Color", CFG::Visuals_Aimbot_FOV_Circle_Color);
					CheckBox("FOV Circle RGB", CFG::Visuals_Aimbot_FOV_Circle_RGB);
					if (CFG::Visuals_Aimbot_FOV_Circle_RGB)
					{
						SliderFloat("RGB Rate", CFG::Visuals_Aimbot_FOV_Circle_RGB_Rate, 0.5f, 10.0f, 0.5f, "%.1f");
						CheckBox("RGB Glow (Bloom)", CFG::Visuals_Aimbot_FOV_Circle_Glow);
						if (CFG::Visuals_Aimbot_FOV_Circle_Glow)
							SliderInt("Bloom Amount", CFG::Visuals_Aimbot_FOV_Circle_Bloom_Amount, 1, 10, 1);
					}
				}

				multiselect("Removals", LocalRemovals, {
					{ "Scope", CFG::Visuals_Remove_Scope },
					{ "Zoom", CFG::Visuals_Remove_Zoom },
					{ "Punch", CFG::Visuals_Remove_Punch },
					{ "Screen Overlay", CFG::Visuals_Remove_Screen_Overlay },
					{ "Screen Shake", CFG::Visuals_Remove_Screen_Shake },
					{ "Screen Fade", CFG::Visuals_Remove_Screen_Fade }
				});

				SelectSingle("Removals Mode", CFG::Visuals_Removals_Mode, {
					{ "Everyone", 0 },
					{ "Local Only", 1 }
					});

				SelectSingle("Tracer Effect", CFG::Visuals_Tracer_Type, {
					{ "Default", 0 },
					{ "C.A.P.P.E.R", 1 },
					{ "Machina (White)", 2 },
					{ "Machina (Team)", 3 },
					{ "Big Nasty", 4 },
					{ "Short Circuit", 5 },
					{ "Mrasmus Zap", 6 },
					{ "Random", 7 },
					{ "Random (No Zap)", 8 }
					});

				SelectSingle("Crit Tracer Effect", CFG::Visuals_Crit_Tracer_Type, {
					{ "Off", 0 },
					{ "C.A.P.P.E.R", 1 },
					{ "Machina (White)", 2 },
					{ "Machina (Team)", 3 },
					{ "Big Nasty", 4 },
					{ "Short Circuit", 5 },
					{ "Mrasmus Zap", 6 },
					{ "Random", 7 },
					{ "Random (No Zap)", 8 }
					});

				SelectSingle("Movement Sim", CFG::Visuals_Simulation_Movement_Style, {
					{ "Off", 0 },
					{ "Line", 1 },
					{ "Dashed", 3 },  // Spaced style = dashed lines
					{ "Arrows", 4 }
					});

				SelectSingle("Projectile Sim", CFG::Visuals_Simulation_Projectile_Style, {
					{ "Off", 0 },
					{ "Line", 1 },
					{ "Dashed", 3 },  // Spaced style = dashed lines
					{ "Arrows", 4 }
					});

				// Trajectory Preview (real-time projectile path before shooting)
				SelectSingle("Trajectory Style", CFG::Visuals_Trajectory_Preview_Style, {
					{ "Off", 0 },
					{ "Line", 1 },
					{ "Arrows", 4 }
					});
				CheckBox("Trajectory Impact Box", CFG::Visuals_Trajectory_Preview_Box);

			}
			GroupBoxEnd();

			GroupBoxStart("Chat", 150);
			{
				CheckBox("Teammate Votes", CFG::Visuals_Chat_Teammate_Votes);
				CheckBox("Enemy Votes", CFG::Visuals_Chat_Enemy_Votes);
				CheckBox("Player List Info", CFG::Visuals_Chat_Player_List_Info);
				CheckBox("Name Tags", CFG::Visuals_Chat_Name_Tags);
				CheckBox("Ban Alerts", CFG::Visuals_Chat_Ban_Alerts);
			}
			GroupBoxEnd();

			GroupBoxStart("Scoreboard", 150);
			{
				CheckBox("Reveal Scoreboard", CFG::Visuals_Reveal_Scoreboard);
				CheckBox("Scoreboard Colors", CFG::Visuals_Scoreboard_Utility);
			}
			GroupBoxEnd();

			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;

			GroupBoxStart("World", 150);
			{
				CheckBox("Flat Textures", CFG::Visuals_Flat_Textures);
				CheckBox("Disable Sky Fog", CFG::Visuals_Remove_Sky_Fog);
				CheckBox("Distance Prop Alpha", CFG::Visuals_Distance_Prop_Alpha);
				CheckBox("Don't Modulate Sky", CFG::Visuals_World_Modulation_No_Sky_Change);

				SelectSingle("World Modulation Mode", CFG::Visuals_World_Modulation_Mode,
				{
					{ "Night Mode", 0 },
					{ "Custom Color", 1 }
				});

				SliderFloat("Night Mode", CFG::Visuals_Night_Mode, 0.0f, 100.0f, 1.0f, "%.0f");

				SelectSingle("Particles Mode", CFG::Visuals_Particles_Mode, {
					{ "Original", 0 },
					{ "Custom Color", 1 },
					{ "Rainbow", 2 }
				});

				SliderFloat("Particles Rainbow Rate", CFG::Visuals_Particles_Rainbow_Rate, 1.0f, 10.0f, 1.0f, "%.0f");
			}
			GroupBoxEnd();

			GroupBoxStart("Spectator List", 150);
			{
				CheckBox("Active", CFG::Visuals_SpectatorList_Active);
				CheckBox("Avatars", CFG::Visuals_SpectatorList_Avatars);
				SliderFloat("Outline Alpha", CFG::Visuals_SpectatorList_Outline_Alpha, 0.1f, 1.0f, 0.1f, "%.1f");
				SliderFloat("Background Alpha", CFG::Visuals_SpectatorList_Background_Alpha, 0.1f, 1.0f, 0.1f, "%.1f");
				SliderInt("Width", CFG::Visuals_SpectatorList_Width, 200, 1000, 1);
			}
			GroupBoxEnd();

			GroupBoxStart("Weather Modifier", 150);
			{
				SelectSingle("Weather", CFG::Visuals_Weather, {
					{ "Off", 0 },
					{ "Rain", 1 },
					{ "Light Rain", 2 }
				});
			}
			GroupBoxEnd();

			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;

			GroupBoxStart("Thirdperson", 150);
			{
				CheckBox("Active", CFG::Visuals_Thirdperson_Active);
				InputKey("Toggle Key", CFG::Visuals_Thirdperson_Key);
				SliderFloat("Offset Forward", CFG::Visuals_Thirdperson_Offset_Forward, 10.0f, 200.0f, 1.0f, "%.0f");
				SliderFloat("Offset Right", CFG::Visuals_Thirdperson_Offset_Right, -50.0f, 50.0f, 1.0f, "%.0f");
				SliderFloat("Offset Up", CFG::Visuals_Thirdperson_Offset_Up, -50.0f, 50.0f, 1.0f, "%.0f");
			}
			GroupBoxEnd();

			GroupBoxStart("View Model", 150);
			{
				CheckBox("Active", CFG::Visuals_ViewModel_Active);
				CheckBox("Sway", CFG::Visuals_ViewModel_Sway);
				SliderFloat("Sway Scale", CFG::Visuals_ViewModel_Sway_Scale, 0.1f, 1.0f, 0.1f, "%.1f");
				SliderFloat("Offset Forward", CFG::Visuals_ViewModel_Offset_Forward, -50.00f, 50.0f, 1.0f, "%.0f");
				SliderFloat("Offset Right", CFG::Visuals_ViewModel_Offset_Right, -50.0f, 50.0f, 1.0f, "%.0f");
				SliderFloat("Offset Up", CFG::Visuals_ViewModel_Offset_Up, -50.0f, 50.0f, 1.0f, "%.0f");
				CheckBox("Minimal Viewmodel", CFG::Visuals_ViewModel_Minimal);
				CheckBox("Flip Viewmodel", CFG::Visuals_Viewmodel_Flip);
				CheckBox("World Model", CFG::Visuals_ViewModel_WorldModel);
			}
			GroupBoxEnd();

			GroupBoxStart("Anti Capture", 150);
			{
				CheckBox("Clean Screenshot", CFG::Misc_Clean_Screenshot);
			}
			GroupBoxEnd();
		}

		if (VisualsTab == EVisualsTabs::OTHER2)
		{
			anchor_x = m_nCursorX;
			anchor_y = m_nCursorY;

			GroupBoxStart("Performance", 150);
			{
				CheckBox("Disable Detail Props", CFG::Visuals_Disable_Detail_Props);
				CheckBox("Disable Ragdolls", CFG::Visuals_Disable_Ragdolls);
				CheckBox("Disable Wearables", CFG::Visuals_Disable_Wearables);
				CheckBox("Disable Post Processing", CFG::Visuals_Disable_Post_Processing);
				CheckBox("Disable Dropped Weapons", CFG::Visuals_Disable_Dropped_Weapons);
				CheckBox("Disable Interp", CFG::Visuals_Disable_Interp);
				CheckBox("Use Simple Models", CFG::Visuals_Simple_Models);
				CheckBox("Auto Interp for Guns", CFG::Visuals_Auto_Interp);
			}
			GroupBoxEnd();

			GroupBoxStart("Paint", 150);
			{
				CheckBox("Active", CFG::Visuals_Paint_Active);
				InputKey("Key", CFG::Visuals_Paint_Key);
				InputKey("Erase Key", CFG::Visuals_Paint_Erase_Key);
				const char *pszFmt = CFG::Visuals_Paint_LifeTime <= 0.0f ? "inf" : "%.0fs";
				SliderFloat("Life Time", CFG::Visuals_Paint_LifeTime, 0.0f, 10.0f, 1.0f, pszFmt);
				SliderInt("Bloom Amount", CFG::Visuals_Paint_Bloom_Amount, 3, 10, 1);
			}
			GroupBoxEnd();

			GroupBoxStart("Team Well-Being", 150);
			{
				CheckBox("Active", CFG::Visuals_TeamWellBeing_Active);
				CheckBox("Medic Only", CFG::Visuals_TeamWellBeing_Medic_Only);
				SliderFloat("Background Alpha", CFG::Visuals_TeamWellBeing_Background_Alpha, 0.1f, 1.0f, 0.1f, "%.1f");
				SliderInt("Width", CFG::Visuals_TeamWellBeing_Width, 200, 1000, 1);
			}
			GroupBoxEnd();

			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;

			GroupBoxStart("Spy Camera", 150);
			{
				CheckBox("Active", CFG::Visuals_SpyCamera_Active);
				SliderFloat("Background Alpha", CFG::Visuals_SpyCamera_Background_Alpha, 0.1f, 1.0f, 0.1f, "%.1f");
				SliderInt("Camera Width", CFG::Visuals_SpyCamera_Pos_W, 100, 600, 10);
				SliderInt("Camera Height", CFG::Visuals_SpyCamera_Pos_H, 100, 600, 10);
				SliderFloat("Camera FOV", CFG::Visuals_SpyCamera_FOV, 70.0f, 170.0f, 1.0f, "%.0f");
			}
			GroupBoxEnd();

			GroupBoxStart("Spy Warning", 150);
			{
				CheckBox("Active", CFG::Viuals_SpyWarning_Active);
				CheckBox("Announce", CFG::Viuals_SpyWarning_Announce);

				multiselect("Ignore", SpyWarningIgnore, {
					{ "Cloaked", CFG::Viuals_SpyWarning_Ignore_Cloaked },
					{ "Friends", CFG::Viuals_SpyWarning_Ignore_Friends },
					{ "Invisible", CFG::Viuals_SpyWarning_Ignore_Invisible }
				});
			}
			GroupBoxEnd();

			GroupBoxStart("Ragdolls", 150);
			{
				CheckBox("Active", CFG::Visuals_Ragdolls_Active);
				CheckBox("No Gib", CFG::Visuals_Ragdolls_No_Gib);
				CheckBox("No Death Animation", CFG::Visuals_Ragdolls_No_Death_Anim);

				SelectSingle("Effect", CFG::Visuals_Ragdolls_Effect, {
					{ "Default", 0 },
					{ "Burning", 1 },
					{ "Electrocuted", 2 },
					{ "Ash", 3 },
					{ "Gold", 4 },
					{ "Ice", 5 },
					{ "Dissolve", 6 },
					{ "Random", 7 }
					});

				SliderFloat("Force Multiplier", CFG::Visuals_Ragdolls_Force_Mult, 0.0f, 5.0f, 1.0f, "%.0f");
			}
			GroupBoxEnd();

			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;

			GroupBoxStart("Beams", 150);
			{
				CheckBox("Active", CFG::Visuals_Beams_Active);
				SliderFloat("Life Time", CFG::Visuals_Beams_LifeTime, 1.0f, 10.0f, 1.0f, "%.0fs");
				SliderFloat("Start Width", CFG::Visuals_Beams_Width, 1.0f, 10.0f, 1.0f, "%.0f");
				SliderFloat("End Width", CFG::Visuals_Beams_EndWidth, 1.0f, 10.0f, 1.0f, "%.0f");
				SliderFloat("Fade Length", CFG::Visuals_Beams_FadeLength, 1.0f, 10.0f, 1.0f, "%.0f");
				SliderFloat("Amplitude", CFG::Visuals_Beams_Amplitude, 0.0f, 10.0f, 0.1f, "%.1f");
				SliderFloat("Speed", CFG::Visuals_Beams_Speed, 0.0f, 10.0f, 1.0f, "%.0f");

				multiselect("Flags", BeamFlags, {
					{ "FBEAM_FADEIN", CFG::Visuals_Beams_Flag_FBEAM_FADEIN },
					{ "FBEAM_FADEOUT", CFG::Visuals_Beams_Flag_FBEAM_FADEOUT },
					{ "FBEAM_SINENOISE", CFG::Visuals_Beams_Flag_FBEAM_SINENOISE },
					{ "FBEAM_SOLID", CFG::Visuals_Beams_Flag_FBEAM_SOLID },
					{ "FBEAM_SHADEIN", CFG::Visuals_Beams_Flag_FBEAM_SHADEIN },
					{ "FBEAM_SHADEOUT", CFG::Visuals_Beams_Flag_FBEAM_SHADEOUT }
					});
			}
			GroupBoxEnd();
		}

		if (VisualsTab == EVisualsTabs::COLORS)
		{
			auto anchor_y{ m_nCursorY };

			GroupBoxStart("Menu Colors", 150);
			{
				ColorPicker("Accent Primary", CFG::Menu_Accent_Primary);
				ColorPicker("Accent Secondary", CFG::Menu_Accent_Secondary);
				ColorPicker("Background", CFG::Menu_Background);
				CheckBox("Menu Snow", CFG::Menu_Snow);
			}
			GroupBoxEnd();
			
			GroupBoxStart("Accent Secondary RGB", 150);
			{
				CheckBox("RGB Mode", CFG::Menu_Accent_Secondary_RGB);
				if (CFG::Menu_Accent_Secondary_RGB)
				{
					SliderFloat("RGB Rate", CFG::Menu_Accent_Secondary_RGB_Rate, 0.5f, 10.0f, 0.5f, "%.1f");
				}
			}
			GroupBoxEnd();

			GroupBoxStart("Visuals", 150);
			{
				ColorPicker("Hands", CFG::Color_Hands);
				ColorPicker("Hands Sheen", CFG::Color_Hands_Sheen);
				ColorPicker("Weapon", CFG::Color_Weapon);
				ColorPicker("Weapon Sheen", CFG::Color_Weapon_Sheen);
			}
			GroupBoxEnd();

			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;

			GroupBoxStart("Visuals", 150);
			{
				ColorPicker("Local", CFG::Color_Local);
				ColorPicker("Friend", CFG::Color_Friend);
				ColorPicker("Enemy", CFG::Color_Enemy);
				ColorPicker("Teammate", CFG::Color_Teammate);
				ColorPicker("Target", CFG::Color_Target);
				ColorPicker("Invulnerable", CFG::Color_Invulnerable);
				ColorPicker("Cheater", CFG::Color_Cheater);
				ColorPicker("Retard Legit", CFG::Color_RetardLegit);
				ColorPicker("F2P", CFG::Color_F2P);
				ColorPicker("Invisible", CFG::Color_Invisible);
				ColorPicker("Over Heal", CFG::Color_OverHeal);
				ColorPicker("Uber", CFG::Color_Uber);
				ColorPicker("Conds", CFG::Color_Conds);
				ColorPicker("Health Pack", CFG::Color_HealthPack);
				ColorPicker("Ammo Pack", CFG::Color_AmmoPack);
				ColorPicker("Beams", CFG::Color_Beams);
				ColorPicker("Halloween Gifts", CFG::Color_Halloween_Gift);
				ColorPicker("MVM Money", CFG::Color_MVM_Money);
				ColorPicker("Particles", CFG::Color_Particles);
				ColorPicker("World Modulation", CFG::Color_World);
				ColorPicker("Sky Modulation", CFG::Color_Sky);
				ColorPicker("Prop Modulation", CFG::Color_Props);
				ColorPicker("Movement Sim", CFG::Color_Simulation_Movement);
				ColorPicker("Projectile Sim", CFG::Color_Simulation_Projectile);
				ColorPicker("Trajectory", CFG::Color_Trajectory);
				ColorPicker("FakeLag", CFG::Color_FakeLag);
				ColorPicker("Fake Model", CFG::Color_FakeModel);
				ColorPicker("Lag Record", CFG::Color_LagRecord);
			}
			GroupBoxEnd();

			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;

			GroupBoxStart("Party Colors", 150);
			{
				ColorPicker("Party 1 (Local)", CFG::Color_Party_1);
				ColorPicker("Party 2", CFG::Color_Party_2);
				ColorPicker("Party 3", CFG::Color_Party_3);
				ColorPicker("Party 4", CFG::Color_Party_4);
				ColorPicker("Party 5", CFG::Color_Party_5);
				ColorPicker("Party 6", CFG::Color_Party_6);
				ColorPicker("Party 7", CFG::Color_Party_7);
				ColorPicker("Party 8", CFG::Color_Party_8);
				ColorPicker("Party 9", CFG::Color_Party_9);
				ColorPicker("Party 10", CFG::Color_Party_10);
				ColorPicker("Party 11", CFG::Color_Party_11);
				ColorPicker("Party 12", CFG::Color_Party_12);
			}
			GroupBoxEnd();
		}
	}

	if (MainTab == EMainTabs::EXPLOITS)
	{
		int nContentX = m_nCursorX;
		int nContentY = m_nCursorY;
		int nContentW = CFG::Menu_Width - (CFG::Menu_Spacing_X * 4);
		int nContentH = CFG::Menu_Height - m_nCursorY - CFG::Menu_Spacing_Y * 2;

		// Initialize GroupBoxes with saved positions from config
		auto LoadGroupBoxPosition = [](int configVal) -> std::pair<EGroupBoxColumn, int> {
			int col = configVal / 100;
			int order = configVal % 100;
			return { static_cast<EGroupBoxColumn>(std::clamp(col, 0, 2)), order };
		};

		// Register Exploits tab GroupBoxes (only once)
		static bool bExploitsInitialized = false;
		if (!bExploitsInitialized)
		{
			auto [col1, ord1] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_Shifting);
			auto [col2, ord2] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_FakeLag);
			auto [col3, ord3] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_Crits);
			auto [col4, ord4] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_NoSpread);
			auto [col5, ord5] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_RegionSelector);
			auto [col6, ord6] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_AntiAim);

			// Sizes: Shifting=Medium, FakeLag=Small, Crithack=Small, NoSpread=ExtraSmall, RegionSelector=Medium, AntiAim=Big
			RegisterGroupBox("Exploits", "Shifting", col1, ord1, 150, EGroupBoxSize::MEDIUM);
			RegisterGroupBox("Exploits", "FakeLag", col2, ord2, 150, EGroupBoxSize::SMALL);
			RegisterGroupBox("Exploits", "AntiAim", col6, ord6, 145, EGroupBoxSize::BIG);
			RegisterGroupBox("Exploits", "Crithack", col3, ord3, 150, EGroupBoxSize::SMALL);
			RegisterGroupBox("Exploits", "No Spread", col4, ord4, 150, EGroupBoxSize::EXTRA_SMALL);
			RegisterGroupBox("Exploits", "Region Selector", col5, ord5, 150, EGroupBoxSize::MEDIUM);
			bExploitsInitialized = true;
		}
		
		// Update GroupBox positions from config (in case config was loaded)
		{
			auto [col1, ord1] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_Shifting);
			auto [col2, ord2] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_FakeLag);
			auto [col3, ord3] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_Crits);
			auto [col4, ord4] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_NoSpread);
			auto [col5, ord5] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_RegionSelector);
			auto [col6, ord6] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_AntiAim);
			
			if (m_mapGroupBoxes.find("Exploits_Shifting") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["Exploits_Shifting"].m_nColumn = col1;
				m_mapGroupBoxes["Exploits_Shifting"].m_nOrderInColumn = ord1;
			}
			if (m_mapGroupBoxes.find("Exploits_FakeLag") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["Exploits_FakeLag"].m_nColumn = col2;
				m_mapGroupBoxes["Exploits_FakeLag"].m_nOrderInColumn = ord2;
			}
			if (m_mapGroupBoxes.find("Exploits_AntiAim") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["Exploits_AntiAim"].m_nColumn = col6;
				m_mapGroupBoxes["Exploits_AntiAim"].m_nOrderInColumn = ord6;
			}
			if (m_mapGroupBoxes.find("Exploits_Crithack") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["Exploits_Crithack"].m_nColumn = col3;
				m_mapGroupBoxes["Exploits_Crithack"].m_nOrderInColumn = ord3;
			}
			if (m_mapGroupBoxes.find("Exploits_No Spread") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["Exploits_No Spread"].m_nColumn = col4;
				m_mapGroupBoxes["Exploits_No Spread"].m_nOrderInColumn = ord4;
			}
			if (m_mapGroupBoxes.find("Exploits_Region Selector") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["Exploits_Region Selector"].m_nColumn = col5;
				m_mapGroupBoxes["Exploits_Region Selector"].m_nOrderInColumn = ord5;
			}
		}

		// Set up render functions for each GroupBox
		m_mapGroupBoxes["Exploits_Shifting"].m_fnRenderContent = [this]() {
			InputKey("Recharge Key", CFG::Exploits_Shifting_Recharge_Key);
			InputKey("Double Tap Key", CFG::Exploits_RapidFire_Key);
			const int nMaxDTTicks = CFG::Misc_AntiCheat_Enabled ? 8 : MAX_COMMANDS;
			SliderInt(CFG::Misc_AntiCheat_Enabled ? "Safe Double Tap Ticks" : "Double Tap Ticks", 
				CFG::Exploits_RapidFire_Ticks, 2, nMaxDTTicks, 1);
			SliderInt("Double Tap Delay Ticks", CFG::Exploits_RapidFire_Min_Ticks_Target_Same, 0, 5, 1);
			CheckBox("Double Tap Antiwarp", CFG::Exploits_RapidFire_Antiwarp);
			InputKey("Warp Key", CFG::Exploits_Warp_Key);
			SelectSingle("Warp Mode", CFG::Exploits_Warp_Mode, {
				{ "Slow", 0 }, { "Full", 1 }
			});
			SelectSingle("Warp Exploit (for 'Full')", CFG::Exploits_Warp_Exploit, {
				{ "None", 0 }, { "Fake Peek", 1 }, { "0 Velocity", 2 }
			});
			CheckBox("Draw Indicator", CFG::Exploits_Shifting_Draw_Indicator);
		};

		m_mapGroupBoxes["Exploits_FakeLag"].m_fnRenderContent = [this]() {
			CheckBox("Enabled (Adaptive)", CFG::Exploits_FakeLag_Enabled);
			const int nMaxFakeLagTicks = CFG::Misc_AntiCheat_Enabled ? 8 : 21;
			SliderInt(CFG::Misc_AntiCheat_Enabled ? "Safe Max Ticks" : "Max Ticks", 
				CFG::Exploits_FakeLag_Max_Ticks, 1, nMaxFakeLagTicks, 1);
			CheckBox("Only When Moving", CFG::Exploits_FakeLag_Only_Moving);
			CheckBox("Activate on Sightline", CFG::Exploits_FakeLag_Activate_On_Sightline);
		};

		m_mapGroupBoxes["Exploits_AntiAim"].m_fnRenderContent = [this]() {
			CheckBox("Enabled", CFG::Exploits_AntiAim_Enabled);
			CheckBox("Legit AA", CFG::Exploits_LegitAA_Enabled);
			
			if (!CFG::Exploits_LegitAA_Enabled)
			{
				SelectSingle("Real Pitch", CFG::Exploits_AntiAim_PitchReal, {
					{ "None", 0 },
					{ "Up", 1 },
					{ "Down", 2 },
					{ "Zero", 3 },
					{ "Jitter", 4 },
					{ "Reverse Jitter", 5 }
				});
				SelectSingle("Fake Pitch", CFG::Exploits_AntiAim_PitchFake, {
					{ "None", 0 },
					{ "Up", 1 },
					{ "Down", 2 },
					{ "Jitter", 3 },
					{ "Reverse Jitter", 4 }
				});
				SelectSingle("Real Yaw", CFG::Exploits_AntiAim_YawReal, {
					{ "Forward", 0 },
					{ "Left", 1 },
					{ "Right", 2 },
					{ "Backwards", 3 },
					{ "Edge", 4 },
					{ "Jitter", 5 },
					{ "Spin", 6 }
				});
				SelectSingle("Fake Yaw", CFG::Exploits_AntiAim_YawFake, {
					{ "Forward", 0 },
					{ "Left", 1 },
					{ "Right", 2 },
					{ "Backwards", 3 },
					{ "Edge", 4 },
					{ "Jitter", 5 },
					{ "Spin", 6 }
				});
				SelectSingle("Real Base", CFG::Exploits_AntiAim_RealYawBase, {
					{ "View", 0 },
					{ "Target", 1 }
				});
				SelectSingle("Fake Base", CFG::Exploits_AntiAim_FakeYawBase, {
					{ "View", 0 },
					{ "Target", 1 }
				});
				SliderFloat("Real Offset", CFG::Exploits_AntiAim_RealYawOffset, -180.0f, 180.0f, 5.0f, "%.0f");
				SliderFloat("Fake Offset", CFG::Exploits_AntiAim_FakeYawOffset, -180.0f, 180.0f, 5.0f, "%.0f");
				// Show yaw value sliders only for Edge/Jitter modes
				if (CFG::Exploits_AntiAim_YawReal == 4 || CFG::Exploits_AntiAim_YawReal == 5)
					SliderFloat("Real Value", CFG::Exploits_AntiAim_RealYawValue, -180.0f, 180.0f, 5.0f, "%.0f");
				if (CFG::Exploits_AntiAim_YawFake == 4 || CFG::Exploits_AntiAim_YawFake == 5)
					SliderFloat("Fake Value", CFG::Exploits_AntiAim_FakeYawValue, -180.0f, 180.0f, 5.0f, "%.0f");
				// Show spin speed only for Spin mode
				if (CFG::Exploits_AntiAim_YawReal == 6 || CFG::Exploits_AntiAim_YawFake == 6)
					SliderFloat("Spin Speed", CFG::Exploits_AntiAim_SpinSpeed, -30.0f, 30.0f, 1.0f, "%.0f");
				CheckBox("Anti-Overlap", CFG::Exploits_AntiAim_AntiOverlap);
				CheckBox("Hide Pitch on Shot", CFG::Exploits_AntiAim_InvalidShootPitch);
			}
			
			CheckBox("Min Walk", CFG::Exploits_AntiAim_MinWalk);
		};

		m_mapGroupBoxes["Exploits_Crithack"].m_fnRenderContent = [this]() {
			InputKey("Key", CFG::Exploits_Crits_Force_Crit_Key);
			InputKey("Melee Key", CFG::Exploits_Crits_Force_Crit_Key_Melee);
			CheckBox("Skip Random Crits", CFG::Exploits_Crits_Skip_Random_Crits);
			CheckBox("Crit Indicator", CFG::Visuals_Crit_Indicator);
			if (CFG::Visuals_Crit_Indicator)
			{
				SliderInt("Bar Width", CFG::Visuals_Crit_Indicator_Width, 100, 300, 10);
				SliderInt("Bar Height", CFG::Visuals_Crit_Indicator_Height, 8, 32, 2);
				CheckBox("Indicator Debug", CFG::Visuals_Crit_Indicator_Debug);
			}
		};

		m_mapGroupBoxes["Exploits_No Spread"].m_fnRenderContent = [this]() {
			CheckBox("Active", CFG::Exploits_SeedPred_Active);
			CheckBox("Draw Indicator", CFG::Exploits_SeedPred_DrawIndicator);
		};

		m_mapGroupBoxes["Exploits_Region Selector"].m_fnRenderContent = [this]() {
			CheckBox("Active", CFG::Exploits_Region_Selector_Active);
			if (CFG::Exploits_Region_Selector_Active)
			{
				multiselect("NA Regions", RegionsNA, {
					{ "Atlanta", CFG::Exploits_Region_ATL },
					{ "Chicago", CFG::Exploits_Region_ORD },
					{ "Dallas", CFG::Exploits_Region_DFW },
					{ "Los Angeles", CFG::Exploits_Region_LAX },
					{ "Seattle", CFG::Exploits_Region_SEA },
					{ "Virginia", CFG::Exploits_Region_IAD }
				});
				multiselect("EU Regions", RegionsEU, {
					{ "Amsterdam", CFG::Exploits_Region_AMS },
					{ "Frankfurt", CFG::Exploits_Region_FRA },
					{ "Helsinki", CFG::Exploits_Region_HEL },
					{ "London", CFG::Exploits_Region_LHR },
					{ "Madrid", CFG::Exploits_Region_MAD },
					{ "Paris", CFG::Exploits_Region_PAR },
					{ "Stockholm", CFG::Exploits_Region_STO },
					{ "Vienna", CFG::Exploits_Region_VIE },
					{ "Warsaw", CFG::Exploits_Region_WAW }
				});
				multiselect("SA Regions", RegionsSA, {
					{ "Buenos Aires", CFG::Exploits_Region_EZE },
					{ "Lima", CFG::Exploits_Region_LIM },
					{ "Santiago", CFG::Exploits_Region_SCL },
					{ "Sao Paulo", CFG::Exploits_Region_GRU }
				});
				multiselect("Asia Regions", RegionsAsia, {
					{ "Chennai", CFG::Exploits_Region_MAA },
					{ "Dubai", CFG::Exploits_Region_DXB },
					{ "Hong Kong", CFG::Exploits_Region_HKG },
					{ "Mumbai", CFG::Exploits_Region_BOM },
					{ "Seoul", CFG::Exploits_Region_SEO },
					{ "Singapore", CFG::Exploits_Region_SGP },
					{ "Tokyo", CFG::Exploits_Region_TYO }
				});
				multiselect("Other Regions", RegionsOther, {
					{ "Sydney", CFG::Exploits_Region_SYD },
					{ "Johannesburg", CFG::Exploits_Region_JNB }
				});
			}
		};

		// Render all draggable GroupBoxes
		RenderDraggableGroupBoxes("Exploits", nContentX, nContentY, nContentW, nContentH);
	}
	
	if (MainTab == EMainTabs::MISC)
	{
		int nContentX = m_nCursorX;
		int nContentY = m_nCursorY;
		int nContentW = CFG::Menu_Width - (CFG::Menu_Spacing_X * 4);
		int nContentH = CFG::Menu_Height - m_nCursorY - CFG::Menu_Spacing_Y * 2;

		// Initialize GroupBoxes with saved positions from config
		auto LoadGroupBoxPosition = [](int configVal) -> std::pair<EGroupBoxColumn, int> {
			int col = configVal / 100;
			int order = configVal % 100;
			return { static_cast<EGroupBoxColumn>(std::clamp(col, 0, 2)), order };
		};

		// Register Misc tab GroupBoxes (only once)
		static bool bMiscInitialized = false;
		if (!bMiscInitialized)
		{
			auto [col1, ord1] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_Misc);
			auto [col2, ord2] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_Game);
			auto [col3, ord3] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_MvM);
			auto [col4, ord4] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_Chat);
			auto [col5, ord5] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_Taunt);
			auto [col6, ord6] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_Auto);
			auto [col7, ord7] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_Movement);

			RegisterGroupBox("Misc", "Misc", col1, ord1, 160);
			RegisterGroupBox("Misc", "Game", col2, ord2, 160);
			RegisterGroupBox("Misc", "Mann vs. Machine", col3, ord3, 160);
			RegisterGroupBox("Misc", "Chat", col4, ord4, 160);
			RegisterGroupBox("Misc", "Taunt", col5, ord5, 150);
			RegisterGroupBox("Misc", "Auto", col6, ord6, 150);
			RegisterGroupBox("Misc", "Movement", col7, ord7, 160);
			bMiscInitialized = true;
		}
		
		// Update GroupBox positions from config (in case config was loaded)
		{
			auto [col1, ord1] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_Misc);
			auto [col2, ord2] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_Game);
			auto [col3, ord3] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_MvM);
			auto [col4, ord4] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_Chat);
			auto [col5, ord5] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_Taunt);
			auto [col6, ord6] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_Auto);
			auto [col7, ord7] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_Movement);
			
			if (m_mapGroupBoxes.find("Misc_Misc") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["Misc_Misc"].m_nColumn = col1;
				m_mapGroupBoxes["Misc_Misc"].m_nOrderInColumn = ord1;
			}
			if (m_mapGroupBoxes.find("Misc_Game") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["Misc_Game"].m_nColumn = col2;
				m_mapGroupBoxes["Misc_Game"].m_nOrderInColumn = ord2;
			}
			if (m_mapGroupBoxes.find("Misc_Mann vs. Machine") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["Misc_Mann vs. Machine"].m_nColumn = col3;
				m_mapGroupBoxes["Misc_Mann vs. Machine"].m_nOrderInColumn = ord3;
			}
			if (m_mapGroupBoxes.find("Misc_Chat") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["Misc_Chat"].m_nColumn = col4;
				m_mapGroupBoxes["Misc_Chat"].m_nOrderInColumn = ord4;
			}
			if (m_mapGroupBoxes.find("Misc_Taunt") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["Misc_Taunt"].m_nColumn = col5;
				m_mapGroupBoxes["Misc_Taunt"].m_nOrderInColumn = ord5;
			}
			if (m_mapGroupBoxes.find("Misc_Auto") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["Misc_Auto"].m_nColumn = col6;
				m_mapGroupBoxes["Misc_Auto"].m_nOrderInColumn = ord6;
			}
			if (m_mapGroupBoxes.find("Misc_Movement") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["Misc_Movement"].m_nColumn = col7;
				m_mapGroupBoxes["Misc_Movement"].m_nOrderInColumn = ord7;
			}
		}

		// Set up render functions for each GroupBox
		m_mapGroupBoxes["Misc_Misc"].m_fnRenderContent = [this]() {
			CheckBox("Bypass sv_pure", CFG::Misc_Pure_Bypass);
			CheckBox("Noise Maker Spam", CFG::Misc_NoiseMaker_Spam);
			CheckBox("No Push", CFG::Misc_No_Push);
			CheckBox("Giant Weapon Sounds", CFG::Misc_MVM_Giant_Weapon_Sounds);
			CheckBox("Equip Region Unlock", CFG::Misc_Equip_Region_Unlock);
			CheckBox("Backpack Expander", CFG::Misc_Backpack_Expander);
			CheckBox("Shield Turn Rate", CFG::Misc_Shield_Turn_Rate);
			CheckBox("Anti Server Angle Change", CFG::Misc_Prevent_Server_Angle_Change);
			CheckBox("Freeze Queue", CFG::Misc_Freeze_Queue);
			CheckBox("Anti-AFK", CFG::Misc_Anti_AFK);
			if (Button("Unlock CVars"))
			{
				auto iter = ICvar::Iterator(I::CVar);
				for (iter.SetFirst(); iter.IsValid(); iter.Next())
				{
					auto cmd = iter.Get();
					if (!cmd) continue;
					if (cmd->m_nFlags & FCVAR_DEVELOPMENTONLY) cmd->m_nFlags &= ~FCVAR_DEVELOPMENTONLY;
					if (cmd->m_nFlags & FCVAR_HIDDEN) cmd->m_nFlags &= ~FCVAR_HIDDEN;
					if (cmd->m_nFlags & FCVAR_PROTECTED) cmd->m_nFlags &= ~FCVAR_PROTECTED;
					if (cmd->m_nFlags & FCVAR_CHEAT) cmd->m_nFlags &= ~FCVAR_CHEAT;
				}
			}
		};
		
		m_mapGroupBoxes["Misc_Movement"].m_fnRenderContent = [this]() {
			CheckBox("Bunnyhop", CFG::Misc_Bunnyhop);
			CheckBox("Choke on Bunnyhop", CFG::Misc_Choke_On_Bhop);
			CheckBox("Fast Stop", CFG::Misc_Fast_Stop);
			CheckBox("Fast Accelerate", CFG::Misc_Fast_Accelerate);
			CheckBox("Duck Speed", CFG::Misc_Duck_Speed);
			CheckBox("Crouch While Airborne", CFG::Misc_Crouch_While_Airborne);
			CheckBox("Auto Strafe", CFG::Misc_Auto_Strafe);
			if (CFG::Misc_Auto_Strafe) {
				CheckBox("Avoid Walls", CFG::Misc_Auto_Strafe_Avoid_Walls);
				SliderFloat("Turn Scale", CFG::Misc_Auto_Strafe_Turn_Scale, 0.0f, 1.0f, 0.1f, "%.1f");
				SliderFloat("Max Delta", CFG::Misc_Auto_Strafe_Max_Delta, 0.0f, 180.0f, 5.0f, "%.0f");
			}
			InputKey("Edge Jump Key", CFG::Misc_Edge_Jump_Key);
			InputKey("Undo Glue Key", CFG::Misc_Movement_Lock_Key);
			InputKey("Auto RJ Key", CFG::Misc_Auto_Rocket_Jump_Key);
			SelectSingle("Auto RJ Mode", CFG::Misc_Auto_Rocket_Jump_Mode, {
				{ "High", 0 }, { "Forward", 1 }, { "Dynamic", 2 }
			});
			InputKey("Auto FaN Key", CFG::Misc_AutoFaN_Key);
		};

		m_mapGroupBoxes["Misc_Game"].m_fnRenderContent = [this]() {
			CheckBox("Network Fix", CFG::Misc_Ping_Reducer);
			CheckBox("Ping Reducer", CFG::Misc_Ping_Reducer_Active);
			if (CFG::Misc_Ping_Reducer_Active)
				SliderFloat("cl_cmdrate", CFG::Misc_Ping_Reducer_Value, 0.5f, 3.0f, 0.01f, "%.2f");
			CheckBox("Prediction Error Jitter Fix", CFG::Misc_Pred_Error_Jitter_Fix);
			CheckBox("ComputeLightingOrigin Fix", CFG::Misc_ComputeLightingOrigin_Fix);
			CheckBox("SetupBones Optimization", CFG::Misc_SetupBones_Optimization);
			CheckBox("Anti-Cheat Compatibility", CFG::Misc_AntiCheat_Enabled);
			if (CFG::Misc_AntiCheat_Enabled)
				CheckBox("Skip Crit Detection", CFG::Misc_AntiCheat_SkipCritDetection);
			if (Button("Fix Invisible Players"))
			{
				// Record and stop demo to refresh client-side state
				I::EngineClient->ClientCmd_Unrestricted("record fix; stop");
			}
		};

		m_mapGroupBoxes["Misc_Mann vs. Machine"].m_fnRenderContent = [this]() {
			InputKey("Instant Respawn", CFG::Misc_MVM_Instant_Respawn_Key);
			CheckBox("Instant Revive", CFG::Misc_MVM_Instant_Revive);
		};

		m_mapGroupBoxes["Misc_Chat"].m_fnRenderContent = [this]() {
			CheckBox("Chat Spammer", CFG::Misc_Chat_Spammer_Active);
			if (CFG::Misc_Chat_Spammer_Active)
			{
				InputText("Spam Text", "Enter message:", CFG::Misc_Chat_Spammer_Text);
				SliderFloat("Interval", CFG::Misc_Chat_Spammer_Interval, 0.1f, 10.0f, 0.1f, "%.1fs");
			}
			CheckBox("Killsay", CFG::Misc_Chat_Killsay_Active);
			if (CFG::Misc_Chat_Killsay_Active)
			{
				InputText("Killsay Text", "Enter message:", CFG::Misc_Chat_Killsay_Text);
			}
		};

		m_mapGroupBoxes["Misc_Taunt"].m_fnRenderContent = [this]() {
			CheckBox("Taunt Slide", CFG::Misc_Taunt_Slide);
			CheckBox("Taunt Control", CFG::Misc_Taunt_Slide_Control);
			InputKey("Taunt Spin Key", CFG::Misc_Taunt_Spin_Key);
			SliderFloat("Taunt Spin Speed", CFG::Misc_Taunt_Spin_Speed, -50.0f, 50.0f, 1.0f, "%.0f");
			CheckBox("Taunt Spin Sine", CFG::Misc_Taunt_Spin_Sine);
			CheckBox("Fake Taunt", CFG::Misc_Fake_Taunt);
		};

		m_mapGroupBoxes["Misc_Auto"].m_fnRenderContent = [this]() {
			CheckBox("Auto Casual Queue", CFG::Misc_Auto_Queue);
			CheckBox("Auto Disguise", CFG::Misc_Auto_Disguise);
			CheckBox("Auto Call Medic", CFG::Misc_Auto_Call_Medic_On_Damage);
			if (CFG::Misc_Auto_Call_Medic_On_Damage) {
				CheckBox("Call Medic Low HP", CFG::Misc_Auto_Call_Medic_Low_HP);
				if (CFG::Misc_Auto_Call_Medic_Low_HP) {
					SelectSingle("Class", CFG::Misc_Auto_Call_Medic_Low_HP_Class, {
						{ "Scout", 0 }, { "Soldier", 1 }, { "Pyro", 2 }, { "Demoman", 3 },
						{ "Heavy", 4 }, { "Engineer", 5 }, { "Sniper", 6 }, { "Spy", 7 }, { "Medic", 8 }
					});
					switch (CFG::Misc_Auto_Call_Medic_Low_HP_Class) {
						case 0: SliderInt("HP Threshold", CFG::Misc_Auto_Call_Medic_HP_Scout, 10, 125, 5); break;
						case 1: SliderInt("HP Threshold", CFG::Misc_Auto_Call_Medic_HP_Soldier, 10, 220, 5); break;
						case 2: SliderInt("HP Threshold", CFG::Misc_Auto_Call_Medic_HP_Pyro, 10, 175, 5); break;
						case 3: SliderInt("HP Threshold", CFG::Misc_Auto_Call_Medic_HP_Demoman, 10, 175, 5); break;
						case 4: SliderInt("HP Threshold", CFG::Misc_Auto_Call_Medic_HP_Heavy, 10, 350, 5); break;
						case 5: SliderInt("HP Threshold", CFG::Misc_Auto_Call_Medic_HP_Engineer, 10, 150, 5); break;
						case 6: SliderInt("HP Threshold", CFG::Misc_Auto_Call_Medic_HP_Sniper, 10, 125, 5); break;
						case 7: SliderInt("HP Threshold", CFG::Misc_Auto_Call_Medic_HP_Spy, 10, 125, 5); break;
						case 8: SliderInt("HP Threshold", CFG::Misc_Auto_Call_Medic_HP_Medic, 10, 150, 5); break;
					}
				}
			}
			CheckBox("Projectile Dodge", CFG::Misc_Projectile_Dodge_Enabled);
			CheckBox("PD Only Warp", CFG::Misc_Projectile_Dodge_Only_Warp);
			CheckBox("PD Use Warp", CFG::Misc_Projectile_Dodge_Use_Warp);
			CheckBox("Disable DT While Airborne", CFG::Misc_Projectile_Dodge_Disable_DT_Airborne);
		};

		m_mapGroupBoxes["Misc_Triggerbot"].m_fnRenderContent = [this]() {
			CheckBox("Master Switch", CFG::Triggerbot_Active);
			InputKey("Triggerbot Key", CFG::Triggerbot_Key);
			CheckBox("Auto Vaccinator", CFG::Triggerbot_AutoVaccinator_Active);
			if (CFG::Triggerbot_AutoVaccinator_Active) {
				CheckBox("Always On", CFG::Triggerbot_AutoVaccinator_Always_On);
				if (CFG::Triggerbot_AutoVaccinator_Always_On)
					CheckBox("No Pop (Cycle Only)", CFG::Triggerbot_AutoVaccinator_NoPop);
				SelectSingle("Pop For", CFG::Triggerbot_AutoVaccinator_Pop, {
					{ "Everyone", 0 }, { "Friends Only", 1 }
				});
			}
		};

		// Render all draggable GroupBoxes
		RenderDraggableGroupBoxes("Misc", nContentX, nContentY, nContentW, nContentH);
	}

	if (MainTab == EMainTabs::PLAYERS)
	{
		m_nCursorX += CFG::Menu_Spacing_X;

		// Helper function to get party color
		auto GetPartyColor = [](int nPartyIndex) -> Color_t
		{
			switch (nPartyIndex)
			{
			case 1: return CFG::Color_Party_1;
			case 2: return CFG::Color_Party_2;
			case 3: return CFG::Color_Party_3;
			case 4: return CFG::Color_Party_4;
			case 5: return CFG::Color_Party_5;
			case 6: return CFG::Color_Party_6;
			case 7: return CFG::Color_Party_7;
			case 8: return CFG::Color_Party_8;
			case 9: return CFG::Color_Party_9;
			case 10: return CFG::Color_Party_10;
			case 11: return CFG::Color_Party_11;
			case 12: return CFG::Color_Party_12;
			default: return CFG::Menu_Text_Inactive;
			}
		};

		if (I::EngineClient->IsConnected())
		{
			const bool bShowAvatars = I::SteamFriends && I::SteamUtils;
			const int nAvatarSize = 24;
			const int nRowHeight = nAvatarSize + CFG::Menu_Spacing_Y;
			const int nPlayersPerPage = 20;

			// Collect all valid players first
			static std::vector<int> vecPlayerIndices;
			vecPlayerIndices.clear();

			for (auto n{ 1 }; n < I::EngineClient->GetMaxClients() + 1; n++)
			{
				if (n == I::EngineClient->GetLocalPlayer())
					continue;

				player_info_t player_info{};
				if (!I::EngineClient->GetPlayerInfo(n, &player_info) || player_info.fakeplayer)
					continue;

				vecPlayerIndices.push_back(n);
			}

			const int nTotalPlayers = static_cast<int>(vecPlayerIndices.size());
			const int nTotalPages = (nTotalPlayers + nPlayersPerPage - 1) / nPlayersPerPage;

			// Page selection state
			static int nCurrentPage = 0;

			// Reset page if out of bounds
			if (nCurrentPage >= nTotalPages)
				nCurrentPage = 0;

			// Draw page buttons if more than one page
			if (nTotalPages > 1)
			{
				auto bx = m_nCursorX;
				auto by = m_nCursorY;

				for (int nPage = 0; nPage < nTotalPages; nPage++)
				{
					char szPageLabel[16];
					snprintf(szPageLabel, sizeof(szPageLabel), "%d", nPage + 1);

					bool bIsCurrentPage = (nPage == nCurrentPage);
					if (Button(szPageLabel, bIsCurrentPage, 25))
					{
						nCurrentPage = nPage;
					}

					m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
					m_nCursorY = by;
				}

				m_nCursorX = bx;
				m_nCursorY = by + H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y * 2;
			}

			// Calculate range for current page
			const int nStartIdx = nCurrentPage * nPlayersPerPage;
			const int nEndIdx = std::min(nStartIdx + nPlayersPerPage, nTotalPlayers);

			// Get local player team for comparison
			auto pResource = GetTFPlayerResource();
			const int nLocalTeam = pResource ? pResource->GetTeam(I::EngineClient->GetLocalPlayer()) : 0;

			// Render players for current page
			for (int i = nStartIdx; i < nEndIdx; i++)
			{
				int n = vecPlayerIndices[i];

				player_info_t player_info{};
				if (!I::EngineClient->GetPlayerInfo(n, &player_info))
					continue;

				PlayerPriority custom_info{};
				F::Players->GetInfo(n, custom_info);

				// Get F2P and party info
				bool bIsF2P = H::Entities->IsF2P(n);
				int nPartyIndex = H::Entities->GetPartyIndex(n);

				auto bx{ m_nCursorX };
				auto by{ m_nCursorY };

				// Draw avatar
				if (bShowAvatars && player_info.friendsID != 0)
				{
					H::Draw->Avatar(m_nCursorX, m_nCursorY, nAvatarSize, nAvatarSize, static_cast<uint32_t>(player_info.friendsID));
				}

				// Move cursor right for name (after avatar)
				m_nCursorX += nAvatarSize + CFG::Menu_Spacing_X;
				m_nCursorY = by + (nAvatarSize - H::Fonts->Get(EFonts::Menu).m_nTall - CFG::Menu_Spacing_Y) / 2;

				// Determine name color based on priority first, then team
				Color_t nameColor = CFG::Menu_Text_Inactive;
				if (custom_info.Ignored)
					nameColor = CFG::Color_Friend;
				else if (custom_info.Cheater)
					nameColor = CFG::Color_Cheater;
				else if (custom_info.RetardLegit)
					nameColor = CFG::Color_RetardLegit;
				else if (pResource)
				{
					// Use team color if no priority set
					int nPlayerTeam = pResource->GetTeam(n);
					if (nPlayerTeam == nLocalTeam)
						nameColor = CFG::Color_Teammate;
					else
						nameColor = CFG::Color_Enemy;
				}

				// Click on name to view player details
				if (playerListButton(Utils::ConvertUtf8ToWide(player_info.name).c_str(), 150, nameColor, false))
				{
					nSelectedPlayerIndex = n;
					MainTab = EMainTabs::PLAYER_DETAILS;
					// Dismiss sourcebans and Vorobey alerts when viewing profile
					uint64_t steamID64 = static_cast<uint64_t>(player_info.friendsID) + 0x0110000100000000ULL;
					DismissSourcebansAlert(steamID64);
					DismissVerobayAlert(steamID64);
				}

				m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
				m_nCursorY = by + (nAvatarSize - H::Fonts->Get(EFonts::Menu).m_nTall - CFG::Menu_Spacing_Y) / 2;

				// F2P indicator
				if (bIsF2P)
				{
					playerListButton(L"F2P", 30, CFG::Color_F2P, true);
				}
				else
				{
					playerListButton(L"-", 30, CFG::Menu_Text_Disabled, true);
				}

				m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
				m_nCursorY = by + (nAvatarSize - H::Fonts->Get(EFonts::Menu).m_nTall - CFG::Menu_Spacing_Y) / 2;

				// Party indicator with color
				if (nPartyIndex > 0)
				{
					wchar_t partyLabel[8];
					swprintf_s(partyLabel, L"P%d", nPartyIndex);
					playerListButton(partyLabel, 30, GetPartyColor(nPartyIndex), true);
				}
				else
				{
					playerListButton(L"-", 30, CFG::Menu_Text_Disabled, true);
				}

				m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
				m_nCursorY = by + (nAvatarSize - H::Fonts->Get(EFonts::Menu).m_nTall - CFG::Menu_Spacing_Y) / 2;

				if (playerListButton(L"ignored", 60, custom_info.Ignored ? CFG::Color_Friend : CFG::Menu_Text_Inactive, true))
				{
					F::Players->Mark(n, { !custom_info.Ignored, false });
				}

				m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
				m_nCursorY = by + (nAvatarSize - H::Fonts->Get(EFonts::Menu).m_nTall - CFG::Menu_Spacing_Y) / 2;

				if (playerListButton(L"cheater", 60, custom_info.Cheater ? CFG::Color_Cheater : CFG::Menu_Text_Inactive, true))
				{
					F::Players->Mark(n, { false, !custom_info.Cheater });
				}

				m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
				m_nCursorY = by + (nAvatarSize - H::Fonts->Get(EFonts::Menu).m_nTall - CFG::Menu_Spacing_Y) / 2;

				if (playerListButton(L"retard legit", 60, custom_info.RetardLegit ? CFG::Color_RetardLegit : CFG::Menu_Text_Inactive, true))
				{
					F::Players->Mark(n, { false, false, !custom_info.RetardLegit });
				}

				// Check for sourcebans alert
				uint64_t steamID64 = static_cast<uint64_t>(player_info.friendsID) + 0x0110000100000000ULL;
				if (HasSourcebansAlert(steamID64))
				{
					m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
					m_nCursorY = by + (nAvatarSize - H::Fonts->Get(EFonts::Menu).m_nTall - CFG::Menu_Spacing_Y) / 2;

					// Draw ALERT box with red background (clickable)
					int alertW = 45;
					int alertH = H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y - 1;
					
					bool bAlertHovered = IsHovered(m_nCursorX, m_nCursorY, alertW, alertH, nullptr);
					Color_t alertBg = bAlertHovered ? Color_t{ 220, 60, 60, 255 } : Color_t{ 180, 40, 40, 255 };
					
					H::Draw->Rect(m_nCursorX, m_nCursorY, alertW, alertH, alertBg);
					H::Draw->OutlinedRect(m_nCursorX, m_nCursorY, alertW, alertH, { 255, 80, 80, 255 });
					H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + alertW / 2, m_nCursorY + alertH / 2 - 1, { 255, 255, 255, 255 }, POS_CENTERXY, "ALERT!");
					
					// Click on ALERT to view player details and dismiss alert
					if (bAlertHovered && H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed)
					{
						m_bClickConsumed = true;
						nSelectedPlayerIndex = n;
						MainTab = EMainTabs::PLAYER_DETAILS;
						DismissSourcebansAlert(steamID64);
					}
				}

				m_nCursorX = bx;
				m_nCursorY = by;

				m_nCursorY += nRowHeight;
			}
		}
	}

	if (MainTab == EMainTabs::CONFIGS)
	{
		static std::string strSelected = {};
		const auto& configFolder = U::Storage->GetConfigFolder();
		const int nConfigTabStartY = m_nCursorY; // Save starting Y position

		int nCount = 0;

		for (const auto &entry : std::filesystem::directory_iterator(configFolder))
		{
			if (std::string(std::filesystem::path(entry).filename().string()).find(".json") == std::string_view::npos)
				continue;

			nCount++;
		}

		if (nCount < 11)
		{
			std::string strInput = {};

			auto anchor_x{ m_nCursorX };
			auto anchor_y{ m_nCursorY };

			if (InputText("Create New", "Enter a Name:", strInput))
			{
				bool bAlreadyExists = [&]() -> bool
				{
					for (const auto &entry : std::filesystem::directory_iterator(configFolder))
					{
						if (std::string(std::filesystem::path(entry).filename().string()).find(".json") == std::string_view::npos)
							continue;

						if (!std::string(std::filesystem::path(entry).filename().string()).compare(strInput))
							return true;
					}

					return false;
				}();

				if (!bAlreadyExists)
				{
					std::string newFile = strInput + ".json";
					Config::Save(configFolder / newFile);
				}
			}
			
			//can't do this nicely after getting rid of std::any..

			/*auto anchor_x2{ anchor_x };

			m_nCursorX = anchor_x + m_nLastButtonW + CFG::Menu_Spacing_X;
			m_nCursorY = anchor_y;

			if (Button("Restore Defaults"))
			{
				for (const auto &var : Config::vecVarPtrs)
				{
					if (var->m_bNoSave)
					{
						continue;
					}

					var->m_Value = var->m_DefaultValue;
				}
			}

			m_nCursorX = anchor_x2;*/
		}

		if (strSelected.empty())
		{
			if (nCount > 0)
			{
				GroupBoxStart("Configs", 150);
				{
					m_nCursorY += CFG::Menu_Spacing_Y;

					for (const auto &entry : std::filesystem::directory_iterator(configFolder))
					{
						if (std::string(std::filesystem::path(entry).filename().string()).find(".json") == std::string_view::npos)
							continue;

						std::string s = entry.path().filename().string();
						s.erase(s.end() - 5, s.end());

						if (Button(s.c_str(), false, ((m_nLastGroupBoxW + 1) - (CFG::Menu_Spacing_X * 6))))
							strSelected = s;
					}
				}
				GroupBoxEnd();
			}
		}

		else
		{
			GroupBoxStart(strSelected.c_str(), 150);
			{
				m_nCursorY += CFG::Menu_Spacing_Y;

				int anchor_y = m_nCursorY;

				if (Button("Load")) {
					std::string fileName = strSelected + ".json";
					Config::Load(configFolder / fileName);
					strSelected = {};
				}

				m_nCursorX += 90;
				m_nCursorY = anchor_y;

				if (Button("Update")) {
					std::string fileName = strSelected + ".json";
					Config::Save(configFolder / fileName);
					strSelected = {};
				}

				if (Button("Delete")) {
					std::string fileName = strSelected + ".json";
					std::filesystem::remove(configFolder / fileName);
					strSelected = {};
				}

				if (Button("Cancel"))
					strSelected = {};

				m_nCursorX -= 90;
			}
			GroupBoxEnd();
		}

		// Legacy seonwdde configs on the right side
		static std::string strLegacySelected = {};
		const auto& legacyFolder = U::Storage->GetLegacyConfigFolder();
		
		if (U::Storage->HasLegacyConfigs())
		{
			// Save current position and move to right column
			int leftColumnX = m_nCursorX;
			int leftColumnY = m_nCursorY;
			m_nCursorX += 330; // Move to right column
			m_nCursorY = nConfigTabStartY; // Reset Y to top
			
			if (strLegacySelected.empty())
			{
				GroupBoxStart("SEOwnedDE (Migrate)", 150);
				{
					m_nCursorY += CFG::Menu_Spacing_Y;

					for (const auto& entry : std::filesystem::directory_iterator(legacyFolder))
					{
						if (std::string(std::filesystem::path(entry).filename().string()).find(".json") == std::string_view::npos)
							continue;

						std::string s = entry.path().filename().string();
						s.erase(s.end() - 5, s.end());

						if (Button(s.c_str(), false, ((m_nLastGroupBoxW + 1) - (CFG::Menu_Spacing_X * 6))))
							strLegacySelected = s;
					}
				}
				GroupBoxEnd();
			}
			else
			{
				GroupBoxStart(strLegacySelected.c_str(), 150);
				{
					m_nCursorY += CFG::Menu_Spacing_Y;

					if (Button("Migrate")) {
						// Copy file from legacy folder to new necromancer folder
						std::string fileName = strLegacySelected + ".json";
						std::filesystem::copy_file(legacyFolder / fileName, configFolder / fileName, std::filesystem::copy_options::skip_existing);
						strLegacySelected = {};
					}

					if (Button("Cancel"))
						strLegacySelected = {};
				}
				GroupBoxEnd();
			}
			
			// Restore position to left column
			m_nCursorX = leftColumnX;
			m_nCursorY = leftColumnY;
		}

		// View Config Folder button
		if (Button("View Config Folder", false, 130))
		{
			ShellExecuteW(NULL, L"open", U::Storage->GetConfigFolder().wstring().c_str(), NULL, NULL, SW_SHOWNORMAL);
		}

		// Unbind All button at bottom right
		int savedX = m_nCursorX;
		int savedY = m_nCursorY;
		
		// Position at bottom right of menu (moved more to the left)
		m_nCursorX = CFG::Menu_Pos_X + CFG::Menu_Width - 130 - CFG::Menu_Spacing_X;
		m_nCursorY = CFG::Menu_Pos_Y + CFG::Menu_Height - 30 - CFG::Menu_Spacing_Y;
		
		if (Button("Unbind All Keys", false, 120))
		{
			// Unbind all keybinds
			CFG::Aimbot_Key = 0;
			CFG::Triggerbot_Key = 0;
			CFG::Visuals_Thirdperson_Key = 0;
			CFG::Visuals_Paint_Key = 0;
			CFG::Visuals_Paint_Erase_Key = 0;
			CFG::Misc_Taunt_Spin_Key = 0;
			CFG::Misc_Edge_Jump_Key = 0;
			CFG::Misc_Auto_Rocket_Jump_Key = 0;
			CFG::Misc_Auto_Medigun_Key = 0;
			CFG::Misc_Movement_Lock_Key = 0;
			CFG::Misc_MVM_Instant_Respawn_Key = 0;
			CFG::Exploits_Shifting_Recharge_Key = 0;
			CFG::Exploits_RapidFire_Key = 0;
			CFG::Exploits_Warp_Key = 0;
			CFG::Exploits_Crits_Force_Crit_Key = 0;
			CFG::Exploits_Crits_Force_Crit_Key_Melee = 0;
		}
		
		m_nCursorX = savedX;
		m_nCursorY = savedY;
	}

	// Player Details View
	if (MainTab == EMainTabs::PLAYER_DETAILS)
	{
		// Back button
		if (Button("< Back to Players", false, 120))
		{
			MainTab = EMainTabs::PLAYERS;
			nSelectedPlayerIndex = -1;
		}

		m_nCursorY += CFG::Menu_Spacing_Y * 2;

		// Check if player is still valid
		player_info_t player_info{};
		if (nSelectedPlayerIndex <= 0 || !I::EngineClient->GetPlayerInfo(nSelectedPlayerIndex, &player_info))
		{
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "Player no longer available");
			return;
		}

		auto pResource = GetTFPlayerResource();
		PlayerPriority custom_info{};
		F::Players->GetInfo(nSelectedPlayerIndex, custom_info);

		const int nLocalTeam = pResource ? pResource->GetTeam(I::EngineClient->GetLocalPlayer()) : 0;
		const int nPlayerTeam = pResource ? pResource->GetTeam(nSelectedPlayerIndex) : 0;
		const bool bIsTeammate = (nPlayerTeam == nLocalTeam);
		const bool bIsF2P = H::Entities->IsF2P(nSelectedPlayerIndex);
		const int nPartyIndex = H::Entities->GetPartyIndex(nSelectedPlayerIndex);

		// Large avatar at top
		const int nLargeAvatarSize = 64;
		const bool bShowAvatars = I::SteamFriends && I::SteamUtils;
		
		int nAvatarX = m_nCursorX;
		int nAvatarY = m_nCursorY;

		if (bShowAvatars && player_info.friendsID != 0)
		{
			H::Draw->Avatar(nAvatarX, nAvatarY, nLargeAvatarSize, nLargeAvatarSize, static_cast<uint32_t>(player_info.friendsID));
		}
		else
		{
			// Placeholder box if no avatar
			H::Draw->OutlinedRect(nAvatarX, nAvatarY, nLargeAvatarSize, nLargeAvatarSize, CFG::Menu_Accent_Primary);
		}

		// Player name next to avatar
		int nInfoX = nAvatarX + nLargeAvatarSize + CFG::Menu_Spacing_X * 2;
		int nInfoY = nAvatarY;

		// Name with team color
		Color_t nameColor = bIsTeammate ? CFG::Color_Teammate : CFG::Color_Enemy;
		if (custom_info.Cheater)
			nameColor = CFG::Color_Cheater;
		else if (custom_info.Ignored)
			nameColor = CFG::Color_Friend;
		else if (custom_info.RetardLegit)
			nameColor = CFG::Color_RetardLegit;

		H::Draw->String(H::Fonts->Get(EFonts::Menu), nInfoX, nInfoY, nameColor, POS_DEFAULT, "%s", player_info.name);
		nInfoY += H::Fonts->Get(EFonts::Menu).m_nTall + 2;

		// Steam ID (convert friendsID to Steam64)
		uint64_t steamID64 = static_cast<uint64_t>(player_info.friendsID) + 0x0110000100000000ULL;
		H::Draw->String(H::Fonts->Get(EFonts::Menu), nInfoX, nInfoY, CFG::Menu_Text_Inactive, POS_DEFAULT, "Steam ID: %llu", steamID64);
		nInfoY += H::Fonts->Get(EFonts::Menu).m_nTall + 2;

		// Team
		const char* szTeam = "Unknown";
		if (nPlayerTeam == TF_TEAM_RED)
			szTeam = "RED";
		else if (nPlayerTeam == TF_TEAM_BLUE)
			szTeam = "BLU";
		else if (nPlayerTeam == TEAM_SPECTATOR)
			szTeam = "Spectator";

		H::Draw->String(H::Fonts->Get(EFonts::Menu), nInfoX, nInfoY, CFG::Menu_Text_Inactive, POS_DEFAULT, "Team: %s", szTeam);

		// Move cursor below avatar section
		m_nCursorY = nAvatarY + nLargeAvatarSize + CFG::Menu_Spacing_Y * 2;

		// Info section with GroupBox
		GroupBoxStart("Player Info", 280);
		{
			m_nCursorY += CFG::Menu_Spacing_Y;

			// F2P Status
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text, POS_DEFAULT, "F2P Status:");
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 120, m_nCursorY, bIsF2P ? CFG::Color_F2P : CFG::Menu_Text_Inactive, POS_DEFAULT, bIsF2P ? "Yes" : "No");
			m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;

			// Party Status
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text, POS_DEFAULT, "Party:");
			if (nPartyIndex > 0)
			{
				char szParty[16];
				snprintf(szParty, sizeof(szParty), "Party %d", nPartyIndex);
				Color_t partyColor;
				switch (nPartyIndex)
				{
				case 1: partyColor = CFG::Color_Party_1; break;
				case 2: partyColor = CFG::Color_Party_2; break;
				case 3: partyColor = CFG::Color_Party_3; break;
				case 4: partyColor = CFG::Color_Party_4; break;
				case 5: partyColor = CFG::Color_Party_5; break;
				case 6: partyColor = CFG::Color_Party_6; break;
				case 7: partyColor = CFG::Color_Party_7; break;
				case 8: partyColor = CFG::Color_Party_8; break;
				case 9: partyColor = CFG::Color_Party_9; break;
				case 10: partyColor = CFG::Color_Party_10; break;
				case 11: partyColor = CFG::Color_Party_11; break;
				case 12: partyColor = CFG::Color_Party_12; break;
				default: partyColor = CFG::Menu_Text_Inactive; break;
				}
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 120, m_nCursorY, partyColor, POS_DEFAULT, szParty);
			}
			else
			{
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 120, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "None");
			}
			m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;

			// Relationship
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text, POS_DEFAULT, "Relation:");
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 120, m_nCursorY, bIsTeammate ? CFG::Color_Teammate : CFG::Color_Enemy, POS_DEFAULT, bIsTeammate ? "Teammate" : "Enemy");
			m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;

			// Player class (if available)
			auto pEntity = I::ClientEntityList->GetClientEntity(nSelectedPlayerIndex);
			if (pEntity && pEntity->GetClassId() == ETFClassIds::CTFPlayer)
			{
				auto pPlayer = pEntity->As<C_TFPlayer>();
				if (pPlayer && !pPlayer->deadflag())
				{
					const char* szClass = "Unknown";
					switch (pPlayer->m_iClass())
					{
					case TF_CLASS_SCOUT: szClass = "Scout"; break;
					case TF_CLASS_SOLDIER: szClass = "Soldier"; break;
					case TF_CLASS_PYRO: szClass = "Pyro"; break;
					case TF_CLASS_DEMOMAN: szClass = "Demoman"; break;
					case TF_CLASS_HEAVYWEAPONS: szClass = "Heavy"; break;
					case TF_CLASS_ENGINEER: szClass = "Engineer"; break;
					case TF_CLASS_MEDIC: szClass = "Medic"; break;
					case TF_CLASS_SNIPER: szClass = "Sniper"; break;
					case TF_CLASS_SPY: szClass = "Spy"; break;
					}
					H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text, POS_DEFAULT, "Class:");
					H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 120, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, szClass);
					m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;

					// Health
					H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text, POS_DEFAULT, "Health:");
					char szHealth[32];
					snprintf(szHealth, sizeof(szHealth), "%d / %d", pPlayer->m_iHealth(), pPlayer->GetMaxHealth());
					H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 120, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, szHealth);
					m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
				}
				else
				{
					H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text, POS_DEFAULT, "Status:");
					H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 120, m_nCursorY, CFG::Color_Cheater, POS_DEFAULT, "Dead");
					m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
				}
			}
		}
		GroupBoxEnd();

		// Your History with this player
		m_nCursorY += CFG::Menu_Spacing_Y;
		GroupBoxStart("Your History", 280);
		{
			m_nCursorY += CFG::Menu_Spacing_Y;

			PlayerStats playerStats{};
			F::Players->GetStats(steamID64, playerStats);

			// Encounters
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text, POS_DEFAULT, "Encounters:");
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 120, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "%d", playerStats.Encounters);
			m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;

			// Kills (you killed them)
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text, POS_DEFAULT, "You killed:");
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 120, m_nCursorY, CFG::Color_Friend, POS_DEFAULT, "%d", playerStats.Kills);
			m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;

			// Deaths (they killed you)
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text, POS_DEFAULT, "Killed you:");
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 120, m_nCursorY, CFG::Color_Cheater, POS_DEFAULT, "%d", playerStats.Deaths);
			m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;

			// K/D ratio
			if (playerStats.Deaths > 0)
			{
				float kd = static_cast<float>(playerStats.Kills) / static_cast<float>(playerStats.Deaths);
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text, POS_DEFAULT, "K/D Ratio:");
				Color_t kdColor = kd >= 1.0f ? CFG::Color_Friend : CFG::Color_Cheater;
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 120, m_nCursorY, kdColor, POS_DEFAULT, "%.2f", kd);
				m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
			}

			auto FormatTimeAgo = [](int64_t timestamp) -> std::string
			{
				if (timestamp <= 0)
					return "Never";
				
				auto now = std::chrono::duration_cast<std::chrono::seconds>(
					std::chrono::system_clock::now().time_since_epoch()).count();
				int64_t diff = now - timestamp;
				
				if (diff < 60)
					return "Just now";
				else if (diff < 3600)
					return std::to_string(diff / 60) + " min ago";
				else if (diff < 86400)
					return std::to_string(diff / 3600) + " hours ago";
				else
					return std::to_string(diff / 86400) + " days ago";
			};

			// First seen
			if (playerStats.FirstSeen > 0)
			{
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text, POS_DEFAULT, "First seen:");
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 120, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, FormatTimeAgo(playerStats.FirstSeen).c_str());
				m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
			}

			// Last seen (shows when you previously saw them - updated after each encounter)
			if (playerStats.LastSeen > 0 && playerStats.Encounters > 1)
			{
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text, POS_DEFAULT, "Last seen:");
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 120, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, FormatTimeAgo(playerStats.LastSeen).c_str());
				m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
			}
		}
		GroupBoxEnd();

		// Priority/Tags section
		m_nCursorY += CFG::Menu_Spacing_Y;
		GroupBoxStart("Player Tags", 280);
		{
			m_nCursorY += CFG::Menu_Spacing_Y;

			// Radio-style tag buttons - only one can be active at a time
			int tagStartX = m_nCursorX;
			int tagY = m_nCursorY;

			// Ignored button
			if (playerListButton(L"ignored", 80, custom_info.Ignored ? CFG::Color_Friend : CFG::Menu_Text_Inactive, true))
			{
				if (!custom_info.Ignored)
					F::Players->Mark(nSelectedPlayerIndex, { true, false, false }); // Set Ignored, clear others
				else
					F::Players->Mark(nSelectedPlayerIndex, { false, false, false }); // Clear all
			}

			m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
			m_nCursorY = tagY;

			// Cheater button
			if (playerListButton(L"cheater", 80, custom_info.Cheater ? CFG::Color_Cheater : CFG::Menu_Text_Inactive, true))
			{
				if (!custom_info.Cheater)
					F::Players->Mark(nSelectedPlayerIndex, { false, true, false }); // Set Cheater, clear others
				else
					F::Players->Mark(nSelectedPlayerIndex, { false, false, false }); // Clear all
			}

			m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
			m_nCursorY = tagY;

			// Retard Legit button
			if (playerListButton(L"retard legit", 80, custom_info.RetardLegit ? CFG::Color_RetardLegit : CFG::Menu_Text_Inactive, true))
			{
				if (!custom_info.RetardLegit)
					F::Players->Mark(nSelectedPlayerIndex, { false, false, true }); // Set RetardLegit, clear others
				else
					F::Players->Mark(nSelectedPlayerIndex, { false, false, false }); // Clear all
			}

			m_nCursorX = tagStartX;
			m_nCursorY = tagY + H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y * 2;
		}
		GroupBoxEnd();

		// Actions section
		m_nCursorY += CFG::Menu_Spacing_Y;
		GroupBoxStart("Actions", 280);
		{
			m_nCursorY += CFG::Menu_Spacing_Y;

			// Open Steam Profile button
			if (Button("Open Steam Profile", false, 150))
			{
				uint64_t steamID64 = static_cast<uint64_t>(player_info.friendsID) + 0x0110000100000000ULL;
				wchar_t szUrl[128];
				swprintf_s(szUrl, L"https://steamcommunity.com/profiles/%llu", steamID64);
				ShellExecuteW(NULL, L"open", szUrl, NULL, NULL, SW_SHOWNORMAL);
			}
		}
		GroupBoxEnd();

		// Sourcebans History section
		m_nCursorY += CFG::Menu_Spacing_Y;
		GroupBoxStart("Sourcebans History", 450);
		{
			m_nCursorY += CFG::Menu_Spacing_Y;

			uint64_t steamID64 = static_cast<uint64_t>(player_info.friendsID) + 0x0110000100000000ULL;

			// Check if we need to fetch data
			SourcebanInfo_t sourcebanInfo;
			GetSourcebansInfo(steamID64, sourcebanInfo);

			if (!sourcebanInfo.m_bFetched && !sourcebanInfo.m_bFetching)
			{
				// Show fetch button
				if (Button("Check Sourcebans", false, 130))
				{
					FetchSourcebans(steamID64);
				}
			}
			else if (sourcebanInfo.m_bFetching)
			{
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "Fetching...");
				m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
			}
			else if (sourcebanInfo.m_bFetched)
			{
				if (sourcebanInfo.m_bHasBans)
				{
					// Show warning
					H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Color_Cheater, POS_DEFAULT, "WARNING: %d ban(s) found!", static_cast<int>(sourcebanInfo.m_vecBans.size()));
					m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;

					// Show each ban (limit to 10 to show more)
					int nBansShown = 0;
					for (const auto& ban : sourcebanInfo.m_vecBans)
					{
						if (nBansShown >= 10)
						{
							H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "... and %d more", static_cast<int>(sourcebanInfo.m_vecBans.size()) - 10);
							m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
							break;
						}

						// Show more text - truncate at 70 chars instead of 40
						std::string displayBan = ban;
						if (displayBan.length() > 70)
							displayBan = displayBan.substr(0, 67) + "...";

						H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, displayBan.c_str());
						m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
						nBansShown++;
					}
				}
				else
				{
					H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Color_Friend, POS_DEFAULT, "No sourcebans found");
					m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
				}

				// Refresh button
				if (Button("Refresh", false, 70))
				{
					ClearSourcebansCache(steamID64);
				}
			}
		}
		GroupBoxEnd();

		// Verobay Database section
		m_nCursorY += CFG::Menu_Spacing_Y;
		GroupBoxStart("Vorobey's Database", 450);
		{
			m_nCursorY += CFG::Menu_Spacing_Y;

			uint64_t steamID64 = static_cast<uint64_t>(player_info.friendsID) + 0x0110000100000000ULL;

			VerobayInfo_t verobayInfo;
			GetVerobayInfo(steamID64, verobayInfo);

			if (!verobayInfo.m_bFetched && !verobayInfo.m_bFetching)
			{
				// Database not loaded yet
				if (Button("Load Database", false, 120))
				{
					FetchVerobayDatabase();
				}
			}
			else if (verobayInfo.m_bFetching)
			{
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "Fetching database...");
				m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
			}
			else if (verobayInfo.m_bFetched)
			{
				if (verobayInfo.m_bFoundInDatabase)
				{
					H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Color_Cheater, POS_DEFAULT, "FOUND IN DATABASE");
					m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
				}
				else
				{
					H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Color_Friend, POS_DEFAULT, "Not found in database");
					m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
				}

				// Refresh button
				if (Button("Refresh", false, 70))
				{
					RefreshVerobayDatabase();
				}
			}
		}
		GroupBoxEnd();
	}
}

void CMenu::Snow()
{
	struct SnowFlake_t
	{
		float m_flPosX = 0.0f;
		float m_flPosY = 0.0f;
		float m_flFallSpeed = 0.0f;
		float m_flDriftXSpeed = 0.0f;
		byte m_nAlpha{ 0 };
		int m_nSize{};
	};

	static std::vector<SnowFlake_t> vecSnowFlakes = {};

	if (!CFG::Menu_Snow)
	{
		if (!vecSnowFlakes.empty())
		{
			vecSnowFlakes.clear();
		}

		return;
	}

	auto GenerateSnowFlake = [](bool bFirstTime = false)
	{
		SnowFlake_t Out = {};

		Out.m_flPosX = static_cast<float>(Utils::RandInt(-(H::Draw->GetScreenW() / 2), H::Draw->GetScreenW()));
		Out.m_flPosY = static_cast<float>(Utils::RandInt(bFirstTime ? -(H::Draw->GetScreenH() * 2) : -100, -50));
		Out.m_flFallSpeed = static_cast<float>(Utils::RandInt(100, 200));
		Out.m_flDriftXSpeed = static_cast<float>(Utils::RandInt(10, 70));
		Out.m_nAlpha = static_cast<byte>(Utils::RandInt(5, 255));
		Out.m_nSize = Utils::RandInt(1, 2);

		return Out;
	};

	if (vecSnowFlakes.empty())
	{
		for (int n = 0; n < 1400; n++)
		{
			vecSnowFlakes.push_back(GenerateSnowFlake(true));
		}
	}

	for (auto &SnowFlake : vecSnowFlakes)
	{
		if (SnowFlake.m_flPosY > H::Draw->GetScreenH() + 50)
		{
			SnowFlake = GenerateSnowFlake();

			continue;
		}

		SnowFlake.m_flPosX += SnowFlake.m_flDriftXSpeed * I::GlobalVars->frametime;
		SnowFlake.m_flPosY += SnowFlake.m_flFallSpeed * I::GlobalVars->frametime;

		int nSize = SnowFlake.m_nSize;

		H::Draw->Rect(static_cast<int>(SnowFlake.m_flPosX), static_cast<int>(SnowFlake.m_flPosY), nSize, nSize, { 230, 230, 230, SnowFlake.m_nAlpha });
	}
}

void CMenu::Indicators()
{
	auto pLocal = H::Entities->GetLocal();

	// FPS and build date display removed
	/*
	int x = 2;
	int tall = H::Fonts->Get(EFonts::ESP_SMALL).m_nTall;
	int numitems = (pLocal && GetTFPlayerResource()) ? 3 : 2;
	int y = H::Draw->GetScreenH() - ((numitems * tall) + 2);
	int offset = 0;
	Color_t clr = { 200, 200, 200, 255 };

	H::Draw->String(H::Fonts->Get(EFonts::ESP_SMALL), x, y + (offset++ * tall), clr, POS_DEFAULT, "fps %d", static_cast<int>(1.0f / I::GlobalVars->absoluteframetime));

	if (auto pPR = GetTFPlayerResource())
	{
		if (pLocal)
		{
			H::Draw->String(H::Fonts->Get(EFonts::ESP_SMALL), x, y + (offset++ * tall), clr, POS_DEFAULT, "ping %d", pPR->GetPing(pLocal->entindex()));
		}
	}

	//H::Draw->String(H::Fonts->Get(EFonts::ESP_SMALL), x, y + (offset++ * tall), clr, POS_DEFAULT, "choked %d", I::ClientState->chokedcommands);
	H::Draw->String(H::Fonts->Get(EFonts::ESP_SMALL), x, y + (offset++ * tall), clr, POS_DEFAULT, "build %hs", __DATE__);
	*/
}

void CMenu::Run()
{
	// Auto-check all players' sourcebans when connected (runs every frame but only checks new players)
	CheckAllPlayersSourcebans();

	if (CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot())
	{
		return;
	}

	if (!H::Input->IsGameFocused() && m_bOpen) {
		m_bOpen = false;
		I::MatSystemSurface->SetCursorAlwaysVisible(false);
		return;
	}

	if (!m_pGradient)
	{
		m_pGradient = std::make_unique<Color_t[]>(200 * 200);

		float hue = 0.0f, sat = 0.99f, lum = 1.0f;

		for (int i = 0; i < 200; i++)
		{
			for (int j = 0; j < 200; j++)
			{
				*reinterpret_cast<Color_t *>(m_pGradient.get() + j + i * 200) = ColorUtils::HSLToRGB(hue, sat, lum);
				hue += 1.0f / 200.0f;
			}

			lum -= 1.0f / 200.0f;
			hue = 0.0f;
		}

		m_nColorPickerTextureId = I::MatSystemSurface->CreateNewTextureID(true);
		I::MatSystemSurface->DrawSetTextureRGBAEx(m_nColorPickerTextureId, reinterpret_cast<const unsigned char *>(m_pGradient.get()), 200, 200, IMAGE_FORMAT_RGBA8888);
	}

	if (H::Input->IsPressed(VK_INSERT) || H::Input->IsPressed(VK_F3))
		I::MatSystemSurface->SetCursorAlwaysVisible(m_bOpen = !m_bOpen);

	// Update animations
	float currentTime = I::EngineClient->Time();
	float deltaTime = currentTime - m_flLastFrameTime;
	if (deltaTime > 0.1f) deltaTime = 0.1f; // Cap delta time
	m_flLastFrameTime = currentTime;
	
	m_animator.Update(deltaTime);
	m_particles.Update(deltaTime);
	
	// Menu open/close animation
	float targetOpenProgress = m_bOpen ? 1.0f : 0.0f;
	float openSpeed = 6.0f;
	m_flMenuOpenProgress += (targetOpenProgress - m_flMenuOpenProgress) * openSpeed * deltaTime;
	m_flMenuOpenProgress = std::max(0.0f, std::min(1.0f, m_flMenuOpenProgress));
	
	// Clear states when menu closes
	if (m_bWasOpen && !m_bOpen)
	{
		m_buttonHoverStates.clear();
		m_buttonPressStates.clear();
	}
	m_bWasOpen = m_bOpen;

	Indicators();

	if (m_bOpen)
	{
		m_bClickConsumed = false;

		H::LateRender->Clear();

		MainWindow();

		H::LateRender->DrawAll();

		//rare cat
		{
			static bool is_running{ false };
			static float last_roll_time{ I::EngineClient->Time() };
			static float progress{ -30.0f };

			if (static_cast<int>(progress) > CFG::Menu_Width + 30)
			{
				is_running = false;
				progress = -30.0f;
			}

			if (!is_running && I::EngineClient->Time() - last_roll_time > 1.0f)
			{
				last_roll_time = I::EngineClient->Time();

				is_running = Utils::RandInt(0, 50) == 50;
			}

			if (is_running)
			{
				progress += 75.0f * I::GlobalVars->frametime;

				static float flLastFrameUpdateTime = I::EngineClient->Time();

				static int nFrame = 0;

				if (I::EngineClient->Time() - flLastFrameUpdateTime > 0.08f)
				{
					flLastFrameUpdateTime = I::EngineClient->Time();

					nFrame++;

					if (nFrame > 7)
					{
						nFrame = 0;
					}
				}

				H::Draw->StartClipping(CFG::Menu_Pos_X, 0, CFG::Menu_Width, H::Draw->GetScreenH());

				int offset{ 0 };

				if (nFrame == 1 || nFrame == 2 || nFrame == 3 || nFrame == 5 || nFrame == 6)
				{
					offset = 1;
				}

				//run test
				H::Draw->Texture
				(
					CFG::Menu_Pos_X + static_cast<int>(progress),
					CFG::Menu_Pos_Y - (13 + offset),
					20,
					13,
					F::VisualUtils->GetCatRun(nFrame),
					POS_DEFAULT
				);

				H::Draw->EndClipping();
			}
		}

		//cats idle
		{
			//idle left
			{
				static float flLastFrameUpdateTime = I::EngineClient->Time();

				static int nFrame = 0;

				if (I::EngineClient->Time() - flLastFrameUpdateTime > 0.2f)
				{
					flLastFrameUpdateTime = I::EngineClient->Time();

					nFrame++;

					if (nFrame > 3)
					{
						nFrame = 0;
					}
				}

				H::Draw->Texture(CFG::Menu_Pos_X + 5, CFG::Menu_Pos_Y - 12, 12, 12, F::VisualUtils->GetCat(nFrame), POS_DEFAULT);
			}

			//idle right
			{
				static float flLastFrameUpdateTime = I::EngineClient->Time();

				static int nFrame = 0;

				if (I::EngineClient->Time() - flLastFrameUpdateTime > 0.25f)
				{
					flLastFrameUpdateTime = I::EngineClient->Time();

					nFrame++;

					if (nFrame > 3)
					{
						nFrame = 0;
					}
				}

				H::Draw->Texture(CFG::Menu_Pos_X + 5 + 40, CFG::Menu_Pos_Y - 12, 12, 12, F::VisualUtils->GetCat2(nFrame), POS_DEFAULT);
			}

			//sleep
			{
				static float flLastFrameUpdateTime = I::EngineClient->Time();

				static int nFrame = 0;

				if (I::EngineClient->Time() - flLastFrameUpdateTime > 0.3f)
				{
					flLastFrameUpdateTime = I::EngineClient->Time();

					nFrame++;

					if (nFrame > 3)
					{
						nFrame = 0;
					}
				}

				H::Draw->Texture(CFG::Menu_Pos_X + 5 + 20, CFG::Menu_Pos_Y - 8, 12, 8, F::VisualUtils->GetCatSleep(nFrame), POS_DEFAULT);
			}
		}

		Snow();
	}
}

CMenu::CMenu()
{
}

// ============================================
// Draggable GroupBox System Implementation
// ============================================

void CMenu::RegisterGroupBox(const std::string& szTab, const std::string& szLabel, EGroupBoxColumn nDefaultColumn, int nOrder, int nWidth, EGroupBoxSize eSize)
{
	std::string szId = szTab + "_" + szLabel;
	
	if (m_mapGroupBoxes.find(szId) == m_mapGroupBoxes.end())
	{
		DraggableGroupBox_t gb;
		gb.m_szId = szId;
		gb.m_szLabel = szLabel;
		gb.m_nColumn = nDefaultColumn;
		gb.m_nOrderInColumn = nOrder;
		gb.m_nWidth = nWidth;
		gb.m_nHeight = 0;
		gb.m_nRenderX = 0;
		gb.m_nRenderY = 0;
		gb.m_eSize = eSize;
		m_mapGroupBoxes[szId] = gb;
	}
}

std::string CMenu::GetGroupBoxConfigKey(const std::string& szId)
{
	return "GroupBox_" + szId;
}

EGroupBoxColumn CMenu::GetColumnFromMouseX(int nContentX, int nContentW)
{
	int mx = H::Input->GetMouseX();
	// Column spacing: 150 width + increased spacing between columns
	int gbWidth = 150;
	int colSpacing = CFG::Menu_Spacing_X * 4;  // Match RenderDraggableGroupBoxes spacing
	int col1End = nContentX + gbWidth + colSpacing;
	int col2End = col1End + gbWidth + colSpacing;
	
	if (mx < col1End)
		return EGroupBoxColumn::LEFT;
	else if (mx < col2End)
		return EGroupBoxColumn::MIDDLE;
	else
		return EGroupBoxColumn::RIGHT;
}

void CMenu::ReorderGroupBoxesInColumn(const std::string& szTab, EGroupBoxColumn nColumn)
{
	std::vector<std::string> boxesInColumn;
	
	for (auto& pair : m_mapGroupBoxes)
	{
		if (pair.second.m_szId.find(szTab + "_") == 0 && pair.second.m_nColumn == nColumn)
		{
			boxesInColumn.push_back(pair.first);
		}
	}
	
	// Sort by order - use stable sort to maintain relative order for equal values
	std::stable_sort(boxesInColumn.begin(), boxesInColumn.end(), [this](const std::string& a, const std::string& b) {
		return m_mapGroupBoxes[a].m_nOrderInColumn < m_mapGroupBoxes[b].m_nOrderInColumn;
	});
	
	// Reassign orders sequentially
	for (int i = 0; i < static_cast<int>(boxesInColumn.size()); i++)
	{
		m_mapGroupBoxes[boxesInColumn[i]].m_nOrderInColumn = i;
	}
}

void CMenu::HandleGroupBoxDrag()
{
	if (!m_bIsDraggingGroupBox)
		return;
	
	if (!H::Input->IsHeld(VK_LBUTTON))
	{
		// Drop the GroupBox
		m_bIsDraggingGroupBox = false;
		m_bShowDropZones = false;
		
		if (m_mapGroupBoxes.find(m_strDraggingGroupBox) != m_mapGroupBoxes.end())
		{
			auto& gb = m_mapGroupBoxes[m_strDraggingGroupBox];
			EGroupBoxColumn oldColumn = gb.m_nColumn;
			EGroupBoxColumn targetColumn = m_nHoveredDropColumn;
			
			// Find the tab name
			std::string szTab = m_strDraggingGroupBox.substr(0, m_strDraggingGroupBox.find('_'));
			
			// Validation for Exploits tab using size categories
			// Rules:
			// - 1 big + 1 medium + 1 extrasmall allowed in 1 lane
			// - 1 big + 1 extrasmall + 2 small allowed in 1 lane
			// - 2 medium + 2 small + 1 extrasmall allowed in 1 lane (can swap 1 extrasmall for 1 small = 2 medium + 3 small)
			if (szTab == "Exploits")
			{
				// Count sizes in target column (excluding the dragged one)
				int nBigInTarget = 0;
				int nMediumInTarget = 0;
				int nSmallInTarget = 0;
				int nExtraSmallInTarget = 0;
				
				for (auto& pair : m_mapGroupBoxes)
				{
					if (pair.first != m_strDraggingGroupBox && 
					    pair.second.m_szId.find("Exploits_") == 0 && 
					    pair.second.m_nColumn == targetColumn)
					{
						switch (pair.second.m_eSize)
						{
							case EGroupBoxSize::BIG: nBigInTarget++; break;
							case EGroupBoxSize::MEDIUM: nMediumInTarget++; break;
							case EGroupBoxSize::SMALL: nSmallInTarget++; break;
							case EGroupBoxSize::EXTRA_SMALL: nExtraSmallInTarget++; break;
						}
					}
				}
				
				// Add the dragged box counts
				int nNewBig = nBigInTarget + (gb.m_eSize == EGroupBoxSize::BIG ? 1 : 0);
				int nNewMedium = nMediumInTarget + (gb.m_eSize == EGroupBoxSize::MEDIUM ? 1 : 0);
				int nNewSmall = nSmallInTarget + (gb.m_eSize == EGroupBoxSize::SMALL ? 1 : 0);
				int nNewExtraSmall = nExtraSmallInTarget + (gb.m_eSize == EGroupBoxSize::EXTRA_SMALL ? 1 : 0);
				
				// Check valid combinations:
				// 1. 1 big + 1 medium + 1 extrasmall
				// 2. 1 big + 1 extrasmall + 2 small
				// 3. 2 medium + 1 small + 1 extrasmall (or 2 medium + 2 small with no extrasmall)
				bool bValidPlacement = false;
				
				// Rule 1: 1 big + 1 medium + 1 extrasmall (no small)
				if (nNewBig == 1 && nNewMedium <= 1 && nNewSmall == 0 && nNewExtraSmall <= 1)
					bValidPlacement = true;
				
				// Rule 2: 1 big + 1 extrasmall + up to 2 small (no medium)
				if (nNewBig == 1 && nNewMedium == 0 && nNewSmall <= 2 && nNewExtraSmall <= 1)
					bValidPlacement = true;
				
				// Rule 3: 2 medium + 1 small + 1 extrasmall (no big)
				if (nNewBig == 0 && nNewMedium <= 2 && nNewSmall <= 1 && nNewExtraSmall <= 1)
					bValidPlacement = true;
				
				// Rule 3 variant: 2 medium + 1 small (no big, no extrasmall)
				if (nNewBig == 0 && nNewMedium <= 2 && nNewSmall <= 1 && nNewExtraSmall == 0)
					bValidPlacement = true;
				
				// Also allow empty or single-item columns
				int totalInColumn = nNewBig + nNewMedium + nNewSmall + nNewExtraSmall;
				if (totalInColumn <= 1)
					bValidPlacement = true;
				
				if (!bValidPlacement)
					targetColumn = oldColumn;
			}
			// Validation for Misc tab
			else if (szTab == "Misc")
			{
				// New rules for Misc tab groupbox placement:
				// Big sections: Game, Auto, Movement, Taunt, Misc
				// Small sections: Chat, Mann vs. Machine
				// Max per column: 3 big + 1 small, OR 2 big + 2 small
				
				auto isBigSection = [](const std::string& id) -> bool {
					return id == "Misc_Game" || id == "Misc_Auto" || id == "Misc_Movement" || 
					       id == "Misc_Taunt" || id == "Misc_Misc" || id == "Misc_Chat";
				};
				
				auto isSmallSection = [](const std::string& id) -> bool {
					return id == "Misc_Mann vs. Machine";
				};
				
				// Extra large sections - can't have all 3 in same column
				auto isExtraLargeSection = [](const std::string& id) -> bool {
					return id == "Misc_Misc" || id == "Misc_Auto" || id == "Misc_Movement";
				};
				
				// Count big, small, and extra large sections in target column (excluding the dragged one)
				int nBigInTarget = 0;
				int nSmallInTarget = 0;
				int nExtraLargeInTarget = 0;
				for (auto& pair : m_mapGroupBoxes)
				{
					if (pair.first != m_strDraggingGroupBox && 
					    pair.second.m_szId.find("Misc_") == 0 && 
					    pair.second.m_nColumn == targetColumn)
					{
						if (isBigSection(pair.first))
							nBigInTarget++;
						if (isSmallSection(pair.first))
							nSmallInTarget++;
						if (isExtraLargeSection(pair.first))
							nExtraLargeInTarget++;
					}
				}
				
				// Check if adding this box would exceed limits
				bool bDraggedIsBig = isBigSection(m_strDraggingGroupBox);
				bool bDraggedIsSmall = isSmallSection(m_strDraggingGroupBox);
				bool bDraggedIsExtraLarge = isExtraLargeSection(m_strDraggingGroupBox);
				
				int nNewBig = nBigInTarget + (bDraggedIsBig ? 1 : 0);
				int nNewSmall = nSmallInTarget + (bDraggedIsSmall ? 1 : 0);
				int nNewExtraLarge = nExtraLargeInTarget + (bDraggedIsExtraLarge ? 1 : 0);
				
				// Valid combinations per column:
				// - Max 3 big + 1 small
				// - OR 2 big + 2 small
				// - BUT can't have all 3 extra large (Misc, Auto, Movement) in same column
				bool bValidPlacement = false;
				if (nNewBig <= 3 && nNewSmall <= 1)
					bValidPlacement = true;
				else if (nNewBig <= 2 && nNewSmall <= 2)
					bValidPlacement = true;
				
				// Extra constraint: Misc, Auto, Movement can't all be in same column
				if (nNewExtraLarge >= 3)
					bValidPlacement = false;
				
				if (!bValidPlacement)
					targetColumn = oldColumn;
			}
			
			// Assign order based on mouse Y position
			int my = H::Input->GetMouseY();
			int insertionIndex = 0;
			
			// Collect all boxes in the target column (excluding the dragged one) and sort by order
			std::vector<DraggableGroupBox_t*> boxesInColumn;
			for (auto& pair : m_mapGroupBoxes)
			{
				if (pair.first != m_strDraggingGroupBox && 
					pair.second.m_szId.find(szTab + "_") == 0 && 
					pair.second.m_nColumn == targetColumn)
				{
					boxesInColumn.push_back(&pair.second);
				}
			}
			
			// Sort by current order
			std::sort(boxesInColumn.begin(), boxesInColumn.end(), [](DraggableGroupBox_t* a, DraggableGroupBox_t* b) {
				return a->m_nOrderInColumn < b->m_nOrderInColumn;
			});
			
			// Calculate logical Y positions (where boxes would be without the dragged item)
			// Use m_nDragContentY which is the consistent starting Y position from rendering
			// This ensures the drop logic matches the visual indicator
			int calcY = m_nDragContentY;
			
			// Find insertion index based on mouse Y against logical positions
			// We compare against the BOTTOM of each box, not the middle
			// This makes it easier to drop below an item
			insertionIndex = 0;
			for (size_t i = 0; i < boxesInColumn.size(); i++)
			{
				int boxHeight = boxesInColumn[i]->m_nHeight > 0 ? boxesInColumn[i]->m_nHeight : 80;
				int boxBottomY = calcY + boxHeight;
				
				// If mouse is past the top half of this box, insert after it
				int boxMidY = calcY + boxHeight / 2;
				if (my > boxMidY)
				{
					insertionIndex = static_cast<int>(i) + 1;
				}
				
				calcY += boxHeight + CFG::Menu_Spacing_Y;
			}
			
			// Now rebuild the order for all boxes in the target column
			// Insert the dragged box at the correct position
			std::vector<std::string> newOrder;
			for (size_t i = 0; i < boxesInColumn.size(); i++)
			{
				if (static_cast<int>(i) == insertionIndex)
				{
					newOrder.push_back(m_strDraggingGroupBox);
				}
				newOrder.push_back(boxesInColumn[i]->m_szId);
			}
			// If insertion is at the end
			if (insertionIndex >= static_cast<int>(boxesInColumn.size()))
			{
				newOrder.push_back(m_strDraggingGroupBox);
			}
			
			// Assign sequential orders to all boxes in the new order
			for (size_t i = 0; i < newOrder.size(); i++)
			{
				m_mapGroupBoxes[newOrder[i]].m_nOrderInColumn = static_cast<int>(i);
			}
			
			// Update the dragged box's column
			gb.m_nColumn = targetColumn;
			
			// Reorder the old column if it's different (to fill the gap)
			if (oldColumn != targetColumn)
			{
				ReorderGroupBoxesInColumn(szTab, oldColumn);
			}
			
			// Save ALL boxes' config values (not just the dragged one)
			// This is necessary because reordering affects other boxes too
			auto SaveGroupBoxConfig = [](const std::string& id, EGroupBoxColumn col, int order) {
				int configValue = static_cast<int>(col) * 100 + order;
				if (id == "Misc_Misc") CFG::Menu_GroupBox_Misc_Misc = configValue;
				else if (id == "Misc_Game") CFG::Menu_GroupBox_Misc_Game = configValue;
				else if (id == "Misc_Mann vs. Machine") CFG::Menu_GroupBox_Misc_MvM = configValue;
				else if (id == "Misc_Chat") CFG::Menu_GroupBox_Misc_Chat = configValue;
				else if (id == "Misc_Taunt") CFG::Menu_GroupBox_Misc_Taunt = configValue;
				else if (id == "Misc_Auto") CFG::Menu_GroupBox_Misc_Auto = configValue;
				else if (id == "Misc_Movement") CFG::Menu_GroupBox_Misc_Movement = configValue;
				else if (id == "Exploits_Shifting") CFG::Menu_GroupBox_Exploits_Shifting = configValue;
				else if (id == "Exploits_FakeLag") CFG::Menu_GroupBox_Exploits_FakeLag = configValue;
				else if (id == "Exploits_AntiAim") CFG::Menu_GroupBox_Exploits_AntiAim = configValue;
				else if (id == "Exploits_Crithack") CFG::Menu_GroupBox_Exploits_Crits = configValue;
				else if (id == "Exploits_No Spread") CFG::Menu_GroupBox_Exploits_NoSpread = configValue;
				else if (id == "Exploits_Region Selector") CFG::Menu_GroupBox_Exploits_RegionSelector = configValue;
			};
			
			// Save config for all boxes in the target column
			for (const auto& boxId : newOrder)
			{
				auto& box = m_mapGroupBoxes[boxId];
				SaveGroupBoxConfig(boxId, box.m_nColumn, box.m_nOrderInColumn);
			}
			
			// Also save config for boxes in the old column if different
			if (oldColumn != targetColumn)
			{
				for (auto& pair : m_mapGroupBoxes)
				{
					if (pair.second.m_szId.find(szTab + "_") == 0 && pair.second.m_nColumn == oldColumn)
					{
						SaveGroupBoxConfig(pair.first, pair.second.m_nColumn, pair.second.m_nOrderInColumn);
					}
				}
			}
		}
		
		m_strDraggingGroupBox.clear();
	}
}

void CMenu::RenderDropZones(int nContentX, int nContentY, int nContentW, int nContentH)
{
	// No visible drop zones - we use per-GroupBox highlighting instead
	if (!m_bShowDropZones)
	{
		m_flDropZoneAlpha = std::max(0.0f, m_flDropZoneAlpha - 6.0f * I::GlobalVars->frametime);
	}
	else
	{
		m_flDropZoneAlpha = std::min(1.0f, m_flDropZoneAlpha + 6.0f * I::GlobalVars->frametime);
	}
}

void CMenu::RenderDraggableGroupBoxes(const std::string& szTab, int nContentX, int nContentY, int nContentW, int nContentH)
{
	// Store content Y for drop calculations
	m_nDragContentY = nContentY;
	
	// Handle ongoing drag
	HandleGroupBoxDrag();
	
	// Update hovered column while dragging
	if (m_bIsDraggingGroupBox)
	{
		m_nHoveredDropColumn = GetColumnFromMouseX(nContentX, nContentW);
		m_bShowDropZones = true;
	}
	
	// Collect GroupBoxes for this tab
	std::vector<DraggableGroupBox_t*> tabBoxes;
	for (auto& pair : m_mapGroupBoxes)
	{
		if (pair.second.m_szId.find(szTab + "_") == 0)
		{
			tabBoxes.push_back(&pair.second);
		}
	}
	
	// Sort by column then order
	std::sort(tabBoxes.begin(), tabBoxes.end(), [](DraggableGroupBox_t* a, DraggableGroupBox_t* b) {
		if (a->m_nColumn != b->m_nColumn)
			return static_cast<int>(a->m_nColumn) < static_cast<int>(b->m_nColumn);
		return a->m_nOrderInColumn < b->m_nOrderInColumn;
	});
	
	// Column spacing: 150 width + increased spacing between columns
	int gbWidth = 150;
	int colSpacing = CFG::Menu_Spacing_X * 4;  // More spacing for middle/right columns
	int columnX[3] = { 
		nContentX,                           // LEFT column starts at content edge
		nContentX + gbWidth + colSpacing,    // MIDDLE column (moved right)
		nContentX + (gbWidth + colSpacing) * 2  // RIGHT column (moved right)
	};
	int columnY[3] = { nContentY, nContentY, nContentY };
	
	// Calculate insertion point while dragging
	int insertionCol = -1;
	int insertionOrder = -1;
	int draggedBoxHeight = 0;
	
	if (m_bIsDraggingGroupBox)
	{
		int my = H::Input->GetMouseY();
		insertionCol = static_cast<int>(m_nHoveredDropColumn);
		insertionOrder = 0;
		
		// Get dragged box info
		auto draggedIt = m_mapGroupBoxes.find(m_strDraggingGroupBox);
		if (draggedIt != m_mapGroupBoxes.end())
		{
			draggedBoxHeight = draggedIt->second.m_nHeight > 0 ? draggedIt->second.m_nHeight : 80;
		}
		
		// Collect boxes in target column (excluding dragged) and calculate their LOGICAL positions
		// We need to calculate where each box WOULD be rendered if the dragged box wasn't there
		std::vector<std::pair<int, int>> targetColumnBoxes; // order, calculated Y position
		int calcY = nContentY;
		
		// Get boxes in target column sorted by order
		std::vector<DraggableGroupBox_t*> sortedTargetBoxes;
		for (auto* gb : tabBoxes)
		{
			if (gb->m_szId == m_strDraggingGroupBox)
				continue;
			if (static_cast<int>(gb->m_nColumn) == insertionCol)
			{
				sortedTargetBoxes.push_back(gb);
			}
		}
		std::sort(sortedTargetBoxes.begin(), sortedTargetBoxes.end(), [](DraggableGroupBox_t* a, DraggableGroupBox_t* b) {
			return a->m_nOrderInColumn < b->m_nOrderInColumn;
		});
		
		// Calculate logical positions (where they would be without the dragged item)
		for (auto* gb : sortedTargetBoxes)
		{
			int boxHeight = gb->m_nHeight > 0 ? gb->m_nHeight : 80;
			int boxMidY = calcY + boxHeight / 2;
			targetColumnBoxes.push_back({ gb->m_nOrderInColumn, boxMidY });
			calcY += boxHeight + CFG::Menu_Spacing_Y;
		}
		
		// Find insertion point based on mouse Y against logical positions
		insertionOrder = 0;
		for (size_t i = 0; i < targetColumnBoxes.size(); i++)
		{
			if (my > targetColumnBoxes[i].second)
			{
				insertionOrder = static_cast<int>(i) + 1;
			}
		}
	}
	
	// First pass: calculate positions and draw insertion placeholder
	bool insertionDrawn = false;
	int insertionY = nContentY;
	
	// Track logical index per column (excluding dragged item)
	int logicalIndex[3] = { 0, 0, 0 };
	
	// Render each GroupBox
	for (auto* gb : tabBoxes)
	{
		bool isDragging = (m_bIsDraggingGroupBox && m_strDraggingGroupBox == gb->m_szId);
		
		int col = static_cast<int>(gb->m_nColumn);
		int boxX, boxY;
		
		if (isDragging)
		{
			// Follow mouse while dragging
			boxX = H::Input->GetMouseX() - m_nDragOffsetX;
			boxY = H::Input->GetMouseY() - m_nDragOffsetY;
		}
		else
		{
			boxX = columnX[col];
			boxY = columnY[col];
			
			// If we're in the insertion column and at the insertion point, add space for the placeholder
			if (m_bIsDraggingGroupBox && col == insertionCol && !insertionDrawn)
			{
				// Check if this box's logical index matches the insertion point
				if (logicalIndex[col] >= insertionOrder)
				{
					// Draw the grey placeholder rectangle at the insertion point
					insertionY = boxY;
					Color_t placeholderColor = { 128, 128, 128, 50 };
					H::Draw->Rect(boxX, boxY, gbWidth, draggedBoxHeight, placeholderColor);
					
					// Draw border around placeholder
					Color_t borderColor = { 150, 150, 150, 80 };
					H::Draw->Line(boxX, boxY, boxX + gbWidth, boxY, borderColor);
					H::Draw->Line(boxX, boxY + draggedBoxHeight, boxX + gbWidth, boxY + draggedBoxHeight, borderColor);
					H::Draw->Line(boxX, boxY, boxX, boxY + draggedBoxHeight, borderColor);
					H::Draw->Line(boxX + gbWidth, boxY, boxX + gbWidth, boxY + draggedBoxHeight, borderColor);
					
					// Shift this box and all subsequent boxes down
					boxY += draggedBoxHeight + CFG::Menu_Spacing_Y;
					columnY[col] = boxY;
					insertionDrawn = true;
				}
			}
		}
		
		// Store render position for drop detection
		gb->m_nRenderX = boxX;
		gb->m_nRenderY = boxY;
		
		// Set cursor position for GroupBox rendering
		m_nCursorX = boxX;
		m_nCursorY = boxY;
		
		// Check if header is being dragged
		// Drag area is a small strip at the very top of the GroupBox header (above the title)
		// Made smaller and further from options to avoid accidental drags
		int dragHandleH = 10;   // Reduced height by half (was 10)
		int dragHandleY = boxY - CFG::Menu_Spacing_Y * 3 + 9;  // Moved down 20 pixels
		int dragHandleW = 147;  // Increased width to 160
		int dragHandleX = boxX + 3;  // Moved 10 pixels to the right
		bool headerHovered = IsHoveredSimple(dragHandleX, dragHandleY, dragHandleW, dragHandleH);
		
		if (headerHovered && H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed && !m_bIsDraggingGroupBox)
		{
			m_bIsDraggingGroupBox = true;
			m_strDraggingGroupBox = gb->m_szId;
			m_nDragOffsetX = H::Input->GetMouseX() - boxX;
			m_nDragOffsetY = H::Input->GetMouseY() - boxY;
			m_bClickConsumed = true;
		}
		
		int startY = m_nCursorY;
		
		// Draw drag handle indicator on hover (when not dragging)
		if (headerHovered && !m_bIsDraggingGroupBox)
		{
			Color_t handleColor = { CFG::Menu_Accent_Primary.r, CFG::Menu_Accent_Primary.g, CFG::Menu_Accent_Primary.b, 60 };
			H::Draw->Rect(dragHandleX, dragHandleY, dragHandleW, dragHandleH, handleColor);
		}
		
		// Draw shadow when dragging
		if (isDragging)
		{
			Color_t shadowColor = { 0, 0, 0, 40 };
			H::Draw->Rect(boxX + 3, boxY + 3, gb->m_nWidth, gb->m_nHeight > 0 ? gb->m_nHeight : 100, shadowColor);
		}
		
		// Render GroupBox content
		GroupBoxStart(gb->m_szLabel.c_str(), gb->m_nWidth);
		if (gb->m_fnRenderContent)
			gb->m_fnRenderContent();
		GroupBoxEnd();
		
		// Calculate height
		gb->m_nHeight = m_nCursorY - startY + CFG::Menu_Spacing_Y * 2;
		
		// Update column Y for next box (only if not dragging this one)
		if (!isDragging)
		{
			columnY[col] = m_nCursorY + CFG::Menu_Spacing_Y;
			// Increment logical index for this column
			logicalIndex[col]++;
		}
	}
	
	// If insertion point is at the end of the column (after all boxes), draw placeholder there
	if (m_bIsDraggingGroupBox && !insertionDrawn && insertionCol >= 0)
	{
		int boxX = columnX[insertionCol];
		int boxY = columnY[insertionCol];
		
		Color_t placeholderColor = { 128, 128, 128, 50 };
		H::Draw->Rect(boxX, boxY, gbWidth, draggedBoxHeight, placeholderColor);
		
		Color_t borderColor = { 150, 150, 150, 80 };
		H::Draw->Line(boxX, boxY, boxX + gbWidth, boxY, borderColor);
		H::Draw->Line(boxX, boxY + draggedBoxHeight, boxX + gbWidth, boxY + draggedBoxHeight, borderColor);
		H::Draw->Line(boxX, boxY, boxX, boxY + draggedBoxHeight, borderColor);
		H::Draw->Line(boxX + gbWidth, boxY, boxX + gbWidth, boxY + draggedBoxHeight, borderColor);
	}
}













































































//the piper never dies
