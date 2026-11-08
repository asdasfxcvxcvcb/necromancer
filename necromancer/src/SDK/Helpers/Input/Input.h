#pragma once

#include <chrono>
#include <string>

#include "../../TF2/IMatSystemSurface.h"

class CInput
{
private:
	enum EKeyState { NONE, PRESSED, HELD };
	EKeyState m_Keys[256] = {};
	int m_nMouseX = 0, m_nMouseY = 0;
	bool m_bGameFocused = false;
	std::wstring m_wsPendingChars;
	std::chrono::steady_clock::time_point m_tpLastChar = std::chrono::steady_clock::now();

public:
	inline bool IsPressed(short key) { return m_Keys[key] == PRESSED; }
	inline bool IsHeld(short key) { return m_Keys[key] == HELD; }
	inline bool IsDown(short key) { return IsPressed(key) || IsHeld(key); }
	inline int GetMouseX() { return m_nMouseX; }
	inline int GetMouseY() { return m_nMouseY; }
	inline bool IsGameFocused() { return m_bGameFocused; }
	void PushChar(unsigned int nUnit)
	{
		m_wsPendingChars.push_back(static_cast<wchar_t>(nUnit));
		m_tpLastChar = std::chrono::steady_clock::now();
	}
	void ClearChars() { m_wsPendingChars.clear(); }
	bool CharsRecent() const { return std::chrono::steady_clock::now() - m_tpLastChar < std::chrono::seconds(1); }
	std::string DrainCharsUtf8();

public:
	bool IsPressedAndHeld(short key);

public:
	void Update();
};

MAKE_SINGLETON_SCOPED(CInput, Input, H);