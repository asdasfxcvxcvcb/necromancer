#pragma once

#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "NetworkShared.h"
#include "NetJson.h"

#pragma comment(lib, "winhttp.lib")

struct NetWsRuntime;

struct NetJob
{
	ENetJob m_eKind = ENetJob::NONE;
	std::string m_strBody;
};

class CNetClient
{
public:
	CNetClient();
	~CNetClient();

	CNetClient(const CNetClient&) = delete;
	CNetClient& operator=(const CNetClient&) = delete;

	bool Start();
	void Shutdown();
	bool IsRunning() const;

	void SetToken(const std::string& strToken);
	void ClearToken();
	std::string CopyToken() const;
	void ResetState();

	void BeginSteamPoll(const std::string& strState);
	void CancelSteamPoll();

	void RequestRegister(const std::string& strUsername, const std::string& strPassword, const std::string& strPubkey, const std::string& strOpenIdState, const std::string& strTicket);
	void RequestLogin(const std::string& strUsername, const std::string& strPassword, const std::string& strPubkey, const std::string& strOpenIdState, const std::string& strTicket);
	void RequestMe();
	void SetAccountPubkey(const std::string& strPubkey);
	void UpdateProfile(const NetProfilePatch& patch);
	void RequestListing();
	void RequestUser(const std::string& strUsername);
	void RequestAvatar(const std::string& strUsername);
	void SendFriendRequest(const std::string& strUsername);
	void RespondFriendRequest(long long llId, bool bAccept);
	void RemoveFriend(const std::string& strUsername);
	void SendDm(const std::string& strTo, const std::string& strNonce, const std::string& strCt);
	void RequestInbox();
	void AckMessages(const std::vector<long long>& vecIds);
	void RequestLogout();
	void RequestSync();

	void ConnectWebSocket();
	void DisconnectWebSocket();
	void SendTyping(const std::string& strTo);
	void SendAck(const std::vector<long long>& vecIds);
	void SendStatus(int nStatus);
	void SendAvatarPush(const std::string& strTo, const std::string& strData, long long llTs);

	void SetEventSink(TNetEventFn fnSink);
	void SetHttpSink(TNetHttpResultFn fnSink);

	void BumpUnread(const std::string& strUsername);
	void ClearUnread(const std::string& strUsername);

	bool CopyOwnUser(NetUser& out) const;
	std::vector<NetUser> CopyFriends() const;
	std::vector<NetUser> CopyListing() const;
	std::vector<NetFriendReq> CopyReqs() const;
	std::map<std::string, int> CopyUnread() const;
	int GetUnread(const std::string& strUsername) const;
	ENetConnection GetConnection() const;
	bool IsWebSocketConnected() const;
	std::string CopyLastError() const;
	bool PopEvent(NetEvent& out);
	unsigned long long GetDataRevision() const;

private:
	void Worker();
	void QueueJob(const NetJob& job);
	void PushEvent(const NetEvent& ev);
	void Fail(const std::string& strError);
	void SetConnection(ENetConnection eState);
	void RunJob(const NetJob& job);
	bool HttpPost(const wchar_t* wszPath, const std::string& strBody, int& nStatusOut, std::string& strBodyOut);
	void HandleResponse(const NetJob& job, bool bOk, int nStatus, const std::string& strBody);
	void ApplyAuthPayload(const CJsonValue& jRoot);
	void ApplyMePayload(const CJsonValue& jRoot);
	void ApplyListingPayload(const CJsonValue& jRoot);
	void ApplyInboxPayload(const CJsonValue& jRoot);
	static bool ParseUser(const CJsonValue& jValue, NetUser& out);
	static bool ParseFriendReq(const CJsonValue& jValue, NetFriendReq& out);
	static bool ParseDm(const CJsonValue& jValue, NetDm& out);
	void ApplyUserUpsert(const NetUser& user);
	void ApplyFriendRemove(const std::string& strUsername);
	void PumpSteamPoll();
	void PumpWebSocket();
	bool OpenWebSocket();
	void CloseWebSocket();
	bool SendWebSocketText(const std::string& strText);
	bool PollWebSocketText(std::string& strOut);
	void DispatchWebSocketText(const std::string& strText);
	void ScheduleReconnect();

	HINTERNET m_hSession = nullptr;
	HINTERNET m_hConnection = nullptr;
	HINTERNET m_hWsRequest = nullptr;

	std::thread m_thread;
	mutable std::mutex m_mutex;
	std::condition_variable m_cv;
	std::atomic<bool> m_bRunning{ false };
	std::deque<NetJob> m_queueJobs;
	std::deque<NetEvent> m_queueEvents;
	TNetEventFn m_fnEventSink;
	TNetHttpResultFn m_fnHttpSink;

	std::string m_strToken;
	std::string m_strAccountPubkey;
	std::string m_strPollState;
	std::string m_strLastError;

	std::chrono::steady_clock::time_point m_tpNextPoll{};
	std::chrono::steady_clock::time_point m_tpLastReceive{};
	std::chrono::steady_clock::time_point m_tpNextPing{};
	std::chrono::steady_clock::time_point m_tpRetryAt{};
	int m_nPollElapsedMs = 0;
	bool m_bPolling = false;
	bool m_bWsWanted = false;
	bool m_bWsConnected = false;
	bool m_bPollMode = false;
	int m_nWsFailCount = 0;
	int m_nWsProxyMode = 0;
	std::chrono::steady_clock::time_point m_tpNextSync{};
	std::chrono::steady_clock::time_point m_tpWsUpAt{};

	std::unique_ptr<NetWsRuntime> m_ws;
	std::atomic<bool> m_bShuttingDown{ false };
	std::atomic<unsigned long long> m_nSessionGeneration{ 0 };
	std::atomic<unsigned long long> m_nDataRevision{ 0 };

	NetUser m_ownUser = {};
	std::vector<NetUser> m_vecFriends;
	std::vector<NetUser> m_vecListing;
	std::vector<NetFriendReq> m_vecReqs;
	std::map<std::string, int> m_mapUnread;
	ENetConnection m_eConnection = ENetConnection::DISCONNECTED;
};
