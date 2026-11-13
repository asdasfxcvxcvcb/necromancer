#include "Menu.h"
#include "LateRenderer/LateRenderer.h"
#include "../CFG.h"
#include "../FakeAngle/FakeAngle.h"
#include "../VisualUtils/VisualUtils.h"
#include "../Players/Players.h"
#include "../Materials/Materials.h"
#include "../Outlines/Outlines.h"
#include "../PacMan/PacMan.h"
#include "../Chat/Chat.h"
#include "../Achievements/Achievements.h"
#include "../Misc/IgnoreVAC/IgnoreVAC.h"
#include "../Misc/Soundpad/Soundpad.h"
#include "../NecroNet/NecroNet.h"
#include "../NecroNet/NetAvatar.h"
#include "../Models/Models.h"
#include "../../CheaterDatabase/CheaterDatabase.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <cctype>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
namespace MenuBackground
{
	static int g_nTextureId = -1;
	static int g_nWidth = 0;
	static int g_nHeight = 0;
	static std::vector<unsigned char> g_vecImageData;
	static bool g_bNeedRefresh = true;
	static bool g_bImageLoaded = false;
	static std::string g_strLastMap = "";
	void Invalidate()
	{
		g_nTextureId = -1;
	}
	void Reset()
	{
		g_nTextureId = -1;
		g_nWidth = 0;
		g_nHeight = 0;
		g_vecImageData.clear();
		g_bImageLoaded = false;
		g_bNeedRefresh = true;
	}
}
#define multiselect(label, unique, ...) static std::vector<std::pair<const char *, bool &>> unique##multiselect = __VA_ARGS__; \
SelectMulti(label, unique##multiselect)
static std::string NetSanitize(const std::string &strIn)
{
	std::string strOut;
	strOut.reserve(strIn.size());
	for (char ch : strIn)
	{
		if (static_cast<unsigned char>(ch) >= 0x20)
			strOut += ch;
	}
	return strOut;
}
static std::string NetEscapeLabel(const std::string &strIn)
{
	const std::string strClean = NetSanitize(strIn);
	std::string strOut;
	strOut.reserve(strClean.size() * 2);
	for (char ch : strClean)
	{
		if (ch == '%')
			strOut += "%%";
		else strOut += ch;
	}
	return strOut;
}

static size_t Utf8GlyphCount(const std::string &strText)
{
	size_t nCount = 0;
	for (unsigned char ch : strText)
	{
		if ((ch & 0xC0) != 0x80)
			nCount++;
	}
	return nCount;
}

static size_t Utf8GlyphOffset(const std::string &strText, size_t nGlyph)
{
	size_t nPos = 0;
	while (nPos < strText.size() && nGlyph > 0)
	{
		nGlyph--;
		nPos++;
		while (nPos < strText.size() && (static_cast<unsigned char>(strText[nPos]) & 0xC0) == 0x80)
			nPos++;
	}
	return nPos;
}

static size_t Utf8PrevBoundary(const std::string &strText, size_t nPos)
{
	if (nPos == 0)
		return 0;
	nPos--;
	while (nPos > 0 && (static_cast<unsigned char>(strText[nPos]) & 0xC0) == 0x80)
		nPos--;
	return nPos;
}

static size_t Utf8NextBoundary(const std::string &strText, size_t nPos)
{
	if (nPos >= strText.size())
		return strText.size();
	nPos++;
	while (nPos < strText.size() && (static_cast<unsigned char>(strText[nPos]) & 0xC0) == 0x80)
		nPos++;
	return nPos;
}

static bool StartsWithNoCase(const std::string &strText, size_t nPos, const char *szNeedle)
{
	for (size_t i = 0; szNeedle[i] != '\0'; i++)
	{
		if (nPos + i >= strText.size())
			return false;

		const unsigned char ucA = static_cast<unsigned char>(strText[nPos + i]);
		const unsigned char ucB = static_cast<unsigned char>(szNeedle[i]);
		if (std::tolower(ucA) != std::tolower(ucB))
			return false;
	}

	return true;
}

static bool IsUrlBodyChar(char ch)
{
	const unsigned char uc = static_cast<unsigned char>(ch);
	if (uc <= 0x20 || uc >= 0x7F)
		return false;

	switch (ch)
	{
	case '"':
	case '<':
	case '>':
	case '`':
	case '{':
	case '}':
	case '|':
	case '\\':
	case '^':
		return false;
	default:
		return true;
	}
}

static bool IsUrlNeighbourChar(char ch)
{
	return IsUrlBodyChar(ch) || ch == '.' || ch == '@';
}

static size_t FindLinkStart(const std::string &strText, size_t nFrom)
{
	for (size_t nPos = nFrom; nPos < strText.size(); nPos++)
	{
		if (nPos > 0 && IsUrlNeighbourChar(strText[nPos - 1]))
			continue;

		if (StartsWithNoCase(strText, nPos, "http://") || StartsWithNoCase(strText, nPos, "https://") || StartsWithNoCase(strText, nPos, "www."))
			return nPos;
	}

	return std::string::npos;
}

static void ScanLinks(const std::string &strText, std::vector<std::pair<size_t, size_t>> &vecLinks)
{
	vecLinks.clear();
	size_t nFrom = 0;
	while (nFrom < strText.size())
	{
		const size_t nStart = FindLinkStart(strText, nFrom);
		if (nStart == std::string::npos)
			break;

		size_t nEnd = nStart;
		while (nEnd < strText.size() && IsUrlBodyChar(strText[nEnd]))
			nEnd++;

		while (nEnd > nStart && (strText[nEnd - 1] == '.' || strText[nEnd - 1] == ',' || strText[nEnd - 1] == ';' || strText[nEnd - 1] == ':' || strText[nEnd - 1] == '!' || strText[nEnd - 1] == '?' || strText[nEnd - 1] == ')' || strText[nEnd - 1] == ']' || strText[nEnd - 1] == '}'))
			nEnd--;

		const size_t nSkip = StartsWithNoCase(strText, nStart, "https://") ? 8 : (StartsWithNoCase(strText, nStart, "http://") ? 7 : 4);
		if (nEnd > nStart + nSkip)
		{
			const size_t nHost = strText.find_first_of(".:", nStart + nSkip);
			if (nHost != std::string::npos && nHost < nEnd)
				vecLinks.emplace_back(nStart, nEnd - nStart);
		}

		nFrom = (std::max)(nEnd, nStart + nSkip);
	}
}

static std::string ClipboardGetText()
{
	std::string strOut;

	if (!OpenClipboard(nullptr))
		return strOut;

	const HANDLE hData = GetClipboardData(CF_UNICODETEXT);

	if (hData)
	{
		if (const wchar_t *pszWide = static_cast<const wchar_t *>(GlobalLock(hData)))
		{
			const size_t nMaxWide = GlobalSize(hData) / sizeof(wchar_t);
			size_t nWideLen = 0;

			while (nWideLen < nMaxWide && pszWide[nWideLen] != L'\0')
				nWideLen++;

			const int nWide = static_cast<int>(nWideLen);

			if (nWide > 0)
			{
				const int nUtf8 = WideCharToMultiByte(CP_UTF8, 0, pszWide, nWide, nullptr, 0, nullptr, nullptr);

				if (nUtf8 > 0)
				{
					strOut.resize(static_cast<size_t>(nUtf8));
					WideCharToMultiByte(CP_UTF8, 0, pszWide, nWide, strOut.data(), nUtf8, nullptr, nullptr);
				}
			}

			GlobalUnlock(hData);
		}
	}

	CloseClipboard();
	return strOut;
}

static void ClipboardSetText(const std::string &strText)
{
	if (strText.empty())
		return;

	const int nWide = MultiByteToWideChar(CP_UTF8, 0, strText.c_str(), static_cast<int>(strText.size()), nullptr, 0);

	if (nWide <= 0)
		return;

	const HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (static_cast<size_t>(nWide) + 1) * sizeof(wchar_t));

	if (!hMem)
		return;

	if (wchar_t *pszWide = static_cast<wchar_t *>(GlobalLock(hMem)))
	{
		MultiByteToWideChar(CP_UTF8, 0, strText.c_str(), static_cast<int>(strText.size()), pszWide, nWide);
		pszWide[nWide] = L'\0';
		GlobalUnlock(hMem);

		if (OpenClipboard(nullptr))
		{
			EmptyClipboard();

			if (!SetClipboardData(CF_UNICODETEXT, hMem))
				GlobalFree(hMem);

			CloseClipboard();
		}
		else GlobalFree(hMem);
	}
	else GlobalFree(hMem);
}

static bool VkToUtf8(UINT nVk, const bool *pbDown, std::string &strOut)
{
	BYTE abState[256] = {};
	for (int n = 0; n < 256; n++)
	{
		if (pbDown[n])
			abState[n] = 0x80;
	}
	if (GetKeyState(VK_CAPITAL) & 1)
		abState[VK_CAPITAL] |= 0x01;

	wchar_t wsBuf[8] = {};
	const int nRet = ToUnicodeEx(nVk, 0, abState, wsBuf, 8, 0, GetKeyboardLayout(0));
	if (nRet <= 0)
		return false;

	const int nUtf8 = WideCharToMultiByte(CP_UTF8, 0, wsBuf, nRet, nullptr, 0, nullptr, nullptr);
	if (nUtf8 <= 0)
		return false;

	strOut.resize(static_cast<size_t>(nUtf8));
	WideCharToMultiByte(CP_UTF8, 0, wsBuf, nRet, strOut.data(), nUtf8, nullptr, nullptr);
	return true;
}

static bool TextInputEdit(std::string &strText, int &nCaret, int &nAnchor, int nMaxLen)
{
	bool bChanged = false;
	const bool bAltGr = H::Input->IsHeld(VK_RMENU);
	const bool bCtrl = H::Input->IsDown(VK_CONTROL) && !bAltGr;
	const bool bShift = H::Input->IsHeld(VK_SHIFT);

	auto EraseSelection = [&]() {
		const int nFrom = (std::min)(nCaret, nAnchor);
		const int nTo = (std::max)(nCaret, nAnchor);

		if (nFrom != nTo)
		{
			strText.erase(static_cast<size_t>(nFrom), static_cast<size_t>(nTo - nFrom));
			nCaret = nAnchor = nFrom;
			bChanged = true;
		}
	};

	auto InsertText = [&](const std::string &strInsert) {
		EraseSelection();

		const int nGlyphs = static_cast<int>(Utf8GlyphCount(strText));
		size_t nTake = 0;
		int nAdded = 0;

		while (nTake < strInsert.size() && nGlyphs + nAdded < nMaxLen)
		{
			nTake = Utf8NextBoundary(strInsert, nTake);
			nAdded++;
		}

		if (nAdded == 0)
			return;

		strText.insert(static_cast<size_t>(nCaret), strInsert, 0, nTake);
		nCaret += static_cast<int>(nTake);
		nAnchor = nCaret;
		bChanged = true;
	};

	auto MoveCaret = [&](int nTo, bool bSelect) {
		nCaret = (std::clamp)(nTo, 0, static_cast<int>(strText.size()));

		if (!bSelect)
			nAnchor = nCaret;
	};

	if (bCtrl && H::Input->IsPressed('A'))
	{
		nAnchor = 0;
		nCaret = static_cast<int>(strText.size());
	}
	else if (bCtrl && H::Input->IsPressed('C'))
	{
		const int nFrom = (std::min)(nCaret, nAnchor);
		const int nTo = (std::max)(nCaret, nAnchor);

		if (nFrom != nTo)
			ClipboardSetText(strText.substr(static_cast<size_t>(nFrom), static_cast<size_t>(nTo - nFrom)));
	}
	else if (bCtrl && H::Input->IsPressed('X'))
	{
		const int nFrom = (std::min)(nCaret, nAnchor);
		const int nTo = (std::max)(nCaret, nAnchor);

		if (nFrom != nTo)
		{
			ClipboardSetText(strText.substr(static_cast<size_t>(nFrom), static_cast<size_t>(nTo - nFrom)));
			EraseSelection();
		}
	}
	else if (bCtrl && H::Input->IsPressed('V'))
	{
		InsertText(NetSanitize(ClipboardGetText()));
	}

	if (H::Input->IsPressedAndHeld(VK_LEFT))
		MoveCaret(static_cast<int>(Utf8PrevBoundary(strText, static_cast<size_t>((std::max)(nCaret, 0)))), bShift);

	if (H::Input->IsPressedAndHeld(VK_RIGHT))
		MoveCaret(static_cast<int>(Utf8NextBoundary(strText, static_cast<size_t>((std::max)(nCaret, 0)))), bShift);

	if (H::Input->IsPressedAndHeld(VK_HOME))
		MoveCaret(0, bShift);

	if (H::Input->IsPressedAndHeld(VK_END))
		MoveCaret(static_cast<int>(strText.size()), bShift);

	if (H::Input->IsPressedAndHeld(VK_BACK))
	{
		const int nFrom = (std::min)(nCaret, nAnchor);
		const int nTo = (std::max)(nCaret, nAnchor);

		if (nFrom != nTo)
			EraseSelection();
		else if (nCaret > 0)
		{
			const int nPrev = static_cast<int>(Utf8PrevBoundary(strText, static_cast<size_t>(nCaret)));

			strText.erase(static_cast<size_t>(nPrev), static_cast<size_t>(nCaret - nPrev));
			nCaret = nAnchor = nPrev;
			bChanged = true;
		}
	}

	if (H::Input->IsPressedAndHeld(VK_DELETE))
	{
		const int nFrom = (std::min)(nCaret, nAnchor);
		const int nTo = (std::max)(nCaret, nAnchor);

		if (nFrom != nTo)
			EraseSelection();
		else if (nCaret < static_cast<int>(strText.size()))
		{
			const int nNext = static_cast<int>(Utf8NextBoundary(strText, static_cast<size_t>(nCaret)));

			strText.erase(static_cast<size_t>(nCaret), static_cast<size_t>(nNext - nCaret));
			bChanged = true;
		}
	}

	const std::string strTyped = H::Input->DrainCharsUtf8();
	std::string strCleanTyped;
	strCleanTyped.reserve(strTyped.size());
	for (char ch : strTyped)
	{
		const unsigned char uc = static_cast<unsigned char>(ch);
		if (uc >= 0x20 && uc != 0x7F)
			strCleanTyped += ch;
	}
	if (!strCleanTyped.empty())
	{
		if (!bCtrl)
			InsertText(strCleanTyped);
	}
	else if (!bCtrl && !H::Input->CharsRecent())
	{
		bool abDown[256] = {};
		for (int n = 0; n < 256; n++)
			abDown[n] = H::Input->IsDown(static_cast<short>(n));

		for (int nVk = 8; nVk < 255; nVk++)
		{
			if (!H::Input->IsPressedAndHeld(static_cast<short>(nVk)))
				continue;
			std::string strCh;
			if (!VkToUtf8(static_cast<UINT>(nVk), abDown, strCh))
				continue;
			std::string strClean;
			strClean.reserve(strCh.size());
			for (char ch : strCh)
			{
				const unsigned char uc = static_cast<unsigned char>(ch);
				if (uc >= 0x20 && uc != 0x7F)
					strClean += ch;
			}
			if (!strClean.empty())
				InsertText(strClean);
		}
	}

	return bChanged;
}

static int NetAvatarTexture(const std::string &strKey, const std::string &strB64)
{
	static std::map<std::string, std::pair<std::string, int>> mapCache;
	if (mapCache.size() > 128)
	{
		if (I::MatSystemSurface)
		{
			for (auto &pair : mapCache)
			{
				if (pair.second.second > 0)
					I::MatSystemSurface->DeleteTextureByID(pair.second.second);
			}
		}
		mapCache.clear();
	}
	auto it = mapCache.find(strKey);
	if (it != mapCache.end())
	{
		if (it->second.first == strB64)
			return it->second.second;
		if (it->second.second > 0 && I::MatSystemSurface)
			I::MatSystemSurface->DeleteTextureByID(it->second.second);
		it->second.first = strB64;
		it->second.second = 0;
	}
	std::vector<unsigned char> vecRGBA;
	int nW = 0, nH = 0;
	if (strB64.empty() || !CNetAvatar::DecodeToRGBA(strB64, vecRGBA, nW, nH) || vecRGBA.empty())
	{
		mapCache[strKey] = { strB64, 0 };
		return 0;
	}
	if (!I::MatSystemSurface)
	{
		mapCache.erase(strKey);
		return 0;
	}
	const int nTex = I::MatSystemSurface->CreateNewTextureID(true);
	if (!I::MatSystemSurface->IsTextureIDValid(nTex))
	{
		mapCache.erase(strKey);
		return 0;
	}
	I::MatSystemSurface->DrawSetTextureRGBAEx(nTex, vecRGBA.data(), nW, nH, IMAGE_FORMAT_RGBA8888);
	mapCache[strKey] = { strB64, nTex };
	return nTex;
}
static void NetDrawAvatar(const std::string &strKey, const std::string &strB64, int x, int y, int nSize)
{
	H::Draw->Rect(x, y, nSize, nSize, CFG::Menu_Background);
	const int nTex = NetAvatarTexture(strKey, strB64);
	if (nTex > 0)
	{
		I::MatSystemSurface->DrawSetColor(255, 255, 255, 255);
		I::MatSystemSurface->DrawSetTexture(nTex);
		I::MatSystemSurface->DrawTexturedRect(x, y, x + nSize, y + nSize);
		return;
	}
}
static void NetDrawOutlined(int x, int y, const Color_t &clr, const char *szText)
{
	const auto &font = H::Fonts->Get(EFonts::Menu);
	const Color_t clrOutline = { 0, 0, 0, 255 };
	H::Draw->String(font, x - 1, y, clrOutline, POS_DEFAULT, "%s", szText);
	H::Draw->String(font, x + 1, y, clrOutline, POS_DEFAULT, "%s", szText);
	H::Draw->String(font, x, y - 1, clrOutline, POS_DEFAULT, "%s", szText);
	H::Draw->String(font, x, y + 1, clrOutline, POS_DEFAULT, "%s", szText);
	H::Draw->String(font, x, y, clr, POS_DEFAULT, "%s", szText);
}
static void NetDrawWrapped(const std::string &strText, int x, int y, int nMaxW, int nMaxLines, const Color_t &clr)
{
	const auto &font = H::Fonts->Get(EFonts::Menu);
	auto Measure = [&](const std::string &strLine) -> int {
		int w_out = 0, h_out = 0;
		I::MatSystemSurface->GetTextSize(font.m_dwFont, Utils::ConvertUtf8ToWide(strLine).c_str(), w_out, h_out);
		return w_out;
	};
	size_t nPos = 0;
	int nLines = 0;
	while (nPos < strText.size() && nLines < nMaxLines)
	{
		size_t nLo = 1, nHi = strText.size() - nPos, nFit = 1;
		while (nLo <= nHi)
		{
			const size_t nMid = (nLo + nHi) / 2;
			if (Measure(strText.substr(nPos, nMid)) <= nMaxW)
			{
				nFit = nMid;
				nLo = nMid + 1;
			}
			else
				nHi = nMid - 1;
		}
		size_t nTake = nFit;
		if (nPos + nFit < strText.size())
		{
			const size_t nSpace = strText.find_last_of(' ', (nPos + nFit) - 1);
			if (nSpace != std::string::npos && nSpace > nPos)
				nTake = (nSpace - nPos) + 1;
		}
		while (nTake > 0 && nPos + nTake < strText.size() && (static_cast<unsigned char>(strText[nPos + nTake]) & 0xC0) == 0x80)
			nTake--;
		if (nTake == 0)
			nTake = 1;
		NetDrawOutlined(x, y + (nLines * (font.m_nTall + 2)), clr, NetEscapeLabel(strText.substr(nPos, nTake)).c_str());
		nPos += nTake;
		while (nPos < strText.size() && strText[nPos] == ' ')
			nPos++;
		nLines++;
	}
}
static uint64_t NetLocalSteamId64()
{
	const std::string strOwnId = F::NecroNet->GetOwnSteamId();
	if (strOwnId.size() != 17 || strOwnId.find_first_not_of("0123456789") != std::string::npos)
		return 0;
	return std::strtoull(strOwnId.c_str(), nullptr, 10);
}
static uint32_t NetLocalAccountId()
{
	const uint64_t ullSteamId = NetLocalSteamId64();
	return ullSteamId > 0x0110000100000000ULL ? static_cast<uint32_t>(ullSteamId - 0x0110000100000000ULL) : 0;
}
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
	m_nCursorY += CFG::Menu_Spacing_Y * 2;
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
	std::string checkboxId = std::string("chk_") + szLabel + "_" + std::to_string(x) + "_" + std::to_string(y);
	if (m_buttonHoverStates.find(checkboxId) == m_buttonHoverStates.end())
		m_buttonHoverStates[checkboxId] = 0.0f;
	float& hoverState = m_buttonHoverStates[checkboxId];
	float targetHover = bHovered ? 1.0f : 0.0f;
	hoverState += (targetHover - hoverState) * 8.0f * I::GlobalVars->frametime;
	if (m_buttonPressStates.find(checkboxId) == m_buttonPressStates.end())
		m_buttonPressStates[checkboxId] = bVar ? 1.0f : 0.0f;
	float& toggleState = m_buttonPressStates[checkboxId];
	float targetToggle = bVar ? 1.0f : 0.0f;
	toggleState += (targetToggle - toggleState) * 10.0f * I::GlobalVars->frametime;
	if (bHovered && H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed) {
		bCallback = m_bClickConsumed = true;
		bVar = !bVar;
		MarkConfigChanged();
	}
	Color_t clr = CFG::Menu_Accent_Primary;
	if (toggleState > 0.01f) {
		byte fillAlpha = static_cast<byte>(25 + toggleState * 55);
		Color_t fillColor = { clr.r, clr.g, clr.b, fillAlpha };
		Color_t fillColorBright = { clr.r, clr.g, clr.b, static_cast<byte>(fillAlpha * 0.5f) };
		H::Draw->GradientRect(x, y, w, h, fillColorBright, fillColor, false);
		int checkSize = static_cast<int>(w * 0.6f * toggleState);
		int checkX = x + (w - checkSize) / 2;
		int checkY = y + (h - checkSize) / 2;
		H::Draw->Rect(checkX, checkY, checkSize, checkSize, clr);
	}
	if (hoverState > 0.01f)
	{
		byte glowAlpha = static_cast<byte>(hoverState * 30);
		Color_t glowColor = { clr.r, clr.g, clr.b, glowAlpha };
		H::Draw->Rect(x - 1, y - 1, w + 2, h + 2, glowColor);
	}
	H::Draw->OutlinedRect(x, y, w, h, clr);
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
				MarkConfigChanged();
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
			{
				if (m_mapStates[&flVar])
					MarkConfigChanged();
				m_mapStates[&flVar] = false;
			}
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
	if (flStep > 0.f)
	{
		flVar = roundf(flVar / flStep) * flStep;
	}
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
	m_nCursorY += h + nTextH + CFG::Menu_Spacing_Y + CFG::Menu_Spacing_Y + 2;
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
				MarkConfigChanged();
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
			{
				if (m_mapStates[&nVar])
					MarkConfigChanged();
				m_mapStates[&nVar] = false;
			}
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
						MarkConfigChanged();
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
						MarkConfigChanged();
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
	std::string buttonId = std::string("btn_") + szLabel + "_" + std::to_string(x) + "_" + std::to_string(y);
	if (m_buttonHoverStates.find(buttonId) == m_buttonHoverStates.end())
		m_buttonHoverStates[buttonId] = 0.0f;
	float targetHover = (bHovered || bActive) ? 1.0f : 0.0f;
	float& currentHover = m_buttonHoverStates[buttonId];
	float hoverSpeed = 8.0f;
	currentHover += (targetHover - currentHover) * hoverSpeed * I::GlobalVars->frametime;
	currentHover = std::max(0.0f, std::min(1.0f, currentHover));
	if (m_buttonPressStates.find(buttonId) == m_buttonPressStates.end())
		m_buttonPressStates[buttonId] = 0.0f;
	float& pressState = m_buttonPressStates[buttonId];
	if (bHovered && H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed)
	{
		bCallback = m_bClickConsumed = true;
		pressState = 1.0f;
	}
	pressState *= 0.85f;
	if (pressState < 0.01f) pressState = 0.0f;
	float hoverAlpha = 50.0f + currentHover * 30.0f;
	float pressScale = 1.0f - pressState * 0.05f;
	float glowAlpha = currentHover * 40.0f;
	int scaledW = static_cast<int>(w * pressScale);
	int scaledH = static_cast<int>(h * pressScale);
	int offsetX = (w - scaledW) / 2;
	int offsetY = (h - scaledH) / 2;
	Color_t clr = CFG::Menu_Accent_Primary;
	if (glowAlpha > 1.0f)
	{
		Color_t glowColor = { clr.r, clr.g, clr.b, static_cast<byte>(glowAlpha) };
		H::Draw->Rect(x + offsetX - 1, y + offsetY - 1, scaledW + 2, scaledH + 2, glowColor);
	}
	Color_t clr_dim = { clr.r, clr.g, clr.b, static_cast<byte>(hoverAlpha) };
	Color_t clr_bright = { clr.r, clr.g, clr.b, static_cast<byte>(hoverAlpha * 0.5f) };
	H::Draw->GradientRect(x + offsetX, y + offsetY, scaledW, scaledH, clr_bright, clr_dim, false);
	H::Draw->OutlinedRect(x + offsetX, y + offsetY, scaledW, scaledH, clr);
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
struct TextInputState_t
{
	std::string m_strUndo;
	int m_nCaret = 0;
	int m_nAnchor = 0;
};

bool CMenu::InputText(const char *szLabel, const char *szLabel2, std::string &strOutput, int nMaxLen)
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
	static std::map<void *, TextInputState_t> mapEditStates;
	TextInputState_t &editState = mapEditStates[&strOutput];
	std::string &strUndo = editState.m_strUndo;
	std::string &strTemp = strOutput;
	int &nCaret = editState.m_nCaret;
	int &nAnchor = editState.m_nAnchor;

	if (bHovered && H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed) {
		for (auto &pairOpen : m_mapStates)
		{
			if (pairOpen.first != &strOutput)
				pairOpen.second = false;
		}
		m_bClickConsumed = m_mapStates[&strOutput] = true;
		std::string strCleanOpen;
		strCleanOpen.reserve(strTemp.size());
		for (char ch : strTemp)
		{
			const unsigned char uc = static_cast<unsigned char>(ch);
			if (uc >= 0x20 && uc != 0x7F)
				strCleanOpen += ch;
		}
		strTemp = strCleanOpen;
		strUndo = strTemp;
		nCaret = nAnchor = static_cast<int>(strTemp.size());
	}
	if (H::Input->IsPressed(VK_ESCAPE) || H::Input->IsPressed(VK_INSERT) || H::Input->IsPressed(VK_F3)) {
		if (m_mapStates[&strOutput])
			strTemp = strUndo;
		m_mapStates[&strOutput] = false;
		strUndo.clear();
	}
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

		auto Measure = [&](const std::string &strLine) -> int {
			int w_out = 0, h_out = 0;
			I::MatSystemSurface->GetTextSize(H::Fonts->Get(EFonts::Menu).m_dwFont, Utils::ConvertUtf8ToWide(strLine).c_str(), w_out, h_out);
			return w_out;
		};

		if (nCaret > static_cast<int>(strTemp.size()))
			nCaret = static_cast<int>(strTemp.size());
		if (nAnchor > static_cast<int>(strTemp.size()))
			nAnchor = static_cast<int>(strTemp.size());

		TextInputEdit(strTemp, nCaret, nAnchor, nMaxLen);

		const int nMaxDisplayW = w - (CFG::Menu_Spacing_X * 2);
		const size_t nTempGlyphs = Utf8GlyphCount(strTemp);
		std::string strDisplay;
		size_t nFirstGlyph = 0;

	strDisplay = strTemp;

	int nTextW = Measure(strDisplay);

	while (nTextW > nMaxDisplayW && Utf8GlyphCount(strDisplay) > 1)
	{
		strDisplay.erase(0, Utf8NextBoundary(strDisplay, 0));
		nTextW = Measure(strDisplay);
	}

	nFirstGlyph = nTempGlyphs - Utf8GlyphCount(strDisplay);

		if (IsHoveredSimple(x, y, w, h) && H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed)
		{
			const int nLocalX = (std::max)(0, H::Input->GetMouseX() - (x + CFG::Menu_Spacing_X));
			size_t nPos = 0;
			int nGlyph = 0;
			int nBestGlyph = static_cast<int>(Utf8GlyphCount(strDisplay));
			int nBestDist = 1 << 30;

			while (true)
			{
				const int nDiff = nLocalX - Measure(strDisplay.substr(0, nPos));
				const int nDist = nDiff < 0 ? -nDiff : nDiff;

				if (nDist < nBestDist)
				{
					nBestDist = nDist;
					nBestGlyph = nGlyph;
				}

				if (nPos >= strDisplay.size())
					break;

				nPos = Utf8NextBoundary(strDisplay, nPos);
				nGlyph++;
			}

			nCaret = nAnchor = static_cast<int>(Utf8GlyphOffset(strTemp, nFirstGlyph + static_cast<size_t>(nBestGlyph)));
			m_bClickConsumed = true;
		}

		if (!m_bClickConsumed && H::Input->IsPressed(VK_LBUTTON))
		{
			m_mapStates[&strOutput] = false;
			strUndo.clear();
		}

		const int nTextX = x + CFG::Menu_Spacing_X;
		const int nTextY = y + h - H::Fonts->Get(EFonts::Menu).m_nTall - 2;
		const int nSelStart = (std::min)(nCaret, nAnchor);
		const int nSelEnd = (std::max)(nCaret, nAnchor);

		if (nSelStart != nSelEnd)
		{
			const int nSelGlyphStart = static_cast<int>(Utf8GlyphCount(strTemp.substr(0, static_cast<size_t>(nSelStart)))) - static_cast<int>(nFirstGlyph);
			const int nSelGlyphEnd = static_cast<int>(Utf8GlyphCount(strTemp.substr(0, static_cast<size_t>(nSelEnd)))) - static_cast<int>(nFirstGlyph);
			const int nDisplayGlyphs = static_cast<int>(Utf8GlyphCount(strDisplay));
			const int nLo = (std::max)(0, nSelGlyphStart);
			const int nHi = (std::min)(nDisplayGlyphs, nSelGlyphEnd);

			if (nLo < nHi)
			{
				const size_t nLoByte = Utf8GlyphOffset(strDisplay, static_cast<size_t>(nLo));
				const size_t nHiByte = Utf8GlyphOffset(strDisplay, static_cast<size_t>(nHi));
				const int nSelX = nTextX + Measure(strDisplay.substr(0, nLoByte));
				const int nSelW = Measure(strDisplay.substr(nLoByte, nHiByte - nLoByte));

				H::LateRender->Rect(nSelX, nTextY, nSelW, H::Fonts->Get(EFonts::Menu).m_nTall, Color_t{ clr.r, clr.g, clr.b, 80 });
			}
		}

		H::LateRender->String(
			H::Fonts->Get(EFonts::Menu),
			nTextX,
			nTextY,
			CFG::Menu_Text_Active,
			POS_DEFAULT, strDisplay.c_str(), {}
		);

		if (fmod(I::GlobalVars->realtime, 1.0f) < 0.5f)
		{
			const int nDisplayGlyphs = static_cast<int>(Utf8GlyphCount(strDisplay));
			const int nCaretGlyph = (std::clamp)(static_cast<int>(Utf8GlyphCount(strTemp.substr(0, static_cast<size_t>(nCaret)))) - static_cast<int>(nFirstGlyph), 0, nDisplayGlyphs);
			const size_t nCaretByte = Utf8GlyphOffset(strDisplay, static_cast<size_t>(nCaretGlyph));
			const int nCaretX = (std::min)(nTextX + Measure(strDisplay.substr(0, nCaretByte)), x + w - CFG::Menu_Spacing_X);

			H::LateRender->Rect(nCaretX, nTextY, 1, H::Fonts->Get(EFonts::Menu).m_nTall, CFG::Menu_Text_Active);
		}

		if (H::Input->IsPressed(VK_RETURN)) {
			bCallback = true;
			m_mapStates[&strOutput] = false;
			strUndo.clear();
		}
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
				MarkConfigChanged();
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
				MarkConfigChanged();
			}
		}
	}
	m_nCursorY += h + nTextH + CFG::Menu_Spacing_Y + CFG::Menu_Spacing_Y;
	return bCallback;
}
struct ColorPickerState {
	float hue = 0.8f;
	float saturation = 1.0f;
	float value = 1.0f;
	int svTextureID = -1;
	int hueTextureID = -1;
	float lastHue = -1.0f;
	float alpha = 1.0f;
};
std::unordered_map<Color_t*, ColorPickerState> m_mapColorStates;
Color_t HSVtoRGB(float h, float s, float v) {
	float r, g, b;
	int i = int(h * 6);
	float f = h * 6 - i;
	float p = v * (1 - s);
	float q = v * (1 - f * s);
	float t = v * (1 - (1 - f) * s);
	switch (i % 6) {
	case 0: r = v, g = t, b = p; break;
	case 1: r = q, g = v, b = p; break;
	case 2: r = p, g = v, b = t; break;
	case 3: r = p, g = q, b = v; break;
	case 4: r = t, g = p, b = v; break;
	case 5: r = v, g = p, b = q; break;
	}
	return Color_t(
		static_cast<int>(r * 255),
		static_cast<int>(g * 255),
		static_cast<int>(b * 255),
		255
	);
}
void RGBtoHSV(const Color_t& rgb, float& h, float& s, float& v)
{
	float r = rgb.r / 255.0f;
	float g = rgb.g / 255.0f;
	float b = rgb.b / 255.0f;
	float max = std::max({ r, g, b });
	float min = std::min({ r, g, b });
	float delta = max - min;
	v = max;
	s = (max > 0.0f) ? (delta / max) : 0.0f;
	if (delta == 0.0f)
		h = 0.0f;
	else if (max == r)
		h = (g - b) / delta + (g < b ? 6.0f : 0.0f);
	else if (max == g)
		h = (b - r) / delta + 2.0f;
	else
		h = (r - g) / delta + 4.0f;
	h /= 6.0f;
}
bool CMenu::ColorPicker(const char* szLabel, Color_t& colVar)
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
		for (auto& pair : m_mapContextMenuStates)
			pair.second = false;
		m_mapStates[&colVar] = !m_mapStates[&colVar];
		if (m_mapStates[&colVar]) {
			ColorPickerState& state = m_mapColorStates[&colVar];
			float h, s, v;
			RGBtoHSV(colVar, h, s, v);
			state.hue = h;
			state.saturation = s;
			state.value = v;
			state.alpha = colVar.a / 255.0f;
		}
		m_bClickConsumed = true;
	}
	if (bHovered && H::Input->IsPressed(VK_RBUTTON) && !m_bClickConsumed) {
		for (auto& pair : m_mapContextMenuStates)
			pair.second = false;
		m_mapContextMenuStates[&colVar] = true;
		m_mapStates[&colVar] = false;
		m_bClickConsumed = true;
	}
	if (H::Input->IsPressed(VK_ESCAPE) || H::Input->IsPressed(VK_INSERT) || H::Input->IsPressed(VK_F3)) {
		m_mapStates[&colVar] = false;
		m_mapContextMenuStates[&colVar] = false;
	}
	H::Draw->Rect(x, y, w, h, colVar);
	H::Draw->OutlinedRect(x, y, w, h, CFG::Menu_Accent_Primary);
	H::Draw->String(
		H::Fonts->Get(EFonts::Menu),
		x + w + CFG::Menu_Spacing_X,
		y + (h / 2),
		(bHovered || m_mapStates[&colVar]) ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive,
		POS_CENTERY, szLabel
	);
	if (m_mapContextMenuStates[&colVar])
	{
		int menuPadding = 4;
		int buttonHeight = 18;
		int buttonSpacing = 4;
		int menuWidth = 80;
		int menuHeight = menuPadding + buttonHeight + buttonSpacing + buttonHeight + menuPadding;
		int menuX = x + w_with_text + 5;
		int menuY = y;
		H::LateRender->Rect(menuX, menuY, menuWidth, menuHeight, CFG::Menu_Background);
		H::LateRender->OutlinedRect(menuX, menuY, menuWidth, menuHeight, CFG::Menu_Accent_Primary);
		int copyButtonX = menuX + menuPadding;
		int copyButtonY = menuY + menuPadding;
		int copyButtonW = menuWidth - menuPadding * 2;
		int pasteButtonX = menuX + menuPadding;
		int pasteButtonY = copyButtonY + buttonHeight + buttonSpacing;
		int pasteButtonW = menuWidth - menuPadding * 2;
		bool hoveredCopy = IsHovered(copyButtonX, copyButtonY, copyButtonW, buttonHeight, &colVar);
		bool hoveredPaste = IsHovered(pasteButtonX, pasteButtonY, pasteButtonW, buttonHeight, &colVar);
		bool hoveredMenu = IsHovered(menuX, menuY, menuWidth, menuHeight, &colVar);
		if (H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed && !hoveredMenu)
			m_mapContextMenuStates[&colVar] = false;
		if (H::Input->IsPressed(VK_LBUTTON) && hoveredCopy && !m_bClickConsumed)
		{
			m_copiedColor = colVar;
			m_mapContextMenuStates[&colVar] = false;
			m_bClickConsumed = true;
		}
		else if (H::Input->IsPressed(VK_LBUTTON) && hoveredPaste && !m_bClickConsumed)
		{
			colVar = m_copiedColor;
			if (m_mapColorStates.count(&colVar))
			{
				ColorPickerState& state = m_mapColorStates[&colVar];
				float h, s, v;
				RGBtoHSV(colVar, h, s, v);
				state.hue = h;
				state.saturation = s;
				state.value = v;
				state.alpha = colVar.a / 255.0f;
			}
			m_mapContextMenuStates[&colVar] = false;
			m_bClickConsumed = true;
		}
		H::LateRender->Rect(copyButtonX, copyButtonY, copyButtonW, buttonHeight, hoveredCopy ? CFG::Menu_Accent_Primary : CFG::Menu_Background);
		H::LateRender->OutlinedRect(copyButtonX, copyButtonY, copyButtonW, buttonHeight, CFG::Menu_Accent_Primary);
		H::LateRender->String(
			H::Fonts->Get(EFonts::Menu),
			copyButtonX + copyButtonW / 2,
			copyButtonY + buttonHeight / 2,
			hoveredCopy ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive,
			POS_CENTERXY,
			"Copy",
			ClipRegion_t{}
		);
		H::LateRender->Rect(pasteButtonX, pasteButtonY, pasteButtonW, buttonHeight, hoveredPaste ? CFG::Menu_Accent_Primary : CFG::Menu_Background);
		H::LateRender->OutlinedRect(pasteButtonX, pasteButtonY, pasteButtonW, buttonHeight, CFG::Menu_Accent_Primary);
		H::LateRender->String(
			H::Fonts->Get(EFonts::Menu),
			pasteButtonX + pasteButtonW / 2,
			pasteButtonY + buttonHeight / 2,
			hoveredPaste ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive,
			POS_CENTERXY,
			"Paste",
			ClipRegion_t{}
		);
	}
	if (m_mapStates[&colVar])
	{
		int pickerX = x;
		int pickerY = m_nCursorY + h + CFG::Menu_Spacing_Y;
		int pickerW = 200;
		int pickerH = 200;
		int hueBarWidth = 12;
		int alphaBarWidth = 12;
		int padding = 4;
		int totalW = pickerW + padding + hueBarWidth + padding + alphaBarWidth + padding * 2;
		int totalH = pickerH + padding * 2;
		H::LateRender->Rect(pickerX - padding, pickerY - padding, totalW, totalH, CFG::Menu_Background);
		H::LateRender->OutlinedRect(pickerX - padding, pickerY - padding, totalW, totalH, CFG::Menu_Accent_Primary);
		ColorPickerState& state = m_mapColorStates[&colVar];
		bool hoveredSV = IsHovered(pickerX, pickerY, pickerW, pickerH, &colVar);
		bool hoveredHue = IsHovered(pickerX + pickerW + padding, pickerY, hueBarWidth, pickerH, &colVar);
		bool hoveredAlpha = IsHovered(pickerX + pickerW + padding + hueBarWidth + padding, pickerY, alphaBarWidth, pickerH, &colVar);
		bool anyHovered = hoveredSV || hoveredHue || hoveredAlpha;
		if (H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed && !anyHovered)
			m_mapStates[&colVar] = false;
		if (H::Input->IsHeld(VK_LBUTTON) && hoveredSV)
		{
			int mouseX = H::Input->GetMouseX();
			int mouseY = H::Input->GetMouseY();
			state.saturation = std::clamp((float)(mouseX - pickerX) / pickerW, 0.0f, 1.0f);
			state.value = 1.0f - std::clamp((float)(mouseY - pickerY) / pickerH, 0.0f, 1.0f);
			colVar = HSVtoRGB(state.hue, state.saturation, state.value);
			colVar.a = (unsigned char)(state.alpha * 255.0f);
			m_bClickConsumed = true;
		}
		else if (H::Input->IsHeld(VK_LBUTTON) && hoveredHue)
		{
			int mouseY = H::Input->GetMouseY();
			state.hue = std::clamp((float)(mouseY - pickerY) / pickerH, 0.0f, 1.0f);
			colVar = HSVtoRGB(state.hue, state.saturation, state.value);
			colVar.a = (unsigned char)(state.alpha * 255.0f);
			m_bClickConsumed = true;
		}
		else if (H::Input->IsHeld(VK_LBUTTON) && hoveredAlpha)
		{
			int mouseY = H::Input->GetMouseY();
			state.alpha = std::clamp((float)(mouseY - pickerY) / pickerH, 0.0f, 1.0f);
			colVar.a = (unsigned char)(state.alpha * 255.0f);
			m_bClickConsumed = true;
		}
		int step = 2;
		for (int j = 0; j < pickerH; j += step)
		{
			for (int i = 0; i < pickerW; i += step)
			{
				float s = (float)i / pickerW;
				float v = 1.0f - ((float)j / pickerH);
				Color_t col = HSVtoRGB(state.hue, s, v);
				H::LateRender->Rect(pickerX + i, pickerY + j, step, step, col);
			}
		}
		for (int j = 0; j < pickerH; j += step)
		{
			float h = (float)j / pickerH;
			Color_t hueColor = HSVtoRGB(h, 1.0f, 1.0f);
			H::LateRender->Rect(pickerX + pickerW + padding, pickerY + j, hueBarWidth, step, hueColor);
		}
		int checkerSize = 4;
		for (int j = 0; j < pickerH; j += checkerSize)
		{
			for (int i = 0; i < alphaBarWidth; i += checkerSize)
			{
				bool isEven = ((i / checkerSize) + (j / checkerSize)) % 2 == 0;
				Color_t checkerColor = isEven ? Color_t{ 200, 200, 200, 255 } : Color_t{ 150, 150, 150, 255 };
				H::LateRender->Rect(pickerX + pickerW + padding + hueBarWidth + padding + i, pickerY + j, checkerSize, checkerSize, checkerColor);
			}
		}
		Color_t baseColor = HSVtoRGB(state.hue, state.saturation, state.value);
		for (int j = 0; j < pickerH; j += step)
		{
			float alpha = (float)j / pickerH;
			Color_t alphaColor = baseColor;
			alphaColor.a = (unsigned char)(alpha * 255.0f);
			H::LateRender->Rect(pickerX + pickerW + padding + hueBarWidth + padding, pickerY + j, alphaBarWidth, step, alphaColor);
		}
		H::LateRender->OutlinedRect(pickerX, pickerY, pickerW, pickerH, CFG::Menu_Accent_Primary);
		H::LateRender->OutlinedRect(pickerX + pickerW + padding, pickerY, hueBarWidth, pickerH, CFG::Menu_Accent_Primary);
		H::LateRender->OutlinedRect(pickerX + pickerW + padding + hueBarWidth + padding, pickerY, alphaBarWidth, pickerH, CFG::Menu_Accent_Primary);
		int cursorX = pickerX + (int)(state.saturation * pickerW);
		int cursorY = pickerY + (int)((1.0f - state.value) * pickerH);
		H::LateRender->OutlinedRect(cursorX - 3, cursorY - 3, 6, 6, { 0, 0, 0, 255 });
		H::LateRender->OutlinedRect(cursorX - 2, cursorY - 2, 4, 4, { 255, 255, 255, 255 });
		int hueCursorY = pickerY + (int)(state.hue * pickerH);
		H::LateRender->Rect(pickerX + pickerW + padding, hueCursorY - 1, hueBarWidth, 3, { 0, 0, 0, 255 });
		H::LateRender->Rect(pickerX + pickerW + padding + 1, hueCursorY, hueBarWidth - 2, 1, { 255, 255, 255, 255 });
		int alphaCursorY = pickerY + (int)(state.alpha * pickerH);
		H::LateRender->Rect(pickerX + pickerW + padding + hueBarWidth + padding, alphaCursorY - 1, alphaBarWidth, 3, { 0, 0, 0, 255 });
		H::LateRender->Rect(pickerX + pickerW + padding + hueBarWidth + padding + 1, alphaCursorY, alphaBarWidth - 2, 1, { 255, 255, 255, 255 });
	}
	m_nCursorY += h + CFG::Menu_Spacing_Y;
	return bCallback;
}
void CMenu::MainWindow()
{
	const char* currentMap = I::EngineClient->GetLevelName();
	std::string currentMapStr = currentMap ? currentMap : "";
	if (currentMapStr != MenuBackground::g_strLastMap)
	{
		MenuBackground::g_strLastMap = currentMapStr;
		MenuBackground::Invalidate();
	}
	if (CFG::Menu_Background_Image_Enabled && MenuBackground::g_bNeedRefresh && !MenuBackground::g_bImageLoaded)
	{
		MenuBackground::g_bNeedRefresh = false;
		std::string strFolderPath = "C:\\necromancer_tf2\\menu_backgrounds";
		std::string strReadmePath = strFolderPath + "\\README.txt";
		try
		{
			if (!std::filesystem::exists(strFolderPath))
				std::filesystem::create_directories(strFolderPath);
			if (!std::filesystem::exists(strReadmePath))
			{
				std::ofstream readme(strReadmePath);
				if (readme.is_open())
				{
					readme << "Place your background image in this folder.\n\n";
					readme << "Rename your image to 'image' with one of these extensions:\n";
					readme << "  - image.png\n";
					readme << "  - image.jpg\n";
					readme << "  - image.jpeg\n";
					readme << "  - image.bmp\n\n";
					readme << "Recommended resolution: " << CFG::Menu_Width << " x " << CFG::Menu_Height << " pixels.\n";
					readme << "Click 'Refresh' in menu to reload after changing the image.\n";
					readme.close();
				}
			}
			std::string strImagePath = "";
			const char* extensions[] = { ".png", ".jpg", ".jpeg", ".bmp" };
			for (const char* ext : extensions)
			{
				std::string testPath = strFolderPath + "\\image" + ext;
				if (std::filesystem::exists(testPath))
				{
					strImagePath = testPath;
					break;
				}
			}
			if (!strImagePath.empty())
			{
				int nChannels = 0;
				unsigned char* pImageData = stbi_load(strImagePath.c_str(), &MenuBackground::g_nWidth, &MenuBackground::g_nHeight, &nChannels, 4);
				if (pImageData && MenuBackground::g_nWidth > 0 && MenuBackground::g_nHeight > 0)
				{
					for (int i = 0; i < MenuBackground::g_nWidth * MenuBackground::g_nHeight; i++)
					{
						std::swap(pImageData[i * 4 + 0], pImageData[i * 4 + 2]);
					}
					size_t dataSize = static_cast<size_t>(MenuBackground::g_nWidth) * MenuBackground::g_nHeight * 4;
					MenuBackground::g_vecImageData.assign(pImageData, pImageData + dataSize);
					MenuBackground::g_bImageLoaded = true;
					MenuBackground::g_nTextureId = -1;
					stbi_image_free(pImageData);
				}
				else if (pImageData)
				{
					stbi_image_free(pImageData);
				}
			}
		}
		catch (...) { }
	}
	if (CFG::Menu_Background_Image_Enabled && MenuBackground::g_bImageLoaded && MenuBackground::g_nTextureId == -1)
	{
		if (!MenuBackground::g_vecImageData.empty() && I::MatSystemSurface)
		{
			MenuBackground::g_nTextureId = I::MatSystemSurface->CreateNewTextureID(true);
			if (MenuBackground::g_nTextureId != -1)
			{
				I::MatSystemSurface->DrawSetTextureRGBAEx(
					MenuBackground::g_nTextureId,
					MenuBackground::g_vecImageData.data(),
					MenuBackground::g_nWidth,
					MenuBackground::g_nHeight,
					IMAGE_FORMAT_BGRA8888
				);
			}
		}
	}
	if (!CFG::Menu_Background_Image_Enabled && MenuBackground::g_bImageLoaded)
	{
		MenuBackground::Reset();
	}
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
	float scale = 0.9f + m_flMenuOpenProgress * 0.1f;
	byte alpha = static_cast<byte>(m_flMenuOpenProgress * 255);
	int scaledW = static_cast<int>(CFG::Menu_Width * scale);
	int scaledH = static_cast<int>(CFG::Menu_Height * scale);
	int offsetX = (CFG::Menu_Width - scaledW) / 2;
	int offsetY = (CFG::Menu_Height - scaledH) / 2;
	int menuX = CFG::Menu_Pos_X + offsetX;
	int menuY = CFG::Menu_Pos_Y + offsetY;
	Color_t blurColor = { 0, 0, 0, static_cast<byte>(25 * (alpha / 255.0f)) };
	for (int i = 3; i >= 1; i--)
	{
		H::Draw->Rect(menuX - i, menuY - i, scaledW + i * 2, scaledH + i * 2, blurColor);
	}
	Color_t bgColor = CFG::Menu_Background;
	bgColor.a = static_cast<byte>((bgColor.a / 255.0f) * alpha);
	H::Draw->Rect(menuX, menuY, scaledW, scaledH, bgColor);
	if (CFG::Menu_Background_Image_Enabled && MenuBackground::g_nTextureId != -1 && I::MatSystemSurface)
	{
		byte imgAlpha = static_cast<byte>(CFG::Menu_Background_Image_Transparency * 255 * (alpha / 255.0f));
		I::MatSystemSurface->DrawSetColor(255, 255, 255, imgAlpha);
		I::MatSystemSurface->DrawSetTexture(MenuBackground::g_nTextureId);
		I::MatSystemSurface->DrawTexturedRect(menuX, menuY, menuX + scaledW, menuY + scaledH);
	}
	Color_t borderColor = CFG::Menu_Accent_Primary;
	borderColor.a = alpha;
	H::Draw->OutlinedRect(menuX, menuY, scaledW, scaledH, borderColor);
	int dragBarY = static_cast<int>((CFG::Menu_Drag_Bar_Height - 1) * scale);
	H::Draw->Line(menuX, menuY + dragBarY, menuX + scaledW - 1, menuY + dragBarY, borderColor);
	if (m_flMenuOpenProgress < 0.3f)
		return;
	m_nCursorX = CFG::Menu_Pos_X + CFG::Menu_Spacing_X;
	m_nCursorY = CFG::Menu_Pos_Y + CFG::Menu_Drag_Bar_Height + CFG::Menu_Spacing_Y;
	enum class EMainTabs { AIM, VISUALS, EXPLOITS, MISC, NAVBOT, PLAYERS, CONFIGS, SOUND, NETWORK, PACMAN, PLAYER_DETAILS };
	static EMainTabs MainTab = EMainTabs::AIM;
	static int nSelectedPlayerIndex = -1;
	static bool bPlayerSearchFocused = false;
	static bool bNetChatFocused = false;
	static std::string strNetChatInput = {};
	if (MainTab != EMainTabs::PLAYERS)
	{
		bPlayerSearchFocused = false;
		m_bWantTextInput = false;
	}
	if (MainTab != EMainTabs::NETWORK)
	{
		bNetChatFocused = false;
		m_bWantTextInput = false;
	}
	auto DrawTabBox = [&](const char* label, int type, bool active) -> bool {
		static float animTime = 0.0f;
		animTime += I::GlobalVars->frametime * 0.8f;
		int x = m_nCursorX;
		int y = m_nCursorY;
		int boxW = CFG::Menu_Tab_Button_Width;
		int boxH = 50;
		bool bHovered = IsHovered(x, y, boxW, boxH, nullptr);
		bool bCallback = false;
		if (bHovered && H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed)
			bCallback = m_bClickConsumed = true;
		std::string tabId = std::string("tab_") + label;
		if (m_buttonHoverStates.find(tabId) == m_buttonHoverStates.end())
			m_buttonHoverStates[tabId] = 0.0f;
		float& hoverState = m_buttonHoverStates[tabId];
		float targetHover = (bHovered || active) ? 1.0f : 0.0f;
		hoverState += (targetHover - hoverState) * 8.0f * I::GlobalVars->frametime;
		Color_t accentColor = CFG::Menu_Accent_Primary;
		Color_t bgColor = { accentColor.r, accentColor.g, accentColor.b, static_cast<byte>(20 + hoverState * 40) };
		Color_t glowColor = { accentColor.r, accentColor.g, accentColor.b, static_cast<byte>(hoverState * 50) };
		Color_t iconColor = active ? accentColor : CFG::Menu_Text_Inactive;
		if (hoverState > 0.01f) {
			H::Draw->Rect(x - 1, y - 1, boxW + 2, boxH + 2, glowColor);
		}
		H::Draw->GradientRect(x, y, boxW, boxH, bgColor, { bgColor.r, bgColor.g, bgColor.b, static_cast<byte>(bgColor.a * 0.5f) }, false);
		H::Draw->OutlinedRect(x, y, boxW, boxH, accentColor);
		int iconSize = 36;
		int iconX = x + (boxW - iconSize) / 2;
		int iconY = y + 4;
		int centerX = iconX + iconSize / 2;
		int centerY = iconY + iconSize / 2;
		int frame = static_cast<int>(animTime) % 2;
		float pulse = active ? (1.0f + std::sin(animTime * 1.5f) * 0.12f) : 1.0f;
		switch(type) {
			case 0:
			{
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
				H::Draw->OutlinedCircle(centerX, centerY, static_cast<int>(11 * pulse), 20, scopeDark);
				H::Draw->OutlinedCircle(centerX, centerY, static_cast<int>(9 * pulse), 20, scopeGray);
				H::Draw->OutlinedCircle(centerX, centerY, static_cast<int>(7 * pulse), 16, scopeGray);
				H::Draw->Rect(centerX - 12, centerY - 1, 3, 2, targetRed);
				H::Draw->Rect(centerX + 10, centerY - 1, 3, 2, targetRed);
				H::Draw->Rect(centerX - 1, centerY - 12, 2, 3, targetRed);
				H::Draw->Rect(centerX - 1, centerY + 10, 2, 3, targetRed);
				if (frame == 0 || active) {
					H::Draw->Rect(centerX - 1, centerY - 1, 2, 2, centerDot);
				}
				for (int i = 1; i <= 3; i++) {
					H::Draw->Rect(centerX - 1, centerY - 3 * i - 4, 2, 2, scopeGray);
					H::Draw->Rect(centerX - 1, centerY + 3 * i + 2, 2, 2, scopeGray);
					H::Draw->Rect(centerX - 3 * i - 4, centerY - 1, 2, 2, scopeGray);
					H::Draw->Rect(centerX + 3 * i + 2, centerY - 1, 2, 2, scopeGray);
				}
				break;
			}
			case 1:
			{
				Color_t eyeWhite = { 240, 240, 250, 255 };
				Color_t eyeOutline = { 80, 80, 100, 255 };
				Color_t pupilBlack = { 20, 20, 30, 255 };
				Color_t lashBrown = { 60, 50, 40, 255 };
				Color_t irisColors[6] = {
					{ 100, 150, 220, 255 },
					{ 100, 200, 120, 255 },
					{ 180, 100, 200, 255 },
					{ 220, 150, 100, 255 },
					{ 200, 100, 120, 255 },
					{ 100, 180, 180, 255 }
				};
				float fullCycle = active ? std::fmod(animTime * 0.25f, 25.8f) : 0.0f;
				int blinkNumber = static_cast<int>(fullCycle / 4.3f);
				float blinkCycle = std::fmod(fullCycle, 4.3f);
				int eyeCenterX = centerX;
				int eyeCenterY = iconY + 16;
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
				if (eyeOpenness > 0.02f) {
					int almondWidths[11] = { 4, 10, 16, 20, 22, 24, 22, 20, 16, 10, 4 };
					int maxHeight = 11;
					int visibleHeight = static_cast<int>(maxHeight * eyeOpenness);
					if (visibleHeight < 1) visibleHeight = 1;
					int halfVisible = visibleHeight / 2;
					int startRow = 5 - halfVisible;
					int endRow = 5 + halfVisible;
					if (visibleHeight % 2 == 0 && visibleHeight > 1) startRow++;
					for (int row = startRow; row <= endRow && row < 11; row++) {
						if (row < 0) continue;
						int rowWidth = almondWidths[row];
						int rowY = eyeCenterY - 5 + row;
						int halfWidth = rowWidth / 2;
						int rowX = eyeCenterX - halfWidth;
						H::Draw->Rect(rowX, rowY, rowWidth - 1, 1, eyeWhite);
					}
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
					H::Draw->Rect(eyeCenterX - 12, eyeCenterY, 1, 1, eyeOutline);
					H::Draw->Rect(eyeCenterX + 11, eyeCenterY, 1, 1, eyeOutline);
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
					if (eyeOpenness > 0.3f) {
						int irisRadius = static_cast<int>(5.0f * eyeOpenness);
						if (irisRadius > 0) {
							H::Draw->FilledCircle(eyeCenterX, eyeCenterY, irisRadius, 16, irisColor);
							H::Draw->OutlinedCircle(eyeCenterX, eyeCenterY, irisRadius, 16, eyeOutline);
						}
						if (eyeOpenness > 0.5f) {
							int pupilRadius = static_cast<int>(2.0f * eyeOpenness);
							if (pupilRadius > 0) {
								H::Draw->FilledCircle(eyeCenterX, eyeCenterY, pupilRadius, 12, pupilBlack);
							}
							H::Draw->Rect(eyeCenterX - 2, eyeCenterY - 2, 2, 2, eyeWhite);
						}
					}
				}
				if (eyeOpenness > 0.6f) {
					H::Draw->Line(eyeCenterX - 10, eyeCenterY - 4, eyeCenterX - 13, eyeCenterY - 8, lashBrown);
					H::Draw->Line(eyeCenterX - 5, eyeCenterY - 5, eyeCenterX - 6, eyeCenterY - 9, lashBrown);
					H::Draw->Line(eyeCenterX, eyeCenterY - 5, eyeCenterX, eyeCenterY - 10, lashBrown);
					H::Draw->Line(eyeCenterX + 5, eyeCenterY - 5, eyeCenterX + 6, eyeCenterY - 9, lashBrown);
					H::Draw->Line(eyeCenterX + 10, eyeCenterY - 4, eyeCenterX + 13, eyeCenterY - 8, lashBrown);
					H::Draw->Line(eyeCenterX - 6, eyeCenterY + 4, eyeCenterX - 7, eyeCenterY + 6, lashBrown);
					H::Draw->Line(eyeCenterX, eyeCenterY + 5, eyeCenterX, eyeCenterY + 8, lashBrown);
					H::Draw->Line(eyeCenterX + 6, eyeCenterY + 4, eyeCenterX + 7, eyeCenterY + 6, lashBrown);
				}
				break;
			}
			case 2:
			{
				Color_t wrenchSilver = { 180, 180, 190, 255 };
				Color_t wrenchDark = { 100, 100, 110, 255 };
				Color_t gearGold = { 220, 180, 80, 255 };
				Color_t gearDark = { 160, 130, 50, 255 };
				Color_t boltGray = { 140, 140, 150, 255 };
				if (!active) {
					wrenchSilver.a = 200;
					gearGold.a = 200;
				}
				H::Draw->Rect(iconX + 20, iconY + 6, 4, 17, wrenchSilver);
				H::Draw->OutlinedRect(iconX + 20, iconY + 6, 4, 17, wrenchDark);
				H::Draw->OutlinedRect(iconX + 18, iconY + 4, 8, 6, wrenchDark);
				H::Draw->Rect(iconX + 20, iconY + 5, 4, 4, wrenchSilver);
				H::Draw->Line(iconX + 19, iconY + 6, iconX + 19, iconY + 8, wrenchDark);
				H::Draw->Line(iconX + 25, iconY + 6, iconX + 25, iconY + 8, wrenchDark);
				float gearAngle = active ? (animTime * 15.0f) : 0.0f;
				for (int i = 0; i < 8; i++) {
					float angle = (i * 45.0f + gearAngle) * (3.14159f / 180.0f);
					int x1 = iconX + 11 + static_cast<int>(std::cos(angle) * 6);
					int y1 = iconY + 17 + static_cast<int>(std::sin(angle) * 6);
					H::Draw->Rect(x1 - 1, y1 - 1, 3, 3, gearGold);
				}
				H::Draw->OutlinedCircle(iconX + 11, iconY + 17, 4, 12, gearDark);
				H::Draw->OutlinedCircle(iconX + 11, iconY + 17, 2, 8, gearDark);
				H::Draw->Rect(iconX + 10, iconY + 16, 3, 3, gearGold);
				H::Draw->Rect(iconX + 9, iconY + 15, 2, 2, boltGray);
				H::Draw->Rect(iconX + 12, iconY + 15, 2, 2, boltGray);
				H::Draw->Rect(iconX + 9, iconY + 18, 2, 2, boltGray);
				H::Draw->Rect(iconX + 12, iconY + 18, 2, 2, boltGray);
				break;
			}
			case 3:
			{
				Color_t maskBlue = { 70, 80, 110, 255 };
				Color_t skinPeach = { 240, 180, 140, 255 };
				Color_t eyeWhite = { 255, 255, 255, 255 };
				Color_t eyeDark = { 40, 50, 70, 255 };
				Color_t mouthDark = { 20, 20, 20, 255 };
				Color_t cigBrown = { 100, 70, 50, 255 };
				Color_t cigEmber = { 255, 140, 40, 255 };
				Color_t collarBeige = { 220, 210, 180, 255 };
				Color_t suitDark = { 50, 60, 80, 255 };
				Color_t tieDark = { 30, 35, 50, 255 };
				if (!active) {
					maskBlue.a = 200;
					skinPeach.a = 200;
					cigEmber.a = 200;
				}
				H::Draw->Line(iconX + 15, iconY + 0, iconX + 21, iconY + 0, maskBlue);
				H::Draw->Line(iconX + 13, iconY + 1, iconX + 23, iconY + 1, maskBlue);
				H::Draw->Line(iconX + 11, iconY + 2, iconX + 25, iconY + 2, maskBlue);
				H::Draw->Line(iconX + 10, iconY + 3, iconX + 26, iconY + 3, maskBlue);
				H::Draw->Line(iconX + 9, iconY + 4, iconX + 27, iconY + 4, maskBlue);
				H::Draw->Line(iconX + 8, iconY + 5, iconX + 28, iconY + 5, maskBlue);
				H::Draw->Line(iconX + 8, iconY + 6, iconX + 28, iconY + 6, maskBlue);
				H::Draw->Line(iconX + 8, iconY + 7, iconX + 28, iconY + 7, maskBlue);
				H::Draw->Line(iconX + 8, iconY + 8, iconX + 28, iconY + 8, maskBlue);
				H::Draw->Line(iconX + 11, iconY + 8, iconX + 14, iconY + 8, eyeDark);
				H::Draw->Line(iconX + 15, iconY + 8, iconX + 16, iconY + 8, skinPeach);
				H::Draw->Line(iconX + 17, iconY + 8, iconX + 20, iconY + 8, eyeDark);
				H::Draw->Line(iconX + 8, iconY + 9, iconX + 28, iconY + 9, maskBlue);
				H::Draw->Rect(iconX + 11, iconY + 9, 4, 1, eyeWhite);
				H::Draw->Rect(iconX + 12, iconY + 9, 2, 1, eyeDark);
				H::Draw->Rect(iconX + 15, iconY + 9, 2, 1, skinPeach);
				H::Draw->Rect(iconX + 17, iconY + 9, 4, 1, eyeWhite);
				H::Draw->Rect(iconX + 18, iconY + 9, 2, 1, eyeDark);
				H::Draw->Line(iconX + 8, iconY + 10, iconX + 28, iconY + 10, maskBlue);
				H::Draw->Line(iconX + 12, iconY + 10, iconX + 20, iconY + 10, skinPeach);
				H::Draw->Line(iconX + 9, iconY + 11, iconX + 27, iconY + 11, maskBlue);
				H::Draw->Rect(iconX + 14, iconY + 11, 1, 1, skinPeach);
				H::Draw->Rect(iconX + 15, iconY + 11, 1, 1, maskBlue);
				H::Draw->Rect(iconX + 16, iconY + 11, 1, 1, skinPeach);
				H::Draw->Line(iconX + 10, iconY + 12, iconX + 26, iconY + 12, maskBlue);
				H::Draw->Line(iconX + 14, iconY + 12, iconX + 20, iconY + 12, skinPeach);
				H::Draw->Line(iconX + 10, iconY + 13, iconX + 25, iconY + 13, maskBlue);
				H::Draw->Line(iconX + 14, iconY + 13, iconX + 15, iconY + 13, skinPeach);
				H::Draw->Rect(iconX + 16, iconY + 13, 4, 1, mouthDark);
				H::Draw->Line(iconX + 20, iconY + 13, iconX + 21, iconY + 13, cigBrown);
				H::Draw->Rect(iconX + 22, iconY + 13, 1, 1, cigEmber);
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
				H::Draw->Line(iconX + 11, iconY + 14, iconX + 24, iconY + 14, maskBlue);
				H::Draw->Line(iconX + 15, iconY + 14, iconX + 19, iconY + 14, skinPeach);
				H::Draw->Line(iconX + 12, iconY + 15, iconX + 22, iconY + 15, maskBlue);
				H::Draw->Line(iconX + 14, iconY + 16, iconX + 20, iconY + 16, maskBlue);
				H::Draw->Line(iconX + 15, iconY + 17, iconX + 19, iconY + 17, maskBlue);
				H::Draw->Line(iconX + 16, iconY + 18, iconX + 18, iconY + 18, maskBlue);
				H::Draw->Line(iconX + 15, iconY + 19, iconX + 19, iconY + 19, suitDark);
				H::Draw->Line(iconX + 14, iconY + 20, iconX + 20, iconY + 20, suitDark);
				H::Draw->Line(iconX + 8, iconY + 21, iconX + 28, iconY + 21, suitDark);
				H::Draw->Line(iconX + 6, iconY + 22, iconX + 30, iconY + 22, suitDark);
				H::Draw->Line(iconX + 5, iconY + 23, iconX + 31, iconY + 23, suitDark);
				H::Draw->Line(iconX + 11, iconY + 23, iconX + 14, iconY + 23, collarBeige);
				H::Draw->Line(iconX + 15, iconY + 23, iconX + 19, iconY + 23, tieDark);
				H::Draw->Line(iconX + 20, iconY + 23, iconX + 23, iconY + 23, collarBeige);
				for (int row = 24; row <= 32; row++) {
					H::Draw->Line(iconX + 5, iconY + row, iconX + 31, iconY + row, suitDark);
					int collarWidth = std::max(0, 5 - (row - 24) / 2);
					if (collarWidth > 0) {
						H::Draw->Line(iconX + 10, iconY + row, iconX + 10 + collarWidth, iconY + row, collarBeige);
						H::Draw->Line(iconX + 24 - collarWidth, iconY + row, iconX + 24, iconY + row, collarBeige);
					}
					H::Draw->Rect(iconX + 17, iconY + row, 2, 1, tieDark);
				}
				break;
			}
			case 4:
			{
				Color_t folderYellow = { 240, 200, 80, 255 };
				Color_t folderDark = { 200, 160, 60, 255 };
				Color_t paperWhite = { 250, 250, 250, 255 };
				Color_t textDark = { 40, 40, 40, 255 };
				if (!active) {
					folderYellow.a = 200;
					paperWhite.a = 200;
				}
				if (active) {
					int slideAmount = (frame == 0 ? 4 : 8);
					int paperY = iconY + 9 - slideAmount;
					int paperX = iconX + 10;
					int paperHeight = 18;
					int visibleHeight = std::min(slideAmount, paperHeight);
					H::Draw->Rect(paperX, paperY, 20, visibleHeight, paperWhite);
					H::Draw->OutlinedRect(paperX, paperY, 20, visibleHeight, textDark);
					if (visibleHeight > 2) H::Draw->Line(paperX + 2, paperY + 2, paperX + 17, paperY + 2, textDark);
					if (visibleHeight > 4) H::Draw->Line(paperX + 2, paperY + 4, paperX + 16, paperY + 4, textDark);
					if (visibleHeight > 6) H::Draw->Line(paperX + 2, paperY + 6, paperX + 18, paperY + 6, textDark);
				}
				H::Draw->Rect(iconX + 4, iconY + 9, 26, 13, folderYellow);
				H::Draw->OutlinedRect(iconX + 4, iconY + 9, 26, 13, folderDark);
				H::Draw->Rect(iconX + 4, iconY + 6, 14, 4, folderYellow);
				H::Draw->OutlinedRect(iconX + 4, iconY + 6, 14, 4, folderDark);
				break;
			}
			case 5:
			{
				Color_t matrixBright = { 0, 255, 70, 255 };
				Color_t matrixMid = { 0, 200, 50, 255 };
				Color_t matrixDim = { 0, 150, 40, 200 };
				Color_t matrixDark = { 0, 80, 20, 150 };
				if (!active) {
					matrixBright.a = 180;
					matrixMid.a = 180;
					matrixDim.a = 150;
					matrixDark.a = 100;
				}
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
				if (active) {
					for (int col = 0; col < 6; col++) {
						matrixOffsets[col] += matrixSpeeds[col] * I::GlobalVars->frametime * 2.0f;
						if (matrixOffsets[col] >= 1.0f) {
							matrixOffsets[col] = 0.0f;
							for (int row = 5; row > 0; row--) {
								matrixDigits[col][row] = matrixDigits[col][row - 1];
							}
							matrixDigits[col][0] = rand() % 10;
						}
					}
				}
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
						if (rowY < iconY || rowY > iconY + iconSize - 4)
							continue;
						Color_t digitColor;
						if (active) {
							if (row == 0) digitColor = matrixBright;
							else if (row == 1) digitColor = matrixMid;
							else if (row < 4) digitColor = matrixDim;
							else digitColor = matrixDark;
						} else {
							digitColor = matrixDim;
						}
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
			case 6:
			{
				int ox = iconX + 2;
				int oy = iconY - 5;
				static float s_flSniperAngle = 0.0f;
				if (active && I::GlobalVars)
					s_flSniperAngle += I::GlobalVars->frametime * 200.0f;
				float fAngle = fmodf(s_flSniperAngle, 360.0f);
				if (fAngle < 0.0f) fAngle += 360.0f;
				int nFrame = static_cast<int>(fAngle / 45.0f) % 8;
				auto PX = [&](int x, int y, int w, int h, Color_t c) {
					if (!active) c.a = 180;
					H::Draw->Rect(ox + x, oy + y, w, h, c);
				};
				if (nFrame == 0)
				{
					PX(13,4,1,3,{51,43,43,255}); PX(12,6,1,2,{64,58,58,255}); PX(13,7,1,1,{47,39,39,255}); PX(11,8,1,1,{26,20,20,255});
					PX(12,8,1,1,{32,24,24,255}); PX(13,8,1,1,{104,66,59,255}); PX(14,8,1,1,{75,40,41,255}); PX(10,9,1,1,{34,28,28,255});
					PX(11,9,1,1,{23,17,18,255}); PX(12,9,1,1,{33,28,28,255}); PX(13,9,1,1,{106,63,62,255}); PX(14,9,1,1,{122,68,66,255});
					PX(10,10,1,1,{33,27,27,255}); PX(11,10,1,1,{23,18,18,255}); PX(12,10,1,1,{27,19,19,255}); PX(13,10,1,1,{36,20,22,255});
					PX(14,10,1,1,{76,35,40,255}); PX(15,10,1,1,{139,87,77,255}); PX(13,11,1,1,{31,18,20,255}); PX(14,11,1,1,{22,12,12,255});
					PX(15,11,1,1,{162,114,94,255}); PX(16,11,1,1,{202,137,115,255}); PX(9,12,1,1,{21,13,13,255}); PX(10,12,1,1,{25,19,19,255});
					PX(13,12,1,1,{32,19,19,255}); PX(14,12,1,1,{26,15,15,255}); PX(15,12,1,1,{116,79,66,255}); PX(16,12,1,1,{167,115,95,255});
					PX(17,12,1,1,{162,96,89,255}); PX(8,13,1,1,{25,14,14,255}); PX(9,13,1,1,{61,39,34,255}); PX(10,13,1,1,{22,12,12,255});
					PX(14,13,1,1,{88,56,51,255}); PX(15,13,1,1,{194,129,110,255}); PX(16,13,1,1,{111,72,62,255}); PX(17,13,1,1,{80,39,41,255});
					PX(18,13,1,1,{45,20,22,255}); PX(6,14,1,1,{63,41,34,255}); PX(7,14,1,1,{77,50,41,255}); PX(8,14,1,1,{31,26,26,255});
					PX(9,14,1,1,{45,28,24,255}); PX(10,14,1,1,{34,19,19,255}); PX(11,14,1,1,{114,74,64,255}); PX(12,14,1,1,{118,83,69,255});
					PX(13,14,1,1,{56,44,39,255}); PX(15,14,1,1,{157,109,91,255}); PX(16,14,1,1,{93,48,49,255}); PX(17,14,1,1,{56,22,25,255});
					PX(6,15,1,1,{53,34,28,255}); PX(7,15,1,1,{46,27,27,255}); PX(8,15,1,1,{32,23,22,255}); PX(9,15,1,1,{39,24,21,255});
					PX(10,15,1,1,{42,22,24,255}); PX(11,15,1,1,{106,66,60,255}); PX(12,15,1,1,{132,91,79,255}); PX(13,15,1,1,{109,86,78,255});
					PX(14,15,1,1,{48,31,30,255}); PX(15,15,1,1,{115,79,66,255}); PX(16,15,1,1,{196,137,112,255}); PX(17,15,1,1,{27,13,15,255});
					PX(6,16,1,1,{44,26,22,255}); PX(7,16,1,1,{41,24,24,255}); PX(8,16,1,1,{30,22,22,255}); PX(9,16,1,1,{36,21,21,255});
					PX(10,16,1,1,{38,21,21,255}); PX(11,16,1,1,{84,50,46,255}); PX(12,16,1,1,{92,61,54,255}); PX(13,16,1,1,{40,25,24,255});
					PX(14,16,1,1,{43,21,22,255}); PX(15,16,1,1,{103,64,54,255}); PX(16,16,1,1,{208,145,119,255}); PX(17,16,1,1,{83,37,42,255});
					PX(8,17,1,1,{36,23,21,255}); PX(9,17,1,1,{29,17,17,255}); PX(10,17,1,1,{21,10,10,255}); PX(11,17,1,1,{40,22,21,255});
					PX(12,17,1,1,{59,28,25,255}); PX(13,17,1,1,{95,35,30,255}); PX(14,17,1,1,{117,41,34,255}); PX(15,17,1,1,{83,31,30,255});
					PX(16,17,1,1,{97,63,53,255}); PX(17,17,1,1,{51,24,26,255}); PX(8,18,1,1,{43,27,22,255}); PX(12,18,1,1,{69,26,25,255});
					PX(13,18,1,1,{88,32,29,255}); PX(14,18,1,1,{67,26,26,255}); PX(15,18,1,1,{41,20,21,255}); PX(16,18,1,1,{32,18,19,255});
					PX(17,18,1,1,{23,16,16,255}); PX(18,18,1,1,{34,15,16,255}); PX(13,19,1,1,{19,11,13,255}); PX(14,19,1,1,{26,23,23,255});
					PX(15,19,1,1,{27,25,25,255}); PX(16,19,1,1,{25,21,21,255}); PX(17,19,1,1,{19,15,15,255}); PX(18,19,1,1,{23,16,16,255});
					PX(19,19,1,1,{27,13,13,255}); PX(14,20,1,1,{25,22,22,255}); PX(15,20,1,1,{32,29,29,255}); PX(16,20,1,1,{30,24,24,255});
					PX(17,20,1,1,{35,31,31,255}); PX(18,20,1,1,{22,14,14,255}); PX(19,20,1,1,{25,13,13,255}); PX(14,21,1,1,{27,19,18,255});
					PX(15,21,1,1,{42,36,36,255}); PX(16,21,1,1,{29,23,23,255}); PX(17,21,1,1,{22,16,16,255}); PX(18,21,1,1,{19,13,13,255});
					PX(19,21,1,1,{17,11,11,255}); PX(14,22,1,1,{39,24,20,255}); PX(15,22,1,1,{54,34,29,255}); PX(16,22,1,1,{31,20,20,255});
					PX(17,22,1,1,{40,27,27,255}); PX(18,22,1,1,{34,23,23,255}); PX(19,22,1,1,{24,15,15,255}); PX(14,23,1,1,{45,28,23,255});
					PX(15,23,1,1,{55,34,30,255}); PX(16,23,1,1,{29,17,17,255}); PX(17,23,1,1,{33,21,20,255}); PX(18,23,1,1,{40,25,25,255});
					PX(19,23,1,1,{36,22,22,255}); PX(15,24,1,1,{51,35,31,255}); PX(16,24,1,1,{39,25,25,255}); PX(17,24,1,1,{20,12,12,255});
					PX(18,24,1,1,{39,28,24,255}); PX(19,24,1,1,{43,28,27,255}); PX(20,24,1,1,{31,18,18,255}); PX(14,25,1,1,{47,32,30,255});
					PX(15,25,1,1,{54,38,33,255}); PX(16,25,1,1,{28,18,18,255}); PX(19,25,1,1,{46,31,29,255}); PX(20,25,1,1,{32,19,19,255});
					PX(12,26,1,1,{51,37,32,255}); PX(13,26,1,1,{54,38,34,255}); PX(14,26,1,1,{49,33,30,255}); PX(15,26,1,1,{28,18,18,255});
					PX(18,26,1,1,{51,34,32,255}); PX(19,26,1,1,{44,30,27,255}); PX(11,27,1,1,{46,32,28,255}); PX(12,27,1,1,{52,36,32,255});
					PX(13,27,1,1,{44,28,27,255}); PX(14,27,1,1,{29,18,18,255}); PX(18,27,1,1,{50,35,31,255}); PX(19,27,1,1,{40,27,25,255});
					PX(11,28,1,1,{45,31,27,255}); PX(12,28,1,1,{48,32,30,255}); PX(13,28,1,1,{32,19,19,255}); PX(17,28,1,1,{29,18,17,255});
					PX(18,28,1,1,{53,37,33,255}); PX(19,28,1,1,{37,23,23,255}); PX(10,29,1,1,{29,19,17,255}); PX(11,29,1,1,{45,31,28,255});
					PX(12,29,1,1,{42,27,26,255}); PX(18,29,1,1,{47,33,29,255}); PX(19,29,1,1,{46,31,29,255}); PX(20,29,1,1,{32,20,20,255});
					PX(11,30,1,1,{41,25,22,255}); PX(12,30,1,1,{29,17,16,255}); PX(13,30,1,1,{31,17,17,255}); PX(18,30,1,1,{34,21,19,255});
					PX(19,30,1,1,{34,20,19,255}); PX(20,30,1,1,{26,15,15,255}); PX(21,30,1,1,{32,18,18,255});
				}
				else if (nFrame == 1)
				{
					PX(14,1,1,1,{63,55,55,255}); PX(14,2,1,5,{54,49,49,255}); PX(14,7,1,1,{63,51,52,255}); PX(12,8,1,1,{28,23,23,255});
					PX(13,8,1,1,{48,30,33,255}); PX(14,8,1,1,{123,77,70,255}); PX(15,8,1,1,{93,62,54,255}); PX(12,9,1,1,{37,31,32,255});
					PX(13,9,1,1,{34,29,29,255}); PX(14,9,1,1,{104,60,58,255}); PX(15,9,1,1,{168,97,92,255}); PX(16,9,1,1,{54,25,29,255});
					PX(12,10,1,1,{29,23,23,255}); PX(13,10,1,1,{25,19,19,255}); PX(14,10,1,1,{39,24,23,255}); PX(15,10,1,1,{104,62,57,255});
					PX(16,10,1,1,{181,116,101,255}); PX(17,10,1,1,{63,29,33,255}); PX(12,11,1,1,{27,21,21,255}); PX(13,11,1,1,{28,22,22,255});
					PX(14,11,1,1,{46,26,27,255}); PX(15,11,1,1,{43,20,23,255}); PX(16,11,1,1,{163,109,92,255}); PX(17,11,1,1,{176,114,99,255});
					PX(18,11,1,1,{61,28,32,255}); PX(12,12,1,1,{25,19,19,255}); PX(13,12,1,1,{32,25,25,255}); PX(14,12,1,1,{115,65,64,255});
					PX(15,12,1,1,{104,47,53,255}); PX(16,12,1,1,{46,30,26,255}); PX(17,12,1,1,{205,139,117,255}); PX(18,12,1,1,{134,71,71,255});
					PX(11,13,1,1,{41,24,24,255}); PX(12,13,1,1,{39,21,22,255}); PX(13,13,1,1,{34,20,21,255}); PX(14,13,1,1,{106,73,61,255});
					PX(15,13,1,1,{89,57,50,255}); PX(16,13,1,1,{40,25,23,255}); PX(17,13,1,1,{143,98,82,255}); PX(18,13,1,1,{99,58,53,255});
					PX(9,14,1,1,{41,24,23,255}); PX(10,14,1,1,{45,28,25,255}); PX(11,14,1,1,{35,20,20,255}); PX(12,14,1,1,{33,19,19,255});
					PX(13,14,1,1,{142,92,81,255}); PX(14,14,1,1,{139,95,79,255}); PX(15,14,1,1,{145,77,77,255}); PX(16,14,1,1,{37,22,20,255});
					PX(17,14,1,1,{94,34,32,255}); PX(18,14,1,1,{89,32,29,255}); PX(19,14,1,1,{41,18,20,255}); PX(9,15,1,1,{53,33,29,255});
					PX(10,15,1,2,{33,19,19,255}); PX(11,15,1,1,{47,25,26,255}); PX(12,15,1,1,{97,62,56,255}); PX(13,15,1,1,{130,84,74,255});
					PX(14,15,1,1,{177,117,100,255}); PX(15,15,1,1,{82,45,46,255}); PX(16,15,1,1,{62,34,33,255}); PX(17,15,1,1,{73,27,24,255});
					PX(18,15,1,1,{80,30,29,255}); PX(9,16,1,1,{35,22,19,255}); PX(11,16,1,1,{45,25,25,255}); PX(12,16,1,1,{62,40,36,255});
					PX(13,16,1,1,{148,103,85,255}); PX(14,16,1,1,{170,99,93,255}); PX(15,16,1,1,{53,28,25,255}); PX(16,16,1,1,{48,22,21,255});
					PX(17,16,1,1,{29,15,18,255}); PX(18,16,1,1,{28,13,15,255}); PX(10,17,1,1,{32,18,18,255}); PX(11,17,1,1,{43,24,24,255});
					PX(12,17,1,1,{91,58,51,255}); PX(13,17,1,1,{189,131,108,255}); PX(14,17,1,1,{134,69,71,255}); PX(15,17,1,1,{41,19,22,255});
					PX(16,17,1,1,{98,36,31,255}); PX(17,17,1,1,{40,18,17,255}); PX(18,17,1,1,{21,15,16,255}); PX(11,18,1,1,{40,22,22,255});
					PX(12,18,1,1,{62,25,25,255}); PX(13,18,1,1,{74,38,36,255}); PX(14,18,1,1,{32,19,19,255}); PX(15,18,1,1,{32,23,24,255});
					PX(16,18,1,1,{55,24,23,255}); PX(17,18,1,1,{76,29,25,255}); PX(18,18,1,1,{19,16,16,255}); PX(13,19,1,1,{34,25,25,255});
					PX(14,19,1,1,{26,20,20,255}); PX(15,19,1,1,{28,24,24,255}); PX(16,19,1,1,{29,20,20,255}); PX(17,19,1,1,{83,31,29,255});
					PX(18,19,1,1,{19,14,14,255}); PX(19,19,1,2,{20,14,14,255}); PX(13,20,1,1,{26,23,23,255}); PX(14,20,1,1,{26,21,21,255});
					PX(15,20,1,1,{31,26,26,255}); PX(16,20,1,1,{23,17,17,255}); PX(17,20,1,1,{56,22,23,255}); PX(18,20,1,1,{31,16,16,255});
					PX(13,21,1,1,{31,26,24,255}); PX(14,21,1,1,{30,24,24,255}); PX(15,21,2,1,{21,16,16,255}); PX(17,21,1,1,{28,13,13,255});
					PX(18,21,1,1,{25,13,13,255}); PX(19,21,1,1,{20,15,15,255}); PX(13,22,1,1,{55,34,30,255}); PX(14,22,1,1,{36,23,22,255});
					PX(15,22,1,1,{42,30,28,255}); PX(16,22,1,1,{34,24,23,255}); PX(17,22,1,1,{34,20,20,255}); PX(18,22,1,1,{34,21,21,255});
					PX(12,23,1,1,{45,28,24,255}); PX(13,23,1,1,{53,34,29,255}); PX(14,23,1,1,{41,27,24,255}); PX(15,23,1,1,{49,34,30,255});
					PX(16,23,1,1,{33,20,20,255}); PX(17,23,1,1,{38,23,23,255}); PX(18,23,1,1,{40,24,24,255}); PX(13,24,1,1,{45,31,28,255});
					PX(14,24,1,1,{52,35,32,255}); PX(15,24,1,1,{29,18,17,255}); PX(17,24,1,1,{40,27,24,255}); PX(18,24,1,1,{44,29,27,255});
					PX(19,24,1,3,{29,18,18,255}); PX(12,25,1,1,{42,28,26,255}); PX(13,25,1,1,{51,36,31,255}); PX(14,25,1,1,{45,29,27,255});
					PX(17,25,1,1,{39,23,22,255}); PX(18,25,1,2,{49,34,30,255}); PX(12,26,1,1,{46,31,28,255}); PX(13,26,1,1,{46,29,28,255});
					PX(14,26,1,1,{29,17,17,255}); PX(17,26,1,1,{52,35,31,255}); PX(11,27,1,1,{46,30,28,255}); PX(12,27,1,1,{49,32,30,255});
					PX(13,27,1,1,{40,24,24,255}); PX(17,27,1,1,{52,35,32,255}); PX(18,27,1,1,{51,35,31,255}); PX(19,27,1,1,{32,19,19,255});
					PX(11,28,1,1,{42,29,25,255}); PX(12,28,1,1,{50,33,30,255}); PX(13,28,1,1,{33,20,20,255}); PX(17,28,1,1,{40,28,24,255});
					PX(18,28,1,1,{56,39,34,255}); PX(19,28,1,1,{41,27,24,255}); PX(11,29,1,1,{43,29,25,255}); PX(12,29,1,1,{49,34,30,255});
					PX(13,29,1,1,{35,21,21,255}); PX(18,29,1,1,{47,33,29,255}); PX(19,29,1,1,{31,19,18,255}); PX(11,30,1,1,{51,33,29,255});
					PX(12,30,1,1,{43,27,24,255}); PX(13,30,1,1,{25,15,14,255}); PX(18,30,1,1,{50,32,27,255}); PX(19,30,1,1,{60,39,33,255});
					PX(20,30,1,1,{44,26,25,255});
				}
				else if (nFrame == 2)
				{
					PX(15,2,1,6,{64,58,58,255}); PX(15,8,1,1,{127,69,69,255}); PX(16,8,1,1,{133,84,74,255}); PX(15,9,1,1,{116,69,64,255});
					PX(16,9,1,1,{169,104,94,255}); PX(17,9,1,1,{180,126,103,255}); PX(15,10,1,1,{47,29,27,255}); PX(16,10,1,1,{47,31,30,255});
					PX(17,10,1,1,{142,97,81,255}); PX(18,10,1,1,{166,105,93,255}); PX(15,11,1,1,{41,26,24,255}); PX(16,11,1,1,{34,25,26,255});
					PX(18,11,1,1,{123,78,69,255}); PX(19,11,1,1,{126,70,67,255}); PX(14,12,1,1,{132,72,71,255}); PX(15,12,1,1,{73,47,41,255});
					PX(16,12,1,1,{28,21,21,255}); PX(18,12,1,1,{115,79,67,255}); PX(19,12,1,1,{158,102,88,255}); PX(13,13,1,1,{86,42,45,255});
					PX(14,13,1,1,{180,116,101,255}); PX(15,13,1,1,{92,58,51,255}); PX(16,13,1,1,{61,40,35,255}); PX(17,13,1,1,{44,24,24,255});
					PX(18,13,1,1,{78,50,44,255}); PX(19,13,1,1,{131,80,72,255}); PX(20,13,1,1,{60,27,30,255}); PX(13,14,1,1,{85,49,46,255});
					PX(14,14,1,1,{153,101,86,255}); PX(15,14,1,1,{74,48,43,255}); PX(16,14,1,1,{105,76,77,255}); PX(17,14,1,1,{132,80,84,255});
					PX(18,14,1,1,{84,33,33,255}); PX(19,14,1,1,{100,36,33,255}); PX(20,14,1,2,{46,19,21,255}); PX(12,15,1,1,{172,113,97,255});
					PX(13,15,1,1,{176,106,97,255}); PX(14,15,1,1,{110,61,61,255}); PX(15,15,1,1,{60,42,39,255}); PX(16,15,1,1,{45,29,28,255});
					PX(17,15,1,1,{37,24,22,255}); PX(18,15,1,1,{55,23,20,255}); PX(19,15,1,1,{99,35,33,255}); PX(11,16,1,1,{176,119,100,255});
					PX(12,16,1,1,{176,108,97,255}); PX(13,16,1,1,{77,38,41,255}); PX(14,16,1,1,{36,20,20,255}); PX(15,16,1,1,{52,21,19,255});
					PX(16,16,1,1,{85,30,26,255}); PX(17,16,1,1,{36,21,21,255}); PX(18,16,1,1,{44,22,22,255}); PX(19,16,1,1,{59,23,24,255});
					PX(20,16,1,1,{45,19,20,255}); PX(11,17,1,1,{148,91,82,255}); PX(12,17,1,1,{102,54,55,255}); PX(13,17,1,1,{48,21,23,255});
					PX(14,17,1,1,{32,16,19,255}); PX(15,17,1,1,{70,26,23,255}); PX(16,17,1,1,{122,42,35,255}); PX(17,17,1,1,{30,19,19,255});
					PX(18,17,1,1,{39,22,23,255}); PX(19,17,1,1,{28,15,16,255}); PX(11,18,1,1,{35,15,17,255}); PX(12,18,1,1,{52,21,23,255});
					PX(13,18,1,1,{33,16,18,255}); PX(14,18,1,1,{23,13,15,255}); PX(15,18,1,2,{92,34,32,255}); PX(16,18,1,1,{87,35,30,255});
					PX(17,18,1,1,{29,26,26,255}); PX(18,18,1,1,{25,21,22,255}); PX(19,18,1,1,{20,14,14,255}); PX(13,19,1,1,{26,22,22,255});
					PX(14,19,1,1,{25,20,20,255}); PX(16,19,1,1,{57,25,24,255}); PX(17,19,1,1,{30,27,27,255}); PX(18,19,1,1,{20,15,15,255});
					PX(19,19,1,1,{18,11,11,255}); PX(13,20,1,1,{24,19,19,255}); PX(14,20,1,1,{31,18,19,255}); PX(15,20,1,1,{86,32,31,255});
					PX(16,20,1,1,{38,20,20,255}); PX(17,20,2,1,{26,23,23,255}); PX(19,20,1,1,{18,13,13,255}); PX(12,21,1,1,{26,17,17,255});
					PX(13,21,1,1,{30,26,26,255}); PX(14,21,1,1,{29,16,16,255}); PX(15,21,1,1,{40,18,19,255}); PX(16,21,2,1,{24,19,19,255});
					PX(18,21,2,1,{22,17,17,255}); PX(12,22,1,1,{31,19,16,255}); PX(13,22,1,1,{42,30,27,255}); PX(14,22,1,1,{41,29,26,255});
					PX(15,22,1,1,{52,36,32,255}); PX(16,22,1,1,{34,25,23,255}); PX(17,22,1,1,{38,26,25,255}); PX(18,22,1,1,{33,20,20,255});
					PX(12,23,1,1,{37,24,22,255}); PX(13,23,1,1,{49,35,30,255}); PX(14,23,1,1,{40,24,23,255}); PX(15,23,1,1,{28,17,17,255});
					PX(16,23,1,1,{53,38,33,255}); PX(17,23,1,1,{49,32,30,255}); PX(18,23,1,2,{38,23,23,255}); PX(12,24,1,1,{36,24,20,255});
					PX(13,24,1,1,{48,32,29,255}); PX(14,24,1,1,{35,21,21,255}); PX(16,24,1,1,{33,23,20,255}); PX(17,24,1,1,{54,38,33,255});
					PX(12,25,1,1,{46,30,29,255}); PX(13,25,1,1,{50,33,30,255}); PX(14,25,1,1,{27,17,15,255}); PX(17,25,1,1,{51,36,31,255});
					PX(18,25,1,2,{45,30,27,255}); PX(12,26,1,1,{36,21,21,255}); PX(13,26,1,1,{41,25,25,255}); PX(14,26,1,1,{31,19,19,255});
					PX(17,26,1,1,{50,33,30,255}); PX(13,27,1,1,{38,23,23,255}); PX(14,27,1,1,{40,24,24,255}); PX(17,27,1,1,{39,26,23,255});
					PX(18,27,1,1,{49,34,30,255}); PX(19,27,1,1,{29,18,18,255}); PX(13,28,1,1,{41,26,25,255}); PX(14,28,1,1,{44,27,27,255});
					PX(15,28,1,1,{19,9,9,255}); PX(17,28,1,1,{30,18,17,255}); PX(18,28,1,1,{51,35,31,255}); PX(19,28,1,1,{33,19,19,255});
					PX(12,29,1,1,{45,32,27,255}); PX(13,29,1,1,{49,32,30,255}); PX(14,29,1,1,{41,25,24,255}); PX(17,29,1,1,{39,23,22,255});
					PX(18,29,1,1,{53,37,32,255}); PX(19,29,1,1,{37,23,22,255}); PX(12,30,1,1,{43,26,23,255}); PX(13,30,1,1,{35,21,20,255});
					PX(14,30,1,1,{25,14,14,255}); PX(18,30,1,1,{32,19,17,255}); PX(19,30,1,1,{38,24,20,255});
				}
				else if (nFrame == 3)
				{
					PX(17,1,1,6,{64,58,58,255}); PX(17,7,1,1,{59,54,54,255}); PX(18,7,1,1,{38,31,31,255}); PX(17,8,1,1,{125,77,72,255});
					PX(18,8,1,1,{122,72,69,255}); PX(19,8,1,1,{46,28,31,255}); PX(20,8,1,1,{27,20,21,255}); PX(17,9,1,1,{98,65,56,255});
					PX(18,9,1,1,{163,98,90,255}); PX(19,9,1,1,{72,37,41,255}); PX(20,9,1,1,{26,19,19,255}); PX(17,10,1,1,{57,37,33,255});
					PX(18,10,1,1,{135,90,77,255}); PX(19,10,1,1,{97,62,57,255}); PX(20,10,1,1,{25,18,18,255}); PX(17,11,1,1,{132,79,73,255});
					PX(18,11,1,1,{162,101,90,255}); PX(19,11,1,1,{57,28,31,255}); PX(16,12,1,1,{127,76,70,255}); PX(17,12,1,1,{167,111,94,255});
					PX(18,12,1,1,{125,65,67,255}); PX(19,12,1,1,{42,23,25,255}); PX(20,12,1,1,{31,19,20,255}); PX(15,13,1,1,{124,69,67,255});
					PX(16,13,1,1,{35,20,19,255}); PX(17,13,1,1,{180,111,100,255}); PX(18,13,1,1,{95,42,49,255}); PX(19,13,1,1,{42,25,23,255});
					PX(20,13,1,1,{61,39,33,255}); PX(21,13,1,1,{41,26,22,255}); PX(15,14,1,1,{76,48,43,255}); PX(16,14,1,1,{47,25,24,255});
					PX(17,14,1,1,{93,34,32,255}); PX(18,14,1,1,{64,25,25,255}); PX(19,14,1,1,{60,27,30,255}); PX(20,14,1,1,{81,46,43,255});
					PX(21,14,1,1,{56,36,30,255}); PX(22,14,1,1,{25,15,13,255}); PX(13,15,1,1,{177,113,98,255}); PX(14,15,1,1,{187,125,106,255});
					PX(15,15,1,1,{72,35,39,255}); PX(16,15,1,1,{41,24,23,255}); PX(17,15,1,1,{87,31,27,255}); PX(18,15,1,1,{104,37,33,255});
					PX(19,15,1,1,{60,26,26,255}); PX(20,15,1,1,{42,24,23,255}); PX(21,15,1,1,{32,19,19,255}); PX(22,15,1,1,{20,10,10,255});
					PX(13,16,1,1,{158,103,87,255}); PX(14,16,1,1,{106,62,56,255}); PX(16,16,1,1,{43,23,22,255}); PX(17,16,1,1,{50,23,22,255});
					PX(18,16,1,1,{116,40,35,255}); PX(19,16,1,1,{61,25,25,255}); PX(20,16,1,1,{24,14,14,255}); PX(21,16,1,1,{32,18,18,255});
					PX(22,16,1,1,{22,13,13,255}); PX(13,17,1,1,{53,24,24,255}); PX(14,17,1,1,{55,23,25,255}); PX(15,17,1,1,{33,17,16,255});
					PX(16,17,1,1,{34,28,28,255}); PX(17,17,1,1,{34,25,25,255}); PX(18,17,1,1,{54,22,21,255}); PX(19,17,1,1,{37,17,19,255});
					PX(20,17,1,1,{28,16,16,255}); PX(21,17,1,1,{27,14,14,255}); PX(14,18,1,1,{54,22,20,255}); PX(15,18,1,1,{31,26,26,255});
					PX(16,18,1,1,{32,29,29,255}); PX(17,18,1,1,{25,23,23,255}); PX(18,18,1,2,{21,18,18,255}); PX(19,18,1,1,{25,16,16,255});
					PX(20,18,1,1,{29,17,17,255}); PX(13,19,1,1,{55,22,19,255}); PX(14,19,1,1,{31,22,22,255}); PX(15,19,1,1,{28,24,24,255});
					PX(16,19,1,1,{21,17,17,255}); PX(17,19,1,1,{23,19,19,255}); PX(13,20,1,1,{30,18,18,255}); PX(14,20,1,1,{29,23,23,255});
					PX(15,20,1,1,{33,29,29,255}); PX(16,20,1,1,{26,22,22,255}); PX(17,20,1,1,{20,16,16,255}); PX(18,20,1,1,{21,17,17,255});
					PX(13,21,1,1,{30,26,26,255}); PX(14,21,1,1,{27,22,22,255}); PX(15,21,1,1,{22,17,17,255}); PX(16,21,1,1,{22,16,16,255});
					PX(17,21,1,1,{23,18,18,255}); PX(13,22,1,1,{28,20,20,255}); PX(14,22,1,1,{35,26,25,255}); PX(15,22,1,1,{40,29,27,255});
					PX(16,22,1,1,{37,23,23,255}); PX(17,22,1,1,{30,17,17,255}); PX(13,23,1,1,{36,25,22,255}); PX(14,23,1,1,{37,25,22,255});
					PX(15,23,1,1,{52,36,32,255}); PX(16,23,1,1,{40,25,25,255}); PX(13,24,1,1,{31,22,18,255}); PX(14,24,1,1,{40,27,24,255});
					PX(15,24,1,1,{50,34,31,255}); PX(16,24,1,1,{28,16,16,255}); PX(14,25,1,1,{48,32,30,255}); PX(15,25,1,1,{51,34,31,255});
					PX(16,25,1,1,{20,11,11,255}); PX(14,26,1,1,{31,18,18,255}); PX(15,26,1,1,{56,40,34,255}); PX(16,26,1,1,{35,20,20,255});
					PX(17,26,1,1,{14,9,9,255}); PX(15,27,1,1,{57,41,35,255}); PX(16,27,1,1,{51,35,31,255}); PX(17,27,1,1,{25,14,14,255});
					PX(15,28,1,1,{46,34,28,255}); PX(16,28,1,1,{56,40,35,255}); PX(17,28,2,1,{29,17,17,255}); PX(15,29,1,1,{31,20,18,255});
					PX(16,29,1,1,{50,36,31,255}); PX(17,29,1,1,{30,18,18,255}); PX(18,29,1,1,{25,15,15,255}); PX(15,30,1,1,{60,40,32,255});
					PX(16,30,1,1,{41,25,23,255}); PX(17,30,1,1,{24,14,14,255}); PX(18,30,1,1,{32,19,19,255});
				}
				else if (nFrame == 4)
				{
					PX(18,1,1,6,{64,58,58,255}); PX(18,7,1,1,{56,50,50,255}); PX(19,7,1,1,{53,46,46,255}); PX(17,8,1,1,{91,63,54,255});
					PX(18,8,1,1,{93,69,60,255}); PX(19,8,1,1,{33,29,29,255}); PX(20,8,1,1,{27,21,21,255}); PX(21,8,1,1,{24,18,18,255});
					PX(17,9,1,1,{134,87,76,255}); PX(18,9,1,1,{71,37,41,255}); PX(19,9,1,1,{26,19,19,255}); PX(20,9,1,1,{26,20,20,255});
					PX(21,9,1,1,{33,27,27,255}); PX(22,9,1,2,{22,15,15,255}); PX(16,10,1,1,{143,91,79,255}); PX(17,10,1,1,{129,81,72,255});
					PX(18,10,1,1,{29,15,17,255}); PX(19,10,1,1,{24,16,17,255}); PX(20,10,1,1,{23,18,18,255}); PX(21,10,1,1,{31,25,25,255});
					PX(15,11,1,1,{202,137,115,255}); PX(16,11,1,1,{145,92,81,255}); PX(17,11,1,1,{23,13,13,255}); PX(18,11,1,1,{35,22,22,255});
					PX(19,11,1,1,{30,22,22,255}); PX(20,11,2,1,{26,20,20,255}); PX(14,12,1,1,{196,137,112,255}); PX(15,12,1,1,{195,128,110,255});
					PX(16,12,1,1,{67,44,38,255}); PX(17,12,1,1,{23,13,14,255}); PX(18,12,1,1,{30,17,18,255}); PX(20,12,1,1,{29,24,24,255});
					PX(21,12,1,1,{23,17,17,255}); PX(22,12,1,1,{39,25,25,255}); PX(13,13,1,1,{109,70,61,255}); PX(14,13,1,1,{159,99,88,255});
					PX(15,13,1,1,{71,35,37,255}); PX(16,13,1,1,{47,22,26,255}); PX(17,13,1,1,{25,14,16,255}); PX(21,13,1,1,{27,16,16,255});
					PX(22,13,1,1,{73,48,40,255}); PX(23,13,1,1,{67,44,36,255}); PX(14,14,1,1,{101,36,31,255}); PX(15,14,1,1,{113,40,34,255});
					PX(16,14,1,1,{49,20,22,255}); PX(17,14,1,1,{31,19,21,255}); PX(18,14,1,1,{63,44,41,255}); PX(19,14,1,1,{103,67,59,255});
					PX(20,14,1,1,{81,52,47,255}); PX(21,14,1,1,{34,20,20,255}); PX(22,14,1,1,{72,47,39,255}); PX(23,14,1,1,{62,41,34,255});
					PX(24,14,1,1,{54,33,29,255}); PX(25,14,1,1,{51,31,28,255}); PX(14,15,1,1,{40,15,13,255}); PX(15,15,1,1,{121,42,35,255});
					PX(16,15,1,1,{115,40,34,255}); PX(17,15,1,1,{56,30,27,255}); PX(18,15,1,1,{102,81,72,255}); PX(19,15,1,1,{159,113,96,255});
					PX(20,15,1,1,{165,95,90,255}); PX(21,15,1,1,{61,29,32,255}); PX(22,15,1,1,{51,32,28,255}); PX(23,15,1,1,{35,22,21,255});
					PX(24,15,1,1,{42,25,25,255}); PX(25,15,1,1,{30,18,18,255}); PX(14,16,1,1,{65,31,34,255}); PX(15,16,1,1,{51,23,21,255});
					PX(16,16,1,1,{112,39,33,255}); PX(17,16,1,1,{116,40,33,255}); PX(18,16,1,1,{80,35,30,255}); PX(19,16,1,1,{79,47,43,255});
					PX(20,16,1,1,{68,37,36,255}); PX(21,16,1,1,{38,21,20,255}); PX(22,16,1,1,{29,17,17,255}); PX(23,16,1,1,{30,22,22,255});
					PX(24,16,1,1,{40,23,23,255}); PX(25,16,1,1,{28,16,16,255}); PX(14,17,1,1,{29,26,26,255}); PX(15,17,1,1,{32,28,28,255});
					PX(16,17,1,1,{40,20,21,255}); PX(17,17,1,1,{68,27,28,255}); PX(18,17,1,1,{42,19,20,255}); PX(19,17,1,1,{34,18,19,255});
					PX(20,17,1,1,{27,15,15,255}); PX(21,17,1,1,{21,10,10,255}); PX(22,17,1,1,{33,20,20,255}); PX(23,17,1,1,{23,14,14,255});
					PX(13,18,1,1,{27,24,24,255}); PX(14,18,1,1,{31,28,28,255}); PX(15,18,1,1,{30,27,27,255}); PX(16,18,1,1,{24,21,21,255});
					PX(17,18,1,1,{28,17,18,255}); PX(18,18,1,1,{28,14,18,255}); PX(19,18,1,1,{25,14,16,255}); PX(22,18,1,1,{27,16,16,255});
					PX(12,19,1,1,{25,22,22,255}); PX(13,19,1,1,{27,23,23,255}); PX(14,19,1,1,{23,18,18,255}); PX(15,19,1,1,{23,19,19,255});
					PX(16,19,1,2,{22,20,20,255}); PX(17,19,1,1,{22,19,19,255}); PX(18,19,1,1,{21,11,11,255}); PX(12,20,1,2,{25,20,20,255});
					PX(13,20,1,1,{31,27,27,255}); PX(14,20,1,1,{30,26,26,255}); PX(15,20,1,1,{20,16,16,255}); PX(17,20,1,1,{19,16,16,255});
					PX(13,21,1,1,{26,21,21,255}); PX(14,21,1,1,{21,16,16,255}); PX(15,21,1,1,{27,24,24,255}); PX(16,21,1,1,{26,21,21,255});
					PX(17,21,1,1,{19,13,13,255}); PX(12,22,1,1,{34,25,24,255}); PX(13,22,1,1,{41,30,28,255}); PX(14,22,1,1,{46,33,30,255});
					PX(15,22,1,1,{41,26,25,255}); PX(16,22,1,1,{39,23,23,255}); PX(17,22,1,2,{15,8,8,255}); PX(12,23,1,1,{48,34,30,255});
					PX(13,23,1,1,{53,37,32,255}); PX(14,23,1,1,{36,22,21,255}); PX(15,23,1,1,{39,24,23,255}); PX(16,23,1,1,{41,25,25,255});
					PX(11,24,1,1,{47,33,29,255}); PX(12,24,1,1,{50,35,31,255}); PX(13,24,1,1,{36,23,22,255}); PX(14,24,1,1,{18,10,10,255});
					PX(15,24,1,1,{56,40,34,255}); PX(16,24,1,1,{42,26,26,255}); PX(11,25,1,1,{44,30,26,255}); PX(12,25,1,1,{44,30,27,255});
					PX(15,25,1,1,{40,27,25,255}); PX(16,25,1,1,{47,29,28,255}); PX(17,25,1,1,{31,19,19,255}); PX(12,26,1,1,{43,28,26,255});
					PX(13,26,1,1,{32,20,18,255}); PX(16,26,1,1,{42,28,24,255}); PX(17,26,1,1,{52,35,31,255}); PX(18,26,1,1,{42,27,26,255});
					PX(19,26,1,1,{32,19,19,255}); PX(12,27,1,1,{46,31,28,255}); PX(13,27,1,1,{41,26,25,255}); PX(17,27,1,1,{38,27,22,255});
					PX(18,27,1,1,{51,36,32,255}); PX(19,27,1,1,{43,27,26,255}); PX(20,27,1,1,{32,18,18,255}); PX(12,28,1,1,{49,35,30,255});
					PX(13,28,1,1,{48,31,29,255}); PX(14,28,1,1,{25,13,13,255}); PX(18,28,1,1,{40,28,24,255}); PX(19,28,1,1,{53,37,33,255});
					PX(20,28,1,1,{37,23,22,255}); PX(11,29,1,1,{39,25,23,255}); PX(12,29,1,1,{48,32,29,255}); PX(13,29,1,1,{39,24,23,255});
					PX(19,29,1,1,{48,33,29,255}); PX(20,29,1,1,{40,25,24,255}); PX(21,29,1,1,{24,14,14,255}); PX(10,30,1,1,{32,18,18,255});
					PX(11,30,1,1,{26,15,15,255}); PX(12,30,1,1,{34,20,19,255}); PX(13,30,1,1,{30,19,17,255}); PX(18,30,1,1,{31,17,17,255});
					PX(19,30,1,1,{29,17,16,255}); PX(20,30,1,1,{41,25,22,255});
				}
				else if (nFrame == 5)
				{
					PX(17,1,1,1,{63,55,55,255}); PX(17,2,1,5,{54,49,49,255}); PX(17,7,1,1,{61,54,54,255}); PX(16,8,1,1,{83,57,49,255});
					PX(17,8,1,1,{43,37,37,255}); PX(18,8,1,1,{34,27,28,255}); PX(19,8,1,1,{26,18,20,255}); PX(15,9,1,1,{116,65,63,255});
					PX(16,9,1,1,{148,83,81,255}); PX(17,9,1,1,{37,29,29,255}); PX(18,9,1,1,{36,30,30,255}); PX(19,9,1,2,{31,24,24,255});
					PX(14,10,1,1,{165,90,89,255}); PX(15,10,1,1,{174,104,96,255}); PX(16,10,1,1,{59,28,33,255}); PX(17,10,1,1,{29,19,18,255});
					PX(18,10,1,2,{30,24,24,255}); PX(13,11,1,1,{125,73,67,255}); PX(14,11,1,1,{203,141,116,255}); PX(15,11,1,1,{114,61,61,255});
					PX(16,11,1,1,{29,16,17,255}); PX(17,11,1,1,{33,21,21,255}); PX(19,11,1,1,{26,19,19,255}); PX(13,12,1,1,{180,126,103,255});
					PX(14,12,1,1,{184,113,102,255}); PX(15,12,1,1,{30,17,17,255}); PX(16,12,1,1,{59,29,33,255}); PX(17,12,1,1,{35,22,22,255});
					PX(18,12,1,1,{28,21,21,255}); PX(19,12,1,1,{36,23,24,255}); PX(12,13,1,1,{109,52,45,255}); PX(13,13,1,1,{109,61,54,255});
					PX(14,13,1,1,{80,38,41,255}); PX(15,13,1,1,{38,23,23,255}); PX(16,13,1,1,{33,20,20,255}); PX(17,13,1,1,{36,18,20,255});
					PX(18,13,1,1,{34,20,19,255}); PX(19,13,1,1,{55,35,30,255}); PX(20,13,1,1,{37,23,20,255}); PX(12,14,1,1,{86,31,26,255});
					PX(13,14,1,1,{110,39,33,255}); PX(14,14,1,1,{58,23,23,255}); PX(15,14,1,1,{34,20,21,255}); PX(16,14,1,1,{78,52,45,255});
					PX(17,14,1,1,{37,21,21,255}); PX(18,14,1,1,{49,30,27,255}); PX(19,14,1,1,{67,44,36,255}); PX(20,14,1,1,{41,31,28,255});
					PX(21,14,1,1,{46,32,28,255}); PX(22,14,1,1,{31,18,17,255}); PX(13,15,1,1,{116,41,34,255}); PX(14,15,1,1,{96,35,33,255});
					PX(15,15,1,1,{43,20,21,255}); PX(16,15,1,1,{120,88,77,255}); PX(17,15,1,1,{124,82,71,255}); PX(18,15,1,1,{60,33,32,255});
					PX(19,15,1,1,{52,35,29,255}); PX(20,15,1,1,{40,28,26,255}); PX(21,15,1,1,{49,31,29,255}); PX(22,15,1,1,{30,18,18,255});
					PX(13,16,1,1,{86,31,25,255}); PX(14,16,1,1,{86,32,30,255}); PX(15,16,1,1,{40,17,19,255}); PX(16,16,1,1,{34,18,20,255});
					PX(17,16,1,1,{106,55,56,255}); PX(18,16,1,1,{41,21,22,255}); PX(19,16,1,1,{35,22,21,255}); PX(20,16,1,1,{33,23,23,255});
					PX(21,16,1,1,{37,23,23,255}); PX(22,16,1,1,{24,13,13,255}); PX(13,17,1,1,{28,15,15,255}); PX(14,17,1,1,{28,19,19,255});
					PX(15,17,1,1,{28,20,20,255}); PX(16,17,1,1,{33,22,22,255}); PX(17,17,1,1,{29,15,18,255}); PX(18,17,1,1,{31,18,18,255});
					PX(19,17,1,1,{43,26,24,255}); PX(20,17,1,1,{39,25,24,255}); PX(21,17,1,1,{25,15,15,255}); PX(13,18,1,1,{30,26,26,255});
					PX(14,18,1,1,{32,29,29,255}); PX(15,18,1,1,{31,28,28,255}); PX(16,18,1,1,{28,25,25,255}); PX(17,18,1,1,{25,20,20,255});
					PX(18,18,1,1,{25,13,15,255}); PX(19,18,1,1,{40,25,22,255}); PX(20,18,1,1,{57,36,31,255}); PX(12,19,1,1,{24,18,18,255});
					PX(13,19,1,1,{24,19,19,255}); PX(14,19,1,1,{31,27,27,255}); PX(15,19,1,1,{23,21,21,255}); PX(16,19,1,2,{22,20,20,255});
					PX(17,19,1,1,{22,19,19,255}); PX(18,19,1,1,{19,16,16,255}); PX(12,20,1,1,{21,16,16,255}); PX(13,20,1,1,{23,18,18,255});
					PX(14,20,1,1,{29,25,25,255}); PX(15,20,1,1,{26,24,24,255}); PX(17,20,1,1,{21,19,19,255}); PX(18,20,1,1,{19,15,15,255});
					PX(12,21,1,1,{24,16,16,255}); PX(13,21,1,1,{37,31,31,255}); PX(14,21,1,1,{38,31,31,255}); PX(15,21,1,1,{37,30,29,255});
					PX(16,21,1,1,{27,20,20,255}); PX(17,21,1,1,{25,19,19,255}); PX(18,21,1,1,{20,13,13,255}); PX(13,22,1,1,{44,30,26,255});
					PX(14,22,1,1,{56,40,34,255}); PX(15,22,1,1,{54,38,34,255}); PX(16,22,1,1,{53,37,32,255}); PX(17,22,1,1,{48,32,29,255});
					PX(18,22,1,1,{29,17,17,255}); PX(13,23,1,1,{51,36,31,255}); PX(14,23,1,1,{49,33,30,255}); PX(15,23,1,1,{35,20,20,255});
					PX(16,23,1,1,{50,34,30,255}); PX(17,23,1,2,{55,39,34,255}); PX(18,23,1,1,{35,20,20,255}); PX(19,23,1,1,{15,8,8,255});
					PX(12,24,1,1,{52,34,30,255}); PX(13,24,1,1,{51,35,31,255}); PX(14,24,1,1,{34,21,20,255}); PX(16,24,1,1,{36,25,21,255});
					PX(18,24,1,1,{37,22,22,255}); PX(12,25,1,1,{48,32,29,255}); PX(13,25,1,1,{47,31,29,255}); PX(14,25,1,1,{25,15,15,255});
					PX(17,25,1,1,{56,40,35,255}); PX(18,25,1,1,{47,32,28,255}); PX(19,25,1,1,{30,18,18,255}); PX(12,26,1,1,{37,25,23,255});
					PX(13,26,1,1,{48,34,29,255}); PX(14,26,1,1,{33,21,20,255}); PX(17,26,1,1,{44,31,27,255}); PX(18,26,1,1,{55,39,34,255});
					PX(19,26,1,1,{40,26,24,255}); PX(12,27,1,1,{42,29,24,255}); PX(13,27,1,1,{52,37,32,255}); PX(14,27,1,1,{34,21,20,255});
					PX(18,27,1,1,{49,34,30,255}); PX(19,27,1,1,{44,28,27,255}); PX(20,27,1,1,{32,18,18,255}); PX(12,28,1,1,{50,35,31,255});
					PX(13,28,1,1,{53,36,32,255}); PX(14,28,1,1,{33,20,19,255}); PX(18,28,1,1,{41,28,25,255}); PX(19,28,1,1,{47,30,29,255});
					PX(20,28,1,1,{32,19,19,255}); PX(12,29,1,1,{42,27,25,255}); PX(13,29,1,1,{43,27,26,255}); PX(18,29,1,1,{41,28,24,255});
					PX(19,29,1,1,{43,28,26,255}); PX(20,29,1,1,{28,15,15,255}); PX(11,30,1,1,{53,33,30,255}); PX(12,30,1,1,{47,29,26,255});
					PX(13,30,1,1,{26,15,14,255}); PX(18,30,1,1,{39,23,21,255}); PX(19,30,1,1,{54,34,30,255}); PX(20,30,1,1,{48,30,27,255});
				}
				else if (nFrame == 6)
				{
					PX(15,2,1,6,{64,58,58,255}); PX(15,8,1,1,{38,33,33,255}); PX(16,8,1,1,{22,13,13,255}); PX(14,9,1,1,{115,76,68,255});
					PX(15,9,1,1,{41,33,32,255}); PX(16,9,1,1,{31,14,15,255}); PX(13,10,1,1,{190,119,106,255}); PX(14,10,1,1,{102,71,62,255});
					PX(15,10,1,1,{39,32,31,255}); PX(16,10,1,1,{21,10,12,255}); PX(12,11,1,1,{179,114,100,255}); PX(13,11,1,1,{124,75,69,255});
					PX(15,11,1,1,{35,28,28,255}); PX(16,11,1,1,{38,17,21,255}); PX(12,12,1,1,{165,101,91,255}); PX(13,12,1,1,{97,58,53,255});
					PX(15,12,1,1,{26,18,19,255}); PX(16,12,1,1,{50,23,25,255}); PX(17,12,1,1,{98,44,50,255}); PX(11,13,1,1,{103,69,59,255});
					PX(12,13,1,1,{125,73,68,255}); PX(13,13,1,1,{57,29,30,255}); PX(14,13,1,1,{39,24,22,255}); PX(15,13,1,1,{50,31,28,255});
					PX(16,13,1,1,{62,40,33,255}); PX(17,13,1,1,{53,33,28,255}); PX(18,13,1,1,{52,34,29,255}); PX(11,14,1,1,{98,35,32,255});
					PX(12,14,1,1,{99,36,33,255}); PX(13,14,1,1,{57,24,25,255}); PX(14,14,1,1,{69,43,37,255}); PX(15,14,1,1,{31,23,21,255});
					PX(16,14,1,1,{79,52,42,255}); PX(17,14,1,1,{50,31,28,255}); PX(18,14,1,1,{32,18,18,255}); PX(11,15,1,1,{87,32,29,255});
					PX(12,15,1,1,{89,33,32,255}); PX(13,15,1,1,{48,24,24,255}); PX(14,15,1,1,{74,48,40,255}); PX(15,15,1,1,{51,32,27,255});
					PX(16,15,1,1,{64,41,35,255}); PX(17,15,1,1,{40,23,23,255}); PX(18,15,1,1,{29,17,17,255}); PX(19,15,1,1,{69,39,39,255});
					PX(11,16,1,1,{97,34,31,255}); PX(12,16,1,1,{84,31,30,255}); PX(13,16,1,1,{57,31,28,255}); PX(14,16,1,1,{44,27,23,255});
					PX(15,16,1,1,{48,35,31,255}); PX(16,16,1,1,{50,30,28,255}); PX(17,16,1,1,{39,23,23,255}); PX(18,16,1,1,{32,18,18,255});
					PX(19,16,1,1,{96,61,52,255}); PX(20,16,1,1,{116,71,62,255}); PX(12,17,1,1,{36,16,17,255}); PX(13,17,1,1,{40,23,21,255});
					PX(14,17,1,1,{35,21,20,255}); PX(15,17,1,1,{34,20,20,255}); PX(16,17,1,1,{31,18,18,255}); PX(17,17,1,1,{36,21,21,255});
					PX(18,17,1,1,{49,24,22,255}); PX(19,17,1,1,{74,28,27,255}); PX(20,17,1,1,{61,24,27,255}); PX(12,18,1,1,{25,20,20,255});
					PX(13,18,1,1,{32,25,25,255}); PX(14,18,1,1,{35,27,25,255}); PX(15,18,1,1,{49,33,29,255}); PX(16,18,1,1,{39,24,23,255});
					PX(17,18,1,1,{28,18,18,255}); PX(18,18,1,1,{31,16,17,255}); PX(19,18,1,1,{55,22,24,255}); PX(20,18,1,1,{45,18,20,255});
					PX(12,19,1,1,{27,25,25,255}); PX(13,19,1,1,{30,28,28,255}); PX(14,19,1,2,{32,29,29,255}); PX(15,19,1,1,{27,25,25,255});
					PX(16,19,1,2,{22,20,20,255}); PX(17,19,1,2,{22,19,19,255}); PX(18,19,1,1,{20,15,15,255}); PX(12,20,1,1,{23,18,18,255});
					PX(13,20,1,1,{29,26,26,255}); PX(15,20,1,1,{25,23,23,255}); PX(18,20,1,1,{19,15,15,255}); PX(12,21,1,1,{31,27,27,255});
					PX(13,21,1,1,{38,33,33,255}); PX(14,21,1,1,{37,30,30,255}); PX(15,21,1,1,{31,25,25,255}); PX(16,21,1,1,{30,24,24,255});
					PX(17,21,1,1,{26,22,22,255}); PX(18,21,1,1,{25,22,22,255}); PX(19,21,1,1,{21,16,16,255}); PX(13,22,1,1,{49,33,30,255});
					PX(14,22,1,1,{55,39,34,255}); PX(15,22,1,1,{56,39,34,255}); PX(16,22,1,1,{54,38,33,255}); PX(17,22,1,1,{46,30,28,255});
					PX(18,22,1,1,{35,21,21,255}); PX(19,22,1,1,{26,15,15,255}); PX(13,23,1,1,{56,39,34,255}); PX(14,23,1,1,{46,30,29,255});
					PX(15,23,1,1,{30,18,18,255}); PX(16,23,1,1,{44,30,26,255}); PX(17,23,1,1,{54,38,33,255}); PX(18,23,1,1,{40,24,24,255});
					PX(19,23,1,2,{29,17,17,255}); PX(13,24,1,1,{49,32,30,255}); PX(14,24,1,1,{45,28,27,255}); PX(15,24,1,1,{27,15,15,255});
					PX(17,24,1,1,{42,29,25,255}); PX(18,24,1,1,{46,31,28,255}); PX(13,25,1,1,{40,25,24,255}); PX(14,25,1,1,{40,24,24,255});
					PX(17,25,1,1,{32,20,18,255}); PX(18,25,1,1,{49,34,30,255}); PX(19,25,1,1,{29,18,18,255}); PX(13,26,1,1,{38,23,23,255});
					PX(14,26,1,1,{32,19,19,255}); PX(17,26,1,1,{52,35,31,255}); PX(18,26,1,1,{50,36,30,255}); PX(12,27,1,1,{45,30,27,255});
					PX(13,27,1,1,{47,31,29,255}); PX(14,27,1,1,{27,15,15,255}); PX(17,27,1,1,{55,39,34,255}); PX(18,27,1,1,{45,30,27,255});
					PX(12,28,1,1,{46,33,30,255}); PX(13,28,1,1,{50,34,30,255}); PX(14,28,1,2,{27,17,15,255}); PX(16,28,1,1,{45,28,26,255});
					PX(17,28,1,1,{57,40,35,255}); PX(18,28,1,1,{45,29,27,255}); PX(12,29,1,1,{47,33,29,255}); PX(13,29,1,1,{53,36,33,255});
					PX(17,29,1,1,{48,33,29,255}); PX(18,29,1,1,{38,25,23,255}); PX(19,29,1,1,{27,16,16,255}); PX(12,30,1,1,{49,31,25,255});
					PX(13,30,1,1,{31,19,16,255}); PX(17,30,1,1,{38,22,21,255}); PX(18,30,1,1,{67,43,36,255}); PX(19,30,1,1,{40,24,23,255});
				}
				else if (nFrame == 7)
				{
					PX(14,1,1,6,{64,58,58,255}); PX(13,7,1,1,{60,53,53,255}); PX(14,7,1,1,{45,40,40,255}); PX(11,8,1,1,{34,29,29,255});
					PX(12,8,1,1,{31,22,22,255}); PX(13,8,1,1,{45,39,39,255}); PX(14,8,1,1,{44,39,39,255}); PX(11,9,1,2,{36,30,30,255});
					PX(12,9,1,2,{31,24,25,255}); PX(13,9,1,1,{39,32,33,255}); PX(14,9,1,1,{35,27,27,255}); PX(13,10,1,1,{28,18,18,255});
					PX(14,10,1,1,{24,13,15,255}); PX(12,11,1,1,{29,23,23,255}); PX(13,11,1,1,{31,21,21,255}); PX(14,11,1,1,{25,14,15,255});
					PX(11,12,1,1,{58,36,32,255}); PX(12,12,1,1,{51,32,28,255}); PX(13,12,1,1,{52,28,30,255}); PX(14,12,1,1,{33,18,20,255});
					PX(15,12,1,1,{169,100,93,255}); PX(10,13,1,1,{54,34,31,255}); PX(11,13,1,1,{53,37,33,255}); PX(12,13,1,1,{69,48,40,255});
					PX(13,13,1,1,{58,35,31,255}); PX(14,13,1,1,{100,52,54,255}); PX(15,13,1,1,{143,91,81,255}); PX(16,13,1,1,{85,38,45,255});
					PX(9,14,1,1,{54,34,30,255}); PX(10,14,1,1,{79,51,42,255}); PX(11,14,1,1,{63,44,38,255}); PX(12,14,1,1,{51,38,34,255});
					PX(13,14,1,1,{51,34,29,255}); PX(14,14,1,1,{63,42,36,255}); PX(15,14,1,1,{102,60,57,255}); PX(16,14,1,1,{139,80,77,255});
					PX(9,15,1,1,{34,20,18,255}); PX(10,15,1,1,{58,36,32,255}); PX(11,15,1,1,{40,26,25,255}); PX(12,15,1,1,{34,25,24,255});
					PX(13,15,1,1,{45,29,26,255}); PX(14,15,1,1,{63,31,34,255}); PX(15,15,1,1,{138,96,83,255}); PX(16,15,1,1,{132,82,74,255});
					PX(17,15,1,1,{185,125,106,255}); PX(18,15,1,1,{113,62,62,255}); PX(9,16,1,1,{43,24,24,255}); PX(10,16,1,1,{46,28,27,255});
					PX(11,16,1,1,{33,23,23,255}); PX(12,16,1,1,{34,23,23,255}); PX(13,16,1,1,{38,23,21,255}); PX(14,16,1,1,{63,32,33,255});
					PX(15,16,1,1,{102,50,51,255}); PX(16,16,1,1,{55,28,26,255}); PX(17,16,1,1,{158,106,89,255}); PX(18,16,1,1,{163,103,91,255});
					PX(10,17,1,1,{25,14,14,255}); PX(11,17,1,1,{29,18,18,255}); PX(12,17,1,1,{33,20,20,255}); PX(13,17,1,1,{28,16,16,255});
					PX(14,17,1,1,{64,25,23,255}); PX(15,17,1,1,{120,42,35,255}); PX(16,17,1,1,{108,39,33,255}); PX(17,17,1,1,{84,36,36,255});
					PX(18,17,1,1,{76,39,40,255}); PX(11,18,1,1,{37,21,19,255}); PX(12,18,1,1,{31,20,19,255}); PX(13,18,1,1,{32,21,22,255});
					PX(14,18,1,1,{40,23,22,255}); PX(15,18,1,1,{64,26,25,255}); PX(16,18,1,1,{39,20,21,255}); PX(17,18,1,1,{32,17,18,255});
					PX(13,19,1,1,{29,25,25,255}); PX(14,19,1,1,{32,29,29,255}); PX(15,19,1,2,{30,27,27,255}); PX(16,19,1,1,{22,20,20,255});
					PX(17,19,1,1,{21,19,19,255}); PX(18,19,1,1,{19,13,13,255}); PX(13,20,1,1,{25,21,21,255}); PX(14,20,1,1,{31,28,28,255});
					PX(16,20,1,1,{23,21,21,255}); PX(17,20,1,1,{19,14,14,255}); PX(18,20,1,1,{21,15,15,255}); PX(14,21,1,1,{35,28,28,255});
					PX(15,21,1,1,{38,33,33,255}); PX(16,21,1,1,{30,27,27,255}); PX(17,21,1,1,{20,17,17,255}); PX(18,21,1,1,{18,13,13,255});
					PX(19,21,1,1,{20,15,15,255}); PX(14,22,1,1,{50,33,29,255}); PX(15,22,1,1,{50,37,32,255}); PX(16,22,1,1,{40,28,27,255});
					PX(17,22,1,1,{39,26,25,255}); PX(18,22,1,1,{34,22,22,255}); PX(19,22,1,1,{27,16,16,255}); PX(15,23,1,1,{45,31,27,255});
					PX(16,23,1,1,{53,38,33,255}); PX(17,23,1,1,{50,31,27,255}); PX(18,23,1,1,{33,19,19,255}); PX(19,23,1,1,{23,13,13,255});
					PX(15,24,1,1,{28,17,16,255}); PX(16,24,1,1,{45,32,28,255}); PX(17,24,1,1,{45,29,27,255}); PX(18,24,1,1,{23,13,13,255});
					PX(15,25,1,1,{31,22,19,255}); PX(16,25,1,1,{55,40,34,255}); PX(17,25,1,1,{37,22,22,255}); PX(14,26,1,1,{38,24,23,255});
					PX(15,26,1,1,{56,41,35,255}); PX(16,26,1,1,{46,29,27,255}); PX(17,26,1,1,{16,9,9,255}); PX(14,27,1,1,{53,37,33,255});
					PX(15,27,1,1,{51,34,31,255}); PX(16,27,1,1,{36,22,22,255}); PX(13,28,1,1,{44,29,26,255}); PX(14,28,1,1,{55,40,34,255});
					PX(15,28,1,1,{48,31,28,255}); PX(16,28,1,1,{29,17,17,255}); PX(13,29,1,1,{31,20,17,255}); PX(14,29,1,1,{41,28,24,255});
					PX(15,29,1,1,{39,23,23,255}); PX(16,29,1,1,{21,12,12,255}); PX(13,30,1,1,{64,41,35,255}); PX(14,30,1,1,{59,37,32,255});
					PX(15,30,1,1,{36,21,21,255}); PX(16,30,1,1,{17,9,9,255});
				}
				break;
			}
			case 7:
			{
				const int nPacCx = centerX - 6;
				const float flChomp = active ? (0.5f + 0.5f * sinf(animTime * 2.0f)) : 0.4f;
				const float flHalfMouth = (0.10f + 0.65f * flChomp) * (3.14159265f * 0.5f);
				auto PX = [&](int x, int y, Color_t c) {
					if (!active) c.a = 180;
					H::Draw->Rect(nPacCx - 13 + x * 2, centerY - 13 + y * 2, 2, 2, c);
				};
				constexpr int nIconR = 6;
				for (int y = -nIconR; y <= nIconR; y++)
				{
					for (int x = -nIconR; x <= nIconR; x++)
					{
						if (sqrtf(float(x * x + y * y)) > nIconR + 0.25f)
							continue;
						if (x > 0 && fabsf(atan2f(float(y), float(x))) < flHalfMouth)
							continue;
						PX(x + nIconR, y + nIconR, { 255, 219, 0, 255 });
					}
				}
				{
					Color_t c = { 255, 183, 151, 255 };
					if (active)
					{
						for (int i = 0; i < 2; i++)
						{
							const float flProgress = fmodf(animTime * 0.4f + i * 0.5f, 1.f);
							const int nDotX = nPacCx + 26 - static_cast<int>(flProgress * 21.f);
							if (nDotX <= nPacCx + 6)
								continue;
							H::Draw->Rect(nDotX, centerY - 1, 3, 3, c);
						}
					}
					else
					{
						c.a = 180;
						H::Draw->Rect(nPacCx + 19, centerY - 1, 3, 3, c);
						H::Draw->Rect(nPacCx + 24, centerY - 1, 3, 3, c);
					}
				}
				break;
			}
			case 8:
			{
				Color_t netHub = { 220, 230, 240, 255 };
				Color_t netNode = { 120, 170, 255, 255 };
				Color_t netLink = { 90, 110, 150, 255 };
				Color_t netPing = { 80, 220, 130, 255 };
				if (!active) {
					netHub.a = 200;
					netNode.a = 200;
					netPing.a = 200;
				}
				H::Draw->Line(centerX, centerY, centerX - 11, centerY - 8, netLink);
				H::Draw->Line(centerX, centerY, centerX + 11, centerY - 8, netLink);
				H::Draw->Line(centerX, centerY, centerX - 11, centerY + 9, netLink);
				H::Draw->Line(centerX, centerY, centerX + 11, centerY + 9, netLink);
				H::Draw->Line(centerX - 11, centerY - 8, centerX + 11, centerY - 8, netLink);
				H::Draw->Line(centerX - 11, centerY + 9, centerX + 11, centerY + 9, netLink);
				H::Draw->OutlinedCircle(centerX, centerY, static_cast<int>(6 * pulse), 12, netNode);
				H::Draw->FilledCircle(centerX, centerY, static_cast<int>(4 * pulse), 12, netHub);
				H::Draw->FilledCircle(centerX - 11, centerY - 8, 2, 8, netNode);
				H::Draw->FilledCircle(centerX + 11, centerY - 8, 2, 8, netNode);
				H::Draw->FilledCircle(centerX - 11, centerY + 9, 2, 8, netNode);
				H::Draw->FilledCircle(centerX + 11, centerY + 9, 2, 8, netNode);
				if (frame == 0 || active)
					H::Draw->Rect(centerX - 1, centerY - 1, 2, 2, netPing);
				break;
			}
			case 9:
			{
				Color_t noteGold = { 255, 205, 90, 255 };
				Color_t noteShade = { 190, 140, 40, 255 };
				Color_t noteShine = { 255, 240, 200, 255 };
				Color_t waveBlue = { 120, 175, 255, 255 };
				if (!active) {
					noteGold.a = 200;
					noteShade.a = 200;
					noteShine.a = 200;
					waveBlue.a = 200;
				}
				auto PX = [&](int px, int py, int pw, int ph, Color_t c) {
					H::Draw->Rect(iconX + px, iconY + py, pw, ph, c);
				};
				const int nBob = active ? -static_cast<int>((std::sin(animTime * 2.2f) * 0.5f + 0.5f) * 2.0f) : 0;
				PX(12, 2 + nBob, 15, 4, noteGold);
				PX(12, 5 + nBob, 15, 1, noteShade);
				PX(12, 5 + nBob, 2, 8, noteGold);
				PX(25, 5 + nBob, 2, 8, noteGold);
				H::Draw->FilledCircle(iconX + 11, iconY + 15 + nBob, 4, 12, noteGold);
				H::Draw->FilledCircle(iconX + 24, iconY + 15 + nBob, 4, 12, noteGold);
				PX(9, 13 + nBob, 2, 1, noteShine);
				PX(22, 13 + nBob, 2, 1, noteShine);
				PX(9, 18 + nBob, 4, 1, noteShade);
				PX(22, 18 + nBob, 4, 1, noteShade);
				for (int i = 0; i < 5; i++) {
					const int nBarH = active
						? 2 + static_cast<int>((std::sin(animTime * 3.0f + i * 1.1f) * 0.5f + 0.5f) * 4.0f)
						: (i == 0 || i == 4) ? 3 : (i == 1 || i == 3) ? 5 : 6;
					PX(5 + i * 6, 26 - nBarH, 3, nBarH, waveBlue);
				}
				break;
			}
		}
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
	if (DrawTabBox("NavBot", 6, MainTab == EMainTabs::NAVBOT))
		MainTab = EMainTabs::NAVBOT;
	if (DrawTabBox("Players", 3, MainTab == EMainTabs::PLAYERS))
		MainTab = EMainTabs::PLAYERS;
	if (DrawTabBox("Configs", 4, MainTab == EMainTabs::CONFIGS))
		MainTab = EMainTabs::CONFIGS;
	if (SoundpadEnabled)
	{
		if (DrawTabBox("Soundpad", 9, MainTab == EMainTabs::SOUND))
			MainTab = EMainTabs::SOUND;
	}
	if (DrawTabBox("NecroNet", 8, MainTab == EMainTabs::NETWORK))
		MainTab = EMainTabs::NETWORK;
	if (DrawTabBox("PacMan", 7, MainTab == EMainTabs::PACMAN))
		MainTab = EMainTabs::PACMAN;
	F::PacMan->NotifyVisible(MainTab == EMainTabs::PACMAN);
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
				CheckBox("Always On", CFG::Aimbot_Always_On);
				if (!CFG::Aimbot_Always_On)
					InputKey("Key", CFG::Aimbot_Key);
				InputKey("Ignore Untagged", CFG::Aimbot_Ignore_Untagged_Key);
				multiselect("Targets", AimbotTargets, {
					{ "Players", CFG::Aimbot_Target_Players },
					{ "Buildings", CFG::Aimbot_Target_Buildings }
					});
				multiselect("Ignore", AimbotIgnores, {
					{ "Friends", CFG::Aimbot_Ignore_Friends },
					{ "Invisible", CFG::Aimbot_Ignore_Invisible },
					{ "Invulnerable", CFG::Aimbot_Ignore_Invulnerable },
					{ "Taunting", CFG::Aimbot_Ignore_Taunting },
					{ "Stickies", CFG::Aimbot_Ignore_Stickies },
					{ "Vaccinator Popped", CFG::Aimbot_Ignore_Vaccinator }
					});
			}
			GroupBoxEnd();
			GroupBoxStart("Melee", 150);
			{
				CheckBox("Active", CFG::Aimbot_Melee_Active);
				CheckBox("Always Active", CFG::Aimbot_Melee_Always_Active);
				CheckBox("Always Crit", CFG::Aimbot_Melee_Always_Crit);
				CheckBox("Target Lag Records", CFG::Aimbot_Melee_Target_LagRecords);
				CheckBox("Predict Swing", CFG::Aimbot_Melee_Predict_Swing);
				CheckBox("Walk To Target", CFG::Aimbot_Melee_Walk_To_Target);
				CheckBox("Whip Teammates", CFG::Aimbot_Melee_Whip_Teammates);
				CheckBox("Auto Repair Buildings", CFG::Aimbot_Melee_Auto_Repair);
				CheckBox("Auto Backstab", CFG::Aimbot_Melee_Auto_Backstab);
				CheckBox("Backstab Fallback", CFG::Aimbot_Melee_Auto_Backstab_Fallback);
				CheckBox("Ignore Razorback", CFG::Aimbot_Melee_Ignore_Razorback);
				CheckBox("Swing Predict Lag", CFG::Aimbot_Melee_Swing_Predict_Lag);
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
				CheckBox("Auto Rev", CFG::Aimbot_Hitscan_Auto_Rev);
				CheckBox("Wait For Headshot", CFG::Aimbot_Hitscan_Wait_For_Headshot);
				CheckBox("Wait For Charge", CFG::Aimbot_Hitscan_Wait_For_Charge);
				CheckBox("smooth when spec", CFG::Aimbot_Hitscan_Smooth_When_Spec);
				CheckBox("FakeLag Fix", CFG::Aimbot_Hitscan_FakeLagFix);
				static int nPrevAimType = CFG::Aimbot_Hitscan_Aim_Type;
				static float flBackupFOV = CFG::Aimbot_Hitscan_FOV;
				SelectSingle("Aim Type", CFG::Aimbot_Hitscan_Aim_Type, {
					{ "Normal", 0 },
					{ "Silent", 1 },
					{ "Smooth", 2 },
					{ "Triggerbot", 3 }
				});
				if (CFG::Aimbot_Hitscan_Aim_Type != nPrevAimType)
				{
					if (CFG::Aimbot_Hitscan_Aim_Type == 3)
					{
						flBackupFOV = CFG::Aimbot_Hitscan_FOV;
						CFG::Aimbot_Hitscan_FOV = 30.0f;
					}
					else if (nPrevAimType == 3)
					{
						CFG::Aimbot_Hitscan_FOV = flBackupFOV;
					}
					nPrevAimType = CFG::Aimbot_Hitscan_Aim_Type;
				}
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
				if (CFG::Aimbot_Hitscan_Sort == 1)
				{
					CheckBox("Visible Only", CFG::Aimbot_Hitscan_Sort_VisibleOnly);
				}
				multiselect("Scan", HitscanScan, {
					{ "Head", CFG::Aimbot_Hitscan_Scan_Head },
					{ "Body", CFG::Aimbot_Hitscan_Scan_Body },
					{ "Arms", CFG::Aimbot_Hitscan_Scan_Arms },
					{ "Legs", CFG::Aimbot_Hitscan_Scan_Legs },
					{ "Buildings", CFG::Aimbot_Hitscan_Scan_Buildings }
					});
				if (CFG::Aimbot_Hitscan_Aim_Type != 3)
				{
					SliderFloat("FOV", CFG::Aimbot_Hitscan_FOV, 1.0f, 180.0f, 1.0f, "%.0f");
				}
				if (CFG::Aimbot_Hitscan_Aim_Type == 2)
				{
					SliderFloat("Smoothing", CFG::Aimbot_Hitscan_Smoothing, 1.5f, 20.0f, 0.5f, "%.1f");
				}
				if (CFG::Aimbot_Hitscan_Aim_Type == 1)
				{
					CheckBox("Heavy Silent Return", CFG::Aimbot_Hitscan_Silent_Return);
					if (CFG::Aimbot_Hitscan_Silent_Return)
						SliderFloat("Return Smooth", CFG::Aimbot_Hitscan_Silent_Return_Speed, 2.0f, 20.0f, 0.5f, "%.1f");
				}
				SliderFloat("Fake Latency", CFG::Aimbot_Hitscan_Fake_Latency, 0.0f, 600.0f, 10.0f, "%.0f ms");
				SliderInt("Hitchance", CFG::Aimbot_Hitscan_Hitchance, 0, 100, 1);
			}
			GroupBoxEnd();
			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;
			GroupBoxStart("Projectile", 150);
			{
				CheckBox("Active", CFG::Aimbot_Projectile_Active);
				CheckBox("Midpoint Aim", CFG::Aimbot_Projectile_Midpoint_Aim);
				CheckBox("Auto Double Donk", CFG::Aimbot_Projectile_Auto_Double_Donk);
				CheckBox("Advanced Head Aim", CFG::Aimbot_Projectile_Advanced_Head_Aim);
				CheckBox("Auto Repair", CFG::Aimbot_Projectile_AutoRepair);
				CheckBox("Ground Strafe Prediction", CFG::Aimbot_Projectile_Ground_Strafe_Prediction);
				CheckBox("Air Strafe Prediction", CFG::Aimbot_Projectile_Air_Strafe_Prediction);
				CheckBox("Prioritize Feet", CFG::Aimbot_Amalgam_Projectile_Hitbox_PrioritizeFeet);
				CheckBox("Charge Weapon", CFG::Aimbot_Amalgam_Projectile_Mod_ChargeWeapon);
				CheckBox("Use Arm Time", CFG::Aimbot_Amalgam_Projectile_Mod_UseArmTime);
				CheckBox("Air Splash", CFG::Aimbot_Amalgam_Projectile_Mod_AirSplash);
				CheckBox("Lob Angles", CFG::Aimbot_Amalgam_Projectile_Mod_LobAngles);
				SliderInt("Hit Chance", CFG::Aimbot_Amalgam_Projectile_HitChance, 0, 100, 1);
				CheckBox(CFG::Misc_AntiCheat_Enabled ? "Neckbreaker (disabled)" : "Neckbreaker", CFG::Aimbot_Projectile_Neckbreaker);
				if (CFG::Aimbot_Projectile_Neckbreaker)
				{
					SliderInt("Neckbreaker Step", CFG::Aimbot_Projectile_NeckbreakerStep, 15, 180, 15);
				}
				SelectSingle("Splash", CFG::Aimbot_Amalgam_Projectile_Splash,
					{
						{ "Off", 0 },
						{ "Include", 1 },
						{ "Prefer", 2 },
						{ "Only", 3 },
					});
				if (CFG::Aimbot_Amalgam_Projectile_Splash != 0)
				{
					SelectSingle("Splash Mode", CFG::Aimbot_Amalgam_Projectile_SplashMode,
						{
							{ "Trace", 0 },
							{ "Face", 1 },
						});
					if (CFG::Aimbot_Amalgam_Projectile_SplashMode == 1)
					{
						SliderFloat("Splash Density Direct", CFG::Aimbot_Amalgam_Projectile_SplashDensity_Direct, 0.0f, 100.0f, 1.0f, "%.0f");
						SliderFloat("Splash Density Arc", CFG::Aimbot_Amalgam_Projectile_SplashDensity_Arc, 0.0f, 100.0f, 1.0f, "%.0f");
					}
				}
				SelectSingle("Aim Type", CFG::Aimbot_Projectile_Aim_Type, {
					{ "Normal", 0 },
					{ "Silent", 1 }
					});
				SelectSingle("Aim Position", CFG::Aimbot_Projectile_Aim_Position, {
					{ "Feet", 0 },
					{ "Body", 1 },
					{ "Head", 2 },
					{ "Auto", 3 }
					});
				SelectSingle("Sort", CFG::Aimbot_Projectile_Sort, {
					{ "FOV", 0 },
					{ "Distance", 1 }
					});
				SliderFloat("FOV", CFG::Aimbot_Projectile_FOV, 1.0f, 180.0f, 1.0f, "%.0f");
				SliderFloat("Max Simulation Time", CFG::Aimbot_Projectile_Max_Simulation_Time, 0.1f, 7.0f, 0.25f, "%.1fs");
				SliderInt("Max Targets", CFG::Aimbot_Projectile_Max_Processing_Targets, 1, 6, 1);
				SliderInt("Splash Points", CFG::Aimbot_Amalgam_Projectile_SplashPoints, 50, 400, 5);
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
				CheckBox("Always On", CFG::Triggerbot_AutoAirblast_Always_On);
				CheckBox("Aimbot Support", CFG::Triggerbot_AutoAirblast_Aimbot_Support);
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
				CheckBox("Prevent Self Damage", CFG::Triggerbot_AutoDetonate_Prevent_Self);
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
				CheckBox("Pop On Medic Call", CFG::AutoUber_Pop_On_Voice);
				CheckBox("Friends Only", CFG::AutoUber_Friends_Only);
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
					{ "Invulnerable", CFG::Triggerbot_AutoBackstab_Ignore_Invulnerable },
					{ "Razorback", CFG::Triggerbot_AutoBackstab_Ignore_Razorback }
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
				SelectSingle("Strict Level", CFG::Triggerbot_AutoVaccinator_Tolerance, {
					{ "Max", 0 },
					{ "High", 1 },
					{ "Medium", 2 },
					{ "Low", 3 }
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
			GroupBoxStart("Auto Heal", 150);
			{
				CheckBox("Auto Heal", CFG::AutoUber_AutoHeal_Active);
				CheckBox("Always On", CFG::AutoHeal_Always_On);
				CheckBox("Friends Only", CFG::AutoHeal_Friends_Only);
				CheckBox("Auto Arrow (heal team)", CFG::Aimbot_Projectile_AutoArrow);
				if (CFG::Aimbot_Projectile_AutoArrow)
					CheckBox("Arrow Friends Only", CFG::Aimbot_Projectile_AutoArrow_FriendsOnly);
			}
			GroupBoxEnd();
			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;
		}
	}
	if (MainTab == EMainTabs::VISUALS)
	{
		enum class EVisualsTabs { ESP, RADAR, MATERIALS, OUTLINES, OTHER, OTHER2, SKINCHANGER, COLORS };
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
		if (Button("Skins", VisualsTab == EVisualsTabs::SKINCHANGER))
			VisualsTab = EVisualsTabs::SKINCHANGER;
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
					{ "Tagged", CFG::ESP_Players_Ignore_Tagged },
					{ "Tagged Teammates", CFG::ESP_Players_Ignore_Tagged_Teammates },
					{ "Friend Teammates", CFG::ESP_Players_Friend_Teammates }
					});
				multiselect("Draw", PlayerDraw, {
					{ "Name", CFG::ESP_Players_Name },
					{ "Weapon Name", CFG::ESP_Players_Weapon_Name },
					{ "Tags", CFG::ESP_Players_Tags },
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
					{ "Invisible", CFG::Radar_Players_Ignore_Invisible },
					{ "Friend Teammates", CFG::Radar_Players_Friend_Teammates }
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
					{ "MVM Money", CFG::Materials_World_Ignore_MVM_Money },
					{ "Dropped Guns", CFG::Materials_World_Ignore_DroppedGuns }
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
					{ "Tagged", CFG::Materials_Players_Ignore_Tagged },
					{ "Tagged Teammates", CFG::Materials_Players_Ignore_Tagged_Teammates },
					{ "Friend Teammates", CFG::Materials_Players_Friend_Teammates }
					});
				CheckBox("Show Team Medics", CFG::Materials_Players_Show_Teammate_Medics);
				CheckBox("Weapons Own Color", CFG::Materials_Players_Weapons_Active);
				if (CFG::Materials_Players_Weapons_Active)
				{
					ColorPicker("Weapon Color", CFG::Color_Materials_Weapons);
				}
			}
			GroupBoxEnd();
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
					{ "Cartoony Alt", 3 },
					{ "TF2 Glow", 4 }
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
					{ "MVM Money", CFG::Outlines_World_Ignore_MVM_Money },
					{ "Dropped Guns", CFG::Outlines_World_Ignore_DroppedGuns }
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
					{ "Tagged", CFG::Outlines_Players_Ignore_Tagged },
					{ "Tagged Teammates", CFG::Outlines_Players_Ignore_Tagged_Teammates },
					{ "Friend Teammates", CFG::Outlines_Players_Friend_Teammates }
					});
				CheckBox("Show Team Medics", CFG::Outlines_Players_Show_Teammate_Medics);
			}
			GroupBoxEnd();
			GroupBoxStart("Lag Records", 150);
			{
				CheckBox("Active", CFG::Outlines_LagRecords_Active);
				SliderFloat("Alpha", CFG::Outlines_LagRecords_Alpha, 0.0f, 1.0f, 0.1f, "%.1f");
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
				CheckBox("Aimbot FOV Circle", CFG::Visuals_Aimbot_FOV_Circle);
				if (CFG::Visuals_Aimbot_FOV_Circle)
				{
					CheckBox("Hide in Third Person", CFG::Visuals_Aimbot_FOV_Circle_Hide_Thirdperson);
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
					{ "Screen Fade", CFG::Visuals_Remove_Screen_Fade },
					{ "MOTD", CFG::Visuals_Remove_MOTD }
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
				SelectSingle("Movement Path Style", CFG::Visuals_Draw_Movement_Path_Style,
					{
						{ "Disabled", 0 },
						{ "Line", 1 },
						{ "Dashed Line", 2 },
						{ "Alt Line", 3 }
					});
				SelectSingle("Projectile Path Style", CFG::Visuals_Draw_Predicted_Path_Style,
					{
						{ "Disabled", 0 },
						{ "Line", 1 },
						{ "Dashed Line", 2 },
						{ "Alt Line", 3 }
					});
				SelectSingle("Projectile Trail", CFG::Visuals_Projectile_Trail,
					{
						{ "Default", 0 },
						{ "None", 1 },
						{ "Rocket", 2 },
						{ "Critical", 3 },
						{ "Energy", 4 },
						{ "Charged", 5 },
						{ "Ray", 6 },
						{ "Fireball", 7 },
						{ "Teleport", 8 },
						{ "Fire", 9 },
						{ "Flame", 10 },
						{ "Sparks", 11 },
						{ "Flare", 12 },
						{ "Trail", 13 },
						{ "Health", 14 },
						{ "Smoke", 15 },
						{ "Bubbles", 16 },
						{ "Halloween", 17 },
						{ "Monoculus", 18 },
						{ "Sparkles", 19 },
						{ "Rainbow", 20 }
					});
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
			GroupBoxStart("Mini Status Window", 150);
			{
				CheckBox("Status Window", CFG::Visuals_StatusWindow_Active);
				if (CFG::Visuals_StatusWindow_Active)
				{
					SliderInt("Width", CFG::Visuals_StatusWindow_Width, 150, 600, 1);
					SliderInt("Length", CFG::Visuals_StatusWindow_Height, 60, 800, 1);
					SliderFloat("Outline Alpha", CFG::Visuals_StatusWindow_Outline_Alpha, 0.1f, 1.0f, 0.1f, "%.1f");
					SliderFloat("Background Alpha", CFG::Visuals_StatusWindow_Background_Alpha, 0.1f, 1.0f, 0.1f, "%.1f");
					multiselect("Show", StatusWindowShow, {
						{ "Aimbot", CFG::Visuals_StatusWindow_Show_Aimbot },
						{ "Fake Lag", CFG::Visuals_StatusWindow_Show_FakeLag },
						{ "Anti-Aim", CFG::Visuals_StatusWindow_Show_AntiAim },
						{ "No Spread", CFG::Visuals_StatusWindow_Show_NoSpread },
						{ "Airblast", CFG::Visuals_StatusWindow_Show_Airblast },
						{ "Auto-Uber", CFG::Visuals_StatusWindow_Show_AutoUber },
						{ "Auto Vacc", CFG::Visuals_StatusWindow_Show_AutoVacc },
						{ "Auto Heal", CFG::Visuals_StatusWindow_Show_AutoHeal },
						{ "Detonate", CFG::Visuals_StatusWindow_Show_AutoDetonate },
						{ "Backstab", CFG::Visuals_StatusWindow_Show_AutoBackstab },
						{ "Duck Air", CFG::Visuals_StatusWindow_Show_DuckAir },
						{ "Jumpbug", CFG::Visuals_StatusWindow_Show_Jumpbug },
						{ "Edge Jump", CFG::Visuals_StatusWindow_Show_EdgeJump },
						{ "Rocket Jump", CFG::Visuals_StatusWindow_Show_RocketJump },
						{ "FaN Jump", CFG::Visuals_StatusWindow_Show_FaNJump },
						{ "Thirdperson", CFG::Visuals_StatusWindow_Show_Thirdperson },
						{ "Warp", CFG::Visuals_StatusWindow_Show_Warp },
						{ "NavBot", CFG::Visuals_StatusWindow_Show_NavBot },
						{ "Killspree", CFG::Visuals_StatusWindow_Show_Killspree }
						});
				}
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
				SelectSingle("Streamer Mode", CFG::Misc_Streamer_Mode, {
					{ "Off", 0 },
					{ "Local", 1 },
					{ "Friends", 2 },
					{ "Party", 3 },
					{ "All", 4 }
					});
			}
			GroupBoxEnd();
			GroupBoxStart("Freecam", 150);
			{
				InputKey("Toggle Key", CFG::Visuals_Freecam_Key);
				SliderFloat("Speed", CFG::Visuals_Freecam_Speed, 100.0f, 2000.0f, 50.0f, "%.0f");
			}
			GroupBoxEnd();
			GroupBoxStart("Aspect Ratio", 150);
			{
				SliderFloat("Ratio", CFG::Visuals_Freecam_AspectRatio, 0.0f, 5.0f, 0.05f, "%.2f");
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
				CheckBox("Minimal Entities", CFG::Perf_Minimal_Entities);
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
			GroupBoxStart("Custom Skybox", 150);
			{
				static std::string strSkySearch;
				InputText("Sky Search", "Search:", strSkySearch, 32);

				std::string strNeedle = strSkySearch;
				for (char &ch : strNeedle)
					ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));

				const auto &vecSkies = F::Models->GetSkyNames();

				int nShown = 0;
				for (const auto &strSky : vecSkies)
				{
					if (nShown >= 9)
						break;

					if (!strNeedle.empty())
					{
						std::string strHay = strSky;
						for (char &ch : strHay)
							ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
						if (strHay.find(strNeedle) == std::string::npos)
							continue;
					}

					if (Button(strSky.c_str(), strSky == CFG::Visuals_Custom_Skybox_Texture_Name))
						CFG::Visuals_Custom_Skybox_Texture_Name = strSky;
					nShown++;
				}

				if (vecSkies.empty())
					Button("No Skies (drop vpk + Refresh)", false);
				else if (vecSkies.size() > static_cast<size_t>(nShown) && strNeedle.empty())
					Button("...more (use search)", false);
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
			GroupBoxStart("Custom Models", 150);
			{
				CheckBox("Active", CFG::Models_Active);
				F::Models->MountUserFolder();
				CheckBox("Necromancer Gloves", CFG::Models_Gloves);
				F::Models->ApplyGloves();
				CheckBox("Hide Material Errors", CFG::Models_HideMaterialErrors);
				F::Models->UpdateMaterialFilter();
				if (Button("Refresh"))
				{
					F::Models->Refresh();
					AddToast("You must rejoin the map to reload models");
				}
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
			GroupBoxStart("Chat ESP", 150);
			{
				CheckBox("Active", CFG::Visuals_ChatESP_Active);
				SliderFloat("Duration", CFG::Visuals_ChatESP_Duration, 1.0f, 15.0f, 1.0f, "%.0fs");
				SliderInt("Max Length", CFG::Visuals_ChatESP_MaxLength, 20, 100, 5);
				SliderFloat("Max Distance", CFG::Visuals_ChatESP_MaxDistance, 500.0f, 5000.0f, 100.0f, "%.0f");
				CheckBox("Show Pointer", CFG::Visuals_ChatESP_ShowPointer);
			}
			GroupBoxEnd();
		}
		if (VisualsTab == EVisualsTabs::SKINCHANGER)
		{
			anchor_x = m_nCursorX;
			anchor_y = m_nCursorY;
			GroupBoxStart("Skin Changer", 150);
			{
				CheckBox("Active", CFG::SkinChanger_Active);
				CheckBox("Australium", CFG::SkinChanger_Australium);
				CheckBox("Festivized", CFG::SkinChanger_Festivized);
			}
			GroupBoxEnd();
			GroupBoxStart("Killstreak", 150);
			{
				SelectSingle("Tier", CFG::SkinChanger_Killstreak_Tier, {
					{ "Off", 0 },
					{ "Basic", 1 },
					{ "Specialized", 2 },
					{ "Professional", 3 }
				});
				if (CFG::SkinChanger_Killstreak_Tier >= 2)
				{
					SelectSingle("Sheen", CFG::SkinChanger_Killstreak_Sheen, {
						{ "None", 0 },
						{ "Team Shine", 1 },
						{ "Deadly Daffodil", 2 },
						{ "Manndarin", 3 },
						{ "Mean Green", 4 },
						{ "Agonizing Emerald", 5 },
						{ "Villainous Violet", 6 },
						{ "Hot Rod", 7 }
					});
				}
				if (CFG::SkinChanger_Killstreak_Tier >= 3)
				{
					SelectSingle("Killstreaker", CFG::SkinChanger_Killstreak_Effect, {
						{ "None", 0 },
						{ "Fire Horns", 1 },
						{ "Cerebral Discharge", 2 },
						{ "Tornado", 3 },
						{ "Flames", 4 },
						{ "Singularity", 5 },
						{ "Incinerator", 6 },
						{ "Hypno-Beam", 7 }
					});
				}
			}
			GroupBoxEnd();
			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;
			GroupBoxStart("Killstreak Sheen", 150);
			{
				CheckBox("Force Sheen", CFG::Visuals_Sheen_Active);
				if (CFG::Visuals_Sheen_Active)
				{
					if (!CFG::Visuals_Sheen_Rainbow)
					{
						SelectSingle("Sheen Color", CFG::Visuals_Sheen_Index, {
							{ "Red", 1 },
							{ "Fire", 3 },
							{ "Green", 4 },
							{ "Cyan", 5 },
							{ "Purple", 6 },
							{ "Pink", 7 },
							{ "Custom", 8 }
						});
					}
					multiselect("Target", SheenTarget, {
						{ "Local", CFG::Visuals_Sheen_Local },
						{ "Friend", CFG::Visuals_Sheen_Friend },
						{ "Teammates", CFG::Visuals_Sheen_Teammates },
						{ "Enemy", CFG::Visuals_Sheen_Enemy }
						});
					CheckBox("RGB", CFG::Visuals_Sheen_Rainbow);
					if (CFG::Visuals_Sheen_Rainbow)
					{
						SliderFloat("RGB Rate", CFG::Visuals_Sheen_Rainbow_Rate, 0.5f, 20.0f, 0.5f, "%.1f");
					}
					SliderFloat("Interval", CFG::Visuals_Sheen_Interval, 0.0f, 15.0f, 0.1f, "%.1f");
					SliderFloat("Intensity", CFG::Visuals_Sheen_Intensity, 0.1f, 3.0f, 0.1f, "%.1f");
					if (!CFG::Visuals_Sheen_Rainbow && CFG::Visuals_Sheen_Index == 8)
					{
						ColorPicker("Color", CFG::Color_Sheen_Tint);
					}
				}
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
				ColorPicker("Targeted", CFG::Color_Targeted);
				ColorPicker("Nigger", CFG::Color_Nigger);
				ColorPicker("Retard Legit", CFG::Color_RetardLegit);
				ColorPicker("Streamer", CFG::Color_Streamer);
				ColorPicker("Follow Player", CFG::Color_FollowPlayer);
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
				ColorPicker("FakeLag", CFG::Color_FakeLag);
				ColorPicker("Fake Model", CFG::Color_FakeModel);
				ColorPicker("Lag Record", CFG::Color_LagRecord);
			}
			GroupBoxEnd();
			GroupBoxStart("Misc Enemy", 150);
			{
				CheckBox("Outline Color by HP", CFG::Visuals_Enemy_Outline_HP_Based);
				CheckBox("Materials Color by HP", CFG::Visuals_Enemy_Materials_HP_Based);
				CheckBox("Custom Name Color", CFG::Misc_Enemy_Custom_Name_Color);
				if (CFG::Misc_Enemy_Custom_Name_Color)
				{
					ColorPicker("Name Color", CFG::Color_Custom_Name);
				}
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
			GroupBoxStart("Background Image", 150);
			{
				CheckBox("Enable", CFG::Menu_Background_Image_Enabled);
				if (CFG::Menu_Background_Image_Enabled)
				{
					SliderFloat("Opacity", CFG::Menu_Background_Image_Transparency, 0.1f, 1.0f, 0.05f, "%.2f");
					if (Button("Refresh"))
					{
						MenuBackground::g_bNeedRefresh = true;
						MenuBackground::g_bImageLoaded = false;
						MenuBackground::g_nTextureId = -1;
					}
					if (Button("Open Folder"))
					{
						std::string folderPath = "C:\\necromancer_tf2\\menu_backgrounds";
						try {
							if (!std::filesystem::exists(folderPath))
								std::filesystem::create_directories(folderPath);
						} catch (...) {}
						ShellExecuteA(NULL, "open", folderPath.c_str(), NULL, NULL, SW_SHOWDEFAULT);
					}
				}
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
		auto LoadGroupBoxPosition = [](int configVal) -> std::pair<EGroupBoxColumn, int> {
			int col = configVal / 100;
			int order = configVal % 100;
			return { static_cast<EGroupBoxColumn>(std::clamp(col, 0, 2)), order };
		};
		static bool bExploitsInitialized = false;
		if (!bExploitsInitialized)
		{
			auto [col1, ord1] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_Shifting);
			auto [col2, ord2] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_FakeLag);
			auto [col3, ord3] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_Crits);
			auto [col4, ord4] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_NoSpread);
			auto [col5, ord5] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_RegionSelector);
			auto [col6, ord6] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_AntiAim);
			auto [col7, ord7] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_AntiBackstab);
			RegisterGroupBox("Exploits", "Shifting", col1, ord1, 150, EGroupBoxSize::VERY_BIG);
			RegisterGroupBox("Exploits", "FakeLag", col2, ord2, 150, EGroupBoxSize::SMALL);
			RegisterGroupBox("Exploits", "AntiAim", col6, ord6, 145, EGroupBoxSize::BIG);
			RegisterGroupBox("Exploits", "Crithack", col3, ord3, 150, EGroupBoxSize::MEDIUM);
			RegisterGroupBox("Exploits", "No Spread", col4, ord4, 150, EGroupBoxSize::EXTRA_SMALL);
			RegisterGroupBox("Exploits", "Region Selector", col5, ord5, 150, EGroupBoxSize::MEDIUM);
			RegisterGroupBox("Exploits", "Anti Backstab", col7, ord7, 150, EGroupBoxSize::EXTRA_SMALL);
			bExploitsInitialized = true;
		}
		{
			auto [col1, ord1] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_Shifting);
			auto [col2, ord2] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_FakeLag);
			auto [col3, ord3] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_Crits);
			auto [col4, ord4] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_NoSpread);
			auto [col5, ord5] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_RegionSelector);
			auto [col6, ord6] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_AntiAim);
			auto [col7, ord7] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Exploits_AntiBackstab);
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
			if (m_mapGroupBoxes.find("Exploits_Anti Backstab") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["Exploits_Anti Backstab"].m_nColumn = col7;
				m_mapGroupBoxes["Exploits_Anti Backstab"].m_nOrderInColumn = ord7;
			}
		}
		m_mapGroupBoxes["Exploits_Shifting"].m_fnRenderContent = [this]() {
			InputKey("Recharge Key", CFG::Exploits_Shifting_Recharge_Key);
			InputKey("Rapid Fire Key", CFG::Exploits_RapidFire_Key);
			InputKey("Fast Sticky Key", CFG::Exploits_FastSticky_Key);
			InputKey("Warp Key", CFG::Exploits_Warp_Key);
			const int nMaxRecharge = std::max(Shifting::nMaxUsrCmdProcessTicks - 1, 2);
			if (CFG::Exploits_Shifting_Recharge_Limit > nMaxRecharge)
				CFG::Exploits_Shifting_Recharge_Limit = nMaxRecharge;
			SliderInt("Recharge Limit", CFG::Exploits_Shifting_Recharge_Limit, 2, nMaxRecharge, 1);
			SliderInt(CFG::Exploits_FastSticky_Ticks ? "Sticky Ticks" : "Sticky Ticks (Auto)", CFG::Exploits_FastSticky_Ticks, 0, nMaxRecharge, 1);
			const bool bLimitTicks = CFG::Misc_AntiCheat_Enabled && !CFG::Misc_AntiCheat_IgnoreTickLimit;
			const int nMaxSlider = bLimitTicks ? 8 : std::max(Shifting::nMaxUsrCmdProcessTicks - 1, 2);
			if (CFG::Exploits_RapidFire_Ticks > nMaxSlider)
				CFG::Exploits_RapidFire_Ticks = nMaxSlider;
			SliderInt(bLimitTicks ? "DT Ticks (Safe)" : "DT Ticks", CFG::Exploits_RapidFire_Ticks, 2, nMaxSlider, 1);
			SliderInt("DT Delay Ticks", CFG::Exploits_RapidFire_Min_Ticks_Target_Same, 0, 5, 1);
			SelectSingle("Warp Mode", CFG::Exploits_Warp_Mode, {
				{ "Slow", 0 }, { "Full", 1 }
			});
			SelectSingle("Warp Exploit", CFG::Exploits_Warp_Exploit, {
				{ "None", 0 }, { "Fake Peek", 1 }, { "0 Velocity", 2 }
			});
			CheckBox("DT Antiwarp", CFG::Exploits_RapidFire_Antiwarp);
			CheckBox("Draw Indicator", CFG::Exploits_Shifting_Draw_Indicator);
			if (CFG::Exploits_Shifting_Draw_Indicator)
			{
				SliderInt("Bar Width", CFG::Exploits_Shifting_Indicator_Width, 100, 300, 10);
				SliderInt("Bar Height", CFG::Exploits_Shifting_Indicator_Height, 8, 32, 2);
			}
		};
		m_mapGroupBoxes["Exploits_FakeLag"].m_fnRenderContent = [this]() {
			CheckBox("Enabled (Adaptive)", CFG::Exploits_FakeLag_Enabled);
			const bool bLimitTicks = CFG::Misc_AntiCheat_Enabled && !CFG::Misc_AntiCheat_IgnoreTickLimit;
			const int nAAFakeLag = F::FakeAngle->AntiAimOn() ? F::FakeAngle->AntiAimTicks() : 0;
			const int nMaxFakeLagTicks = bLimitTicks ? 8 : std::min(21, std::max(Shifting::nMaxUsrCmdProcessTicks - nAAFakeLag, 1));
			if (CFG::Exploits_FakeLag_Max_Ticks > nMaxFakeLagTicks)
				CFG::Exploits_FakeLag_Max_Ticks = nMaxFakeLagTicks;
			SliderInt(bLimitTicks ? "Safe Max Ticks" : "Max Ticks",
				CFG::Exploits_FakeLag_Max_Ticks, 1, nMaxFakeLagTicks, 1);
			CheckBox("Only When Moving", CFG::Exploits_FakeLag_Only_Moving);
			CheckBox("Activate on Sightline", CFG::Exploits_FakeLag_Activate_On_Sightline);
		};
		m_mapGroupBoxes["Exploits_Anti Backstab"].m_fnRenderContent = [this]() {
			SelectSingle("Mode", CFG::Exploits_AntiBackstab_Mode, {
				{ "Off", 0 },
				{ "Yaw", 1 },
				{ "Pitch", 2 },
				{ "Fake", 3 }
			});
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
				if (CFG::Exploits_AntiAim_YawReal == 4 || CFG::Exploits_AntiAim_YawReal == 5)
					SliderFloat("Real Value", CFG::Exploits_AntiAim_RealYawValue, -180.0f, 180.0f, 5.0f, "%.0f");
				if (CFG::Exploits_AntiAim_YawFake == 4 || CFG::Exploits_AntiAim_YawFake == 5)
					SliderFloat("Fake Value", CFG::Exploits_AntiAim_FakeYawValue, -180.0f, 180.0f, 5.0f, "%.0f");
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
			CheckBox("Crit Effects", CFG::Exploits_Crits_Effects);
			CheckBox("Crit Indicator", CFG::Visuals_Crit_Indicator);
			if (CFG::Visuals_Crit_Indicator)
			{
				CheckBox("Text Mode", CFG::Visuals_Crit_Indicator_TextMode);
				if (CFG::Visuals_Crit_Indicator_TextMode)
				{
					SelectSingle("Text Size", CFG::Visuals_Crit_Indicator_TextSize, {
						{ "Small", 100 },
						{ "Medium", 110 }
					});
				}
				else
				{
					SliderInt("Bar Width", CFG::Visuals_Crit_Indicator_Width, 100, 300, 10);
					SliderInt("Bar Height", CFG::Visuals_Crit_Indicator_Height, 8, 32, 2);
				}
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
		RenderDraggableGroupBoxes("Exploits", nContentX, nContentY, nContentW, nContentH);
	}
	if (MainTab == EMainTabs::MISC)
	{
		int nContentX = m_nCursorX;
		int nContentY = m_nCursorY;
		int nContentW = CFG::Menu_Width - (CFG::Menu_Spacing_X * 4);
		int nContentH = CFG::Menu_Height - m_nCursorY - CFG::Menu_Spacing_Y * 2;
		auto LoadGroupBoxPosition = [](int configVal) -> std::pair<EGroupBoxColumn, int> {
			int col = configVal / 100;
			int order = configVal % 100;
			return { static_cast<EGroupBoxColumn>(std::clamp(col, 0, 2)), order };
		};
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
			auto [col8, ord8] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_Sound);
			RegisterGroupBox("Misc", "Misc", col1, ord1, 160);
			RegisterGroupBox("Misc", "Game", col2, ord2, 160);
			RegisterGroupBox("Misc", "Mann vs. Machine", col3, ord3, 160);
			RegisterGroupBox("Misc", "Chat", col4, ord4, 160);
			RegisterGroupBox("Misc", "Taunt", col5, ord5, 150);
			RegisterGroupBox("Misc", "Auto", col6, ord6, 150);
			RegisterGroupBox("Misc", "Movement", col7, ord7, 160);
			RegisterGroupBox("Misc", "Sound", col8, ord8, 150);
			bMiscInitialized = true;
		}
		{
			auto [col1, ord1] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_Misc);
			auto [col2, ord2] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_Game);
			auto [col3, ord3] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_MvM);
			auto [col4, ord4] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_Chat);
			auto [col5, ord5] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_Taunt);
			auto [col6, ord6] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_Auto);
			auto [col7, ord7] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_Movement);
			auto [col8, ord8] = LoadGroupBoxPosition(CFG::Menu_GroupBox_Misc_Sound);
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
			if (m_mapGroupBoxes.find("Misc_Sound") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["Misc_Sound"].m_nColumn = col8;
				m_mapGroupBoxes["Misc_Sound"].m_nOrderInColumn = ord8;
			}
		}
		m_mapGroupBoxes["Misc_Misc"].m_fnRenderContent = [this]() {
			CheckBox("Bypass sv_pure", CFG::Misc_Pure_Bypass);
			CheckBox("Noise Maker Spam", CFG::Misc_NoiseMaker_Spam);
			CheckBox("No Push", CFG::Misc_No_Push);
			CheckBox("Giant Weapon Sounds", CFG::Misc_MVM_Giant_Weapon_Sounds);
			CheckBox("Equip Region Unlock", CFG::Misc_Equip_Region_Unlock);
			CheckBox("Backpack Expander", CFG::Misc_Backpack_Expander);
			CheckBox("Shield Turn Rate", CFG::Misc_Shield_Turn_Rate);
			CheckBox("Anti Server Angle Change", CFG::Misc_Prevent_Server_Angle_Change);
			CheckBox("Extend Queue", CFG::Misc_Extend_Queue);
			CheckBox("Anti-AFK", CFG::Misc_Anti_AFK);
			CheckBox("PDA Exploit - Sniper", CFG::Misc_PDA_Exploit_Sniper);
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
			CheckBox("Edge Jump", CFG::Misc_EdgeJump);
			CheckBox("Auto Jumpbug", CFG::Misc_AutoJumpbug);
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
			CheckBox("MoveSim Debug", CFG::MoveSim_Debug);
			if (CFG::MoveSim_Debug)
				InputKey("Sim Save Key", CFG::MoveSim_Debug_Key);
		};
		m_mapGroupBoxes["Misc_Sound"].m_fnRenderContent = [this]() {
			CheckBox("Block Footsteps", CFG::Misc_Sound_Block_Footsteps);
			CheckBox("Block Noisemaker", CFG::Misc_Sound_Block_Noisemaker);
			CheckBox("Block Frying Pan", CFG::Misc_Sound_Block_FryingPan);
			CheckBox("Block Water", CFG::Misc_Sound_Block_Water);
		};
		m_mapGroupBoxes["Misc_Game"].m_fnRenderContent = [this]() {
			CheckBox("Network Fix", CFG::Misc_Ping_Reducer);
			CheckBox("Ping Reducer", CFG::Misc_Ping_Reducer_Active);
			if (CFG::Misc_Ping_Reducer_Active)
				SliderFloat("cl_cmdrate", CFG::Misc_Ping_Reducer_Value, 0.5f, 3.0f, 0.01f, "%.2f");
			CheckBox("Prediction Error Jitter Fix", CFG::Misc_Pred_Error_Jitter_Fix);
			CheckBox("ComputeLightingOrigin Fix", CFG::Misc_ComputeLightingOrigin_Fix);
			CheckBox("SetupBones Optimization", CFG::Misc_SetupBones_Optimization);
			CheckBox("Ignore VAC", CFG::Misc_IgnoreVAC_Enabled);
			{
				static bool s_bWasAntiCheatEnabled = CFG::Misc_AntiCheat_Enabled;
				static bool s_bWasIgnoreTickLimit = CFG::Misc_AntiCheat_IgnoreTickLimit;
				static int s_nSavedRapidFireTicks = 0;
				static int s_nSavedFakeLagTicks = 0;
				const bool bOldValue = CFG::Misc_AntiCheat_Enabled;
				CheckBox("Anti-Cheat Compatibility", CFG::Misc_AntiCheat_Enabled);
				if (CFG::Misc_AntiCheat_Enabled != bOldValue)
				{
					if (CFG::Misc_AntiCheat_Enabled && !CFG::Misc_AntiCheat_IgnoreTickLimit)
					{
						s_nSavedRapidFireTicks = CFG::Exploits_RapidFire_Ticks;
						s_nSavedFakeLagTicks = CFG::Exploits_FakeLag_Max_Ticks;
						if (CFG::Exploits_RapidFire_Ticks > 8)
							CFG::Exploits_RapidFire_Ticks = 8;
						if (CFG::Exploits_FakeLag_Max_Ticks > 8)
							CFG::Exploits_FakeLag_Max_Ticks = 8;
					}
					else
					{
						if (s_nSavedRapidFireTicks > 0)
							CFG::Exploits_RapidFire_Ticks = s_nSavedRapidFireTicks;
						if (s_nSavedFakeLagTicks > 0)
							CFG::Exploits_FakeLag_Max_Ticks = s_nSavedFakeLagTicks;
					}
				}
				s_bWasAntiCheatEnabled = CFG::Misc_AntiCheat_Enabled;
				s_bWasIgnoreTickLimit = CFG::Misc_AntiCheat_IgnoreTickLimit;
			}
			if (CFG::Misc_AntiCheat_Enabled)
				CheckBox("Skip Crit Detection", CFG::Misc_AntiCheat_SkipCritDetection);
			if (CFG::Misc_AntiCheat_Enabled)
				CheckBox("Ignore Tick Limit", CFG::Misc_AntiCheat_IgnoreTickLimit);
			if (CFG::Misc_AntiCheat_Enabled)
				CheckBox("Ignore NoSpread Detection", CFG::Misc_AntiCheat_IgnoreNoSpreadDetection);
			if (Button("Fix Invisible Players"))
			{
				I::EngineClient->ClientCmd_Unrestricted("record fix; stop");
			}
			if (Button("Fix Chams/Outlines"))
			{
				F::Materials->CleanUp();
				F::Outlines->CleanUp();
				I::EngineClient->ClientCmd_Unrestricted("record fix; stop");
			}
		};
		m_mapGroupBoxes["Misc_Mann vs. Machine"].m_fnRenderContent = [this]() {
			InputKey("Instant Respawn", CFG::Misc_MVM_Instant_Respawn_Key);
			CheckBox("Instant Revive", CFG::Misc_MVM_Instant_Revive);
		};
		m_mapGroupBoxes["Misc_Chat"].m_fnRenderContent = [this]() {
			CheckBox("Chat Spammer", CFG::Misc_Chat_Spammer_Active);
			CheckBox("Killsay", CFG::Misc_Chat_Killsay_Active);
			if (CFG::Menu_ShowMoreOptions)
			{
				CheckBox("Auto Math Solver", CFG::Misc_Chat_AutoMath_Active);
				if (CFG::Misc_Chat_AutoMath_Active)
				{
					SliderFloat("Answer Delay", CFG::Misc_Chat_AutoMath_Delay, 0.0f, 10.0f, 0.5f, "%.1fs");
				}
				CheckBox("VoteBan When Lifted", CFG::Misc_Chat_VoteBanOnLifted);
				CheckBox("VoteMute When Lifted", CFG::Misc_Chat_VoteMuteOnLifted);
			}
			if (CFG::Misc_Chat_Spammer_Active || CFG::Misc_Chat_Killsay_Active)
			{
				if (CFG::Misc_Chat_Spammer_Active)
				{
					SliderFloat("Interval", CFG::Misc_Chat_Spammer_Interval, 0.1f, 10.0f, 0.1f, "%.1fs");
				}
				if (CFG::Misc_Chat_Killsay_Active)
				{
					CheckBox("Tagged Only", CFG::Misc_Chat_Killsay_Tagged_Only);
				}
				if (Button("Text Files"))
				{
					OpenChatTextFiles();
				}
				if (Button("Refresh"))
				{
					ReloadChatSpammerMessages();
					ReloadKillsayMessages();
				}
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
			CheckBox("Auto F1", CFG::Misc_Auto_F1);
			CheckBox("Auto F2", CFG::Misc_Auto_F2);
			CheckBox("Auto Accept Items", CFG::Misc_Auto_Accept_Items);
			if (CFG::Menu_ShowMoreOptions)
			{
				CheckBox("Class Switch on Death", CFG::Misc_Auto_FastClassSwitch);
			}
			CheckBox("Auto Disguise", CFG::Misc_Auto_Disguise);
			if (CFG::Menu_ShowMoreOptions)
			{
				CheckBox("Rejoin When Kicked", CFG::Misc_Auto_Rejoin_On_Kick);
			}
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
			CheckBox("Voice Command Spam", CFG::Misc_Auto_VoiceCommand_Spam);
			if (CFG::Misc_Auto_VoiceCommand_Spam) {
				SelectSingle("Voice Command", CFG::Misc_Auto_VoiceCommand_Spam_Command, {
					{ "Random", 0 }, { "Medic", 1 }, { "Thanks", 2 }, { "Nice Shot", 3 },
					{ "Cheers", 4 }, { "Jeers", 5 }, { "Go Go Go", 6 }, { "Move Up", 7 },
					{ "Go Left", 8 }, { "Go Right", 9 }, { "Yes", 10 }, { "No", 11 },
					{ "Incoming", 12 }, { "Spy", 13 }, { "Sentry Ahead", 14 },
					{ "Need Teleporter", 15 }, { "Pootis", 16 }, { "Need Sentry", 17 },
					{ "Activate Charge", 18 }, { "Help", 19 }, { "Battle Cry", 20 }
				});
			}
			CheckBox("Projectile Dodge", CFG::Misc_Projectile_Dodge_Enabled);
			if (CFG::Misc_Projectile_Dodge_Enabled) {
				CheckBox("PD Only Warp", CFG::Misc_Projectile_Dodge_Only_Warp);
				CheckBox("PD Use Warp", CFG::Misc_Projectile_Dodge_Use_Warp);
				CheckBox("Disable DT While Airborne", CFG::Misc_Projectile_Dodge_Disable_DT_Airborne);
			}
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
				SelectSingle("Strict Level", CFG::Triggerbot_AutoVaccinator_Tolerance, {
					{ "Max", 0 }, { "High", 1 }, { "Medium", 2 }, { "Low", 3 }
				});
			}
		};
		RenderDraggableGroupBoxes("Misc", nContentX, nContentY, nContentW, nContentH);
	}
	if (MainTab == EMainTabs::NAVBOT)
	{
		int nContentX = m_nCursorX;
		int nContentY = m_nCursorY;
		int nContentW = CFG::Menu_Width - m_nCursorX + CFG::Menu_Pos_X;
		int nContentH = CFG::Menu_Height - m_nCursorY - CFG::Menu_Spacing_Y * 2;
		auto LoadGroupBoxPosition = [](int configVal) -> std::pair<EGroupBoxColumn, int> {
			int col = configVal / 100;
			int order = configVal % 100;
			return { static_cast<EGroupBoxColumn>(std::clamp(col, 0, 2)), order };
		};
		static bool bNavBotInitialized = false;
		if (!bNavBotInitialized)
		{
			auto [col1, ord1] = LoadGroupBoxPosition(CFG::Menu_GroupBox_NavBot_General);
			auto [col2, ord2] = LoadGroupBoxPosition(CFG::Menu_GroupBox_NavBot_Movement);
			auto [col3, ord3] = LoadGroupBoxPosition(CFG::Menu_GroupBox_NavBot_Preferences);
			auto [col4, ord4] = LoadGroupBoxPosition(CFG::Menu_GroupBox_NavBot_Weapon);
			auto [col5, ord5] = LoadGroupBoxPosition(CFG::Menu_GroupBox_NavBot_AutoScope);
			auto [col6, ord6] = LoadGroupBoxPosition(CFG::Menu_GroupBox_NavBot_Debug);
			auto [col7, ord7] = LoadGroupBoxPosition(CFG::Menu_GroupBox_NavBot_Performance);
			RegisterGroupBox("NavBot", "General", col1, ord1, 150, EGroupBoxSize::BIG);
			RegisterGroupBox("NavBot", "Movement", col2, ord2, 150, EGroupBoxSize::SMALL);
			RegisterGroupBox("NavBot", "Behavior", col3, ord3, 150, EGroupBoxSize::BIG);
			RegisterGroupBox("NavBot", "Weapon", col4, ord4, 150, EGroupBoxSize::SMALL);
			RegisterGroupBox("NavBot", "Auto Scope", col5, ord5, 150, EGroupBoxSize::MEDIUM);
			RegisterGroupBox("NavBot", "Debug", col6, ord6, 150, EGroupBoxSize::SMALL);
			RegisterGroupBox("NavBot", "EXTREME Perf", col7, ord7, 150, EGroupBoxSize::VERY_BIG);
			bNavBotInitialized = true;
		}
		{
			auto [col1, ord1] = LoadGroupBoxPosition(CFG::Menu_GroupBox_NavBot_General);
			auto [col2, ord2] = LoadGroupBoxPosition(CFG::Menu_GroupBox_NavBot_Movement);
			auto [col3, ord3] = LoadGroupBoxPosition(CFG::Menu_GroupBox_NavBot_Preferences);
			auto [col4, ord4] = LoadGroupBoxPosition(CFG::Menu_GroupBox_NavBot_Weapon);
			auto [col5, ord5] = LoadGroupBoxPosition(CFG::Menu_GroupBox_NavBot_AutoScope);
			auto [col6, ord6] = LoadGroupBoxPosition(CFG::Menu_GroupBox_NavBot_Debug);
			auto [col7, ord7] = LoadGroupBoxPosition(CFG::Menu_GroupBox_NavBot_Performance);
			if (m_mapGroupBoxes.find("NavBot_General") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["NavBot_General"].m_nColumn = col1;
				m_mapGroupBoxes["NavBot_General"].m_nOrderInColumn = ord1;
			}
			if (m_mapGroupBoxes.find("NavBot_Movement") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["NavBot_Movement"].m_nColumn = col2;
				m_mapGroupBoxes["NavBot_Movement"].m_nOrderInColumn = ord2;
			}
			if (m_mapGroupBoxes.find("NavBot_Behavior") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["NavBot_Behavior"].m_nColumn = col3;
				m_mapGroupBoxes["NavBot_Behavior"].m_nOrderInColumn = ord3;
			}
			if (m_mapGroupBoxes.find("NavBot_Weapon") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["NavBot_Weapon"].m_nColumn = col4;
				m_mapGroupBoxes["NavBot_Weapon"].m_nOrderInColumn = ord4;
			}
			if (m_mapGroupBoxes.find("NavBot_Auto Scope") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["NavBot_Auto Scope"].m_nColumn = col5;
				m_mapGroupBoxes["NavBot_Auto Scope"].m_nOrderInColumn = ord5;
			}
			if (m_mapGroupBoxes.find("NavBot_Debug") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["NavBot_Debug"].m_nColumn = col6;
				m_mapGroupBoxes["NavBot_Debug"].m_nOrderInColumn = ord6;
			}
			if (m_mapGroupBoxes.find("NavBot_EXTREME Perf") != m_mapGroupBoxes.end()) {
				m_mapGroupBoxes["NavBot_EXTREME Perf"].m_nColumn = col7;
				m_mapGroupBoxes["NavBot_EXTREME Perf"].m_nOrderInColumn = ord7;
			}
		}
		m_mapGroupBoxes["NavBot_General"].m_fnRenderContent = [this]() {
			CheckBox("Enabled", CFG::NavBot_Enabled);
			CheckBox("Auto Jump", CFG::NavBot_AutoJump);
			CheckBox("Route Variety", CFG::NavBot_RouteVariety);
			if (CFG::NavBot_RouteVariety) {
				SliderFloat("Variety Strength", CFG::NavBot_RouteVarietyStrength, 0.0f, 1.0f, 0.05f, "%.2f");
			}
			CheckBox("Wander When Idle", CFG::NavBot_WanderWhenIdle);
			SelectSingle("Auto Join Class", CFG::NavBot_AutoJoinClass, {
				{ "Off", 0 },
				{ "Scout", 1 },
				{ "Sniper", 2 },
				{ "Soldier", 3 },
				{ "Demoman", 4 },
				{ "Medic", 5 },
				{ "Heavy", 6 },
				{ "Pyro", 7 },
				{ "Spy", 8 },
				{ "Engineer", 9 }
				});
			SelectSingle("Auto Join Team", CFG::NavBot_AutoJoinTeam, {
				{ "Off", 0 },
				{ "Blue", 1 },
				{ "Red", 2 },
				{ "Spectator", 3 },
				{ "Random", 4 }
				});
		};
		m_mapGroupBoxes["NavBot_Movement"].m_fnRenderContent = [this]() {
			CheckBox("Avoid Obstacles", CFG::NavBot_AvoidObstacles);
			SliderFloat("Path Look-Ahead", CFG::NavBot_PathLookAhead, 0.0f, 250.0f, 10.0f, "%.0f");
			SliderFloat("Corner Speed", CFG::NavBot_CornerSpeed, 0.3f, 1.0f, 0.05f, "%.2f");
			SliderFloat("Stuck Threshold", CFG::NavBot_StuckThreshold, 5.0f, 50.0f, 1.0f, "%.1f");
			SliderFloat("Teleport Threshold", CFG::NavBot_TeleportThreshold, 100.0f, 1000.0f, 50.0f, "%.0f");
			CheckBox("Stuck Wiggle", CFG::NavBot_StuckWiggle);
			CheckBox("Wander Without Mesh", CFG::NavBot_BlindWander);
			CheckBox("Death Pause", CFG::NavBot_DeathPause);
			if (CFG::NavBot_DeathPause)
			{
				SliderFloat("Pause Duration", CFG::NavBot_DeathPauseDuration, 0.5f, 5.0f, 0.5f, "%.1fs");
			}
			CheckBox("Nav Debug Output", CFG::NavBot_Debug);
		};
		m_mapGroupBoxes["NavBot_Behavior"].m_fnRenderContent = [this]() {
			multiselect("Behavior", NavBotBehavior, {
				{ "Capture Objectives", CFG::NavBot_CaptureObjectives },
				{ "Defend Objectives", CFG::NavBot_DefendObjectives },
				{ "Search Health", CFG::NavBot_SearchHealth },
				{ "Search Ammo", CFG::NavBot_SearchAmmo },
				{ "Reload Weapons", CFG::NavBot_ReloadWeapons },
				{ "Stalk Enemies", CFG::NavBot_StalkEnemies },
				{ "Sniper Spots", CFG::NavBot_SniperSpots },
				{ "Escape Danger", CFG::NavBot_EscapeDanger },
				{ "Wait For Setup", CFG::NavBot_WaitForSetup },
				{ "Follow Teammates", CFG::NavBot_FollowTeammates },
				{ "Follow Tagged", CFG::NavBot_FollowTaggedPlayers },
				{ "Group With Others", CFG::NavBot_GroupWithOthers }
			});
			multiselect("Danger Blacklist", DangerBlacklist, {
				{ "Normal Threats", CFG::NavBot_DangerBL_NormalThreats },
				{ "Dormant Threats", CFG::NavBot_DangerBL_DormantThreats },
				{ "Players", CFG::NavBot_DangerBL_Players },
				{ "Stickies", CFG::NavBot_DangerBL_Stickies },
				{ "Projectiles", CFG::NavBot_DangerBL_Projectiles },
				{ "Sentries", CFG::NavBot_DangerBL_Sentries }
				});
			if (CFG::NavBot_FollowTaggedPlayers)
			{
				SliderFloat("Follow Distance", CFG::NavBot_FollowDistance, 50.0f, 500.0f, 10.0f, "%.0f");
				SliderFloat("Supply Distance", CFG::NavBot_FollowSupplyDistance, 0.0f, 1000.0f, 50.0f, "%.0f");
				if (Button("Clear Follow Tags"))
				{
					F::Players->ClearAllFollowPlayer();
				}
			}
			};
			m_mapGroupBoxes["NavBot_Weapon"].m_fnRenderContent = [this]() {
			SelectSingle("Force Weapon", CFG::NavBot_WeaponPreference, {
				{ "Off", 0 },
				{ "Best", 1 },
				{ "Primary", 2 },
				{ "Secondary", 3 },
				{ "Melee", 4 }
				});
			if (CFG::NavBot_WeaponPreference == 1) {
				CheckBox("Avoid Melee", CFG::NavBot_AvoidMelee);
			}
		};
		m_mapGroupBoxes["NavBot_Auto Scope"].m_fnRenderContent = [this]() {
			SelectSingle("Mode", CFG::NavBot_AutoScope, {
				{ "Off", 0 },
				{ "Simple", 1 },
				{ "MoveSim", 2 }
				});
			if (CFG::NavBot_AutoScope) {
				SliderFloat("Cancel Time", CFG::NavBot_AutoScopeCancelTime, 1.0f, 5.0f, 0.5f, "%.1fs");
				SliderFloat("Wait After Shot", CFG::NavBot_AutoScopeWaitAfterShot, 0.0f, 5.0f, 0.1f, "%.1fs");
			}
		};
		m_mapGroupBoxes["NavBot_Debug"].m_fnRenderContent = [this]() {
			CheckBox("Look At Path", CFG::NavBot_LookAtPath);
			if (CFG::NavBot_LookAtPath) {
				SliderFloat("Look Speed", CFG::NavBot_LookSpeed, 1.0f, 20.0f, 1.0f, "%.1f");
			}
			CheckBox("Draw Waypoints", CFG::NavBot_DrawWaypoints);
		};
		m_mapGroupBoxes["NavBot_EXTREME Perf"].m_fnRenderContent = [this]() {
			CheckBox("No Visuals (XTRM)", CFG::Perf_Extreme_Skip_All_Visuals);
			CheckBox("Minimal Render (XTRM)", CFG::Perf_Extreme_Minimal_Render);
			CheckBox("No World Render (XTRM)", CFG::Perf_Extreme_Skip_World_Render);
			CheckBox("No Textures (XTRM)", CFG::Perf_Extreme_Skip_World_Textures);
			CheckBox("No Shadows (XTRM)", CFG::Perf_Extreme_Skip_Shadows);
			CheckBox("No Particles (XTRM)", CFG::Perf_Extreme_Skip_Particles);
			CheckBox("No Decals (XTRM)", CFG::Perf_Extreme_Skip_Decals);
			CheckBox("No Entities (XTRM)", CFG::Perf_Extreme_Skip_Unused_Entities);
			CheckBox("No Sound (XTRM)", CFG::Perf_Extreme_Skip_Sound);
			CheckBox("Low Textures (XTRM)", CFG::Perf_Extreme_Low_Textures);
			CheckBox("No ESP (XTRM)", CFG::Perf_Extreme_Skip_ESP);
			CheckBox("No Outlines (XTRM)", CFG::Perf_Extreme_Skip_Outlines);
			CheckBox("No Teammate Lag (XTRM)", CFG::Perf_Extreme_Skip_LagRecords_Teammates);
			CheckBox("No Anim Updates (XTRM)", CFG::Perf_Extreme_Skip_Anim_Updates);
			CheckBox("No Move Sim (XTRM)", CFG::Perf_Extreme_Skip_MovementSimulation);
			CheckBox("No Vel Fix (XTRM)", CFG::Perf_Extreme_Skip_VelFix);
			CheckBox("Limit Ent Cache (XTRM)", CFG::Perf_Extreme_Limit_Entity_Cache);
			SliderInt("FPS Limit (0=off)", CFG::Perf_Extreme_FPS_Limit, 0, 300, 5);
		};
		RenderDraggableGroupBoxes("NavBot", nContentX, nContentY, nContentW, nContentH);
	}
	if (MainTab == EMainTabs::PLAYERS)
	{
		m_nCursorX += CFG::Menu_Spacing_X;
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
		{
			const bool bShowAvatars = I::SteamFriends && I::SteamUtils;
			const int nAvatarSize = 24;
			const int nRowHeight = nAvatarSize + CFG::Menu_Spacing_Y;
			const int nPlayersPerPage = 20;
			static std::string strSearchFilter = {};
			static int nSearchCaret = 0;
			static int nSearchAnchor = 0;
			{
				auto bx = m_nCursorX;
				auto by = m_nCursorY;
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text_Active, POS_DEFAULT, "Search:");
				m_nCursorX += 50;
				const int nInputWidth = 150;
				const int nInputHeight = H::Fonts->Get(EFonts::Menu).m_nTall + 4;
				bool bHovered = IsHoveredSimple(m_nCursorX, m_nCursorY - 2, nInputWidth, nInputHeight);
				if (bHovered && H::Input->IsPressed(VK_LBUTTON))
				{
					bPlayerSearchFocused = true;
					const int nLocalX = (std::max)(0, H::Input->GetMouseX() - (m_nCursorX + 4));
					size_t nPos = 0;
					int nBestDist = 1 << 30;
					int nCaretByte = static_cast<int>(strSearchFilter.size());
					while (true)
					{
						int w_out = 0, h_out = 0;
						I::MatSystemSurface->GetTextSize(H::Fonts->Get(EFonts::Menu).m_dwFont, Utils::ConvertUtf8ToWide(strSearchFilter.substr(0, nPos)).c_str(), w_out, h_out);
						const int nDiff = nLocalX - w_out;
						const int nDist = nDiff < 0 ? -nDiff : nDiff;
						if (nDist < nBestDist)
						{
							nBestDist = nDist;
							nCaretByte = static_cast<int>(nPos);
						}
						if (nPos >= strSearchFilter.size())
							break;
						nPos = Utf8NextBoundary(strSearchFilter, nPos);
					}
					nSearchCaret = nSearchAnchor = nCaretByte;
				}
				else if (H::Input->IsPressed(VK_LBUTTON) && !bHovered)
				{
					bPlayerSearchFocused = false;
				}
				if (H::Input->IsPressed(VK_ESCAPE))
				{
					bPlayerSearchFocused = false;
				}
				m_bWantTextInput = bPlayerSearchFocused;
				H::Draw->Rect(m_nCursorX, m_nCursorY - 2, nInputWidth, nInputHeight, CFG::Menu_Background);
				H::Draw->OutlinedRect(m_nCursorX, m_nCursorY - 2, nInputWidth, nInputHeight, bPlayerSearchFocused ? CFG::Menu_Accent_Primary : CFG::Menu_Text_Inactive);
				H::Draw->StartClipping(m_nCursorX + 2, m_nCursorY - 2, nInputWidth - 4, nInputHeight);
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 4, m_nCursorY, CFG::Menu_Text_Active, POS_DEFAULT, "%s", strSearchFilter.c_str());
				H::Draw->EndClipping();
				if (bPlayerSearchFocused)
				{
					if (nSearchCaret > static_cast<int>(strSearchFilter.size()))
						nSearchCaret = static_cast<int>(strSearchFilter.size());
					if (nSearchAnchor > static_cast<int>(strSearchFilter.size()))
						nSearchAnchor = static_cast<int>(strSearchFilter.size());
					TextInputEdit(strSearchFilter, nSearchCaret, nSearchAnchor, 64);
					int nTextWidth = 0, nTextHeight = 0;
					I::MatSystemSurface->GetTextSize(H::Fonts->Get(EFonts::Menu).m_dwFont, Utils::ConvertUtf8ToWide(strSearchFilter.substr(0, static_cast<size_t>(nSearchCaret))).c_str(), nTextWidth, nTextHeight);
					int nCursorDrawX = m_nCursorX + 4 + nTextWidth;
					if (nCursorDrawX > m_nCursorX + nInputWidth - 4)
						nCursorDrawX = m_nCursorX + nInputWidth - 4;
					if (fmod(I::GlobalVars->realtime, 1.0f) < 0.5f)
					{
						H::Draw->Line(nCursorDrawX, m_nCursorY, nCursorDrawX, m_nCursorY + H::Fonts->Get(EFonts::Menu).m_nTall - 2, CFG::Menu_Text_Active);
					}
				}
				m_nCursorX = bx;
				m_nCursorY = by + nInputHeight + CFG::Menu_Spacing_Y;
			}
			static std::vector<int> vecPlayerIndices;
			vecPlayerIndices.clear();
			bool bLocalRowFound = false;
			std::string strSearchLower = strSearchFilter;
			std::transform(strSearchLower.begin(), strSearchLower.end(), strSearchLower.begin(), ::tolower);
			for (auto n{ 1 }; n < I::EngineClient->GetMaxClients() + 1; n++)
			{
				player_info_t player_info{};
				if (!I::EngineClient->GetPlayerInfo(n, &player_info) || player_info.fakeplayer)
					continue;
				if (n == I::EngineClient->GetLocalPlayer())
					bLocalRowFound = true;
				if (!strSearchFilter.empty())
				{
					std::string strName = player_info.name;
					std::transform(strName.begin(), strName.end(), strName.begin(), ::tolower);
					if (strName.find(strSearchLower) == std::string::npos)
						continue;
				}
				vecPlayerIndices.push_back(n);
			}
			if (!bLocalRowFound)
				vecPlayerIndices.insert(vecPlayerIndices.begin(), -1);
			const int nTotalPlayers = static_cast<int>(vecPlayerIndices.size());
			const int nTotalPages = (nTotalPlayers + nPlayersPerPage - 1) / nPlayersPerPage;
			static int nCurrentPage = 0;
			if (nCurrentPage >= nTotalPages)
				nCurrentPage = 0;
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
			const int nStartIdx = nCurrentPage * nPlayersPerPage;
			const int nEndIdx = std::min(nStartIdx + nPlayersPerPage, nTotalPlayers);
			auto pResource = GetTFPlayerResource();
			const int nLocalTeam = pResource ? pResource->GetTeam(I::EngineClient->GetLocalPlayer()) : 0;
			for (int i = nStartIdx; i < nEndIdx; i++)
			{
				int n = vecPlayerIndices[i];
				player_info_t player_info{};
				const bool bHaveInfo = n > 0 && I::EngineClient->GetPlayerInfo(n, &player_info);
				if (!bHaveInfo && n != -1)
					continue;
				const bool bIsLocal = (n == -1) || (n == I::EngineClient->GetLocalPlayer());
				const uint32_t nFriendsId = bHaveInfo ? player_info.friendsID : NetLocalAccountId();
				PlayerPriority custom_info{};
				if (bHaveInfo)
					F::Players->GetInfo(n, custom_info);
				bool bIsF2P = H::Entities->IsF2P(n);
				int nPartyIndex = H::Entities->GetPartyIndex(n);
				auto bx{ m_nCursorX };
				auto by{ m_nCursorY };
				if (bShowAvatars && nFriendsId != 0)
				{
					H::Draw->Avatar(m_nCursorX, m_nCursorY, nAvatarSize, nAvatarSize, nFriendsId);
				}
				m_nCursorX += nAvatarSize + CFG::Menu_Spacing_X;
				m_nCursorY = by + (nAvatarSize - H::Fonts->Get(EFonts::Menu).m_nTall - CFG::Menu_Spacing_Y) / 2;
				Color_t nameColor = CFG::Menu_Text_Inactive;
				if (bIsLocal)
					nameColor = CFG::Color_Friend;
				else if (custom_info.Ignored)
					nameColor = CFG::Color_Friend;
				else if (custom_info.Cheater)
					nameColor = CFG::Color_Cheater;
				else if (custom_info.Targeted)
					nameColor = CFG::Color_Targeted;
				else if (custom_info.Nigger)
					nameColor = CFG::Color_Nigger;
				else if (custom_info.RetardLegit)
					nameColor = CFG::Color_RetardLegit;
				else if (custom_info.Streamer)
					nameColor = CFG::Color_Streamer;
				else if (custom_info.FollowPlayer)
					nameColor = CFG::Color_FollowPlayer;
				else if (pResource)
				{
					int nPlayerTeam = pResource->GetTeam(n);
					if (nPlayerTeam == nLocalTeam)
						nameColor = CFG::Color_Teammate;
					else
						nameColor = CFG::Color_Enemy;
				}
				const uint64_t steamID64 = static_cast<uint64_t>(nFriendsId) + 0x0110000100000000ULL;
				const bool bRowAlert = HasSourcebansAlert(steamID64);
				const char *pszRowName = bHaveInfo ? player_info.name : (I::SteamFriends ? I::SteamFriends->GetPersonaName() : "you");
				const int nNameWidth = bRowAlert ? 104 : 150;
				if (playerListButton(Utils::ConvertUtf8ToWide(pszRowName && pszRowName[0] ? pszRowName : "you").c_str(), nNameWidth, nameColor, false))
				{
					nSelectedPlayerIndex = n;
					MainTab = EMainTabs::PLAYER_DETAILS;
					DismissSourcebansAlert(steamID64);
					DismissVerobayAlert(steamID64);
				}
				m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
				m_nCursorY = by + (nAvatarSize - H::Fonts->Get(EFonts::Menu).m_nTall - CFG::Menu_Spacing_Y) / 2;
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
				if (!bIsLocal)
				{
					if (playerListButton(L"ignored", 60, custom_info.Ignored ? CFG::Color_Friend : CFG::Menu_Text_Inactive, true))
					{
						F::Players->Mark(n, { !custom_info.Ignored, false, false, false, false, false, custom_info.FollowPlayer });
					}
					m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
					m_nCursorY = by + (nAvatarSize - H::Fonts->Get(EFonts::Menu).m_nTall - CFG::Menu_Spacing_Y) / 2;
					if (playerListButton(L"cheater", 60, custom_info.Cheater ? CFG::Color_Cheater : CFG::Menu_Text_Inactive, true))
					{
						F::Players->Mark(n, { false, !custom_info.Cheater, false, false, false, false, custom_info.FollowPlayer });
					}
					m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
					m_nCursorY = by + (nAvatarSize - H::Fonts->Get(EFonts::Menu).m_nTall - CFG::Menu_Spacing_Y) / 2;
					if (playerListButton(L"retard legit", 60, custom_info.RetardLegit ? CFG::Color_RetardLegit : CFG::Menu_Text_Inactive, true))
					{
						F::Players->Mark(n, { false, false, !custom_info.RetardLegit, false, false, false, custom_info.FollowPlayer });
					}
				}
				else
				{
					playerListButton(L"you", 60, CFG::Color_Friend, true);
					m_nCursorX += (m_nLastButtonW + CFG::Menu_Spacing_X) * 3;
					m_nCursorY = by + (nAvatarSize - H::Fonts->Get(EFonts::Menu).m_nTall - CFG::Menu_Spacing_Y) / 2;
				}
				if (bRowAlert)
				{
					if (!bIsLocal)
						m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
					m_nCursorY = by + (nAvatarSize - H::Fonts->Get(EFonts::Menu).m_nTall - CFG::Menu_Spacing_Y) / 2;
					int alertW = 45;
					int alertH = H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y - 1;
					bool bAlertHovered = IsHovered(m_nCursorX, m_nCursorY, alertW, alertH, nullptr);
					Color_t alertBg = bAlertHovered ? Color_t{ 220, 60, 60, 255 } : Color_t{ 180, 40, 40, 255 };
					H::Draw->Rect(m_nCursorX, m_nCursorY, alertW, alertH, alertBg);
					H::Draw->OutlinedRect(m_nCursorX, m_nCursorY, alertW, alertH, { 255, 80, 80, 255 });
					H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + alertW / 2, m_nCursorY + alertH / 2 - 1, { 255, 255, 255, 255 }, POS_CENTERXY, "ALERT!");
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
		const int nConfigTabStartY = m_nCursorY;
		int nCount = 0;
		for (const auto &entry : std::filesystem::directory_iterator(configFolder))
		{
			if (std::string(std::filesystem::path(entry).filename().string()).find(".json") == std::string_view::npos)
				continue;
			nCount++;
		}
		if (nCount < 11)
		{
			static std::string strInput = {};
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
					strInput = {};
				}
			}
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
		static std::string strLegacySelected = {};
		const auto& legacyFolder = U::Storage->GetLegacyConfigFolder();
		{
			int leftColumnX = m_nCursorX;
			int leftColumnY = m_nCursorY;
			m_nCursorX += 330;
			m_nCursorY = nConfigTabStartY;
			if (U::Storage->HasLegacyConfigs())
			{
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
							std::string fileName = strLegacySelected + ".json";
							std::filesystem::copy_file(legacyFolder / fileName, configFolder / fileName, std::filesystem::copy_options::skip_existing);
							strLegacySelected = {};
						}
						if (Button("Cancel"))
							strLegacySelected = {};
					}
					GroupBoxEnd();
				}
			}
			GroupBoxStart("Achievements", 150);
			{
				m_nCursorY += CFG::Menu_Spacing_Y;
				if (Button("Unlock All", false, ((m_nLastGroupBoxW + 1) - (CFG::Menu_Spacing_X * 6))))
					m_eConfirmAction = EConfirmAction::UNLOCK_ACHIEVEMENTS;
				if (Button("Lock All", false, ((m_nLastGroupBoxW + 1) - (CFG::Menu_Spacing_X * 6))))
					m_eConfirmAction = EConfirmAction::LOCK_ACHIEVEMENTS;
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + CFG::Menu_Spacing_X, m_nCursorY,
					m_strAchievementStatus.empty() ? CFG::Menu_Text_Inactive : CFG::Menu_Text_Active,
					POS_DEFAULT, "%s", m_strAchievementStatus.empty() ? "Status: -" : m_strAchievementStatus.c_str());
				m_nCursorY += 20;
			}
			GroupBoxEnd();
			m_nCursorX = leftColumnX;
			m_nCursorY = leftColumnY;
		}
		if (Button("View Config Folder", false, 130))
		{
			ShellExecuteW(NULL, L"open", U::Storage->GetConfigFolder().wstring().c_str(), NULL, NULL, SW_SHOWNORMAL);
		}
		m_nCursorY += CFG::Menu_Spacing_Y;
		GroupBoxStart("Autosave", 150);
		{
			m_nCursorY += CFG::Menu_Spacing_Y;
			static bool bLoadOnInject = U::Storage->GetLoadAutosaveOnInject();
			if (CheckBox("Load Latest on Inject", bLoadOnInject))
			{
				U::Storage->SetLoadAutosaveOnInject(bLoadOnInject);
			}
			m_nCursorY += CFG::Menu_Spacing_Y;
			const auto& autosaveFolder = U::Storage->GetAutosaveFolder();
			for (int i = 1; i <= 5; i++)
			{
				std::string fileName = "autosave_" + std::to_string(i) + ".json";
				auto path = autosaveFolder / fileName;
				std::string label;
				if (i == 1)
					label = "AUTOSAVE LATEST";
				else
					label = "AUTOSAVE " + std::to_string(i);
				bool bExists = std::filesystem::exists(path);
				if (bExists)
				{
					if (Button(label.c_str(), false, ((m_nLastGroupBoxW + 1) - (CFG::Menu_Spacing_X * 6))))
					{
						U::Storage->LoadAutosave(i);
					}
				}
				else
				{
					Color_t oldColor = CFG::Menu_Text_Inactive;
					H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + CFG::Menu_Spacing_X, m_nCursorY, oldColor, POS_DEFAULT, "%s (empty)", label.c_str());
					m_nCursorY += 20;
				}
			}
		}
		GroupBoxEnd();
		int savedX = m_nCursorX;
		int savedY = m_nCursorY;
		m_nCursorX = CFG::Menu_Pos_X + CFG::Menu_Spacing_X + 80;
		m_nCursorY = CFG::Menu_Pos_Y + CFG::Menu_Height - 30 - CFG::Menu_Spacing_Y;
		CheckBox("Show More Options", CFG::Menu_ShowMoreOptions);
		m_nCursorX = CFG::Menu_Pos_X + CFG::Menu_Width - 130 - CFG::Menu_Spacing_X;
		m_nCursorY = CFG::Menu_Pos_Y + CFG::Menu_Height - 30 - CFG::Menu_Spacing_Y;
		if (Button("Unbind All Keys", false, 120))
		{
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
	if (SoundpadEnabled && MainTab == EMainTabs::SOUND)
	{
		int anchor_y = m_nCursorY;
		GroupBoxStart("Playback", 150);
		{
			CheckBox("Active", CFG::Soundpad_Active);
			SliderFloat("Volume", CFG::Soundpad_Volume, 0.0f, 300.0f, 5.0f, "%.0f%%");
			SliderFloat("Speed", CFG::Soundpad_Speed, 0.25f, 3.0f, 0.05f, "%.2fx");
			SelectSingle("Overlap", CFG::Soundpad_Overlap, {
				{ "Single", 0 },
				{ "Double", 1 },
				{ "Triple", 2 },
				{ "All", 3 }
				});
			CheckBox("Randomize", CFG::Soundpad_Randomize);
			CheckBox("Mute Mic While Playing", CFG::Soundpad_MuteMic);
			CheckBox("Hear Yourself", CFG::Soundpad_Monitor);
			CheckBox("Playing Indicator", CFG::Soundpad_Indicator);
			CheckBox("Voice Key Stops Song", CFG::Soundpad_KeyStops);
		}
		GroupBoxEnd();
		m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
		m_nCursorY = anchor_y;
		GroupBoxStart("Sounds", 150);
		{
			const auto vecSounds = F::Soundpad->CopySounds();
			const int nTotal = static_cast<int>(vecSounds.size());
			constexpr int nPerPage = 11;
			const int nTotalPages = (nTotal + nPerPage - 1) / nPerPage;
			static int nSoundPage = 0;
			if (nSoundPage >= nTotalPages)
				nSoundPage = 0;
			{
				const int bx = m_nCursorX;
				const int by = m_nCursorY;
				if (Button("Refresh", false, 58))
					F::Soundpad->RefreshFiles();
				const int nAdv = m_nCursorY - by;
				m_nCursorX = bx + m_nLastButtonW + CFG::Menu_Spacing_X * 2;
				m_nCursorY = by;
				if (Button("Stop All", false, 58))
					F::Soundpad->StopAll();
				m_nCursorX = bx;
				m_nCursorY = by + nAdv;
			}
			if (nTotalPages > 1)
			{
				const int bx = m_nCursorX;
				const int by = m_nCursorY;
				char szPage[8];
				for (int nPage = 0; nPage < nTotalPages; nPage++)
				{
					snprintf(szPage, sizeof(szPage), "%d", nPage + 1);
					if (Button(szPage, nPage == nSoundPage, 22))
						nSoundPage = nPage;
					m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
					m_nCursorY = by;
				}
				m_nCursorX = bx;
				m_nCursorY = by + H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
			}
			const int nStart = nSoundPage * nPerPage;
			const int nEnd = (std::min)(nStart + nPerPage, nTotal);
			for (int i = nStart; i < nEnd; i++)
			{
				const auto& sound = vecSounds[i];
				std::string strLabel = sound.m_strName;
				if (strLabel.size() > 18)
					strLabel = strLabel.substr(0, 17) + "..";
				const int rowX = m_nCursorX;
				const int rowY = m_nCursorY;
				if (Button(strLabel.c_str(), false, 108))
					F::Soundpad->PlayFile(sound.m_strName);
				const int nRowAdvance = m_nCursorY - rowY;
				m_nCursorX = rowX + 112;
				m_nCursorY = rowY + 1;
				bool bSelected = sound.m_bSelected;
				if (CheckBox("", bSelected))
					F::Soundpad->ToggleSelected(sound.m_strName);
				m_nCursorX = rowX;
				m_nCursorY = rowY + nRowAdvance;
			}
			if (nTotal == 0)
			{
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "no sounds yet");
				m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "drop mp3/ogg/wav files in");
				m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "C:\\necromancer_tf2\\soundpad");
				m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
			}
		}
		GroupBoxEnd();
		m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
		m_nCursorY = anchor_y;
		GroupBoxStart("Multi Select", 150);
		{
			const int nSelected = F::Soundpad->GetSelectedCount();
			if (Button("Play Selected", false, 100))
				F::Soundpad->PlaySelected();
			if (Button("Clear Selection", false, 100))
			{
				for (const auto& sound : F::Soundpad->CopySounds())
				{
					if (sound.m_bSelected)
						F::Soundpad->ToggleSelected(sound.m_strName);
				}
			}
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "Selected: %d", nSelected);
			m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
		}
		GroupBoxEnd();
		GroupBoxStart("Folder", 150);
		{
			if (Button("Open Folder", false, 100))
				ShellExecuteA(NULL, "open", "C:\\necromancer_tf2\\soundpad", NULL, NULL, SW_SHOWDEFAULT);
		}
		GroupBoxEnd();
	}
	if (MainTab == EMainTabs::PACMAN)
	{
		const bool bRunning = F::PacMan->IsRunning();
		if (Button(bRunning ? "Stop Pac Man" : "Run Pac Man", bRunning, 150))
		{
			if (bRunning)
				F::PacMan->StopGame();
			else
				F::PacMan->StartGame();
		}
		m_nCursorY += CFG::Menu_Spacing_Y * 2;
		if (bRunning)
		{
			const int nAvailW = (CFG::Menu_Pos_X + CFG::Menu_Width - 1) - m_nCursorX;
			const int nAvailH = (CFG::Menu_Pos_Y + CFG::Menu_Height - 1) - m_nCursorY;
			F::PacMan->Render(m_nCursorX, m_nCursorY, nAvailW, nAvailH);
		}
		else
		{
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 2, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "Insert coin - press Run Pac Man to start playing");
			m_nCursorY += 20;
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 2, m_nCursorY, CFG::Menu_Text_Active, POS_DEFAULT, "High Score: %d", CFG::PacMan_HighScore);
			m_nCursorY += 20;
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 2, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "Controls: Arrow keys / WASD");
		}
	}
	if (MainTab == EMainTabs::NETWORK)
	{
		enum class ENetPages { CONNECT, HOME, FRIENDS, LISTING, CHAT };
		static ENetPages NetPage = ENetPages::CONNECT;
		static int nNetStatus = 0;
		static bool bNetListed = false;
		static bool bPresenceSynced = false;
		static int nChatScroll = 0;
		static std::string strChatScrollTarget = {};
		static float flChatScrollRepeat = 0.0f;
		static bool bChatThumbDrag = false;
		static int nThumbGrabOffset = 0;
		static int nChatCaret = 0;
		static int nChatAnchor = 0;
		static bool bChatSelecting = false;
		static bool bChatSelValid = false;
		static int nChatSelAnchorMsg = 0;
		static size_t nChatSelAnchorByte = 0;
		static int nChatSelCaretMsg = 0;
		static size_t nChatSelCaretByte = 0;
		static bool bChatLinkPress = false;
		static int nChatLinkPressX = 0;
		static int nChatLinkPressY = 0;
		static bool bChatLmbWasDown = false;
		const bool bLoggedIn = F::NecroNet->IsLoggedIn();
		const bool bSteamOk = F::NecroNet->IsSteamAvailable();
		if (!bLoggedIn)
		{
			NetPage = ENetPages::CONNECT;
			bPresenceSynced = false;
			bNetChatFocused = false;
			bChatThumbDrag = false;
			strNetChatInput.clear();
		}
		const auto &fontMenu = H::Fonts->Get(EFonts::Menu);
		const int nLineH = fontMenu.m_nTall + CFG::Menu_Spacing_Y;
		const int nRowLimit = CFG::Menu_Pos_Y + CFG::Menu_Height - 30;
		auto MeasureTextW = [&](const std::string &strText) -> int {
			int w_out = 0, h_out = 0;
			I::MatSystemSurface->GetTextSize(fontMenu.m_dwFont, Utils::ConvertUtf8ToWide(strText).c_str(), w_out, h_out);
			return w_out;
		};
		const auto &fontChat = H::Fonts->Get(EFonts::NecroNet);
		const int nChatLineH = fontChat.m_nTall + 4;
		auto MeasureChatW = [&](const std::string &strText) -> int {
			int w_out = 0, h_out = 0;
			I::MatSystemSurface->GetTextSize(fontChat.m_dwFont, Utils::ConvertUtf8ToWide(strText).c_str(), w_out, h_out);
			return w_out;
		};
		int anchor_x = m_nCursorX;
		int anchor_y = m_nCursorY;
		if (bLoggedIn)
		{
			if (Button("Home", NetPage == ENetPages::HOME))
				NetPage = ENetPages::HOME;
			m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
			m_nCursorY = anchor_y;
			if (Button("Friends", NetPage == ENetPages::FRIENDS))
				NetPage = ENetPages::FRIENDS;
			m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
			m_nCursorY = anchor_y;
			if (Button("Listing", NetPage == ENetPages::LISTING))
				NetPage = ENetPages::LISTING;
			m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
			m_nCursorY = anchor_y;
			if (Button("Chat", NetPage == ENetPages::CHAT))
				NetPage = ENetPages::CHAT;
			H::Draw->Line(
				anchor_x - CFG::Menu_Spacing_X,
				m_nCursorY,
				CFG::Menu_Pos_X + CFG::Menu_Width - 1,
				m_nCursorY,
				CFG::Menu_Accent_Primary
			);
			m_nCursorX = anchor_x + CFG::Menu_Spacing_X;
			m_nCursorY += CFG::Menu_Spacing_Y;
		}
		if (!bLoggedIn)
		{
			const ENetFlow eFlow = F::NecroNet->GetFlowState();
			const std::string strStatus = F::NecroNet->GetConnectStatus();
			const std::string strConfirmed = F::NecroNet->GetConfirmedSteamId();
			GroupBoxStart("Connect", 200);
			{
				NetDrawOutlined(m_nCursorX, m_nCursorY, { 255, 255, 255, 255 }, NetEscapeLabel(strStatus.empty() ? "Offline" : strStatus).c_str());
				m_nCursorY += nLineH;
				if (eFlow == ENetFlow::AUTH_WAIT || eFlow == ENetFlow::LOGGING_IN)
				{
					const int nDot = static_cast<int>(fmod(I::GlobalVars->realtime * 3.0f, 3.0f));
					for (int n = 0; n < 3; n++)
						H::Draw->Rect(m_nCursorX + (n * 8), m_nCursorY, 5, 5, n == nDot ? CFG::Menu_Accent_Primary : CFG::Menu_Text_Disabled);
					m_nCursorY += 8 + CFG::Menu_Spacing_Y;
					if (Button("Cancel", false, 70))
						F::NecroNet->CancelConnect();
				}
				if (!strConfirmed.empty() || (bSteamOk && eFlow == ENetFlow::AUTH_WAIT))
				{
					if (!strConfirmed.empty())
					{
						H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "Steam ID: %s", NetSanitize(strConfirmed).c_str());
						m_nCursorY += nLineH;
					}
					H::Draw->StartClipping(m_nCursorX, m_nCursorY, m_nLastGroupBoxW, nLineH + 2);
					NetDrawOutlined(m_nCursorX, m_nCursorY, CFG::Color_RetardLegit, "YOUR PASSWORD WILL BE SHARED");
					H::Draw->EndClipping();
					m_nCursorY += nLineH;
					const int auth_x = m_nCursorX;
					const int auth_y = m_nCursorY;
					if (Button("Login", !F::NecroNet->m_bRegisterMode))
						F::NecroNet->m_bRegisterMode = false;
					m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
					m_nCursorY = auth_y;
					if (Button("Register", F::NecroNet->m_bRegisterMode))
						F::NecroNet->m_bRegisterMode = true;
					m_nCursorX = auth_x;
					InputText("Username", "Username:", F::NecroNet->m_strUsername, NET_USERNAME_MAX);
					InputText("Password", "Password:", F::NecroNet->m_strPassword, NET_PASSWORD_MAX);
					if (Button(F::NecroNet->m_bRegisterMode ? "Create Account" : "Sign In", false, 110))
						m_eConfirmAction = F::NecroNet->m_bRegisterMode ? EConfirmAction::AUTH_REGISTER : EConfirmAction::AUTH_LOGIN;
				}
				else if (eFlow == ENetFlow::IDLE)
				{
					if (Button("Connect", false, 80))
						F::NecroNet->Connect();
					H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, bSteamOk ? "Opens the Steam overlay to sign in" : "Steam unavailable - opens your browser to sign in");
					m_nCursorY += nLineH;
				}
			}
			GroupBoxEnd();
		}
		if (bLoggedIn && NetPage == ENetPages::HOME)
		{
			NetUser ownUser{};
			const bool bHasOwn = F::NecroNet->CopyOwnUser(ownUser);
			if (!bPresenceSynced && bHasOwn)
			{
				nNetStatus = std::clamp(ownUser.m_nStatus, 0, NET_STATUS_COUNT - 1);
				bNetListed = ownUser.m_bListed;
				bPresenceSynced = true;
			}
			anchor_y = m_nCursorY;
			GroupBoxStart("Profile", 200);
			{
				const int nAvatarX = m_nCursorX;
				const int nAvatarY = m_nCursorY;
				const int nAvatarSize = 48;
				const std::string strProfileName = F::NecroNet->GetOwnUsername();
				NetDrawAvatar(strProfileName, F::NecroNet->GetAvatarB64(strProfileName), nAvatarX, nAvatarY, nAvatarSize);
				const int nInfoX = nAvatarX + nAvatarSize + 10;
				m_nCursorX = nInfoX;
				m_nCursorY = nAvatarY;
				if (bHasOwn)
				{
					H::Draw->String(H::Fonts->Get(EFonts::Menu), nInfoX, nAvatarY, CFG::Menu_Text_Active, POS_DEFAULT, "%s", NetSanitize(ownUser.m_strName.empty() ? ownUser.m_strUsername : ownUser.m_strName).c_str());
					H::Draw->String(H::Fonts->Get(EFonts::Menu), nInfoX, nAvatarY + nLineH, CFG::Menu_Text_Disabled, POS_DEFAULT, "%s", NetSanitize(F::NecroNet->GetOwnSteamId()).c_str());
				}
				m_nCursorX = nAvatarX;
				m_nCursorY = nAvatarY + nAvatarSize + CFG::Menu_Spacing_Y;
				if (Button("Change Avatar", false, 100))
				{
					char szFile[MAX_PATH] = {};
					OPENFILENAMEA ofn = {};
					ofn.lStructSize = sizeof(ofn);
					ofn.lpstrFilter = "Images\0*.png;*.jpg;*.jpeg;*.bmp\0All files\0*.*\0";
					ofn.lpstrFile = szFile;
					ofn.nMaxFile = MAX_PATH;
					ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
					if (GetOpenFileNameA(&ofn))
						F::NecroNet->UploadAvatarFromFile(szFile);
				}
				InputText("Display Name", "Display Name:", F::NecroNet->m_strEditName, NET_MAX_NAME);
				InputText("Bio", "Bio:", F::NecroNet->m_strEditBio, NET_MAX_BIO);
				if (Button("Save Profile", false, 100))
					F::NecroNet->ApplyProfile();
			}
			GroupBoxEnd();
			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;
			GroupBoxStart("Presence", 200);
			{
				const int nPrevStatus = nNetStatus;
				SelectSingle("Status", nNetStatus, {
					{ NET_STATUS_NAMES[0], 0 },
					{ NET_STATUS_NAMES[1], 1 },
					{ NET_STATUS_NAMES[2], 2 },
					{ NET_STATUS_NAMES[3], 3 },
					{ NET_STATUS_NAMES[4], 4 },
					{ NET_STATUS_NAMES[5], 5 }
					});
				if (nNetStatus != nPrevStatus)
					F::NecroNet->SetStatus(static_cast<ENetStatus>(nNetStatus));
				if (CheckBox("Listed On Listing", bNetListed))
					F::NecroNet->SetListed(bNetListed);
				CheckBox("Notifications", CFG::NecroNet_Notifications);
				const ENetConnection eServerState = F::NecroNet->GetServerState();
				const bool bWsConnected = eServerState == ENetConnection::CONNECTED;
				const Color_t clrServer = bWsConnected ? CFG::Color_Friend : (eServerState == ENetConnection::DISCONNECTED ? CFG::Color_Cheater : CFG::Menu_Accent_Primary);
				const char *pszServer = bWsConnected ? "Server: connected" : (eServerState == ENetConnection::CONNECTING ? "Server: connecting..." : (eServerState == ENetConnection::WAIT_RETRY ? "Server: retrying..." : "Server: offline"));
				H::Draw->FilledCircle(m_nCursorX + 4, m_nCursorY + (fontMenu.m_nTall / 2), 3, 12, clrServer);
				NetDrawOutlined(m_nCursorX + 12, m_nCursorY, { 255, 255, 255, 255 }, pszServer);
				m_nCursorY += nLineH;
				if (!bWsConnected)
				{
					const std::string strServerError = F::NecroNet->GetServerError();
					if (!strServerError.empty())
					{
						NetDrawOutlined(m_nCursorX + 12, m_nCursorY, { 255, 255, 255, 255 }, NetSanitize(strServerError).c_str());
						m_nCursorY += nLineH;
					}
				}
			}
			GroupBoxEnd();
			GroupBoxStart("Session", 200);
			{
				if (Button("Logout", false, 70))
					F::NecroNet->Logout();
			}
			GroupBoxEnd();
		}
		if (bLoggedIn && NetPage == ENetPages::FRIENDS)
		{
			anchor_y = m_nCursorY;
			GroupBoxStart("Friends", 230);
			{
				const int nRowH = (std::max)(nLineH, 18 + CFG::Menu_Spacing_Y);
				const std::vector<NetUser> &vecFriends = F::NecroNet->CopyFriends();
				int nShown = 0;
				if (vecFriends.empty())
				{
					H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "No friends yet");
					m_nCursorY += nLineH;
				}
				for (const auto &fr : vecFriends)
				{
					if (m_nCursorY + nRowH > nRowLimit)
					{
						H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "... and %d more", static_cast<int>(vecFriends.size()) - nShown);
						m_nCursorY += nLineH;
						break;
					}
					nShown++;
					const int nRowX = m_nCursorX;
					const int nRowY = m_nCursorY;
					const int nUnread = F::NecroNet->GetUnread(fr.m_strUsername);
					NetDrawAvatar(fr.m_strUsername, F::NecroNet->GetAvatarB64(fr.m_strUsername), nRowX, nRowY, 18);
					H::Draw->FilledCircle(nRowX + 14, nRowY + 14, 3, 12, fr.m_bOnline ? CFG::Color_Friend : CFG::Menu_Text_Disabled);
					m_nCursorX = nRowX + 26;
					m_nCursorY = nRowY;
					if (playerListButton(Utils::ConvertUtf8ToWide(NetEscapeLabel(fr.m_strName.empty() ? fr.m_strUsername : fr.m_strName)).c_str(), 100, fr.m_bOnline ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive, false))
					{
						F::NecroNet->OpenChat(fr.m_strUsername);
						NetPage = ENetPages::CHAT;
					}
					const char *pszStatusName = NET_STATUS_NAMES[std::clamp(fr.m_nStatus, 0, NET_STATUS_COUNT - 1)];
					NetDrawOutlined(nRowX + 132, nRowY, { 255, 255, 255, 255 }, fr.m_bOnline ? pszStatusName : "Offline");
					if (nUnread > 0)
					{
						char szBadge[8];
						snprintf(szBadge, sizeof(szBadge), "%d", nUnread > 99 ? 99 : nUnread);
						const int nBadgeW = 20;
						const int nBadgeH = fontMenu.m_nTall - 2;
						const int nBadgeX = nRowX + 202 - nBadgeW;
						H::Draw->Rect(nBadgeX, nRowY, nBadgeW, nBadgeH, CFG::Menu_Accent_Primary);
						H::Draw->String(H::Fonts->Get(EFonts::Menu), nBadgeX + (nBadgeW / 2), nRowY + (nBadgeH / 2), { 255, 255, 255, 255 }, POS_CENTERXY, szBadge);
					}
					m_nCursorX = nRowX;
					m_nCursorY = nRowY + nRowH;
				}
			}
			GroupBoxEnd();
			m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
			m_nCursorY = anchor_y;
			GroupBoxStart("Requests", 230);
			{
				const std::vector<NetFriendReq> &vecReqs = F::NecroNet->CopyReqs();
				int nReqShown = 0;
				if (vecReqs.empty())
				{
					H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "No pending requests");
					m_nCursorY += nLineH;
				}
				for (const auto &req : vecReqs)
				{
					if (m_nCursorY + (nLineH * 2) > nRowLimit)
					{
						H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "... and %d more", static_cast<int>(vecReqs.size()) - nReqShown);
						m_nCursorY += nLineH;
						break;
					}
					nReqShown++;
					const int nReqX = m_nCursorX;
					const int nReqY = m_nCursorY;
					H::Draw->String(H::Fonts->Get(EFonts::Menu), nReqX, nReqY, CFG::Menu_Text_Inactive, POS_DEFAULT, "%s", NetSanitize(req.m_strUsername).c_str());
					m_nCursorY = nReqY + nLineH;
					const int nBtnY = m_nCursorY;
					if (playerListButton(L"Accept", 50, CFG::Color_Friend, true))
						F::NecroNet->RespondFriendRequest(req.m_llId, true);
					m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
					m_nCursorY = nBtnY;
					if (playerListButton(L"Decline", 50, CFG::Color_Cheater, true))
						F::NecroNet->RespondFriendRequest(req.m_llId, false);
					m_nCursorX = nReqX;
					m_nCursorY = nBtnY + nLineH;
				}
				InputText("Add Friend", "Username:", F::NecroNet->m_strAddFriend, NET_USERNAME_MAX);
				if (Button("Send Request", false, 110))
					F::NecroNet->SendFriendRequest();
			}
			GroupBoxEnd();
			const std::string strFriendsStatus = F::NecroNet->GetConnectStatus();
			if (!strFriendsStatus.empty())
			{
				m_nCursorY += CFG::Menu_Spacing_Y;
				NetDrawOutlined(m_nCursorX, m_nCursorY, { 255, 255, 255, 255 }, NetEscapeLabel(strFriendsStatus).c_str());
				m_nCursorY += nLineH;
			}
		}
		if (bLoggedIn && NetPage == ENetPages::LISTING)
		{
			anchor_y = m_nCursorY;
			if (Button("Refresh", false, 70))
				F::NecroNet->RefreshListing();
			m_nCursorY += CFG::Menu_Spacing_Y;
			const std::string strOwnName = F::NecroNet->GetOwnUsername();
			GroupBoxStart("Public Users", 230);
			{
				const int nRowH = (std::max)(nLineH, 18 + CFG::Menu_Spacing_Y);
				const std::vector<NetUser> &vecListing = F::NecroNet->CopyListing();
				int nShown = 0;
				if (vecListing.empty())
				{
					NetDrawOutlined(m_nCursorX, m_nCursorY, { 255, 255, 255, 255 }, "Nobody on the listing right now");
					m_nCursorY += nLineH;
				}
				for (const auto &lu : vecListing)
				{
					if (m_nCursorY + nRowH > nRowLimit)
					{
						NetDrawOutlined(m_nCursorX, m_nCursorY, { 255, 255, 255, 255 }, ("... and " + std::to_string(static_cast<int>(vecListing.size()) - nShown) + " more").c_str());
						m_nCursorY += nLineH;
						break;
					}
					nShown++;
					const int nRowX = m_nCursorX;
					const int nRowY = m_nCursorY;
					NetDrawAvatar(lu.m_strUsername, F::NecroNet->GetAvatarB64(lu.m_strUsername), nRowX, nRowY, 18);
					H::Draw->FilledCircle(nRowX + 14, nRowY + 14, 3, 12, lu.m_bOnline ? CFG::Color_Friend : CFG::Menu_Text_Disabled);
					m_nCursorX = nRowX + 26;
					m_nCursorY = nRowY;
					if (playerListButton(Utils::ConvertUtf8ToWide(NetEscapeLabel(lu.m_strName.empty() ? lu.m_strUsername : lu.m_strName)).c_str(), 90, CFG::Menu_Text_Active, false))
						F::NecroNet->ViewUser(lu.m_strUsername);
					const char *pszStatusName = NET_STATUS_NAMES[std::clamp(lu.m_nStatus, 0, NET_STATUS_COUNT - 1)];
					NetDrawOutlined(nRowX + 124, nRowY, { 255, 255, 255, 255 }, lu.m_bOnline ? pszStatusName : "Offline");
					if (lu.m_strUsername != strOwnName)
					{
						m_nCursorX = nRowX + 178;
						m_nCursorY = nRowY;
						if (playerListButton(L"Add", 30, CFG::Color_Friend, true))
						{
							F::NecroNet->m_strAddFriend = lu.m_strUsername;
							F::NecroNet->SendFriendRequest();
						}
					}
					m_nCursorX = nRowX;
					m_nCursorY = nRowY + nRowH;
				}
			}
			GroupBoxEnd();
			NetUser viewedUser{};
			if (F::NecroNet->CopyViewedUser(viewedUser))
			{
				m_nCursorX += m_nLastGroupBoxW + (CFG::Menu_Spacing_X * 2);
				m_nCursorY = anchor_y + CFG::Menu_Spacing_Y;
				GroupBoxStart("User", 200);
				{
					const int nViewX = m_nCursorX;
					const int nViewY = m_nCursorY;
					NetDrawAvatar(viewedUser.m_strUsername, F::NecroNet->GetAvatarB64(viewedUser.m_strUsername), nViewX, nViewY, 48);
					const char *pszViewStatus = NET_STATUS_NAMES[std::clamp(viewedUser.m_nStatus, 0, NET_STATUS_COUNT - 1)];
					const int nTextX = nViewX + 58;
					NetDrawOutlined(nTextX, nViewY, { 255, 255, 255, 255 }, NetEscapeLabel(viewedUser.m_strName.empty() ? viewedUser.m_strUsername : viewedUser.m_strName).c_str());
					NetDrawOutlined(nTextX, nViewY + nLineH, { 255, 255, 255, 255 }, ("@" + NetEscapeLabel(viewedUser.m_strUsername)).c_str());
					NetDrawOutlined(nTextX, nViewY + (nLineH * 2), { 255, 255, 255, 255 }, viewedUser.m_bOnline ? pszViewStatus : "Offline");
					m_nCursorX = nViewX;
					m_nCursorY = nViewY + 48 + CFG::Menu_Spacing_Y;
					if (!viewedUser.m_strBio.empty())
					{
						NetDrawWrapped(NetSanitize(viewedUser.m_strBio), nViewX, m_nCursorY, m_nLastGroupBoxW - (CFG::Menu_Spacing_X * 4), 4, { 255, 255, 255, 255 });
						m_nCursorY += (nLineH * 4) + CFG::Menu_Spacing_Y;
					}
					if (viewedUser.m_strUsername != strOwnName)
					{
						if (Button("Add Friend", false, 90))
						{
							F::NecroNet->m_strAddFriend = viewedUser.m_strUsername;
							F::NecroNet->SendFriendRequest();
						}
						m_nCursorX = nViewX;
					}
					if (Button("Close", false, 60))
						F::NecroNet->CloseUserView();
					m_nCursorX = nViewX;
				}
				GroupBoxEnd();
			}
			const std::string strListStatus = F::NecroNet->GetConnectStatus();
			if (!strListStatus.empty())
			{
				m_nCursorY += CFG::Menu_Spacing_Y;
				NetDrawOutlined(m_nCursorX, m_nCursorY, { 255, 255, 255, 255 }, NetEscapeLabel(strListStatus).c_str());
				m_nCursorY += nLineH;
			}
		}
		if (bLoggedIn && NetPage == ENetPages::CHAT)
		{
			anchor_y = m_nCursorY;
			const std::string strTarget = F::NecroNet->GetChatTarget();
			const std::vector<NetUser> &vecChatFriends = F::NecroNet->CopyFriends();
			const NetUser *pTargetUser = nullptr;
			for (const auto &fr : vecChatFriends)
			{
				if (fr.m_strUsername == strTarget)
				{
					pTargetUser = &fr;
					break;
				}
			}
				if (!pTargetUser)
				{
					NetDrawOutlined(m_nCursorX, m_nCursorY, { 255, 255, 255, 255 }, strTarget.empty() ? "Select a friend on the Friends page to start chatting" : "Friend not found");
					m_nCursorY += nLineH;
				}
			else
			{
				struct ChatLink_t { size_t nStart; size_t nLen; int nX; int nW; };
				struct ChatLine_t { bool bOut; std::string strText; int nMsg; size_t nByte; int nTextW; std::vector<ChatLink_t> vecLinks; };
				static std::vector<ChatLine_t> vecWrapped = {};
				static std::vector<std::string> vecWrappedMsgs = {};
				static size_t nWrapFirst = 0;
				static std::string strWrappedTarget = {};
				static size_t nWrappedCount = 0;
				const int nChatX = m_nCursorX;
				const int nChatY = m_nCursorY;
				const int nChatW = (CFG::Menu_Pos_X + CFG::Menu_Width - 1) - nChatX - CFG::Menu_Spacing_X;
				const int nInputW = nChatW - 70 - CFG::Menu_Spacing_X;
				const int nInputLineH = fontChat.m_nTall + 3;
				static std::string strInputWrapCache;
				static int nInputWrapW = 0;
				static std::vector<std::string> vecInputLines;
				static std::vector<int> vecInputGlyphStarts;
				static std::vector<size_t> vecInputLineByteStarts;
				if (strInputWrapCache != strNetChatInput || nInputWrapW != nInputW)
				{
					vecInputLines.clear();
					vecInputGlyphStarts.clear();
					vecInputLineByteStarts.clear();
					if (strNetChatInput.empty())
					{
						vecInputLines.emplace_back("");
						vecInputGlyphStarts.push_back(0);
						vecInputLineByteStarts.push_back(0);
					}
					else
					{
						size_t nPos = 0;
						int nGlyphBase = 0;
						while (nPos < strNetChatInput.size() && vecInputLines.size() < 512)
						{
							const size_t nRemaining = strNetChatInput.size() - nPos;
							const size_t nMeasureCap = (std::min)(nRemaining, static_cast<size_t>(nInputW) * 2);
							size_t nFit = nRemaining;
							if (MeasureChatW(strNetChatInput.substr(nPos, nMeasureCap)) > nInputW - 8)
							{
								size_t nLo = 0, nHi = nMeasureCap, nBest = 0;
								while (nLo <= nHi)
								{
									const size_t nMid = (nLo + nHi) / 2;
									if (MeasureChatW(strNetChatInput.substr(nPos, nMid)) <= nInputW - 8)
									{
										nBest = nMid;
										nLo = nMid + 1;
									}
									else
									{
										if (nMid == 0)
											break;
										nHi = nMid - 1;
									}
								}
								nFit = nBest;
								if (nPos + nFit < strNetChatInput.size())
								{
									const size_t nSpace = strNetChatInput.find_last_of(' ', (nPos + nFit) - 1);
									if (nSpace != std::string::npos && nSpace > nPos)
										nFit = (nSpace - nPos) + 1;
								}
								while (nFit > 0 && nPos + nFit < strNetChatInput.size() && (static_cast<unsigned char>(strNetChatInput[nPos + nFit]) & 0xC0) == 0x80)
									nFit--;
								if (nFit == 0)
									nFit = 1;
							}
							vecInputLines.push_back(strNetChatInput.substr(nPos, nFit));
							vecInputGlyphStarts.push_back(nGlyphBase);
							vecInputLineByteStarts.push_back(nPos);
							nGlyphBase += static_cast<int>(Utf8GlyphCount(strNetChatInput.substr(nPos, nFit)));
							nPos += nFit;
						}
					}
					strInputWrapCache = strNetChatInput;
					nInputWrapW = nInputW;
				}
				const int nInputVisibleLines = (std::min)(static_cast<int>(vecInputLines.size()), 6);
				const int nInputH = (std::max)(fontMenu.m_nTall + 6, nInputVisibleLines * nInputLineH + 8);
				const int nViewY = nChatY + (nLineH * 2);
				const int nViewH = (std::max)(40, (CFG::Menu_Pos_Y + CFG::Menu_Height - 20) - nViewY - nInputH - (CFG::Menu_Spacing_Y * 2));
				const int nInputY = nViewY + nViewH + CFG::Menu_Spacing_Y;
				const std::string strDisplayName = NetSanitize(pTargetUser->m_strName.empty() ? pTargetUser->m_strUsername : pTargetUser->m_strName);
				const char *pszStatusName = NET_STATUS_NAMES[std::clamp(pTargetUser->m_nStatus, 0, NET_STATUS_COUNT - 1)];
				const std::string strStatusLine = pTargetUser->m_bOnline ? pszStatusName : std::string("Offline");
				NetDrawAvatar(strTarget, F::NecroNet->GetAvatarB64(strTarget), nChatX, nChatY, 28);
				H::Draw->FilledCircle(nChatX + 23, nChatY + 23, 3, 12, pTargetUser->m_bOnline ? CFG::Color_Friend : CFG::Menu_Text_Disabled);
				H::Draw->StartClipping(nChatX, nChatY, nChatW - 70, nLineH * 2);
				NetDrawOutlined(nChatX + 36, nChatY, CFG::Menu_Text_Active, NetEscapeLabel(strDisplayName).c_str());
				NetDrawOutlined(nChatX + 36, nChatY + nLineH, { 255, 255, 255, 255 }, strStatusLine.c_str());
				H::Draw->EndClipping();
				m_nCursorX = nChatX + nChatW - 62;
				m_nCursorY = nChatY;
				if (playerListButton(L"Remove", 56, CFG::Color_Cheater, true))
				{
					F::NecroNet->RemoveFriend(strTarget);
					NetPage = ENetPages::FRIENDS;
				}
				if (F::NecroNet->IsFriendTyping(strTarget))
				{
					const int nDots = static_cast<int>(fmod(I::GlobalVars->realtime * 2.0f, 4.0f));
					const std::string strTyping = strDisplayName + " is typing" + std::string(nDots, '.');
					int nTypingW = 0, nTypingH = 0;
					I::MatSystemSurface->GetTextSize(fontMenu.m_dwFont, Utils::ConvertUtf8ToWide(strTyping).c_str(), nTypingW, nTypingH);
					int nTypingX = (nChatX + nChatW - 74) - nTypingW;
					int nTypingY = nChatY + nLineH;

					if (nTypingY + nTypingH > nViewY)
						nTypingY = (std::max)(nChatY, nViewY - nTypingH);

					H::Draw->StartClipping(nChatX, nChatY, nChatW - 70, (nTypingY + nTypingH + 1) - nChatY);
					H::Draw->String(H::Fonts->Get(EFonts::Menu), nTypingX, nTypingY, CFG::Menu_Accent_Primary, POS_DEFAULT, "%s", strTyping.c_str());
					H::Draw->EndClipping();
				}
				const std::vector<NetHistoryLine> &vecHistory = F::NecroNet->CopyHistory(strTarget);
				if (strWrappedTarget != strTarget || nWrappedCount != vecHistory.size())
				{
					strWrappedTarget = strTarget;
					nWrappedCount = vecHistory.size();
					vecWrapped.clear();
					vecWrappedMsgs.clear();
					const int nMaxTextW = nChatW - 24;
					const size_t nFirst = vecHistory.size() > 300 ? vecHistory.size() - 300 : 0;
					nWrapFirst = nFirst;
					for (size_t i = nFirst; i < vecHistory.size() && vecWrapped.size() < 800; i++)
					{
						const NetHistoryLine &line = vecHistory[i];
						std::string strMsg = NetSanitize(line.m_strText);
						if (strMsg.empty())
							strMsg = " ";
						vecWrappedMsgs.push_back(strMsg);
						size_t nPos = 0;
						while (nPos < strMsg.size() && vecWrapped.size() < 800)
						{
							const size_t nRemaining = strMsg.size() - nPos;
							size_t nLo = 1, nHi = nRemaining, nFit = 1;
							while (nLo <= nHi)
							{
								const size_t nMid = (nLo + nHi) / 2;
								if (MeasureChatW(strMsg.substr(nPos, nMid)) <= nMaxTextW)
								{
									nFit = nMid;
									nLo = nMid + 1;
								}
								else
								{
									if (nMid == 0)
										break;
									nHi = nMid - 1;
								}
							}
							size_t nTake = nFit;
							if (nPos + nFit < strMsg.size())
							{
								const size_t nSpace = strMsg.find_last_of(' ', (nPos + nFit) - 1);
								if (nSpace != std::string::npos && nSpace > nPos)
									nTake = (nSpace - nPos) + 1;
							}
							while (nTake > 0 && nPos + nTake < strMsg.size() && (static_cast<unsigned char>(strMsg[nPos + nTake]) & 0xC0) == 0x80)
								nTake--;
							if (nTake == 0)
								nTake = 1;
							ChatLine_t wrapped;
							wrapped.bOut = line.m_bOut;
							wrapped.strText = strMsg.substr(nPos, nTake);
							wrapped.nMsg = static_cast<int>(i);
							wrapped.nByte = nPos;
							wrapped.nTextW = MeasureChatW(wrapped.strText);
							std::vector<std::pair<size_t, size_t>> vecSpans;
							ScanLinks(wrapped.strText, vecSpans);
							for (const auto &span : vecSpans)
							{
								ChatLink_t link;
								link.nStart = span.first;
								link.nLen = span.second;
								link.nX = MeasureChatW(wrapped.strText.substr(0, span.first));
								link.nW = MeasureChatW(wrapped.strText.substr(span.first, span.second));
								wrapped.vecLinks.push_back(link);
							}
							vecWrapped.push_back(std::move(wrapped));
							nPos += nTake;
							while (nPos < strMsg.size() && strMsg[nPos] == ' ')
								nPos++;
						}
					}
				}
				if (strChatScrollTarget != strTarget)
				{
					strChatScrollTarget = strTarget;
					nChatScroll = 0;
					strNetChatInput.clear();
					bChatSelValid = false;
					bChatSelecting = false;
					bChatLinkPress = false;
				}
				const int nContentH = static_cast<int>(vecWrapped.size()) * nChatLineH;
				const int nMaxScroll = (std::max)(0, nContentH - nViewH);
				const bool bScrollUp = H::Input->IsDown(VK_PRIOR) || (!bNetChatFocused && H::Input->IsDown(VK_UP));
				const bool bScrollDown = H::Input->IsDown(VK_NEXT) || (!bNetChatFocused && H::Input->IsDown(VK_DOWN));
				if (bScrollUp || bScrollDown)
				{
					if (I::GlobalVars->realtime - flChatScrollRepeat > 0.05f)
					{
						nChatScroll += bScrollUp ? 30 : -30;
						flChatScrollRepeat = I::GlobalVars->realtime;
					}
				}
				else flChatScrollRepeat = 0.0f;
				nChatScroll = (std::clamp)(nChatScroll, 0, nMaxScroll);
				if (nMaxScroll > 0)
				{
					const int nTrackX = nChatX + nChatW - 6;
					const int nThumbH = (std::max)(24, (nViewH * nViewH) / (std::max)(1, nContentH));
					const int nThumbY = nViewY + static_cast<int>((static_cast<float>(nMaxScroll - nChatScroll) / static_cast<float>(nMaxScroll)) * static_cast<float>(nViewH - nThumbH));
					if (!bChatThumbDrag && IsHoveredSimple(nTrackX - 2, nViewY, 8, nViewH) && H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed)
					{
						bChatThumbDrag = true;
						nThumbGrabOffset = H::Input->GetMouseY() - nThumbY;
						m_bClickConsumed = true;
					}
					if (bChatThumbDrag)
					{
						if (H::Input->IsDown(VK_LBUTTON))
						{
							const int nWant = (std::clamp)((H::Input->GetMouseY() - nThumbGrabOffset) - nViewY, 0, (std::max)(1, nViewH - nThumbH));
							nChatScroll = nMaxScroll - static_cast<int>((static_cast<float>(nWant) / static_cast<float>((std::max)(1, nViewH - nThumbH))) * static_cast<float>(nMaxScroll));
						}
						else bChatThumbDrag = false;
					}
					nChatScroll = (std::clamp)(nChatScroll, 0, nMaxScroll);
					H::Draw->Rect(nTrackX, nViewY, 4, nViewH, { 255, 255, 255, 15 });
					H::Draw->Rect(nTrackX, nThumbY, 4, nThumbH, bChatThumbDrag ? CFG::Menu_Accent_Primary : Color_t{ CFG::Menu_Accent_Primary.r, CFG::Menu_Accent_Primary.g, CFG::Menu_Accent_Primary.b, 120 });
				}
				else bChatThumbDrag = false;
				const bool bAnyPopupActive = [&]() -> bool {
					for (const auto &State : m_mapStates)
					{
						if (State.second)
							return true;
					}
					return false;
				}();
				auto HitTestChat = [&](int nMx, int nMy, int &nLineOut, size_t &nByteOut) -> bool
				{
					if (vecWrapped.empty())
						return false;

					const int nBaseY = nViewY + 2 - (std::max)(0, nContentH - nViewH) + nChatScroll;
					const int nLine = (std::clamp)((nMy - nBaseY) / nChatLineH, 0, static_cast<int>(vecWrapped.size()) - 1);
					const ChatLine_t &cl = vecWrapped[nLine];
					const int nTextX = cl.bOut ? (nChatX + nChatW - 16 - cl.nTextW) : (nChatX + 8);
					const int nLocalX = (std::max)(0, nMx - nTextX);
					size_t nBestByte = 0;
					int nBestDist = nLocalX;
					for (size_t nByte = 0; nByte < cl.strText.size();)
					{
						nByte = Utf8NextBoundary(cl.strText, nByte);
						const int nDelta = nLocalX - MeasureChatW(cl.strText.substr(0, nByte));
						const int nDist = nDelta < 0 ? -nDelta : nDelta;
						if (nDist <= nBestDist)
						{
							nBestDist = nDist;
							nBestByte = nByte;
						}
					}
					nLineOut = nLine;
					nByteOut = nBestByte;
					return true;
				};
				auto ChatSelectionRange = [&](std::pair<int, size_t> &tLo, std::pair<int, size_t> &tHi)
				{
					tLo = { nChatSelAnchorMsg, nChatSelAnchorByte };
					tHi = { nChatSelCaretMsg, nChatSelCaretByte };
					if (tHi < tLo)
						std::swap(tLo, tHi);
				};
				auto BuildChatSelection = [&]() -> std::string
				{
					std::string strOut;
					if (!bChatSelValid)
						return strOut;

					std::pair<int, size_t> tLo, tHi;
					ChatSelectionRange(tLo, tHi);

					int nGroupMsg = -1;
					size_t nGroupFrom = 0;
					size_t nGroupTo = 0;
					auto FlushGroup = [&]()
					{
						if (nGroupMsg < 0)
							return;

						const std::string *pMsg = nullptr;
						if (nGroupMsg >= static_cast<int>(nWrapFirst) && static_cast<size_t>(nGroupMsg - static_cast<int>(nWrapFirst)) < vecWrappedMsgs.size())
							pMsg = &vecWrappedMsgs[nGroupMsg - static_cast<int>(nWrapFirst)];

						if (pMsg && nGroupTo > nGroupFrom && nGroupFrom < pMsg->size())
						{
							if (!strOut.empty())
								strOut += "\n";
							strOut += pMsg->substr(nGroupFrom, (std::min)(nGroupTo, pMsg->size()) - nGroupFrom);
						}

						nGroupMsg = -1;
					};

					for (const ChatLine_t &cl : vecWrapped)
					{
						const std::pair<int, size_t> tLineStart = { cl.nMsg, cl.nByte };
						const std::pair<int, size_t> tLineEnd = { cl.nMsg, cl.nByte + cl.strText.size() };
						if (tLineEnd <= tLo || tLineStart >= tHi)
							continue;

						size_t nFrom = cl.nByte;
						size_t nTo = cl.nByte + cl.strText.size();
						if (cl.nMsg == tLo.first && tLo.second > nFrom)
							nFrom = tLo.second;
						if (cl.nMsg == tHi.first && tHi.second < nTo)
							nTo = tHi.second;

						if (cl.nMsg != nGroupMsg)
						{
							FlushGroup();
							nGroupMsg = cl.nMsg;
							nGroupFrom = nFrom;
						}
						nGroupTo = nTo;
					}
					FlushGroup();
					return strOut;
				};
				const int nMx = H::Input->GetMouseX();
				const int nMy = H::Input->GetMouseY();
				const bool bLmbDown = H::Input->IsDown(VK_LBUTTON);
				const bool bInChatView = nMx >= nChatX && nMx < nChatX + nChatW - 8 && nMy >= nViewY && nMy < nViewY + nViewH;
				if (H::Input->IsPressed(VK_LBUTTON) && bInChatView && !m_bClickConsumed && !bChatThumbDrag && !bAnyPopupActive)
				{
					int nLine = 0;
					size_t nByte = 0;
					if (HitTestChat(nMx, nMy, nLine, nByte))
					{
						const ChatLine_t &cl = vecWrapped[nLine];
						bChatLinkPress = false;
						for (const auto &link : cl.vecLinks)
						{
							if (nByte >= link.nStart && nByte <= link.nStart + link.nLen)
							{
								bChatLinkPress = true;
								break;
							}
						}
						nChatLinkPressX = nMx;
						nChatLinkPressY = nMy;
						bChatSelecting = true;
						bChatSelValid = true;
						nChatSelAnchorMsg = nChatSelCaretMsg = cl.nMsg;
						nChatSelAnchorByte = nChatSelCaretByte = cl.nByte + nByte;
						bNetChatFocused = false;
						m_bClickConsumed = true;
					}
				}
				if (bChatSelecting && bLmbDown)
				{
					if (nMy < nViewY)
						nChatScroll += 8;
					else if (nMy > nViewY + nViewH)
						nChatScroll -= 8;
					nChatScroll = (std::clamp)(nChatScroll, 0, nMaxScroll);
					int nLine = 0;
					size_t nByte = 0;
					if (HitTestChat(nMx, nMy, nLine, nByte))
					{
						nChatSelCaretMsg = vecWrapped[nLine].nMsg;
						nChatSelCaretByte = vecWrapped[nLine].nByte + nByte;
					}
					m_bClickConsumed = true;
				}
				if (!bLmbDown && bChatLmbWasDown)
				{
					if (bChatSelecting)
					{
						bChatSelecting = false;
						if (nChatSelAnchorMsg == nChatSelCaretMsg && nChatSelAnchorByte == nChatSelCaretByte)
							bChatSelValid = false;
					}
					if (bChatLinkPress)
					{
						int nLine = 0;
						size_t nByte = 0;
						const int nDx = nMx - nChatLinkPressX;
						const int nDy = nMy - nChatLinkPressY;
						const bool bSameSpot = nDx >= -4 && nDx <= 4 && nDy >= -4 && nDy <= 4;
						if (bSameSpot && HitTestChat(nMx, nMy, nLine, nByte))
						{
							const ChatLine_t &cl = vecWrapped[nLine];
							for (const auto &link : cl.vecLinks)
							{
								if (nByte >= link.nStart && nByte <= link.nStart + link.nLen)
								{
									std::string strUrl = cl.strText.substr(link.nStart, link.nLen);
									if (!StartsWithNoCase(strUrl, 0, "http://") && !StartsWithNoCase(strUrl, 0, "https://"))
										strUrl = "http://" + strUrl;
									ShellExecuteA(nullptr, "open", strUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
									bChatSelValid = false;
									break;
								}
							}
						}
						bChatLinkPress = false;
					}
				}
				bChatLmbWasDown = bLmbDown;
				if (H::Input->IsPressed(VK_ESCAPE))
				{
					bChatSelValid = false;
					bChatSelecting = false;
					bChatLinkPress = false;
				}
				if (!bNetChatFocused && H::Input->IsDown(VK_CONTROL))
				{
					if (H::Input->IsPressed('A') && bInChatView && !vecWrapped.empty())
					{
						bChatSelValid = true;
						bChatSelecting = false;
						nChatSelAnchorMsg = vecWrapped.front().nMsg;
						nChatSelAnchorByte = vecWrapped.front().nByte;
						nChatSelCaretMsg = vecWrapped.back().nMsg;
						nChatSelCaretByte = vecWrapped.back().nByte + vecWrapped.back().strText.size();
					}
					if (H::Input->IsPressed('C') && bChatSelValid)
					{
						const std::string strSel = BuildChatSelection();
						if (!strSel.empty())
							ClipboardSetText(strSel);
					}
				}
				H::Draw->Rect(nChatX, nViewY, nChatW - 8, nViewH, { 0, 0, 0, 100 });
				H::Draw->OutlinedRect(nChatX, nViewY, nChatW - 8, nViewH, { CFG::Menu_Accent_Primary.r, CFG::Menu_Accent_Primary.g, CFG::Menu_Accent_Primary.b, 120 });
				H::Draw->StartClipping(nChatX + 2, nViewY + 2, nChatW - 12, nViewH - 4);
				if (vecWrapped.empty())
				{
					H::Draw->String(fontChat, nChatX + (nChatW / 2), nViewY + (nViewH / 2), CFG::Menu_Text_Disabled, POS_CENTERXY, "No messages yet. Say hi!");
				}
				else
				{
					std::pair<int, size_t> tSelLo = {}, tSelHi = {};
					if (bChatSelValid)
						ChatSelectionRange(tSelLo, tSelHi);
					const Color_t clrSelRect = { 255, 255, 255, 55 };
					const Color_t clrShadow = { 0, 0, 0, 160 };
					int nDrawY = nViewY + 2 - (std::max)(0, nContentH - nViewH) + nChatScroll;
					for (const auto &cl : vecWrapped)
					{
						if (nDrawY + nChatLineH >= nViewY && nDrawY <= nViewY + nViewH)
						{
							const int nTextX = cl.bOut ? (nChatX + nChatW - 16 - cl.nTextW) : (nChatX + 8);
							const Color_t clrText = cl.bOut ? CFG::Color_Friend : CFG::Menu_Text_Active;
							if (bChatSelValid)
							{
								const std::pair<int, size_t> tLineStart = { cl.nMsg, cl.nByte };
								const std::pair<int, size_t> tLineEnd = { cl.nMsg, cl.nByte + cl.strText.size() };
								if (!(tLineEnd <= tSelLo || tLineStart >= tSelHi))
								{
									size_t nFrom = 0;
									size_t nTo = cl.strText.size();
									if (cl.nMsg == tSelLo.first && tSelLo.second > cl.nByte)
										nFrom = tSelLo.second - cl.nByte;
									if (cl.nMsg == tSelHi.first && tSelHi.second - cl.nByte < nTo)
										nTo = tSelHi.second - cl.nByte;
									const int nX0 = nTextX + MeasureChatW(cl.strText.substr(0, nFrom));
									const int nX1 = nTextX + MeasureChatW(cl.strText.substr(0, nTo));
									H::Draw->Rect(nX0, nDrawY, (std::max)(1, nX1 - nX0), nChatLineH, clrSelRect);
								}
							}
							H::Draw->String(fontChat, nTextX + 1, nDrawY + 1, clrShadow, POS_DEFAULT, "%s", cl.strText.c_str());
							if (cl.vecLinks.empty())
							{
								H::Draw->String(fontChat, nTextX, nDrawY, clrText, POS_DEFAULT, "%s", cl.strText.c_str());
							}
							else
							{
								int nCurX = 0;
								size_t nCurByte = 0;
								for (const auto &link : cl.vecLinks)
								{
									if (link.nStart > nCurByte)
										H::Draw->String(fontChat, nTextX + nCurX, nDrawY, clrText, POS_DEFAULT, "%s", cl.strText.substr(nCurByte, link.nStart - nCurByte).c_str());
									const int nLinkX = nTextX + link.nX;
									const bool bHovered = nMx >= nLinkX && nMx <= nLinkX + link.nW && nMy >= nDrawY && nMy < nDrawY + nChatLineH;
									const Color_t clrLink = bHovered ? Color_t{ 130, 195, 255, 255 } : Color_t{ 85, 145, 255, 255 };
									H::Draw->String(fontChat, nLinkX, nDrawY, clrLink, POS_DEFAULT, "%s", cl.strText.substr(link.nStart, link.nLen).c_str());
									H::Draw->Rect(nLinkX, nDrawY + fontChat.m_nTall - 1, link.nW, 1, clrLink);
									nCurX = link.nX + link.nW;
									nCurByte = link.nStart + link.nLen;
								}
								if (nCurByte < cl.strText.size())
									H::Draw->String(fontChat, nTextX + nCurX, nDrawY, clrText, POS_DEFAULT, "%s", cl.strText.substr(nCurByte).c_str());
							}
						}
						nDrawY += nChatLineH;
					}
				}
				H::Draw->EndClipping();
				const bool bInputHovered = IsHoveredSimple(nChatX, nInputY, nInputW, nInputH);
				if (bInputHovered && H::Input->IsPressed(VK_LBUTTON) && !m_bClickConsumed && !bAnyPopupActive)
					bNetChatFocused = true;
				else if (H::Input->IsPressed(VK_LBUTTON) && !bInputHovered && !m_bClickConsumed)
					bNetChatFocused = false;
				if (H::Input->IsPressed(VK_ESCAPE))
					bNetChatFocused = false;
				if (bNetChatFocused)
				{
					if (nChatCaret > static_cast<int>(strNetChatInput.size()))
						nChatCaret = static_cast<int>(strNetChatInput.size());
					if (nChatAnchor > static_cast<int>(strNetChatInput.size()))
						nChatAnchor = static_cast<int>(strNetChatInput.size());
					if (TextInputEdit(strNetChatInput, nChatCaret, nChatAnchor, NET_MAX_MESSAGE_CHARS))
						F::NecroNet->NotifyTyping();
				}
				H::Draw->Rect(nChatX, nInputY, nInputW, nInputH, CFG::Menu_Background);
				H::Draw->OutlinedRect(nChatX, nInputY, nInputW, nInputH, bNetChatFocused ? CFG::Menu_Accent_Primary : CFG::Menu_Text_Inactive);
				H::Draw->StartClipping(nChatX + 2, nInputY, nInputW - 4, nInputH);
				const int nCaretGlyphPos = static_cast<int>(Utf8GlyphCount(strNetChatInput.substr(0, static_cast<size_t>((std::max)(nChatCaret, 0)))));
				int nCaretLine = 0;
				for (int i = 0; i < static_cast<int>(vecInputGlyphStarts.size()); i++)
				{
					if (nCaretGlyphPos >= vecInputGlyphStarts[i])
						nCaretLine = i;
					else
						break;
				}
				const int nFirstLine = (std::max)(0, (std::min)(nCaretLine - nInputVisibleLines + 1, static_cast<int>(vecInputLines.size()) - 1));
				const int nInputSelFrom = (std::clamp)((std::min)(nChatCaret, nChatAnchor), 0, static_cast<int>(strNetChatInput.size()));
				const int nInputSelTo = (std::clamp)((std::max)(nChatCaret, nChatAnchor), 0, static_cast<int>(strNetChatInput.size()));
				for (int i = nFirstLine; i < static_cast<int>(vecInputLines.size()) && i < nFirstLine + nInputVisibleLines; i++)
				{
					const int nLineY = nInputY + 4 + (i - nFirstLine) * nInputLineH;
					if (nInputSelFrom != nInputSelTo && i < static_cast<int>(vecInputLineByteStarts.size()))
					{
						const size_t nLineStart = vecInputLineByteStarts[i];
						const size_t nLineEnd = nLineStart + vecInputLines[i].size();
						const size_t nFrom = (std::max)(static_cast<size_t>(nInputSelFrom), nLineStart);
						const size_t nTo = (std::min)(static_cast<size_t>(nInputSelTo), nLineEnd);
						if (nFrom < nTo)
						{
							const int nX0 = nChatX + 4 + MeasureChatW(vecInputLines[i].substr(0, nFrom - nLineStart));
							const int nX1 = nChatX + 4 + MeasureChatW(vecInputLines[i].substr(0, nTo - nLineStart));
							H::Draw->Rect(nX0, nLineY, (std::max)(1, nX1 - nX0), nInputLineH, { 255, 255, 255, 55 });
						}
					}
					H::Draw->String(fontChat, nChatX + 4, nLineY, CFG::Menu_Text_Active, POS_DEFAULT, "%s", vecInputLines[i].c_str());
				}
				if (bNetChatFocused && fmod(I::GlobalVars->realtime, 1.0f) < 0.5f)
				{
					const std::string &strLine = vecInputLines[nCaretLine];
					const size_t nCol = static_cast<size_t>((std::clamp)(nCaretGlyphPos - vecInputGlyphStarts[nCaretLine], 0, static_cast<int>(Utf8GlyphCount(strLine))));
					int nCaretX = nChatX + 4 + MeasureChatW(strLine.substr(0, Utf8GlyphOffset(strLine, nCol)));
					nCaretX = (std::min)(nCaretX, nChatX + nInputW - 5);
					const int nCaretY = nInputY + 4 + (nCaretLine - nFirstLine) * nInputLineH;
					H::Draw->Line(nCaretX, nCaretY, nCaretX, nCaretY + nInputLineH - 1, CFG::Menu_Text_Active);
				}
				H::Draw->EndClipping();
				{
					const size_t nTyped = strNetChatInput.size();
					Color_t clrCount = nTyped >= NET_MAX_MESSAGE_CHARS ? CFG::Color_Cheater : (nTyped >= NET_MAX_MESSAGE_CHARS * 9 / 10 ? Color_t{250, 130, 49, 255} : CFG::Menu_Text_Disabled);
					H::Draw->String(H::Fonts->Get(EFonts::ESP_SMALL), nChatX + nInputW - 52, nInputY + nInputH - 14, clrCount, POS_DEFAULT, "%d/%d", static_cast<int>(nTyped), NET_MAX_MESSAGE_CHARS);
				}
				bool bSend = bNetChatFocused && H::Input->IsPressed(VK_RETURN) && !strNetChatInput.empty();
				m_nCursorX = nChatX + nInputW + CFG::Menu_Spacing_X;
				m_nCursorY = nInputY;
				if (Button("Send", false, 60) && !strNetChatInput.empty())
					bSend = true;
				if (bSend)
				{
					F::NecroNet->SendChatText(strNetChatInput);
					strNetChatInput.clear();
				}
				m_bWantTextInput = bNetChatFocused;
			}
		}
	}
	if (MainTab == EMainTabs::PLAYER_DETAILS)
	{
		if (Button("< Back to Players", false, 120))
		{
			MainTab = EMainTabs::PLAYERS;
			nSelectedPlayerIndex = -1;
		}
		m_nCursorY += CFG::Menu_Spacing_Y * 2;
		player_info_t player_info{};
		const bool bLocalOnlyDetails = (nSelectedPlayerIndex == -1);
		if (bLocalOnlyDetails)
		{
			if (I::SteamFriends)
			{
				const char *pszPersona = I::SteamFriends->GetPersonaName();
				if (pszPersona && pszPersona[0])
					strncpy_s(player_info.name, sizeof(player_info.name), pszPersona, _TRUNCATE);
			}
			player_info.friendsID = NetLocalAccountId();
			if (!player_info.friendsID)
			{
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "Steam identity unavailable");
				return;
			}
		}
		else if (nSelectedPlayerIndex <= 0 || !I::EngineClient->GetPlayerInfo(nSelectedPlayerIndex, &player_info))
		{
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "Player no longer available");
			return;
		}
		const bool bIsLocalRow = bLocalOnlyDetails || (nSelectedPlayerIndex == I::EngineClient->GetLocalPlayer());
		auto pResource = GetTFPlayerResource();
		PlayerPriority custom_info{};
		if (!bLocalOnlyDetails)
			F::Players->GetInfo(nSelectedPlayerIndex, custom_info);
		const int nLocalTeam = pResource ? pResource->GetTeam(I::EngineClient->GetLocalPlayer()) : 0;
		const int nPlayerTeam = pResource ? pResource->GetTeam(nSelectedPlayerIndex) : 0;
		const bool bIsTeammate = (nPlayerTeam == nLocalTeam);
		const bool bIsF2P = H::Entities->IsF2P(nSelectedPlayerIndex);
		const int nPartyIndex = H::Entities->GetPartyIndex(nSelectedPlayerIndex);
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
			H::Draw->OutlinedRect(nAvatarX, nAvatarY, nLargeAvatarSize, nLargeAvatarSize, CFG::Menu_Accent_Primary);
		}
		int nInfoX = nAvatarX + nLargeAvatarSize + CFG::Menu_Spacing_X * 2;
		int nInfoY = nAvatarY;
		Color_t nameColor = bIsTeammate ? CFG::Color_Teammate : CFG::Color_Enemy;
		if (custom_info.Cheater)
			nameColor = CFG::Color_Cheater;
		else if (custom_info.Targeted)
			nameColor = CFG::Color_Targeted;
		else if (custom_info.Nigger)
			nameColor = CFG::Color_Nigger;
		else if (custom_info.Ignored)
			nameColor = CFG::Color_Friend;
		else if (custom_info.RetardLegit)
			nameColor = CFG::Color_RetardLegit;
		else if (custom_info.Streamer)
			nameColor = CFG::Color_Streamer;
		else if (custom_info.FollowPlayer)
			nameColor = CFG::Color_FollowPlayer;
		H::Draw->String(H::Fonts->Get(EFonts::Menu), nInfoX, nInfoY, nameColor, POS_DEFAULT, "%s", player_info.name);
		nInfoY += H::Fonts->Get(EFonts::Menu).m_nTall + 2;
		uint64_t steamID64 = static_cast<uint64_t>(player_info.friendsID) + 0x0110000100000000ULL;
		H::Draw->String(H::Fonts->Get(EFonts::Menu), nInfoX, nInfoY, CFG::Menu_Text_Inactive, POS_DEFAULT, "Steam ID: %llu", steamID64);
		nInfoY += H::Fonts->Get(EFonts::Menu).m_nTall + 2;
		const char* szTeam = "Unknown";
		if (nPlayerTeam == TF_TEAM_RED)
			szTeam = "RED";
		else if (nPlayerTeam == TF_TEAM_BLUE)
			szTeam = "BLU";
		else if (nPlayerTeam == TEAM_SPECTATOR)
			szTeam = "Spectator";
		H::Draw->String(H::Fonts->Get(EFonts::Menu), nInfoX, nInfoY, CFG::Menu_Text_Inactive, POS_DEFAULT, "Team: %s", szTeam);
		m_nCursorY = nAvatarY + nLargeAvatarSize + CFG::Menu_Spacing_Y * 2;
		GroupBoxStart("Player Info", 280);
		{
			m_nCursorY += CFG::Menu_Spacing_Y;
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text, POS_DEFAULT, "F2P Status:");
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 120, m_nCursorY, bIsF2P ? CFG::Color_F2P : CFG::Menu_Text_Inactive, POS_DEFAULT, bIsF2P ? "Yes" : "No");
			m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
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
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text, POS_DEFAULT, "Relation:");
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 120, m_nCursorY, bIsTeammate ? CFG::Color_Teammate : CFG::Color_Enemy, POS_DEFAULT, bIsTeammate ? "Teammate" : "Enemy");
			m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
			auto pEntity = nSelectedPlayerIndex > 0 ? I::ClientEntityList->GetClientEntity(nSelectedPlayerIndex) : nullptr;
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
		m_nCursorY += CFG::Menu_Spacing_Y;
		GroupBoxStart("Your History", 280);
		{
			m_nCursorY += CFG::Menu_Spacing_Y;
			PlayerStats playerStats{};
			F::Players->GetStats(steamID64, playerStats);
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text, POS_DEFAULT, "Encounters:");
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 120, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "%d", playerStats.Encounters);
			m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text, POS_DEFAULT, "You killed:");
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 120, m_nCursorY, CFG::Color_Friend, POS_DEFAULT, "%d", playerStats.Kills);
			m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text, POS_DEFAULT, "Killed you:");
			H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 120, m_nCursorY, CFG::Color_Cheater, POS_DEFAULT, "%d", playerStats.Deaths);
			m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
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
			if (playerStats.FirstSeen > 0)
			{
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text, POS_DEFAULT, "First seen:");
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 120, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, FormatTimeAgo(playerStats.FirstSeen).c_str());
				m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
			}
			if (playerStats.LastSeen > 0 && playerStats.Encounters > 1)
			{
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text, POS_DEFAULT, "Last seen:");
				H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX + 120, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, FormatTimeAgo(playerStats.LastSeen).c_str());
				m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
			}
		}
		GroupBoxEnd();
		m_nCursorY += CFG::Menu_Spacing_Y;
		if (!bIsLocalRow)
		{
		GroupBoxStart("Player Tags", 280);
		{
			m_nCursorY += CFG::Menu_Spacing_Y;
			int tagStartX = m_nCursorX;
			int tagY = m_nCursorY;
			if (playerListButton(L"ignored", 80, custom_info.Ignored ? CFG::Color_Friend : CFG::Menu_Text_Inactive, true))
			{
				if (!custom_info.Ignored)
					F::Players->Mark(nSelectedPlayerIndex, { true, false, false, false, false, false, custom_info.FollowPlayer });
				else
					F::Players->Mark(nSelectedPlayerIndex, { false, false, false, false, false, false, custom_info.FollowPlayer });
			}
			m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
			m_nCursorY = tagY;
			if (playerListButton(L"cheater", 80, custom_info.Cheater ? CFG::Color_Cheater : CFG::Menu_Text_Inactive, true))
			{
				if (!custom_info.Cheater)
					F::Players->Mark(nSelectedPlayerIndex, { false, true, false, false, false, false, custom_info.FollowPlayer });
				else
					F::Players->Mark(nSelectedPlayerIndex, { false, false, false, false, false, false, custom_info.FollowPlayer });
			}
			m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
			m_nCursorY = tagY;
			if (playerListButton(L"targeted", 80, custom_info.Targeted ? CFG::Color_Targeted : CFG::Menu_Text_Inactive, true))
			{
				if (!custom_info.Targeted)
					F::Players->Mark(nSelectedPlayerIndex, { false, false, false, true, false, false, custom_info.FollowPlayer });
				else
					F::Players->Mark(nSelectedPlayerIndex, { false, false, false, false, false, false, custom_info.FollowPlayer });
			}
			m_nCursorX = tagStartX;
			m_nCursorY = tagY + H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y * 3;
			int tagY2 = m_nCursorY;
			if (playerListButton(L"retard legit", 80, custom_info.RetardLegit ? CFG::Color_RetardLegit : CFG::Menu_Text_Inactive, true))
			{
				if (!custom_info.RetardLegit)
					F::Players->Mark(nSelectedPlayerIndex, { false, false, true, false, false, false, custom_info.FollowPlayer });
				else
					F::Players->Mark(nSelectedPlayerIndex, { false, false, false, false, false, false, custom_info.FollowPlayer });
			}
			m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
			m_nCursorY = tagY2;
			if (playerListButton(L"streamer", 80, custom_info.Streamer ? CFG::Color_Streamer : CFG::Menu_Text_Inactive, true))
			{
				if (!custom_info.Streamer)
					F::Players->Mark(nSelectedPlayerIndex, { false, false, false, false, true, false, custom_info.FollowPlayer });
				else
					F::Players->Mark(nSelectedPlayerIndex, { false, false, false, false, false, false, custom_info.FollowPlayer });
			}
			m_nCursorX += m_nLastButtonW + CFG::Menu_Spacing_X;
			m_nCursorY = tagY2;
			if (playerListButton(L"nigger", 80, custom_info.Nigger ? CFG::Color_Nigger : CFG::Menu_Text_Inactive, true))
			{
				if (!custom_info.Nigger)
					F::Players->Mark(nSelectedPlayerIndex, { false, false, false, false, false, true, custom_info.FollowPlayer });
				else
					F::Players->Mark(nSelectedPlayerIndex, { false, false, false, false, false, false, custom_info.FollowPlayer });
			}
			m_nCursorX = tagStartX;
			m_nCursorY = tagY2 + H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y * 3;
			int tagY3 = m_nCursorY;
			if (playerListButton(L"follow player", 100, custom_info.FollowPlayer ? CFG::Color_FollowPlayer : CFG::Menu_Text_Inactive, true))
			{
				custom_info.FollowPlayer = !custom_info.FollowPlayer;
				F::Players->Mark(nSelectedPlayerIndex, custom_info);
			}
			m_nCursorX = tagStartX;
			m_nCursorY = tagY3 + H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y * 2;
		}
		GroupBoxEnd();
		}
		m_nCursorY += CFG::Menu_Spacing_Y;
		GroupBoxStart("Actions", 280);
		{
			m_nCursorY += CFG::Menu_Spacing_Y;
			if (Button("Open Steam Profile", false, 150))
			{
				uint64_t steamID64 = static_cast<uint64_t>(player_info.friendsID) + 0x0110000100000000ULL;
				wchar_t szUrl[128];
				swprintf_s(szUrl, L"https://steamcommunity.com/profiles/%llu", steamID64);
				ShellExecuteW(NULL, L"open", szUrl, NULL, NULL, SW_SHOWNORMAL);
			}
		}
		GroupBoxEnd();
		m_nCursorY += CFG::Menu_Spacing_Y;
		GroupBoxStart("Sourcebans History", 450);
		{
			m_nCursorY += CFG::Menu_Spacing_Y;
			uint64_t steamID64 = static_cast<uint64_t>(player_info.friendsID) + 0x0110000100000000ULL;
			SourcebanInfo_t sourcebanInfo;
			GetSourcebansInfo(steamID64, sourcebanInfo);
			if (!sourcebanInfo.m_bFetched && !sourcebanInfo.m_bFetching)
			{
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
					H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Color_Cheater, POS_DEFAULT, "WARNING: %d ban(s) found!", static_cast<int>(sourcebanInfo.m_vecBans.size()));
					m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
					int nBansShown = 0;
					for (const auto& ban : sourcebanInfo.m_vecBans)
					{
						if (nBansShown >= 10)
						{
							H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCursorX, m_nCursorY, CFG::Menu_Text_Inactive, POS_DEFAULT, "... and %d more", static_cast<int>(sourcebanInfo.m_vecBans.size()) - 10);
							m_nCursorY += H::Fonts->Get(EFonts::Menu).m_nTall + CFG::Menu_Spacing_Y;
							break;
						}
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
				if (Button("Refresh", false, 70))
				{
					ClearSourcebansCache(steamID64);
				}
			}
		}
		GroupBoxEnd();
		m_nCursorY += CFG::Menu_Spacing_Y;
		GroupBoxStart("Vorobey's Database", 450);
		{
			m_nCursorY += CFG::Menu_Spacing_Y;
			uint64_t steamID64 = static_cast<uint64_t>(player_info.friendsID) + 0x0110000100000000ULL;
			VerobayInfo_t verobayInfo;
			GetVerobayInfo(steamID64, verobayInfo);
			if (!verobayInfo.m_bFetched && !verobayInfo.m_bFetching)
			{
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
}
void CMenu::DrawNetNotifications()
{
	if (!CFG::NecroNet_Notifications)
		return;

	const std::vector<NetNotification> &vecNotes = F::NecroNet->Notifications();
	if (vecNotes.empty())
		return;

	const auto &font = H::Fonts->Get(EFonts::Menu);
	const int nW = 280;
	const int nH = 52;
	const int nAvatarSize = 36;
	const int nScreenW = H::Draw->GetScreenW();
	const int nScreenH = H::Draw->GetScreenH();
	const auto tpNow = std::chrono::steady_clock::now();
	constexpr float flLifetime = 6.0f;
	constexpr float flFade = 0.5f;

	int nY = nScreenH - 10;
	int nShown = 0;
	for (auto it = vecNotes.rbegin(); it != vecNotes.rend() && nShown < 5; ++it, nShown++)
	{
		const NetNotification &note = *it;
		const float flAge = std::chrono::duration<float>(tpNow - note.m_tpBorn).count();
		float flAlpha = 1.0f;
		if (flAge > flLifetime - flFade)
			flAlpha = std::max(0.0f, 1.0f - (flAge - (flLifetime - flFade)) / flFade);
		if (flAlpha <= 0.0f)
			continue;

		nY -= nH + 6;
		const int nX = nScreenW - 10 - nW;

		Color_t clrBg = CFG::Menu_Background;
		clrBg.a = static_cast<byte>(250.0f * flAlpha);
		Color_t clrOutline = CFG::Menu_Accent_Primary;
		clrOutline.a = static_cast<byte>(clrOutline.a * flAlpha);
		H::Draw->Rect(nX, nY, nW, nH, clrBg);
		H::Draw->OutlinedRect(nX, nY, nW, nH, clrOutline);

		NetDrawAvatar(note.m_strUsername, note.m_strAvatar, nX + 8, nY + (nH - nAvatarSize) / 2, nAvatarSize);

		const int nTextX = nX + 8 + nAvatarSize + 8;
		const int nTextW = nX + nW - nTextX - 8;

		std::string strName = NetSanitize(note.m_strName.empty() ? note.m_strUsername : note.m_strName);
		if (strName.size() > 32)
			strName = strName.substr(0, 32);
		Color_t clrTitle = CFG::Menu_Text_Active;
		clrTitle.a = static_cast<byte>(clrTitle.a * flAlpha);
		Color_t clrBody = CFG::Menu_Text_Inactive;
		clrBody.a = static_cast<byte>(clrBody.a * flAlpha);

		H::Draw->StartClipping(nTextX, nY, nTextW, nH);
		if (note.m_eKind == ENetNotify::FRIEND_REQ)
		{
			Color_t clrAccent = CFG::Menu_Accent_Primary;
			clrAccent.a = static_cast<byte>(clrAccent.a * flAlpha);
			H::Draw->String(font, nTextX, nY + 8, clrAccent, POS_DEFAULT, "Friend request");
			H::Draw->String(font, nTextX, nY + 8 + font.m_nTall + 2, clrTitle, POS_DEFAULT, "%s", strName.c_str());
		}
		else
		{
			H::Draw->String(font, nTextX, nY + 8, clrTitle, POS_DEFAULT, "%s", strName.c_str());
			std::string strBody = NetSanitize(note.m_strText);
			if (strBody.size() > 64)
				strBody = strBody.substr(0, 64);
			H::Draw->String(font, nTextX, nY + 8 + font.m_nTall + 2, clrBody, POS_DEFAULT, "%s", strBody.c_str());
		}
		H::Draw->EndClipping();
	}
}

void CMenu::AddToast(const std::string &strText)
{
	m_vecToasts.push_back({ strText, std::chrono::steady_clock::now() });
	while (m_vecToasts.size() > 4)
		m_vecToasts.erase(m_vecToasts.begin());
}

void CMenu::DrawToasts()
{
	const auto tpNow = std::chrono::steady_clock::now();
	constexpr float flLifetime = 5.0f;
	constexpr float flFade = 0.5f;

	while (!m_vecToasts.empty() && std::chrono::duration<float>(tpNow - m_vecToasts.front().m_tpBorn).count() > flLifetime)
		m_vecToasts.erase(m_vecToasts.begin());

	if (m_vecToasts.empty())
		return;

	const auto &font = H::Fonts->Get(EFonts::Menu);
	const int nW = 280;
	const int nH = 34;
	int nY = H::Draw->GetScreenH() - 10;

	for (auto it = m_vecToasts.rbegin(); it != m_vecToasts.rend(); ++it)
	{
		const float flAge = std::chrono::duration<float>(tpNow - it->m_tpBorn).count();
		float flAlpha = 1.0f;
		if (flAge > flLifetime - flFade)
			flAlpha = std::max(0.0f, 1.0f - (flAge - (flLifetime - flFade)) / flFade);

		nY -= nH + 6;

		Color_t clrBg = CFG::Menu_Background;
		clrBg.a = static_cast<byte>(250.0f * flAlpha);
		Color_t clrOutline = CFG::Menu_Accent_Primary;
		clrOutline.a = static_cast<byte>(clrOutline.a * flAlpha);
		Color_t clrText = CFG::Menu_Text_Active;
		clrText.a = static_cast<byte>(clrText.a * flAlpha);

		H::Draw->Rect(10, nY, nW, nH, clrBg);
		H::Draw->OutlinedRect(10, nY, nW, nH, clrOutline);
		H::Draw->String(font, 18, nY + (nH - font.m_nTall) / 2, clrText, POS_DEFAULT, "%s", it->m_strText.c_str());
	}
}

void CMenu::Run()
{
	U::Storage->Update();
	ProcessAutosave();
	CheckAllPlayersSourcebans(NetLocalSteamId64());
	if (CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot())
	{
		return;
	}
	if (!H::Input->IsGameFocused() && m_bOpen) {
		m_bOpen = false;
		I::MatSystemSurface->SetCursorAlwaysVisible(false);
		F::PacMan->NotifyVisible(false);
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
	float currentTime = I::EngineClient->Time();
	float deltaTime = currentTime - m_flLastFrameTime;
	if (deltaTime > 0.1f) deltaTime = 0.1f;
	m_flLastFrameTime = currentTime;
	m_animator.Update(deltaTime);
	m_particles.Update(deltaTime);
	float targetOpenProgress = m_bOpen ? 1.0f : 0.0f;
	float openSpeed = 6.0f;
	m_flMenuOpenProgress += (targetOpenProgress - m_flMenuOpenProgress) * openSpeed * deltaTime;
	m_flMenuOpenProgress = std::max(0.0f, std::min(1.0f, m_flMenuOpenProgress));
	if (m_bWasOpen && !m_bOpen)
	{
		m_buttonHoverStates.clear();
		m_buttonPressStates.clear();
	}
	m_bWasOpen = m_bOpen;
	if (!m_bOpen)
		F::PacMan->NotifyVisible(false);
	Indicators();
	if (m_bOpen)
	{
		if (!m_bWantTextInput)
			H::Input->ClearChars();
		m_bClickConsumed = false;
		m_bWantTextInput = false;
		HandleAchievementConfirmInput();
		H::LateRender->Clear();
		MainWindow();
		H::LateRender->DrawAll();
		DrawAchievementConfirm();
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
		{
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
		DrawNetNotifications();
		DrawToasts();
	}
CMenu::CMenu()
{
}
void CMenu::MarkConfigChanged()
{
	m_bConfigChanged = true;
	m_flLastChangeTime = I::EngineClient->Time();
}
void CMenu::ProcessAutosave()
{
	if (!m_bConfigChanged)
		return;
	float flCurrentTime = I::EngineClient->Time();
	if (flCurrentTime - m_flLastChangeTime >= AUTOSAVE_DELAY)
	{
		U::Storage->DoAutosave();
		m_bConfigChanged = false;
	}
}
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
	int gbWidth = 150;
	int colSpacing = CFG::Menu_Spacing_X * 4;
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
	std::stable_sort(boxesInColumn.begin(), boxesInColumn.end(), [this](const std::string& a, const std::string& b) {
		return m_mapGroupBoxes[a].m_nOrderInColumn < m_mapGroupBoxes[b].m_nOrderInColumn;
	});
	for (int i = 0; i < static_cast<int>(boxesInColumn.size()); i++)
	{
		m_mapGroupBoxes[boxesInColumn[i]].m_nOrderInColumn = i;
	}
}
int CMenu::CalculateGroupBoxInsertionIndex(const std::string& szTab, EGroupBoxColumn nColumn, int nContentY, int nMouseY, const std::string& szExcludeId)
{
	std::vector<DraggableGroupBox_t*> boxesInColumn;
	for (auto& pair : m_mapGroupBoxes)
	{
		if (pair.first != szExcludeId &&
			pair.second.m_szId.find(szTab + "_") == 0 &&
			pair.second.m_nColumn == nColumn)
		{
			boxesInColumn.push_back(&pair.second);
		}
	}
	std::sort(boxesInColumn.begin(), boxesInColumn.end(), [](DraggableGroupBox_t* a, DraggableGroupBox_t* b) {
		return a->m_nOrderInColumn < b->m_nOrderInColumn;
	});
	int insertionIndex = 0;
	int calcY = nContentY;
	for (size_t i = 0; i < boxesInColumn.size(); i++)
	{
		int boxHeight = boxesInColumn[i]->m_nHeight > 0 ? boxesInColumn[i]->m_nHeight : 80;
		int boxMidY = calcY + boxHeight / 2;
		if (nMouseY > boxMidY)
		{
			insertionIndex = static_cast<int>(i) + 1;
		}
		calcY += boxHeight + CFG::Menu_Spacing_Y;
	}
	return insertionIndex;
}
void CMenu::HandleAchievementConfirmInput()
{
	if (m_eConfirmAction == EConfirmAction::NONE)
		return;
	const int nDlgW = 260;
	const int nDlgH = 100;
	const int nDlgX = CFG::Menu_Pos_X + (CFG::Menu_Width - nDlgW) / 2;
	const int nDlgY = CFG::Menu_Pos_Y + (CFG::Menu_Height - nDlgH) / 2;
	const int nBtnW = 70;
	const int nBtnH = 20;
	const int nBtnY = nDlgY + nDlgH - nBtnH - 10;
	const int nYesX = nDlgX + (nDlgW / 2) - nBtnW - 8;
	const int nNoX = nDlgX + (nDlgW / 2) + 8;
	if (H::Input->IsPressed(VK_LBUTTON))
	{
		m_bClickConsumed = true;
		const int mx = H::Input->GetMouseX();
		const int my = H::Input->GetMouseY();
		const bool bYes = (mx >= nYesX && mx <= nYesX + nBtnW && my >= nBtnY && my <= nBtnY + nBtnH);
		const bool bNo = (mx >= nNoX && mx <= nNoX + nBtnW && my >= nBtnY && my <= nBtnY + nBtnH);
		if (bYes)
		{
			if (m_eConfirmAction == EConfirmAction::UNLOCK_ACHIEVEMENTS)
			{
				const auto res = Achievements::UnlockAll();
				m_strAchievementStatus = res.bSuccess ? "Unlocked: " + std::to_string(res.nCount) : std::string("Failed: ") + res.szReason;
			}
			else if (m_eConfirmAction == EConfirmAction::LOCK_ACHIEVEMENTS)
			{
				const auto res = Achievements::LockAll();
				m_strAchievementStatus = res.bSuccess ? "Locked: " + std::to_string(res.nCount) : std::string("Failed: ") + res.szReason;
			}
			else if (m_eConfirmAction == EConfirmAction::AUTH_LOGIN || m_eConfirmAction == EConfirmAction::AUTH_REGISTER)
			{
				F::NecroNet->SubmitAuth();
			}
			m_eConfirmAction = EConfirmAction::NONE;
		}
		else if (bNo)
		{
			m_eConfirmAction = EConfirmAction::NONE;
		}
	}
}
void CMenu::DrawAchievementConfirm()
{
	if (m_eConfirmAction == EConfirmAction::NONE)
		return;
	const bool bAuth = (m_eConfirmAction == EConfirmAction::AUTH_LOGIN || m_eConfirmAction == EConfirmAction::AUTH_REGISTER);
	const int nDlgW = 260;
	const int nDlgH = bAuth ? 130 : 100;
	const int nDlgX = CFG::Menu_Pos_X + (CFG::Menu_Width - nDlgW) / 2;
	const int nDlgY = CFG::Menu_Pos_Y + (CFG::Menu_Height - nDlgH) / 2;
	const int nBtnW = 70;
	const int nBtnH = 20;
	const int nBtnY = nDlgY + nDlgH - nBtnH - 10;
	const int nYesX = nDlgX + (nDlgW / 2) - nBtnW - 8;
	const int nNoX = nDlgX + (nDlgW / 2) + 8;
	Color_t clr = CFG::Menu_Accent_Primary;
	H::Draw->Rect(CFG::Menu_Pos_X, CFG::Menu_Pos_Y, CFG::Menu_Width, CFG::Menu_Height, { 0, 0, 0, 150 });
	H::Draw->Rect(nDlgX, nDlgY, nDlgW, nDlgH, CFG::Menu_Background);
	H::Draw->OutlinedRect(nDlgX, nDlgY, nDlgW, nDlgH, clr);
	H::Draw->Line(nDlgX, nDlgY + 26, nDlgX + nDlgW, nDlgY + 26, clr);
	H::Draw->String(H::Fonts->Get(EFonts::Menu), nDlgX + (nDlgW / 2), nDlgY + 8, CFG::Menu_Text_Active, POS_CENTERX,
		m_eConfirmAction == EConfirmAction::UNLOCK_ACHIEVEMENTS ? "Unlock All Achievements" :
		(m_eConfirmAction == EConfirmAction::LOCK_ACHIEVEMENTS ? "Lock All Achievements" :
		(m_eConfirmAction == EConfirmAction::AUTH_REGISTER ? "Create Account" : "Sign In")));
	if (bAuth)
	{
		H::Draw->String(H::Fonts->Get(EFonts::Menu), nDlgX + (nDlgW / 2), nDlgY + 34, CFG::Menu_Text_Active, POS_CENTERX, "username : %s", NetSanitize(F::NecroNet->m_strUsername).c_str());
		H::Draw->String(H::Fonts->Get(EFonts::Menu), nDlgX + (nDlgW / 2), nDlgY + 48, CFG::Menu_Text_Active, POS_CENTERX, "password : %s", NetSanitize(F::NecroNet->m_strPassword).c_str());
		H::Draw->String(H::Fonts->Get(EFonts::Menu), nDlgX + (nDlgW / 2), nDlgY + 66, CFG::Menu_Text_Inactive, POS_CENTERX,
			m_eConfirmAction == EConfirmAction::AUTH_REGISTER ? "are you sure you want to create an account?" : "are you sure you want to login in this account?");
	}
	else
	{
		H::Draw->String(H::Fonts->Get(EFonts::Menu), nDlgX + (nDlgW / 2), nDlgY + 34, CFG::Menu_Text_Inactive, POS_CENTERX, "Are you sure you want to do this?");
	}
	const int mx = H::Input->GetMouseX();
	const int my = H::Input->GetMouseY();
	const bool bYesHover = (mx >= nYesX && mx <= nYesX + nBtnW && my >= nBtnY && my <= nBtnY + nBtnH);
	const bool bNoHover = (mx >= nNoX && mx <= nNoX + nBtnW && my >= nBtnY && my <= nBtnY + nBtnH);
	auto DrawBtn = [&](int x, const char* szLabel, bool bHover)
	{
		H::Draw->Rect(x, nBtnY, nBtnW, nBtnH, { clr.r, clr.g, clr.b, static_cast<byte>(bHover ? 70 : 30) });
		H::Draw->OutlinedRect(x, nBtnY, nBtnW, nBtnH, clr);
		H::Draw->String(H::Fonts->Get(EFonts::Menu), x + (nBtnW / 2), nBtnY + (nBtnH / 2),
			bHover ? CFG::Menu_Text_Active : CFG::Menu_Text_Inactive, POS_CENTERXY, szLabel);
	};
	DrawBtn(nYesX, "Yes", bYesHover);
	DrawBtn(nNoX, "No", bNoHover);
}
void CMenu::HandleGroupBoxDrag()
{
	if (!m_bIsDraggingGroupBox)
		return;
	if (!H::Input->IsHeld(VK_LBUTTON))
	{
		m_bIsDraggingGroupBox = false;
		m_bShowDropZones = false;
		if (m_mapGroupBoxes.find(m_strDraggingGroupBox) != m_mapGroupBoxes.end())
		{
			auto& gb = m_mapGroupBoxes[m_strDraggingGroupBox];
			EGroupBoxColumn oldColumn = gb.m_nColumn;
			EGroupBoxColumn targetColumn = m_nHoveredDropColumn;
			std::string szTab = m_strDraggingGroupBox.substr(0, m_strDraggingGroupBox.find('_'));
			if (szTab == "Exploits")
			{
				int nVeryBigInTarget = 0;
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
							case EGroupBoxSize::VERY_BIG: nVeryBigInTarget++; break;
							case EGroupBoxSize::BIG: nBigInTarget++; break;
							case EGroupBoxSize::MEDIUM: nMediumInTarget++; break;
							case EGroupBoxSize::SMALL: nSmallInTarget++; break;
							case EGroupBoxSize::EXTRA_SMALL: nExtraSmallInTarget++; break;
						}
					}
				}
				int nNewVeryBig = nVeryBigInTarget + (gb.m_eSize == EGroupBoxSize::VERY_BIG ? 1 : 0);
				int nNewBig = nBigInTarget + (gb.m_eSize == EGroupBoxSize::BIG ? 1 : 0);
				int nNewMedium = nMediumInTarget + (gb.m_eSize == EGroupBoxSize::MEDIUM ? 1 : 0);
				int nNewSmall = nSmallInTarget + (gb.m_eSize == EGroupBoxSize::SMALL ? 1 : 0);
				int nNewExtraSmall = nExtraSmallInTarget + (gb.m_eSize == EGroupBoxSize::EXTRA_SMALL ? 1 : 0);
				bool bHasShifting = false;
				bool bHasCrithack = false;
				bool bHasFakeLag = false;
				bool bHasNoSpread = false;
				bool bHasRegionSelector = false;
				bool bHasAntiAim = false;
				for (auto& pair : m_mapGroupBoxes)
				{
					if (pair.second.m_szId.find("Exploits_") == 0 && pair.second.m_nColumn == targetColumn)
					{
						if (pair.first == "Exploits_Shifting" || (pair.first == m_strDraggingGroupBox && m_strDraggingGroupBox == "Exploits_Shifting"))
							bHasShifting = true;
						if (pair.first == "Exploits_Crithack" || (pair.first == m_strDraggingGroupBox && m_strDraggingGroupBox == "Exploits_Crithack"))
							bHasCrithack = true;
						if (pair.first == "Exploits_FakeLag" || (pair.first == m_strDraggingGroupBox && m_strDraggingGroupBox == "Exploits_FakeLag"))
							bHasFakeLag = true;
						if (pair.first == "Exploits_No Spread" || (pair.first == m_strDraggingGroupBox && m_strDraggingGroupBox == "Exploits_No Spread"))
							bHasNoSpread = true;
						if (pair.first == "Exploits_Region Selector" || (pair.first == m_strDraggingGroupBox && m_strDraggingGroupBox == "Exploits_Region Selector"))
							bHasRegionSelector = true;
						if (pair.first == "Exploits_AntiAim" || (pair.first == m_strDraggingGroupBox && m_strDraggingGroupBox == "Exploits_AntiAim"))
							bHasAntiAim = true;
					}
				}
				if (m_strDraggingGroupBox == "Exploits_Shifting") bHasShifting = true;
				if (m_strDraggingGroupBox == "Exploits_Crithack") bHasCrithack = true;
				if (m_strDraggingGroupBox == "Exploits_FakeLag") bHasFakeLag = true;
				if (m_strDraggingGroupBox == "Exploits_No Spread") bHasNoSpread = true;
				if (m_strDraggingGroupBox == "Exploits_Region Selector") bHasRegionSelector = true;
				if (m_strDraggingGroupBox == "Exploits_AntiAim") bHasAntiAim = true;
				bool bValidPlacement = false;
				if (bHasShifting)
				{
					bool bHasDisallowed = bHasRegionSelector || bHasAntiAim;
					int nOtherCount = (bHasCrithack ? 1 : 0) + (bHasFakeLag ? 1 : 0) + (bHasNoSpread ? 1 : 0);
					if (!bHasDisallowed)
					{
						if (nOtherCount == 0)
						{
							bValidPlacement = true;
						}
						else if (nOtherCount == 1 && bHasCrithack)
						{
							bValidPlacement = true;
						}
						else if (nOtherCount == 1 && bHasFakeLag)
						{
							bValidPlacement = true;
						}
						else if (nOtherCount == 1 && bHasNoSpread)
						{
							bValidPlacement = true;
						}
						else if (nOtherCount == 2 && bHasFakeLag && bHasNoSpread)
						{
							bValidPlacement = true;
						}
					}
				}
				if (nNewVeryBig == 0 && nNewBig == 1 && nNewMedium <= 1 && nNewSmall == 0 && nNewExtraSmall <= 1)
					bValidPlacement = true;
				if (nNewVeryBig == 0 && nNewBig == 1 && nNewMedium == 0 && nNewSmall <= 1 && nNewExtraSmall <= 1)
					bValidPlacement = true;
				if (nNewVeryBig == 0 && nNewBig == 0 && nNewMedium <= 2 && nNewSmall <= 1 && nNewExtraSmall <= 1)
					bValidPlacement = true;
				int totalInColumn = nNewVeryBig + nNewBig + nNewMedium + nNewSmall + nNewExtraSmall;
				if (totalInColumn <= 1 && !bHasShifting)
					bValidPlacement = true;
				if (!bValidPlacement)
					targetColumn = oldColumn;
			}
			else if (szTab == "Misc")
			{
				auto isBigSection = [](const std::string& id) -> bool {
					return id == "Misc_Game" || id == "Misc_Auto" || id == "Misc_Movement" ||
					       id == "Misc_Taunt" || id == "Misc_Misc" || id == "Misc_Chat" || id == "Misc_Triggerbot";
				};
				auto isSmallSection = [](const std::string& id) -> bool {
					return id == "Misc_Mann vs. Machine" || id == "Misc_Sound";
				};
				auto isExtraLargeSection = [](const std::string& id) -> bool {
					return id == "Misc_Misc" || id == "Misc_Auto" || id == "Misc_Movement";
				};
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
				bool bDraggedIsBig = isBigSection(m_strDraggingGroupBox);
				bool bDraggedIsSmall = isSmallSection(m_strDraggingGroupBox);
				bool bDraggedIsExtraLarge = isExtraLargeSection(m_strDraggingGroupBox);
				int nNewBig = nBigInTarget + (bDraggedIsBig ? 1 : 0);
				int nNewSmall = nSmallInTarget + (bDraggedIsSmall ? 1 : 0);
				int nNewExtraLarge = nExtraLargeInTarget + (bDraggedIsExtraLarge ? 1 : 0);
				bool bValidPlacement = false;
				if (nNewBig <= 3 && nNewSmall <= 1)
					bValidPlacement = true;
				else if (nNewBig <= 2 && nNewSmall <= 2)
					bValidPlacement = true;
				if (nNewExtraLarge >= 3)
					bValidPlacement = false;
				if (!bValidPlacement)
					targetColumn = oldColumn;
			}
			else if (szTab == "NavBot")
			{
				int nBoxesInTarget = 0;
				for (auto& pair : m_mapGroupBoxes)
				{
					if (pair.first != m_strDraggingGroupBox &&
					    pair.second.m_szId.find("NavBot_") == 0 &&
					    pair.second.m_nColumn == targetColumn)
					{
						nBoxesInTarget++;
					}
				}
				if (nBoxesInTarget >= 3)
					targetColumn = oldColumn;
			}
			if (targetColumn != oldColumn)
			{
				std::vector<const DraggableGroupBox_t*> vecTarget;
				for (auto& pair : m_mapGroupBoxes)
				{
					if (pair.first != m_strDraggingGroupBox &&
						pair.second.m_szId.find(szTab + "_") == 0 &&
						pair.second.m_nColumn == targetColumn)
					{
						vecTarget.push_back(&pair.second);
					}
				}
				std::sort(vecTarget.begin(), vecTarget.end(), [](const DraggableGroupBox_t* a, const DraggableGroupBox_t* b) {
					return a->m_nOrderInColumn < b->m_nOrderInColumn;
				});
				const int nInsert = CalculateGroupBoxInsertionIndex(szTab, targetColumn, m_nDragContentY, H::Input->GetMouseY(), m_strDraggingGroupBox);
				const auto itDragged = m_mapGroupBoxes.find(m_strDraggingGroupBox);
				int nCursor = m_nDragContentY;
				bool bInserted = false;
				for (int i = 0; i <= static_cast<int>(vecTarget.size()); i++)
				{
					if (!bInserted && i == nInsert)
					{
						const int nH = (itDragged != m_mapGroupBoxes.end() && itDragged->second.m_nHeight > 0) ? itDragged->second.m_nHeight : 80;
						nCursor += nH - CFG::Menu_Spacing_Y;
						bInserted = true;
					}
					if (i < static_cast<int>(vecTarget.size()))
						nCursor += (vecTarget[i]->m_nHeight > 0 ? vecTarget[i]->m_nHeight : 80) - CFG::Menu_Spacing_Y;
				}
				if (nCursor - CFG::Menu_Spacing_Y > m_nDragContentY + m_nDragContentH)
					targetColumn = oldColumn;
			}
			int my = H::Input->GetMouseY();
			int insertionIndex = CalculateGroupBoxInsertionIndex(szTab, targetColumn, m_nDragContentY, my, m_strDraggingGroupBox);
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
			std::sort(boxesInColumn.begin(), boxesInColumn.end(), [](DraggableGroupBox_t* a, DraggableGroupBox_t* b) {
				return a->m_nOrderInColumn < b->m_nOrderInColumn;
			});
			std::vector<std::string> newOrder;
			for (size_t i = 0; i < boxesInColumn.size(); i++)
			{
				if (static_cast<int>(i) == insertionIndex)
				{
					newOrder.push_back(m_strDraggingGroupBox);
				}
				newOrder.push_back(boxesInColumn[i]->m_szId);
			}
			if (insertionIndex >= static_cast<int>(boxesInColumn.size()))
			{
				newOrder.push_back(m_strDraggingGroupBox);
			}
			for (size_t i = 0; i < newOrder.size(); i++)
			{
				m_mapGroupBoxes[newOrder[i]].m_nOrderInColumn = static_cast<int>(i);
			}
			gb.m_nColumn = targetColumn;
			if (oldColumn != targetColumn)
			{
				ReorderGroupBoxesInColumn(szTab, oldColumn);
			}
			auto SaveGroupBoxConfig = [](const std::string& id, EGroupBoxColumn col, int order) {
				int configValue = static_cast<int>(col) * 100 + order;
				if (id == "Misc_Misc") CFG::Menu_GroupBox_Misc_Misc = configValue;
				else if (id == "Misc_Game") CFG::Menu_GroupBox_Misc_Game = configValue;
				else if (id == "Misc_Mann vs. Machine") CFG::Menu_GroupBox_Misc_MvM = configValue;
				else if (id == "Misc_Chat") CFG::Menu_GroupBox_Misc_Chat = configValue;
				else if (id == "Misc_Taunt") CFG::Menu_GroupBox_Misc_Taunt = configValue;
				else if (id == "Misc_Auto") CFG::Menu_GroupBox_Misc_Auto = configValue;
				else if (id == "Misc_Movement") CFG::Menu_GroupBox_Misc_Movement = configValue;
				else if (id == "Misc_Sound") CFG::Menu_GroupBox_Misc_Sound = configValue;
				else if (id == "Exploits_Shifting") CFG::Menu_GroupBox_Exploits_Shifting = configValue;
				else if (id == "Exploits_FakeLag") CFG::Menu_GroupBox_Exploits_FakeLag = configValue;
				else if (id == "Exploits_AntiAim") CFG::Menu_GroupBox_Exploits_AntiAim = configValue;
				else if (id == "Exploits_Crithack") CFG::Menu_GroupBox_Exploits_Crits = configValue;
				else if (id == "Exploits_No Spread") CFG::Menu_GroupBox_Exploits_NoSpread = configValue;
				else if (id == "Exploits_Region Selector") CFG::Menu_GroupBox_Exploits_RegionSelector = configValue;
				else if (id == "NavBot_General") CFG::Menu_GroupBox_NavBot_General = configValue;
				else if (id == "NavBot_Movement") CFG::Menu_GroupBox_NavBot_Movement = configValue;
				else if (id == "NavBot_Preferences") CFG::Menu_GroupBox_NavBot_Preferences = configValue;
				else if (id == "NavBot_Behavior") CFG::Menu_GroupBox_NavBot_Preferences = configValue;
				else if (id == "NavBot_Debug") CFG::Menu_GroupBox_NavBot_Debug = configValue;
				else if (id == "NavBot_Weapon") CFG::Menu_GroupBox_NavBot_Weapon = configValue;
				else if (id == "NavBot_Auto Scope") CFG::Menu_GroupBox_NavBot_AutoScope = configValue;
				else if (id == "NavBot_EXTREME Perf") CFG::Menu_GroupBox_NavBot_Performance = configValue;
			};
			for (const auto& boxId : newOrder)
			{
				auto& box = m_mapGroupBoxes[boxId];
				SaveGroupBoxConfig(boxId, box.m_nColumn, box.m_nOrderInColumn);
			}
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
	m_nDragContentY = nContentY;
	m_nDragContentH = nContentH;
	HandleGroupBoxDrag();
	if (m_bIsDraggingGroupBox)
	{
		m_nHoveredDropColumn = GetColumnFromMouseX(nContentX, nContentW);
		m_bShowDropZones = true;
	}
	std::vector<DraggableGroupBox_t*> tabBoxes;
	for (auto& pair : m_mapGroupBoxes)
	{
		if (pair.second.m_szId.find(szTab + "_") == 0)
		{
			tabBoxes.push_back(&pair.second);
		}
	}
	std::sort(tabBoxes.begin(), tabBoxes.end(), [](DraggableGroupBox_t* a, DraggableGroupBox_t* b) {
		if (a->m_nColumn != b->m_nColumn)
			return static_cast<int>(a->m_nColumn) < static_cast<int>(b->m_nColumn);
		return a->m_nOrderInColumn < b->m_nOrderInColumn;
	});
	int gbWidth = 150;
	int colSpacing = CFG::Menu_Spacing_X * 4;
	int columnX[3] = {
		nContentX,
		nContentX + gbWidth + colSpacing,
		nContentX + (gbWidth + colSpacing) * 2
	};
	int columnY[3] = { nContentY, nContentY, nContentY };
	int insertionCol = -1;
	int insertionOrder = -1;
	int draggedBoxHeight = 0;
	if (m_bIsDraggingGroupBox)
	{
		int my = H::Input->GetMouseY();
		insertionCol = static_cast<int>(m_nHoveredDropColumn);
		insertionOrder = CalculateGroupBoxInsertionIndex(szTab, m_nHoveredDropColumn, nContentY, my, m_strDraggingGroupBox);
		auto draggedIt = m_mapGroupBoxes.find(m_strDraggingGroupBox);
		if (draggedIt != m_mapGroupBoxes.end())
		{
			draggedBoxHeight = draggedIt->second.m_nHeight > 0 ? draggedIt->second.m_nHeight : 80;
		}
	}
	bool insertionDrawn = false;
	int insertionY = nContentY;
	int logicalIndex[3] = { 0, 0, 0 };
	for (auto* gb : tabBoxes)
	{
		bool isDragging = (m_bIsDraggingGroupBox && m_strDraggingGroupBox == gb->m_szId);
		int col = static_cast<int>(gb->m_nColumn);
		int boxX, boxY;
		if (isDragging)
		{
			boxX = H::Input->GetMouseX() - m_nDragOffsetX;
			boxY = H::Input->GetMouseY() - m_nDragOffsetY;
		}
		else
		{
			boxX = columnX[col];
			boxY = columnY[col];
			if (m_bIsDraggingGroupBox && col == insertionCol && !insertionDrawn)
			{
				if (logicalIndex[col] >= insertionOrder)
				{
					insertionY = boxY;
					Color_t placeholderColor = { 128, 128, 128, 50 };
					H::Draw->Rect(boxX, boxY, gbWidth, draggedBoxHeight, placeholderColor);
					Color_t borderColor = { 150, 150, 150, 80 };
					H::Draw->Line(boxX, boxY, boxX + gbWidth, boxY, borderColor);
					H::Draw->Line(boxX, boxY + draggedBoxHeight, boxX + gbWidth, boxY + draggedBoxHeight, borderColor);
					H::Draw->Line(boxX, boxY, boxX, boxY + draggedBoxHeight, borderColor);
					H::Draw->Line(boxX + gbWidth, boxY, boxX + gbWidth, boxY + draggedBoxHeight, borderColor);
					boxY += draggedBoxHeight + CFG::Menu_Spacing_Y;
					columnY[col] = boxY;
					insertionDrawn = true;
				}
			}
		}
		gb->m_nRenderX = boxX;
		gb->m_nRenderY = boxY;
		m_nCursorX = boxX;
		m_nCursorY = boxY;
		int dragHandleH = 10;
		int dragHandleY = boxY - CFG::Menu_Spacing_Y * 3 + 9;
		int dragHandleW = 147;
		int dragHandleX = boxX + 3;
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
		if (headerHovered && !m_bIsDraggingGroupBox)
		{
			Color_t handleColor = { CFG::Menu_Accent_Primary.r, CFG::Menu_Accent_Primary.g, CFG::Menu_Accent_Primary.b, 60 };
			H::Draw->Rect(dragHandleX, dragHandleY, dragHandleW, dragHandleH, handleColor);
		}
		if (isDragging)
		{
			Color_t shadowColor = { 0, 0, 0, 40 };
			H::Draw->Rect(boxX + 3, boxY + 3, gb->m_nWidth, gb->m_nHeight > 0 ? gb->m_nHeight : 100, shadowColor);
		}
		GroupBoxStart(gb->m_szLabel.c_str(), gb->m_nWidth);
		if (gb->m_fnRenderContent)
			gb->m_fnRenderContent();
		GroupBoxEnd();
		gb->m_nHeight = m_nCursorY - startY + CFG::Menu_Spacing_Y * 2;
		if (!isDragging)
		{
			columnY[col] = m_nCursorY + CFG::Menu_Spacing_Y;
			logicalIndex[col]++;
		}
	}
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
