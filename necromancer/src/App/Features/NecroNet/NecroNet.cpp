#include "NecroNet.h"
#include "NetAvatar.h"
#include "../Menu/Menu.h"

#include <filesystem>
#include <fstream>
#include <utility>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winhttp.lib")

namespace
{
	constexpr int NET_HISTORY_CACHE_LINES = 512;
	constexpr int NET_MAX_EVENTS_PER_FRAME = 256;
	constexpr int NET_DM_MAX_DECRYPT_TRIES = 5;
	constexpr int NET_NOTIFY_MAX = 5;
	constexpr int NET_NOTIFY_LIFETIME_MS = 6000;

	std::string Trimmed(const std::string& strText)
	{
		const size_t nBegin = strText.find_first_not_of(" \t\r\n");
		if (nBegin == std::string::npos)
			return {};

		const size_t nEnd = strText.find_last_not_of(" \t\r\n");
		return strText.substr(nBegin, nEnd - nBegin + 1);
	}

	long long UnixNow()
	{
		return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

	bool FindFriend(const std::vector<NetUser>& vecFriends, const std::string& strUsername, NetUser& out)
	{
		for (const NetUser& user : vecFriends)
		{
			if (user.m_strUsername == strUsername)
			{
				out = user;
				return true;
			}
		}

		return false;
	}

	bool ValidOwnSteamId(const std::string& strId)
	{
		if (strId.size() != 17)
			return false;

		for (const char cChar : strId)
		{
			if (cChar < '0' || cChar > '9')
				return false;
		}

		return true;
	}

	void SweepLegacyAvatarFiles(const std::string& strFolder)
	{
		if (strFolder.empty())
			return;

		WIN32_FIND_DATAA wfd{};
		const std::string strPattern = strFolder + "\\*";
		HANDLE hFind = FindFirstFileA(strPattern.c_str(), &wfd);
		if (hFind == INVALID_HANDLE_VALUE)
			return;

		do
		{
			const std::string strFile = wfd.cFileName;
			const size_t nDot = strFile.rfind('.');
			if (nDot == std::string::npos)
				continue;

			if (strFile.substr(nDot) != ".txt")
				continue;

			if (!ValidOwnSteamId(strFile.substr(0, nDot)))
				continue;

			DeleteFileA((strFolder + "\\" + strFile).c_str());
		} while (FindNextFileA(hFind, &wfd));

		FindClose(hFind);
	}

	bool WriteProfile(const CNetStore& store, const CNetCrypto& crypto, const CNetClient& client, const std::string& strOwnSteamId, const NetUser& user, const std::string& strFallbackUsername)
	{
		NetProfile profile;
		profile.m_strToken = client.CopyToken();
		profile.m_strSteamId = strOwnSteamId;
		profile.m_strUsername = !user.m_strUsername.empty() ? user.m_strUsername : strFallbackUsername;
		profile.m_strName = user.m_strName;
		profile.m_strPubkey = crypto.GetPublicKeyB64();
		profile.m_strPrivKey = crypto.GetPrivateKeyB64();

		if (profile.m_strToken.empty() || profile.m_strPubkey.empty() || profile.m_strPrivKey.empty())
			return false;

		if (!ValidOwnSteamId(profile.m_strSteamId))
			return false;

		return store.SaveProfile(profile);
	}
}

void CNecroNet::Init()
{
	if (m_bInitialized)
		return;

	m_bInitialized = true;

	m_steam.Refresh();

	if (m_store.Init(U::Storage->GetWorkFolder().string()))
		SetFlow(ENetFlow::IDLE, "offline");
	else
		SetFlow(ENetFlow::IDLE, "storage unavailable");

	SweepLegacyAvatarFiles(GetAvatarFolder());

	bool bHaveProfile = false;
	NetProfile profile;
	if (m_store.LoadProfile(profile) && !profile.m_strToken.empty())
	{
		if (m_crypto.RestoreKeyPair(profile.m_strPrivKey))
		{
			bHaveProfile = true;
			m_strUsername = profile.m_strUsername;
		}
		else
		{
			m_store.ClearProfile();
			SetFlow(ENetFlow::IDLE, "stored identity is unreadable - connect again");
		}
	}

	if (!m_crypto.HasKeyPair() && !m_crypto.GenerateKeyPair())
		SetFlow(ENetFlow::IDLE, "crypto unavailable - login disabled");
	m_client.SetAccountPubkey(m_crypto.GetPublicKeyB64());

	m_client.Start();

	if (bHaveProfile && !profile.m_strToken.empty() && m_crypto.HasKeyPair())
	{
		m_bTokenTried = true;
		m_client.SetToken(profile.m_strToken);
		m_client.RequestMe();
		SetFlow(ENetFlow::LOGGING_IN, "restoring session...");
	}
}

void CNecroNet::Shutdown()
{
	if (!m_bInitialized)
		return;

	m_bInitialized = false;

	m_client.DisconnectWebSocket();
	m_client.Shutdown();
}

void CNecroNet::Run()
{
	if (!m_bInitialized)
		return;

	NetEvent ev;
	int nProcessed = 0;
	while (nProcessed++ < NET_MAX_EVENTS_PER_FRAME && m_client.PopEvent(ev))
		HandleEvent(ev);

	const auto tpNow = std::chrono::steady_clock::now();
	std::erase_if(m_mapTyping, [tpNow](const std::pair<const std::string, std::chrono::steady_clock::time_point>& pair)
	{
		return tpNow - pair.second > std::chrono::milliseconds(NET_TYPING_VISIBLE_MS);
	});
	TickNotifications();

	if (m_eFlow == ENetFlow::READY && tpNow - m_tpAvatarCheck > std::chrono::seconds(20))
	{
		m_tpAvatarCheck = tpNow;
		RefreshCaches();
		CheckAvatars();
	}
}

void CNecroNet::Connect()
{
	if (!m_bInitialized || m_eFlow != ENetFlow::IDLE)
		return;

	m_steam.Refresh();

	m_strOpenIdState = CNetCrypto::RandomHex(32);
	if (m_strOpenIdState.size() != 64)
	{
		SetFlow(ENetFlow::IDLE, "failed to start the steam auth flow");
		return;
	}

	m_client.BeginSteamPoll(m_strOpenIdState);

	const std::string strUrl = NetMakeSteamStartUrl(m_strOpenIdState);
	if (!m_steam.OpenOverlayPage(strUrl) && !m_steam.OpenInBrowser(strUrl))
	{
		m_client.CancelSteamPoll();
		m_strOpenIdState.clear();
		SetFlow(ENetFlow::IDLE, "could not open the steam auth page");
		return;
	}

	SetFlow(ENetFlow::AUTH_WAIT, m_steam.IsAvailable() ? "waiting for steam sign in..." : "sign in through the opened browser page...");
}

void CNecroNet::CancelConnect()
{
	if (!m_bInitialized)
		return;

	if (m_eFlow == ENetFlow::AUTH_WAIT)
	{
		m_client.CancelSteamPoll();
		m_strOpenIdState.clear();
		m_strConfirmedSteamId.clear();
		SetFlow(ENetFlow::IDLE, "connect cancelled");
		return;
	}

	if (m_eFlow == ENetFlow::LOGGING_IN)
	{
		if (m_bTokenTried)
			m_client.ClearToken();
		SetFlow(ENetFlow::IDLE, "connect cancelled");
	}
}

void CNecroNet::SubmitAuth()
{
	if (!m_bInitialized || m_eFlow != ENetFlow::AUTH_WAIT)
		return;

	const auto StripControls = [](const std::string &strIn)
	{
		std::string strOut;
		strOut.reserve(strIn.size());
		for (char ch : strIn)
		{
			const unsigned char uc = static_cast<unsigned char>(ch);
			if (uc >= 0x20 && uc != 0x7F)
				strOut += ch;
		}
		return strOut;
	};
	m_strUsername = Trimmed(StripControls(m_strUsername));
	m_strPassword = StripControls(m_strPassword);
	const std::string strUsername = m_strUsername;
	if (strUsername.size() < NET_USERNAME_MIN || strUsername.size() > NET_USERNAME_MAX)
	{
		SetFlow(ENetFlow::AUTH_WAIT, "username must be 3-16 characters");
		return;
	}

	if (m_strPassword.size() < NET_PASSWORD_MIN || m_strPassword.size() > NET_PASSWORD_MAX)
	{
		SetFlow(ENetFlow::AUTH_WAIT, "password must be 6-128 characters");
		return;
	}

	if (!m_crypto.HasKeyPair() && !m_crypto.GenerateKeyPair())
	{
		SetFlow(ENetFlow::AUTH_WAIT, "crypto unavailable");
		return;
	}
	m_client.SetAccountPubkey(m_crypto.GetPublicKeyB64());

	SubmitAuthRequest();
}

void CNecroNet::Logout()
{
	if (!m_bInitialized)
		return;

	if (m_eFlow != ENetFlow::READY && m_client.CopyToken().empty())
		return;

	m_bLogoutPending = true;
	m_store.ClearProfile();
	m_strPassword.clear();
	m_client.RequestLogout();
	SetFlow(m_eFlow, "logging out...");
}

void CNecroNet::ApplyProfile()
{
	if (!m_bInitialized || m_eFlow != ENetFlow::READY)
		return;

	NetProfilePatch patch;

	const std::string strName = Trimmed(m_strEditName);
	if (!strName.empty())
	{
		patch.m_bSetName = true;
		patch.m_strName = strName.substr(0, NET_MAX_NAME);
	}

	patch.m_bSetBio = true;
	patch.m_strBio = m_strEditBio.substr(0, NET_MAX_BIO);

	m_client.UpdateProfile(patch);
	SetFlow(ENetFlow::READY, "saving profile...");
}

void CNecroNet::SetStatus(ENetStatus eStatus)
{
	if (!m_bInitialized || m_eFlow != ENetFlow::READY)
		return;

	const int nStatus = static_cast<int>(eStatus);
	if (nStatus < 0 || nStatus >= NET_STATUS_COUNT)
		return;

	if (m_client.IsWebSocketConnected())
	{
		m_client.SendStatus(nStatus);
		return;
	}

	NetProfilePatch patch;
	patch.m_bSetStatus = true;
	patch.m_nStatus = nStatus;
	m_client.UpdateProfile(patch);
}

void CNecroNet::SetListed(bool bListed)
{
	if (!m_bInitialized || m_eFlow != ENetFlow::READY)
		return;

	NetProfilePatch patch;
	patch.m_bSetListed = true;
	patch.m_bListed = bListed;
	m_client.UpdateProfile(patch);
}

void CNecroNet::RefreshListing()
{
	if (!m_bInitialized)
		return;

	if (m_eFlow != ENetFlow::READY)
	{
		SetFlow(m_eFlow, "log in to view the listing");
		return;
	}

	m_client.RequestListing();
	SetFlow(ENetFlow::READY, "refreshing listing...");
}

void CNecroNet::SendFriendRequest()
{
	if (!m_bInitialized || m_eFlow != ENetFlow::READY)
		return;

	const std::string strUsername = Trimmed(m_strAddFriend);
	if (strUsername.size() < NET_USERNAME_MIN || strUsername.size() > NET_USERNAME_MAX)
	{
		SetFlow(ENetFlow::READY, "username must be 3-16 characters");
		return;
	}

	m_strAddFriend = strUsername;
	m_client.SendFriendRequest(strUsername);
}

void CNecroNet::RespondFriendRequest(long long llId, bool bAccept)
{
	if (!m_bInitialized || m_eFlow != ENetFlow::READY || llId <= 0)
		return;

	m_client.RespondFriendRequest(llId, bAccept);
}

void CNecroNet::RemoveFriend(const std::string& strUsername)
{
	if (!m_bInitialized || m_eFlow != ENetFlow::READY || !CNetStore::IsValidUsername(strUsername))
		return;

	if (m_strChatTarget == strUsername)
		m_strChatTarget.clear();

	m_client.RemoveFriend(strUsername);
}

void CNecroNet::OpenChat(const std::string& strUsername)
{
	if (!m_bInitialized || !CNetStore::IsValidUsername(strUsername))
		return;

	m_strChatTarget = strUsername;
	CacheHistory(strUsername);
	m_client.ClearUnread(strUsername);
}

void CNecroNet::SendChatText(const std::string& strText)
{
	if (!m_bInitialized || m_eFlow != ENetFlow::READY || m_strChatTarget.empty())
		return;

	if (strText.empty() || strText.size() > NET_MAX_MESSAGE_CHARS)
		return;

	NetUser user;
	if (!FindFriend(m_client.CopyFriends(), m_strChatTarget, user))
	{
		SetFlow(ENetFlow::READY, "you are not friends with this user");
		return;
	}

	if (!m_crypto.HasKey(m_strChatTarget) && !TryDeriveFriendKey(user))
	{
		SetFlow(ENetFlow::READY, "no encryption key for this friend");
		return;
	}

	std::string strNonce;
	std::string strCt;
	if (!m_crypto.EncryptMessage(m_strChatTarget, strText, strNonce, strCt) || strNonce.empty() || strCt.empty() || strCt.size() > NET_MAX_CT_B64)
	{
		SetFlow(ENetFlow::READY, "failed to encrypt the message");
		return;
	}

	m_client.SendDm(m_strChatTarget, strNonce, strCt);
	AppendHistoryLine(m_strChatTarget, true, strText, UnixNow());
}

void CNecroNet::NotifyTyping()
{
	if (!m_bInitialized || m_eFlow != ENetFlow::READY || m_strChatTarget.empty())
		return;

	const auto tpNow = std::chrono::steady_clock::now();
	auto& tpNext = m_mapTypingSent[m_strChatTarget];
	if (tpNow < tpNext)
		return;

	tpNext = tpNow + std::chrono::milliseconds(NET_TYPING_THROTTLE_MS);
	m_client.SendTyping(m_strChatTarget);
}

bool CNecroNet::IsLoggedIn() const
{
	return m_eFlow == ENetFlow::READY;
}

ENetFlow CNecroNet::GetFlowState() const
{
	return m_eFlow;
}

std::string CNecroNet::GetConnectStatus() const
{
	return m_strFlowStatus;
}

std::string CNecroNet::GetConfirmedSteamId() const
{
	return m_strConfirmedSteamId;
}

std::string CNecroNet::GetOwnSteamId() const
{
	return m_steam.GetSteamId();
}

std::string CNecroNet::GetOwnUsername() const
{
	NetUser user;
	if (m_client.CopyOwnUser(user) && !user.m_strUsername.empty())
		return user.m_strUsername;

	return m_strUsername;
}

std::string CNecroNet::GetChatTarget() const
{
	return m_strChatTarget;
}

bool CNecroNet::IsSteamAvailable() const
{
	return m_steam.IsAvailable();
}

bool CNecroNet::IsWebSocketConnected() const
{
	return m_client.IsWebSocketConnected();
}

ENetConnection CNecroNet::GetServerState() const
{
	return m_client.GetConnection();
}

std::string CNecroNet::GetServerError() const
{
	return m_client.CopyLastError();
}

bool CNecroNet::UploadAvatarFromFile(const std::string& strPath)
{
	if (!m_bInitialized)
		return false;

	std::string strB64;
	std::string strError;
	if (!CNetAvatar::EncodeFromFile(strPath, strB64, strError) || strB64.empty())
	{
		SetFlow(m_eFlow, strError.empty() ? "failed to load that image" : strError);
		return false;
	}

	const std::string strOwnName = GetOwnUsername();
	if (strOwnName.empty() || !CNetStore::IsValidUsername(strOwnName))
	{
		SetFlow(m_eFlow, "log in before changing your avatar");
		return false;
	}

	StoreAvatar(strOwnName, UnixNow(), strB64, true);
	UploadOwnAvatar();
	PushAvatarBroadcast();
	SetFlow(m_eFlow, "avatar updated - streaming to friends");
	return true;
}

void CNecroNet::ViewUser(const std::string& strUsername)
{
	if (!m_bInitialized || !CNetStore::IsValidUsername(strUsername))
		return;

	if (m_strViewUser == strUsername && m_bUserViewValid)
		return;

	m_strViewUser = strUsername;
	m_UserView = {};
	m_bUserViewValid = false;
	m_client.RequestUser(strUsername);
}

void CNecroNet::CloseUserView()
{
	m_strViewUser.clear();
	m_UserView = {};
	m_bUserViewValid = false;
}

bool CNecroNet::CopyViewedUser(NetUser& out) const
{
	if (m_strViewUser.empty() || !m_bUserViewValid)
		return false;

	out = m_UserView;
	return true;
}

void CNecroNet::RefreshCaches() const
{
	const unsigned long long nRevision = m_client.GetDataRevision();
	if (nRevision == m_nDataSeen)
		return;

	m_nDataSeen = nRevision;
	m_bOwnUserValid = m_client.CopyOwnUser(m_ownUserCache);
	m_vecFriendsCache = m_client.CopyFriends();
	m_vecReqsCache = m_client.CopyReqs();
	m_vecListingCache = m_client.CopyListing();
}

bool CNecroNet::CopyOwnUser(NetUser& out) const
{
	RefreshCaches();
	out = m_ownUserCache;
	return m_bOwnUserValid;
}

const std::vector<NetUser>& CNecroNet::CopyFriends() const
{
	RefreshCaches();
	return m_vecFriendsCache;
}

const std::vector<NetFriendReq>& CNecroNet::CopyReqs() const
{
	RefreshCaches();
	return m_vecReqsCache;
}

const std::vector<NetUser>& CNecroNet::CopyListing() const
{
	RefreshCaches();
	return m_vecListingCache;
}

int CNecroNet::GetUnread(const std::string& strUsername) const
{
	return m_client.GetUnread(strUsername);
}

bool CNecroNet::IsFriendTyping(const std::string& strUsername) const
{
	const auto it = m_mapTyping.find(strUsername);
	if (it == m_mapTyping.end())
		return false;

	return std::chrono::steady_clock::now() - it->second <= std::chrono::milliseconds(NET_TYPING_VISIBLE_MS);
}

const std::vector<NetHistoryLine>& CNecroNet::CopyHistory(const std::string& strUsername) const
{
	static const std::vector<NetHistoryLine> vecEmpty;

	const auto it = m_mapHistory.find(strUsername);
	if (it == m_mapHistory.end())
		return vecEmpty;

	return it->second;
}

const std::vector<NetNotification>& CNecroNet::Notifications() const
{
	return m_vecNotifications;
}

void CNecroNet::TickNotifications()
{
	const auto tpNow = std::chrono::steady_clock::now();
	std::erase_if(m_vecNotifications, [tpNow](const NetNotification& note)
	{
		return tpNow - note.m_tpBorn > std::chrono::milliseconds(NET_NOTIFY_LIFETIME_MS);
	});
}

void CNecroNet::PushNotification(ENetNotify eKind, const std::string& strUsername, const std::string& strName, const std::string& strAvatar, const std::string& strText)
{
	if (eKind == ENetNotify::NONE)
		return;

	for (const NetNotification& note : m_vecNotifications)
	{
		if (note.m_eKind == eKind && note.m_strUsername == strUsername && note.m_strText == strText)
			return;
	}

	NetNotification note;
	note.m_eKind = eKind;
	note.m_strUsername = strUsername;
	note.m_strName = strName;
	note.m_strAvatar = strAvatar;
	note.m_strText = strText;
	note.m_tpBorn = std::chrono::steady_clock::now();
	m_vecNotifications.push_back(std::move(note));
	while (m_vecNotifications.size() > NET_NOTIFY_MAX)
		m_vecNotifications.erase(m_vecNotifications.begin());
}

std::string CNecroNet::GetAvatarFolder() const
{
	return (U::Storage->GetWorkFolder() / "net" / "avatars").string();
}

bool CNecroNet::LoadAvatarFile(const std::string& strUsername, NetAvatarFile& out)
{
	if (!CNetStore::IsValidUsername(strUsername))
		return false;

	std::ifstream file(std::filesystem::path(GetAvatarFolder()) / (strUsername + ".txt"));
	if (!file)
		return false;

	std::string strTs;
	if (!std::getline(file, strTs))
		return false;

	std::string strB64;
	if (!std::getline(file, strB64) || strB64.empty() || strB64.size() > static_cast<size_t>(NET_AVATAR_MAX_B64))
		return false;

	out.m_llTs = strtoll(strTs.c_str(), nullptr, 10);
	out.m_strB64 = std::move(strB64);
	return true;
}

void CNecroNet::StoreAvatar(const std::string& strUsername, long long llTs, const std::string& strB64, bool bSaveToDisk)
{
	if (!CNetStore::IsValidUsername(strUsername) || strB64.empty() || strB64.size() > static_cast<size_t>(NET_AVATAR_MAX_B64))
		return;

	NetAvatarFile& file = m_mapAvatars[strUsername];
	file.m_llTs = llTs;
	file.m_strB64 = strB64;

	if (!bSaveToDisk)
		return;

	if (!m_bAvatarDirTried)
	{
		m_bAvatarDirTried = true;
		std::error_code ec;
		std::filesystem::create_directories(GetAvatarFolder(), ec);
	}

	std::ofstream out(std::filesystem::path(GetAvatarFolder()) / (strUsername + ".txt"), std::ios::trunc);
	if (!out)
		return;

	out << llTs << "\n" << strB64 << "\n";
}

std::string CNecroNet::GetAvatarB64(const std::string& strUsername)
{
	if (!CNetStore::IsValidUsername(strUsername))
		return {};

	const auto it = m_mapAvatars.find(strUsername);
	if (it != m_mapAvatars.end())
		return it->second.m_strB64;

	NetAvatarFile file;
	if (LoadAvatarFile(strUsername, file) && !file.m_strB64.empty())
	{
		m_mapAvatars[strUsername] = std::move(file);
		return m_mapAvatars[strUsername].m_strB64;
	}

	return {};
}

void CNecroNet::MigrateSelfAvatar(const std::string& strB64)
{
	if (strB64.empty() || strB64.size() > static_cast<size_t>(NET_AVATAR_MAX_B64))
		return;

	const std::string strOwnName = GetOwnUsername();
	if (strOwnName.empty() || !CNetStore::IsValidUsername(strOwnName))
		return;

	if (!GetAvatarB64(strOwnName).empty())
		return;

	std::vector<unsigned char> vecRGBA;
	int nW = 0, nH = 0;
	if (!CNetAvatar::DecodeToRGBA(strB64, vecRGBA, nW, nH))
		return;

	StoreAvatar(strOwnName, 1, strB64, true);
}

void CNecroNet::EnsureSelfAvatar()
{
	const std::string strOwnName = GetOwnUsername();
	if (strOwnName.empty() || !CNetStore::IsValidUsername(strOwnName))
		return;

	if (!GetAvatarB64(strOwnName).empty())
		return;

	const std::string strOwnId = GetOwnSteamId();
	if (!ValidOwnSteamId(strOwnId))
		return;

	if (!I::SteamFriends || !I::SteamUtils)
		return;

	CSteamID steamId(strtoull(strOwnId.c_str(), nullptr, 10));
	const int nImage = I::SteamFriends->GetMediumFriendAvatar(steamId);
	if (nImage <= 0)
		return;

	uint32_t nW = 0, nH = 0;
	if (!I::SteamUtils->GetImageSize(nImage, &nW, &nH) || nW == 0 || nH == 0 || nW > 512 || nH > 512)
		return;

	std::vector<unsigned char> vecRgba(static_cast<size_t>(nW) * static_cast<size_t>(nH) * 4);
	if (!I::SteamUtils->GetImageRGBA(nImage, vecRgba.data(), static_cast<int>(vecRgba.size())))
		return;

	std::string strB64;
	std::string strError;
	if (!CNetAvatar::EncodeFromRGBA(vecRgba.data(), static_cast<int>(nW), static_cast<int>(nH), strB64, strError) || strB64.empty())
		return;

	StoreAvatar(strOwnName, UnixNow(), strB64, true);
	UploadOwnAvatar();
}

void CNecroNet::CheckAvatar(const NetUser& user)
{
	if (!m_bInitialized || user.m_strUsername.empty() || user.m_llAvatarTs <= 0)
		return;

	m_mapAvatarTs[user.m_strUsername] = user.m_llAvatarTs;

	long long llCached = 0;
	const auto it = m_mapAvatars.find(user.m_strUsername);
	if (it != m_mapAvatars.end())
		llCached = it->second.m_llTs;
	else
	{
		NetAvatarFile file;
		if (LoadAvatarFile(user.m_strUsername, file))
			llCached = file.m_llTs;
	}

	if (llCached >= user.m_llAvatarTs)
		return;

	const double flNow = Plat_FloatTime();
	const auto asked = m_mapAvatarAsked.find(user.m_strUsername);
	if (asked != m_mapAvatarAsked.end() && (flNow - asked->second) < 20.0)
		return;

	m_mapAvatarAsked[user.m_strUsername] = flNow;
	m_client.RequestAvatar(user.m_strUsername);
}

void CNecroNet::CheckAvatars()
{
	if (!m_bInitialized)
		return;

	for (const NetUser& user : m_vecListingCache)
		CheckAvatar(user);

	for (const NetUser& user : m_vecFriendsCache)
		CheckAvatar(user);
}

void CNecroNet::StoreFriendAvatars()
{
	if (!m_bInitialized)
		return;

	for (const NetUser& user : m_client.CopyFriends())
	{
		if (user.m_strUsername.empty() || user.m_strAvatar.empty() || user.m_strAvatar.size() > static_cast<size_t>(NET_AVATAR_MAX_B64))
			continue;

		long long llCached = 0;
		const auto it = m_mapAvatars.find(user.m_strUsername);
		if (it != m_mapAvatars.end())
			llCached = it->second.m_llTs;
		else
		{
			NetAvatarFile file;
			if (LoadAvatarFile(user.m_strUsername, file))
				llCached = file.m_llTs;
		}

		if (llCached >= user.m_llAvatarTs)
			continue;

		StoreAvatar(user.m_strUsername, user.m_llAvatarTs > 0 ? user.m_llAvatarTs : UnixNow(), user.m_strAvatar, true);
	}
}

void CNecroNet::UploadOwnAvatar()
{
	if (!m_bInitialized)
		return;

	const std::string strOwnName = GetOwnUsername();
	if (strOwnName.empty())
		return;

	const std::string strB64 = GetAvatarB64(strOwnName);
	if (strB64.empty() || strB64.size() > static_cast<size_t>(NET_AVATAR_MAX_B64))
		return;

	NetProfilePatch patch;
	patch.m_bSetAvatar = true;
	patch.m_strAvatar = strB64;
	m_client.UpdateProfile(patch);
}

void CNecroNet::PushAvatarTo(const std::string& strTo)
{
	const std::string strOwnName = GetOwnUsername();
	if (strOwnName.empty() || strTo.empty())
		return;

	const std::string strB64 = GetAvatarB64(strOwnName);
	if (strB64.empty())
		return;

	long long llTs = 0;
	const auto it = m_mapAvatars.find(strOwnName);
	if (it != m_mapAvatars.end())
		llTs = it->second.m_llTs;

	m_client.SendAvatarPush(strTo, strB64, llTs);
}

void CNecroNet::PushAvatarBroadcast()
{
	const std::string strOwnName = GetOwnUsername();
	if (strOwnName.empty())
		return;

	const std::string strB64 = GetAvatarB64(strOwnName);
	if (strB64.empty())
		return;

	long long llTs = 0;
	const auto it = m_mapAvatars.find(strOwnName);
	if (it != m_mapAvatars.end())
		llTs = it->second.m_llTs;

	m_client.SendAvatarPush(std::string(), strB64, llTs);
}

void CNecroNet::HandleEvent(const NetEvent& ev)
{
	switch (ev.m_eKind)
	{
	case ENetEventKind::STEAM_POLL:
	case ENetEventKind::AUTH:
	case ENetEventKind::ME:
	case ENetEventKind::PROFILE:
	case ENetEventKind::LISTING:
	case ENetEventKind::USER:
	case ENetEventKind::FRIEND_REQ:
	case ENetEventKind::FRIEND_RESPOND:
	case ENetEventKind::FRIEND_REMOVE:
	case ENetEventKind::DM_SENT:
	case ENetEventKind::INBOX:
	case ENetEventKind::ACK:
	case ENetEventKind::LOGOUT:
	case ENetEventKind::SYNC:
		HandleHttpEvent(ev);
		break;

	case ENetEventKind::WS_OPENED:
	case ENetEventKind::WS_CLOSED:
	case ENetEventKind::WS_DM:
	case ENetEventKind::WS_TYPING:
	case ENetEventKind::WS_PRESENCE:
	case ENetEventKind::WS_FRIEND_ADD:
	case ENetEventKind::WS_FRIEND_UPD:
	case ENetEventKind::WS_FRIEND_DEL:
	case ENetEventKind::WS_FREQ:
	case ENetEventKind::WS_ACKD:
	case ENetEventKind::WS_ERROR:
	case ENetEventKind::WS_AVATAR:
		HandleWebSocketEvent(ev);
		break;

	default:
		break;
	}
}

void CNecroNet::HandleHttpEvent(const NetEvent& ev)
{
	switch (ev.m_eKind)
	{
	case ENetEventKind::STEAM_POLL:
	{
		if (ev.m_bOk && ev.m_bFlag)
		{
			if (m_eFlow != ENetFlow::AUTH_WAIT)
				break;

			m_strConfirmedSteamId = ev.m_strSteamId;
			SetFlow(ENetFlow::AUTH_WAIT, "steam confirmed - choose register or login");
		}
		else if (!ev.m_bOk && m_eFlow == ENetFlow::AUTH_WAIT)
		{
			m_client.CancelSteamPoll();
			m_strOpenIdState.clear();
			m_strConfirmedSteamId.clear();
			SetFlow(ENetFlow::IDLE, ev.m_strError.empty() ? "steam auth timed out" : "steam auth failed: " + ev.m_strError);
		}
		break;
	}

	case ENetEventKind::AUTH:
	{
		if (m_eFlow != ENetFlow::LOGGING_IN)
			break;

		if (!ev.m_bOk)
		{
			m_client.CancelSteamPoll();
			m_strOpenIdState.clear();
			m_strConfirmedSteamId.clear();
			SetFlow(ENetFlow::IDLE, ev.m_strError.empty() ? "login failed" : "login failed: " + ev.m_strError);
			break;
		}

		m_bTokenTried = true;
		m_client.CancelSteamPoll();

		NetUser user = ev.m_User;
		if (user.m_strUsername.empty())
			m_client.CopyOwnUser(user);

		const std::string strOwnSteamId = !m_strConfirmedSteamId.empty() ? m_strConfirmedSteamId : GetOwnSteamId();

		m_strOpenIdState.clear();
		m_strConfirmedSteamId.clear();

		const bool bSaved = WriteProfile(m_store, m_crypto, m_client, strOwnSteamId, user, m_strUsername);

		m_strEditName = user.m_strName;
		m_strEditBio = user.m_strBio;

		for (const NetUser& friendUser : m_client.CopyFriends())
			TryDeriveFriendKey(friendUser);

		StoreFriendAvatars();

		MigrateSelfAvatar(user.m_strAvatar);
		EnsureSelfAvatar();

		m_client.ConnectWebSocket();
		SetFlow(ENetFlow::READY, bSaved ? "connected" : "connected (profile not saved)");
		break;
	}

	case ENetEventKind::SYNC:
	{
		StoreFriendAvatars();
		RefreshCaches();
		CheckAvatars();
		break;
	}

	case ENetEventKind::ME:
	{
		if (!ev.m_bOk)
		{
			if (m_eFlow == ENetFlow::READY)
			{
				SetFlow(ENetFlow::READY, ev.m_strError.empty() ? "refresh failed" : "refresh failed: " + ev.m_strError);
				break;
			}

			if (m_eFlow != ENetFlow::LOGGING_IN)
				break;

			if (m_bTokenTried)
				m_client.ClearToken();

			SetFlow(ENetFlow::IDLE, "could not restore the session - connect again");
			break;
		}

		m_bTokenTried = true;

		NetUser user = ev.m_User;
		if (user.m_strUsername.empty())
			m_client.CopyOwnUser(user);

		m_strEditName = user.m_strName;
		m_strEditBio = user.m_strBio;

		for (const NetUser& friendUser : m_client.CopyFriends())
			TryDeriveFriendKey(friendUser);

		StoreFriendAvatars();

		MigrateSelfAvatar(user.m_strAvatar);
		EnsureSelfAvatar();

		if (m_eFlow == ENetFlow::READY)
		{
			SetFlow(ENetFlow::READY, "friends updated");
			break;
		}

		if (m_eFlow != ENetFlow::LOGGING_IN || m_bLogoutPending || m_client.CopyToken().empty())
			break;

		WriteProfile(m_store, m_crypto, m_client, GetOwnSteamId(), user, m_strUsername);
		m_client.ConnectWebSocket();
		SetFlow(ENetFlow::READY, "connected");
		break;
	}

	case ENetEventKind::PROFILE:
	{
		if (!ev.m_bOk)
		{
			SetFlow(ENetFlow::READY, ev.m_strError.empty() ? "profile update failed" : "profile update failed: " + ev.m_strError);
			break;
		}

		if (!ev.m_User.m_strUsername.empty())
		{
			m_strEditName = ev.m_User.m_strName;
			m_strEditBio = ev.m_User.m_strBio;
		}

		SetFlow(ENetFlow::READY, "profile saved");
		break;
	}

	case ENetEventKind::AVATAR:
	{
		if (!ev.m_bOk || ev.m_User.m_strUsername.empty() || ev.m_User.m_strAvatar.empty())
			break;

		StoreAvatar(ev.m_User.m_strUsername, ev.m_User.m_llAvatarTs, ev.m_User.m_strAvatar, true);
		m_mapAvatarTs[ev.m_User.m_strUsername] = ev.m_User.m_llAvatarTs;
		break;
	}

	case ENetEventKind::LISTING:
		SetFlow(ENetFlow::READY, ev.m_bOk ? "listing updated" : "listing failed: " + ev.m_strError);
		RefreshCaches();
		CheckAvatars();
		break;

	case ENetEventKind::USER:
	{
		if (ev.m_bOk)
		{
			if (!ev.m_User.m_strAvatar.empty())
				StoreAvatar(ev.m_User.m_strUsername, ev.m_User.m_llAvatarTs, ev.m_User.m_strAvatar, true);

			CheckAvatar(ev.m_User);
		}

		if (ev.m_bOk && !ev.m_User.m_strUsername.empty() && ev.m_User.m_strUsername == m_strViewUser)
		{
			m_UserView = ev.m_User;
			m_bUserViewValid = true;
		}
		break;
	}

	case ENetEventKind::FRIEND_REQ:
	{
		if (!ev.m_bOk)
		{
			SetFlow(ENetFlow::READY, ev.m_strError.empty() ? "friend request failed" : "friend request failed: " + ev.m_strError);
			break;
		}

		m_strAddFriend.clear();
		SetFlow(ENetFlow::READY, !ev.m_User.m_strUsername.empty() ? "friend added" : (ev.m_bFlag ? "friend request already pending" : "friend request sent"));
		break;
	}

	case ENetEventKind::FRIEND_RESPOND:
	{
		if (!ev.m_bOk)
		{
			SetFlow(ENetFlow::READY, ev.m_strError.empty() ? "friend response failed" : "friend response failed: " + ev.m_strError);
			break;
		}

		if (ev.m_bFlag && !ev.m_User.m_strPubkey.empty())
			TryDeriveFriendKey(ev.m_User);

		SetFlow(ENetFlow::READY, ev.m_bFlag ? "friend added" : "request declined");
		break;
	}

	case ENetEventKind::FRIEND_REMOVE:
	{
		if (!ev.m_bOk)
		{
			SetFlow(ENetFlow::READY, ev.m_strError.empty() ? "friend removal failed" : "friend removal failed: " + ev.m_strError);
			break;
		}

		m_crypto.DropDerivedKeys();
		SetFlow(ENetFlow::READY, "friend removed");
		break;
	}

	case ENetEventKind::DM_SENT:
	{
		if (!ev.m_bOk)
			SetFlow(ENetFlow::READY, ev.m_strError.empty() ? "message failed to send" : "message failed: " + ev.m_strError);
		break;
	}

	case ENetEventKind::INBOX:
	{
		if (!ev.m_bOk)
		{
			SetFlow(ENetFlow::READY, ev.m_strError.empty() ? "inbox fetch failed" : "inbox fetch failed: " + ev.m_strError);
			break;
		}

		ProcessInbox(ev.m_vecDms);
		break;
	}

	case ENetEventKind::LOGOUT:
	{
		ResetSession();
		break;
	}

	default:
		break;
	}
}

void CNecroNet::HandleWebSocketEvent(const NetEvent& ev)
{
	switch (ev.m_eKind)
	{
	case ENetEventKind::WS_OPENED:
	{
		if (m_eFlow != ENetFlow::READY)
			break;

		m_crypto.DropDerivedKeys();
		m_client.RequestInbox();
		EnsureSelfAvatar();
		PushAvatarBroadcast();
		SetFlow(ENetFlow::READY, "connected");
		break;
	}

	case ENetEventKind::WS_CLOSED:
	{
		if (m_eFlow == ENetFlow::READY && !m_bLogoutPending)
			SetFlow(ENetFlow::READY, "connection lost - reconnecting...");
		break;
	}

	case ENetEventKind::WS_DM:
	{
		if (!ev.m_vecDms.empty())
		{
			for (const NetDm& dm : ev.m_vecDms)
				ProcessDm(dm, false);
			break;
		}

		NetDm dm;
		dm.m_llId = ev.m_llId;
		dm.m_strFrom = ev.m_strFrom;
		dm.m_strNonce = ev.m_strNonce;
		dm.m_strCt = ev.m_strCt;
		dm.m_llTs = ev.m_llTs;
		ProcessDm(dm, false);
		break;
	}

	case ENetEventKind::WS_TYPING:
	{
		if (!ev.m_strFrom.empty())
			m_mapTyping[ev.m_strFrom] = std::chrono::steady_clock::now();
		break;
	}

	case ENetEventKind::WS_FRIEND_ADD:
	case ENetEventKind::WS_FRIEND_UPD:
	{
		if (!ev.m_User.m_strUsername.empty() && !ev.m_User.m_strPubkey.empty())
		{
			NetUser old;
			if (FindFriend(m_client.CopyFriends(), ev.m_User.m_strUsername, old) && old.m_strPubkey != ev.m_User.m_strPubkey)
				m_crypto.DropKey(ev.m_User.m_strUsername);
			TryDeriveFriendKey(ev.m_User);
		}

		if (ev.m_eKind == ENetEventKind::WS_FRIEND_ADD && !ev.m_User.m_strUsername.empty())
			PushAvatarTo(ev.m_User.m_strUsername);
		break;
	}

	case ENetEventKind::WS_FRIEND_DEL:
	{
		m_crypto.DropDerivedKeys();
		if (m_strChatTarget == ev.m_strUsername)
			m_strChatTarget.clear();
		break;
	}

	case ENetEventKind::WS_FREQ:
	{
		const std::string strReqName = !ev.m_strName.empty() ? ev.m_strName : ev.m_strUsername;
		PushNotification(ENetNotify::FRIEND_REQ, ev.m_strFrom, strReqName, std::string(), std::string());
		m_client.RequestMe();
		break;
	}

	case ENetEventKind::WS_ERROR:
	{
		if (m_eFlow == ENetFlow::READY)
			SetFlow(ENetFlow::READY, ev.m_strError.empty() ? "network error" : "error: " + ev.m_strError);
		break;
	}

	case ENetEventKind::WS_AVATAR:
	{
		if (ev.m_strData.empty() || ev.m_strData.size() > static_cast<size_t>(NET_AVATAR_MAX_B64))
			break;

		NetUser fromUser;
		if (!FindFriend(m_client.CopyFriends(), ev.m_strFrom, fromUser))
			break;

		std::vector<unsigned char> vecRGBA;
		int nW = 0, nH = 0;
		if (!CNetAvatar::DecodeToRGBA(ev.m_strData, vecRGBA, nW, nH))
			break;

		const auto it = m_mapAvatars.find(ev.m_strFrom);
		if (it != m_mapAvatars.end() && it->second.m_llTs >= ev.m_llTs)
			break;

		StoreAvatar(ev.m_strFrom, ev.m_llTs > 0 ? ev.m_llTs : UnixNow(), ev.m_strData, true);
		break;
	}

	default:
		break;
	}
}

void CNecroNet::ProcessDm(const NetDm& dm, bool bFromInbox)
{
	if (dm.m_llId <= 0 || !CNetStore::IsValidUsername(dm.m_strFrom))
		return;

	auto AckDm = [&]()
	{
		if (bFromInbox || !m_client.IsWebSocketConnected())
			m_client.AckMessages({ dm.m_llId });
		else
			m_client.SendAck({ dm.m_llId });
	};

	auto CountDecryptFail = [&]()
	{
		int& nTries = m_mapDecryptFails[dm.m_llId];
		nTries++;
		if (nTries < NET_DM_MAX_DECRYPT_TRIES)
			return;

		m_mapDecryptFails.erase(dm.m_llId);
		m_setSeenIds.insert(dm.m_llId);
		AckDm();
		SetFlow(m_eFlow, "dropped an unreadable message");
	};

	auto CountAppendFail = [&]()
	{
		int& nTries = m_mapAppendFails[dm.m_llId];
		nTries++;
		if (nTries < NET_DM_MAX_DECRYPT_TRIES)
			return;

		m_mapAppendFails.erase(dm.m_llId);
		m_setSeenIds.insert(dm.m_llId);
		AckDm();
		SetFlow(m_eFlow, "dropped a message that could not be saved");
	};

	if (m_setSeenIds.count(dm.m_llId))
	{
		AckDm();
		return;
	}

	if (!m_crypto.HasKey(dm.m_strFrom))
	{
		NetUser user;
		if (!FindFriend(m_client.CopyFriends(), dm.m_strFrom, user) || !TryDeriveFriendKey(user))
		{
			CountDecryptFail();
			return;
		}
	}

	std::string strText;
	if (!m_crypto.DecryptMessage(dm.m_strFrom, dm.m_strNonce, dm.m_strCt, strText))
	{
		CountDecryptFail();
		return;
	}

	if (strText.empty() || strText.size() > NET_MAX_MESSAGE_CHARS)
	{
		CountDecryptFail();
		return;
	}

	NetHistoryLine line;
	line.m_bOut = false;
	line.m_strFrom = dm.m_strFrom;
	line.m_strText = strText;
	line.m_llTs = dm.m_llTs != 0 ? dm.m_llTs : UnixNow();

	if (!m_store.AppendHistory(dm.m_strFrom, line))
	{
		CountAppendFail();
		return;
	}

	m_mapAppendFails.erase(dm.m_llId);
	m_setSeenIds.insert(dm.m_llId);

	if (!F::Menu->IsOpen() || dm.m_strFrom != m_strChatTarget)
	{
		NetUser fromUser;
		FindFriend(m_client.CopyFriends(), dm.m_strFrom, fromUser);
		PushNotification(ENetNotify::DM, dm.m_strFrom, fromUser.m_strName.empty() ? fromUser.m_strUsername : fromUser.m_strName, GetAvatarB64(dm.m_strFrom), strText);
	}

	const auto it = m_mapHistory.find(dm.m_strFrom);
	if (it != m_mapHistory.end())
	{
		it->second.push_back(std::move(line));
		while (it->second.size() > NET_HISTORY_CACHE_LINES)
			it->second.erase(it->second.begin());
	}

	if (dm.m_strFrom != m_strChatTarget)
		m_client.BumpUnread(dm.m_strFrom);

	AckDm();
}

void CNecroNet::ProcessInbox(const std::vector<NetDm>& vecDms)
{
	for (const NetDm& dm : vecDms)
		ProcessDm(dm, true);
}

bool CNecroNet::TryDeriveFriendKey(const NetUser& user)
{
	if (user.m_strUsername.empty() || user.m_strPubkey.empty())
		return false;

	if (m_crypto.HasKey(user.m_strUsername))
		return true;

	return m_crypto.DeriveKey(user.m_strUsername, user.m_strPubkey);
}

void CNecroNet::AppendHistoryLine(const std::string& strUsername, bool bOut, const std::string& strText, long long llTs)
{
	if (!CNetStore::IsValidUsername(strUsername) || strText.empty())
		return;

	NetHistoryLine line;
	line.m_bOut = bOut;
	line.m_strFrom = bOut ? GetOwnUsername() : strUsername;
	line.m_strText = strText;
	line.m_llTs = llTs;

	if (!m_store.AppendHistory(strUsername, line))
		return;

	const auto it = m_mapHistory.find(strUsername);
	if (it == m_mapHistory.end())
		return;

	it->second.push_back(std::move(line));
	while (it->second.size() > NET_HISTORY_CACHE_LINES)
		it->second.erase(it->second.begin());
}

void CNecroNet::CacheHistory(const std::string& strUsername)
{
	if (!CNetStore::IsValidUsername(strUsername) || m_mapHistory.count(strUsername))
		return;

	std::vector<NetHistoryLine> vecLines;
	m_store.LoadHistory(strUsername, vecLines);

	while (vecLines.size() > NET_HISTORY_CACHE_LINES)
		vecLines.erase(vecLines.begin());

	m_mapHistory[strUsername] = std::move(vecLines);
}

void CNecroNet::SubmitAuthRequest()
{
	if (m_eFlow != ENetFlow::AUTH_WAIT)
		return;

	if (m_strOpenIdState.empty() || m_strConfirmedSteamId.empty())
	{
		SetFlow(ENetFlow::IDLE, "no steam auth available - run connect first");
		return;
	}

	const std::string strPubkey = m_crypto.GetPublicKeyB64();
	if (strPubkey.empty())
	{
		SetFlow(ENetFlow::IDLE, "crypto unavailable");
		return;
	}

	if (m_bRegisterMode)
		m_client.RequestRegister(Trimmed(m_strUsername), m_strPassword, strPubkey, m_strOpenIdState, "");
	else
		m_client.RequestLogin(Trimmed(m_strUsername), m_strPassword, strPubkey, m_strOpenIdState, "");

	m_client.CancelSteamPoll();
	SetFlow(ENetFlow::LOGGING_IN, m_bRegisterMode ? "registering..." : "logging in...");
}

void CNecroNet::SetFlow(ENetFlow eFlow, const std::string& strStatus)
{
	m_eFlow = eFlow;
	m_strFlowStatus = strStatus;
}

void CNecroNet::ResetSession(bool bClearToken)
{
	m_eFlow = ENetFlow::IDLE;
	m_strFlowStatus = "logged out";
	m_strOpenIdState.clear();
	m_strConfirmedSteamId.clear();
	m_strChatTarget.clear();
	CloseUserView();
	m_mapHistory.clear();
	m_mapTyping.clear();
	m_setSeenIds.clear();
	m_mapDecryptFails.clear();
	m_mapAppendFails.clear();
	m_mapTypingSent.clear();
	m_vecNotifications.clear();
	m_bTokenTried = false;
	m_bLogoutPending = false;
	if (bClearToken)
		m_client.ClearToken();
	m_client.DisconnectWebSocket();
	m_client.ResetState();
	m_crypto.DropDerivedKeys();
}
