#include "../../../SDK/SDK.h"
#include "NetSteam.h"

#pragma comment(lib, "shell32.lib")

namespace
{
	constexpr char NET_STEAMUSER_VERSION[] = "SteamUser018";

	using FnGetHSteamUser = int (*)();
	using FnFindOrCreateUserInterface = void* (*)(int, const char*);

	HSteamPipe g_hSteamPipe = 0;
	HSteamUser g_hSteamUser = 0;
	INetSteamUser* g_pSteamUser = nullptr;
	unsigned long long g_ullSteamUserId = 0;

	HMODULE GetSteamApiModule()
	{
		if (const HMODULE hSteamApi = GetModuleHandleA("steam_api64.dll"))
			return hSteamApi;

		return GetModuleHandleA("steam_api.dll");
	}
}

bool RefreshSteamSession()
{
	if (!I::SteamClient)
		return false;

	if (g_pSteamUser && g_hSteamUser && g_hSteamPipe)
	{
		if (auto pCheck = static_cast<INetSteamUser*>(I::SteamClient->GetISteamUser(g_hSteamUser, g_hSteamPipe, NET_STEAMUSER_VERSION)))
		{
			const unsigned long long ullNow = pCheck->GetSteamID().ConvertToUint64();
			if (ullNow && ullNow == g_ullSteamUserId)
				return false;
		}
	}

	if (g_hSteamPipe)
		I::SteamClient->BReleaseSteamPipe(g_hSteamPipe);
	g_hSteamPipe = 0;
	g_hSteamUser = 0;
	g_pSteamUser = nullptr;
	g_ullSteamUserId = 0;

	const HSteamPipe hPipe = I::SteamClient->CreateSteamPipe();
	if (!hPipe)
		return false;
	const HSteamUser hUser = I::SteamClient->ConnectToGlobalUser(hPipe);
	if (!hUser)
	{
		I::SteamClient->BReleaseSteamPipe(hPipe);
		return false;
	}

	g_hSteamPipe = hPipe;
	g_hSteamUser = hUser;
	g_pSteamUser = static_cast<INetSteamUser*>(I::SteamClient->GetISteamUser(hUser, hPipe, NET_STEAMUSER_VERSION));
	g_ullSteamUserId = g_pSteamUser ? g_pSteamUser->GetSteamID().ConvertToUint64() : 0;

	I::SteamFriends = I::SteamClient->GetISteamFriends(hUser, hPipe, STEAMFRIENDS_INTERFACE_VERSION);
	I::SteamUtils = I::SteamClient->GetISteamUtils(hPipe, STEAMUTILS_INTERFACE_VERSION);
	return true;
}

bool CNetSteam::Refresh()
{
	m_bAvailable = false;
	m_strSteamId.clear();
	m_hSteamApi = GetSteamApiModule();
	m_pSteamUser = AcquireSteamUser();

	if (!m_pSteamUser)
		return false;

	if (!m_pSteamUser->BLoggedOn())
		return false;

	const CSteamID steamId = m_pSteamUser->GetSteamID();
	const uint64 unSteamId = steamId.ConvertToUint64();
	if (!unSteamId)
		return false;

	m_strSteamId = std::to_string(unSteamId);
	m_bAvailable = true;
	return true;
}

bool CNetSteam::IsAvailable() const
{
	return m_bAvailable && m_pSteamUser != nullptr;
}

std::string CNetSteam::GetSteamId() const
{
	return m_strSteamId;
}

bool CNetSteam::OpenOverlayPage(const std::string& strUrl) const
{
	if (!IsOverlayAvailable() || strUrl.empty())
		return false;

	I::SteamFriends->ActivateGameOverlayToWebPage(strUrl.c_str(), 0);
	return true;
}

bool CNetSteam::OpenInBrowser(const std::string& strUrl) const
{
	if (strUrl.empty())
		return false;

	const std::wstring wsUrl = Utils::ConvertUtf8ToWide(strUrl);
	return reinterpret_cast<intptr_t>(ShellExecuteW(nullptr, L"open", wsUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

INetSteamUser* CNetSteam::AcquireSteamUser()
{
	RefreshSteamSession();

	if (g_pSteamUser && g_ullSteamUserId)
	{
		m_hSteamApi = GetSteamApiModule();
		return g_pSteamUser;
	}

	if (m_hSteamApi)
	{
		const auto fnGetHSteamUser = reinterpret_cast<FnGetHSteamUser>(GetProcAddress(m_hSteamApi, "SteamAPI_GetHSteamUser"));
		const auto fnFindOrCreate = reinterpret_cast<FnFindOrCreateUserInterface>(GetProcAddress(m_hSteamApi, "SteamInternal_FindOrCreateUserInterface"));
		if (fnGetHSteamUser && fnFindOrCreate)
		{
			if (const HSteamUser hUser = fnGetHSteamUser())
			{
				if (void* pUser = fnFindOrCreate(hUser, NET_STEAMUSER_VERSION))
					return static_cast<INetSteamUser*>(pUser);
			}
		}
	}

	return nullptr;
}

bool CNetSteam::IsOverlayAvailable() const
{
	return I::SteamFriends != nullptr;
}
