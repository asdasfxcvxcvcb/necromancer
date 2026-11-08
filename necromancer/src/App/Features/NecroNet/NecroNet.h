#pragma once
#include "../../../SDK/SDK.h"

#include <chrono>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "NetworkShared.h"
#include "NetClient.h"
#include "NetCrypto.h"
#include "NetStore.h"
#include "NetSteam.h"

enum class ENetFlow { IDLE, AUTH_WAIT, LOGGING_IN, READY };
enum class ENetNotify { NONE = 0, FRIEND_REQ = 1, DM = 2 };

struct NetNotification
{
	ENetNotify m_eKind = ENetNotify::NONE;
	std::string m_strUsername;
	std::string m_strName;
	std::string m_strAvatar;
	std::string m_strText;
	std::chrono::steady_clock::time_point m_tpBorn;
};

class CNecroNet {
public:
	void Init();
	void Shutdown();
	void Run();

	std::string m_strUsername;
	std::string m_strPassword;
	std::string m_strEditName;
	std::string m_strEditBio;
	std::string m_strAddFriend;
	bool m_bRegisterMode = false;

	void Connect();
	void CancelConnect();
	void SubmitAuth();
	void Logout();
	void ApplyProfile();
	void SetStatus(ENetStatus eStatus);
	void SetListed(bool bListed);
	void RefreshListing();
	void SendFriendRequest();
	void RespondFriendRequest(long long llId, bool bAccept);
	void RemoveFriend(const std::string& strUsername);
	void OpenChat(const std::string& strUsername);
	void SendChatText(const std::string& strText);
	void NotifyTyping();
	bool UploadAvatarFromFile(const std::string& strPath);
	void ViewUser(const std::string& strUsername);
	void CloseUserView();
	bool CopyViewedUser(NetUser& out) const;

	bool IsLoggedIn() const;
	ENetFlow GetFlowState() const;
	std::string GetConnectStatus() const;
	std::string GetConfirmedSteamId() const;
	std::string GetOwnSteamId() const;
	std::string GetOwnUsername() const;
	std::string GetChatTarget() const;
	std::string GetAvatarB64(const std::string& strUsername);
	bool IsSteamAvailable() const;
	bool IsWebSocketConnected() const;
	ENetConnection GetServerState() const;
	std::string GetServerError() const;
	bool CopyOwnUser(NetUser& out) const;
	const std::vector<NetUser>& CopyFriends() const;
	const std::vector<NetFriendReq>& CopyReqs() const;
	const std::vector<NetUser>& CopyListing() const;
	int GetUnread(const std::string& strUsername) const;
	bool IsFriendTyping(const std::string& strUsername) const;
	const std::vector<NetHistoryLine>& CopyHistory(const std::string& strUsername) const;
	const std::vector<NetNotification>& Notifications() const;
	void TickNotifications();

private:
	void HandleEvent(const NetEvent& ev);
	void CheckAvatar(const NetUser& user);
	void CheckAvatars();
	void StoreFriendAvatars();
	void UploadOwnAvatar();
	void HandleHttpEvent(const NetEvent& ev);
	void HandleWebSocketEvent(const NetEvent& ev);
	void ProcessDm(const NetDm& dm, bool bFromInbox);
	void ProcessInbox(const std::vector<NetDm>& vecDms);
	bool TryDeriveFriendKey(const NetUser& user);
	void AppendHistoryLine(const std::string& strUsername, bool bOut, const std::string& strText, long long llTs);
	void CacheHistory(const std::string& strUsername);
	void PushNotification(ENetNotify eKind, const std::string& strUsername, const std::string& strName, const std::string& strAvatar, const std::string& strText);

	struct NetAvatarFile
	{
		long long m_llTs = 0;
		std::string m_strB64;
	};
	std::string GetAvatarFolder() const;
	bool LoadAvatarFile(const std::string& strUsername, NetAvatarFile& out);
	void StoreAvatar(const std::string& strUsername, long long llTs, const std::string& strB64, bool bSaveToDisk);
	void MigrateSelfAvatar(const std::string& strB64);
	void EnsureSelfAvatar();
	void PushAvatarTo(const std::string& strTo);
	void PushAvatarBroadcast();
	void SubmitAuthRequest();
	void SetFlow(ENetFlow eFlow, const std::string& strStatus);
	void ResetSession(bool bClearToken = true);
	void RefreshCaches() const;

	CNetCrypto m_crypto;
	CNetClient m_client;
	CNetStore m_store;
	CNetSteam m_steam;

	ENetFlow m_eFlow = ENetFlow::IDLE;
	std::string m_strFlowStatus;
	std::string m_strOpenIdState;
	std::string m_strConfirmedSteamId;
	std::string m_strChatTarget;
	std::string m_strViewUser;
	NetUser m_UserView = {};
	bool m_bUserViewValid = false;
	std::map<std::string, std::vector<NetHistoryLine>> m_mapHistory;
	std::map<std::string, std::chrono::steady_clock::time_point> m_mapTyping;
	std::set<long long> m_setSeenIds;
	std::map<long long, int> m_mapDecryptFails;
	std::map<long long, int> m_mapAppendFails;
	std::map<std::string, std::chrono::steady_clock::time_point> m_mapTypingSent;
	std::map<std::string, NetAvatarFile> m_mapAvatars;
	std::map<std::string, long long> m_mapAvatarTs;
	std::map<std::string, double> m_mapAvatarAsked;
	std::chrono::steady_clock::time_point m_tpAvatarCheck = {};
	std::vector<NetNotification> m_vecNotifications;
	bool m_bAvatarDirTried = false;
	bool m_bTokenTried = false;
	bool m_bLogoutPending = false;
	bool m_bInitialized = false;

	mutable unsigned long long m_nDataSeen = 0;
	mutable NetUser m_ownUserCache = {};
	mutable bool m_bOwnUserValid = false;
	mutable std::vector<NetUser> m_vecFriendsCache;
	mutable std::vector<NetFriendReq> m_vecReqsCache;
	mutable std::vector<NetUser> m_vecListingCache;
};

MAKE_SINGLETON_SCOPED(CNecroNet, NecroNet, F);
