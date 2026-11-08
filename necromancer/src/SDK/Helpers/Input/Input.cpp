#include "Input.h"

#include "../../SDK.h"

std::string CInput::DrainCharsUtf8()
{
	std::string strOut;

	size_t nCount = m_wsPendingChars.size();
	if (!nCount)
		return strOut;

	if (m_wsPendingChars[nCount - 1] >= 0xD800 && m_wsPendingChars[nCount - 1] <= 0xDBFF)
		nCount--;

	if (nCount)
	{
		const int nUtf8 = WideCharToMultiByte(CP_UTF8, 0, m_wsPendingChars.data(), static_cast<int>(nCount), nullptr, 0, nullptr, nullptr);
		if (nUtf8 > 0)
		{
			strOut.resize(static_cast<size_t>(nUtf8));
			WideCharToMultiByte(CP_UTF8, 0, m_wsPendingChars.data(), static_cast<int>(nCount), strOut.data(), nUtf8, nullptr, nullptr);
		}
		m_wsPendingChars.erase(m_wsPendingChars.begin(), m_wsPendingChars.begin() + static_cast<ptrdiff_t>(nCount));
	}
	else
		m_wsPendingChars.clear();

	return strOut;
}

bool CInput::IsPressedAndHeld(short key)
{
	auto Now = std::chrono::steady_clock::now();

	static std::chrono::time_point<std::chrono::steady_clock> KeyTimes[256] = { Now };

	if (m_Keys[key] == PRESSED) {
		KeyTimes[key] = Now;
		return true;
	}

	if (m_Keys[key] == HELD && std::chrono::duration_cast<std::chrono::milliseconds>(Now - KeyTimes[key]).count() > 400) {
		KeyTimes[key] = Now;
		return true;
	}

	return false;
}

void CInput::Update()
{
	m_bGameFocused = SDKUtils::IsGameWindowInFocus();

	for (int n = 0; n < 256; n++)
	{
		if (!m_bGameFocused) {
			m_Keys[n] = NONE;
			continue;
		}

		if (GetAsyncKeyState(n) & 0x8000)
		{
			if (m_Keys[n] == PRESSED)
				m_Keys[n] = HELD;

			else if (m_Keys[n] != HELD)
				m_Keys[n] = PRESSED;
		}

		else m_Keys[n] = NONE;
	}

	I::MatSystemSurface->SurfaceGetCursorPos(m_nMouseX, m_nMouseY);
}