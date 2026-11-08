#include "NetClient.h"

#include <algorithm>
#include <cstdlib>

#pragma comment(lib, "winhttp.lib")

static constexpr const wchar_t* g_wszNetUserAgent = L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
static constexpr int NETCLI_WORKER_IDLE_MS = 250;
static constexpr int NETCLI_WS_PUMP_MS = 50;
static constexpr int NETCLI_WS_BUFFER_BYTES = 16384;
static constexpr int NETCLI_WS_RECEIVE_TIMEOUT_MS = 120000;
static constexpr size_t NETCLI_WS_MAX_PARTIAL = 1u << 20;
static constexpr size_t NETCLI_WS_MAX_FRAMES = 512;
static constexpr size_t NETCLI_WS_MAX_PENDING = 64;
static constexpr size_t NETCLI_MAX_EVENTS = 512;
static constexpr size_t NETCLI_MAX_JOBS = 256;
static constexpr size_t NETCLI_HTTP_MAX_BODY = 8u << 20;
static constexpr size_t NETCLI_MAX_AVATAR_B64 = 65536;
static constexpr int NETCLI_HANDSHAKE_TIMEOUT_MS = NET_HTTP_TIMEOUT_MS;
static constexpr size_t NETCLI_LASTERROR_MAX = 256;
static constexpr size_t NETCLI_MAX_REQS = 128;
static constexpr int NETCLI_POLL_INTERVAL_MS = 3000;
static constexpr int NETCLI_WS_FAILS_BEFORE_POLL = 2;

using NetClock = std::chrono::steady_clock;
using NetTimePoint = NetClock::time_point;

struct NetWsRuntime
{
	std::mutex m_mtxFrames;
	HINTERNET m_hSession = nullptr;
	HINTERNET m_hConnect = nullptr;
	HINTERNET m_hRequest = nullptr;
	HINTERNET m_hWebSocket = nullptr;
	std::atomic<bool> m_bStarted{ false };
	std::atomic<bool> m_bSendRequestDone{ false };
	std::atomic<bool> m_bResponseRequested{ false };
	std::atomic<bool> m_bHeadersAvailable{ false };
	std::atomic<bool> m_bUpgraded{ false };
	std::atomic<bool> m_bRequestError{ false };
	std::atomic<bool> m_bReceiveArmed{ false };
	std::atomic<bool> m_bSendOutstanding{ false };
	std::atomic<bool> m_bCloseSeen{ false };
	std::atomic<bool> m_bWsHandleClosed{ false };
	std::atomic<bool> m_bOverflow{ false };
	std::atomic<DWORD> m_dwError{ 0 };
	NetTimePoint m_tpHandshake{};
	DWORD m_dwBytesRead = 0;
	WINHTTP_WEB_SOCKET_BUFFER_TYPE m_eBufferType = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
	char m_chBuffer[NETCLI_WS_BUFFER_BYTES] = {};
	std::string m_strSendBuffer;
	std::string m_strPartial;
	std::deque<std::pair<int, std::string>> m_vecFrames;
	std::deque<std::string> m_vecPendingSends;
};

static std::wstring NetWiden(const std::string& strText)
{
	if (strText.empty())
		return std::wstring();
	const int nSize = MultiByteToWideChar(CP_UTF8, 0, strText.c_str(), (int)strText.size(), nullptr, 0);
	if (nSize <= 0)
		return std::wstring();
	std::wstring strOut((size_t)nSize, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, strText.c_str(), (int)strText.size(), strOut.data(), nSize);
	return strOut;
}

static std::string NetJsonStr(const CJsonValue& jObject, const char* szKey)
{
	const CJsonValue* pValue = jObject.Find(szKey);
	return (pValue && pValue->IsString()) ? pValue->GetString() : std::string();
}

static std::string NetCappedStr(const CJsonValue& jObject, const char* szKey, size_t nMax)
{
	std::string strOut = NetJsonStr(jObject, szKey);
	if (strOut.size() > nMax)
		strOut.resize(nMax);
	return strOut;
}

static long long NetJsonInt(const CJsonValue& jObject, const char* szKey)
{
	const CJsonValue* pValue = jObject.Find(szKey);
	if (!pValue)
		return 0;
	if (pValue->IsNumber())
		return pValue->GetInt();
	if (pValue->IsString())
		return std::strtoll(pValue->GetString().c_str(), nullptr, 10);
	return 0;
}

static bool NetJsonBool(const CJsonValue& jObject, const char* szKey)
{
	const CJsonValue* pValue = jObject.Find(szKey);
	return (pValue && pValue->IsBoolean()) ? pValue->GetBool() : false;
}

static std::vector<long long> NetParseIdArray(const CJsonValue& jObject, const char* szKey)
{
	std::vector<long long> vecIds;
	const CJsonValue* pIds = jObject.Find(szKey);
	if (!pIds || !pIds->IsArray())
		return vecIds;
	for (const CJsonValue& jId : pIds->GetArray())
	{
		if (jId.IsNumber())
			vecIds.push_back(jId.GetInt());
		else if (jId.IsString())
			vecIds.push_back(std::strtoll(jId.GetString().c_str(), nullptr, 10));
	}
	return vecIds;
}

static const wchar_t* NetHttpPathForJob(ENetJob eKind)
{
	switch (eKind)
	{
	case ENetJob::POST_REGISTER: return NET_PATH_REGISTER_W;
	case ENetJob::POST_LOGIN: return NET_PATH_LOGIN_W;
	case ENetJob::POST_ME:
	case ENetJob::POST_PROFILE: return NET_PATH_ME_W;
	case ENetJob::POST_LISTING: return NET_PATH_LISTING_W;
	case ENetJob::POST_USER: return NET_PATH_USER_W;
	case ENetJob::POST_AVATAR: return NET_PATH_AVATAR_W;
	case ENetJob::POST_FREQ: return NET_PATH_FREQ_W;
	case ENetJob::POST_FRESPOND: return NET_PATH_FRESPOND_W;
	case ENetJob::POST_FREMOVE: return NET_PATH_FREMOVE_W;
	case ENetJob::POST_DM: return NET_PATH_DM_W;
	case ENetJob::POST_INBOX: return NET_PATH_INBOX_W;
	case ENetJob::POST_ACK: return NET_PATH_ACK_W;
	case ENetJob::POST_LOGOUT: return NET_PATH_LOGOUT_W;
	case ENetJob::POST_SYNC: return NET_PATH_SYNC_W;
	case ENetJob::POST_TYPING: return NET_PATH_TYPING_W;
	default: return nullptr;
	}
}

static ENetEventKind NetEventKindForJob(ENetJob eKind)
{
	switch (eKind)
	{
	case ENetJob::POST_REGISTER:
	case ENetJob::POST_LOGIN: return ENetEventKind::AUTH;
	case ENetJob::POST_ME: return ENetEventKind::ME;
	case ENetJob::POST_PROFILE: return ENetEventKind::PROFILE;
	case ENetJob::POST_LISTING: return ENetEventKind::LISTING;
	case ENetJob::POST_USER: return ENetEventKind::USER;
	case ENetJob::POST_AVATAR: return ENetEventKind::AVATAR;
	case ENetJob::POST_FREQ: return ENetEventKind::FRIEND_REQ;
	case ENetJob::POST_FRESPOND: return ENetEventKind::FRIEND_RESPOND;
	case ENetJob::POST_FREMOVE: return ENetEventKind::FRIEND_REMOVE;
	case ENetJob::POST_DM: return ENetEventKind::DM_SENT;
	case ENetJob::POST_INBOX: return ENetEventKind::INBOX;
	case ENetJob::POST_ACK: return ENetEventKind::ACK;
	case ENetJob::POST_LOGOUT: return ENetEventKind::LOGOUT;
	case ENetJob::POST_SYNC: return ENetEventKind::SYNC;
	case ENetJob::POST_TYPING: return ENetEventKind::SYNC;
	default: return ENetEventKind::NONE;
	}
}

static void CALLBACK NetWsStatusCallback(HINTERNET hInternet, DWORD_PTR dwContext, DWORD dwInternetStatus, LPVOID lpvStatusInformation, DWORD dwStatusInformationLength)
{
	NetWsRuntime* pWs = reinterpret_cast<NetWsRuntime*>(dwContext);
	if (!pWs)
		return;
	NetWsRuntime& ws = *pWs;

	switch (dwInternetStatus)
	{
	case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
	{
		ws.m_bSendRequestDone = true;
		if (!ws.m_bResponseRequested.exchange(true) && ws.m_hRequest)
			WinHttpReceiveResponse(ws.m_hRequest, 0);
		break;
	}
	case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
	{
		ws.m_bHeadersAvailable = true;
		if (!ws.m_hWebSocket && ws.m_hRequest && ws.m_bStarted)
		{
			HINTERNET hWebSocket = WinHttpWebSocketCompleteUpgrade(ws.m_hRequest, reinterpret_cast<DWORD_PTR>(&ws));
			if (hWebSocket)
			{
				ws.m_hWebSocket = hWebSocket;
				ws.m_bUpgraded = true;
			}
			else
			{
				ws.m_dwError = GetLastError();
				ws.m_bRequestError = true;
			}
		}
		break;
	}
	case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
	{
		if (!lpvStatusInformation || dwStatusInformationLength < sizeof(WINHTTP_WEB_SOCKET_STATUS))
		{
			ws.m_bReceiveArmed = false;
			break;
		}
		const WINHTTP_WEB_SOCKET_STATUS* pStatus = static_cast<const WINHTTP_WEB_SOCKET_STATUS*>(lpvStatusInformation);
		DWORD dwBytes = pStatus->dwBytesTransferred;
		if (dwBytes > (DWORD)NETCLI_WS_BUFFER_BYTES)
			dwBytes = (DWORD)NETCLI_WS_BUFFER_BYTES;
		{
			std::lock_guard<std::mutex> lock(ws.m_mtxFrames);
			std::string strChunk(ws.m_chBuffer, dwBytes);
			ws.m_bReceiveArmed = false;
			if (ws.m_vecFrames.size() >= NETCLI_WS_MAX_FRAMES)
				ws.m_bOverflow = true;
			else
				ws.m_vecFrames.push_back({ (int)pStatus->eBufferType, std::move(strChunk) });
		}
		break;
	}
	case WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE:
	{
		ws.m_bSendOutstanding = false;
		break;
	}
	case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
	{
		DWORD dwError = 0;
		if (lpvStatusInformation && dwStatusInformationLength >= sizeof(WINHTTP_WEB_SOCKET_ASYNC_RESULT))
		{
			const WINHTTP_WEB_SOCKET_ASYNC_RESULT* pWsResult = static_cast<const WINHTTP_WEB_SOCKET_ASYNC_RESULT*>(lpvStatusInformation);
			dwError = pWsResult->AsyncResult.dwError;
		}
		else if (lpvStatusInformation && dwStatusInformationLength >= sizeof(WINHTTP_ASYNC_RESULT))
		{
			const WINHTTP_ASYNC_RESULT* pResult = static_cast<const WINHTTP_ASYNC_RESULT*>(lpvStatusInformation);
			dwError = pResult->dwError;
		}
		ws.m_dwError = dwError;
		ws.m_bRequestError = true;
		ws.m_bSendOutstanding = false;
		ws.m_bReceiveArmed = false;
		break;
	}
	case WINHTTP_CALLBACK_STATUS_CLOSE_COMPLETE:
	case WINHTTP_CALLBACK_STATUS_SHUTDOWN_COMPLETE:
	{
		ws.m_bCloseSeen = true;
		break;
	}
	case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
	{
		if (hInternet != nullptr && hInternet == ws.m_hWebSocket)
			ws.m_bWsHandleClosed = true;
		break;
	}
	default:
		break;
	}
}

static bool NetArmReceive(NetWsRuntime& ws, HINTERNET hWebSocket)
{
	if (!hWebSocket || ws.m_bReceiveArmed || ws.m_bCloseSeen || ws.m_bWsHandleClosed)
		return true;
	const DWORD dwResult = WinHttpWebSocketReceive(hWebSocket, ws.m_chBuffer, (DWORD)NETCLI_WS_BUFFER_BYTES, &ws.m_dwBytesRead, &ws.m_eBufferType);
	if (dwResult == ERROR_IO_PENDING)
	{
		ws.m_bReceiveArmed = true;
		return true;
	}
	return dwResult == NO_ERROR;
}

CNetClient::CNetClient() = default;

CNetClient::~CNetClient()
{
	Shutdown();
}

bool CNetClient::Start()
{
	bool bExpected = false;
	if (!m_bRunning.compare_exchange_strong(bExpected, true))
		return true;

	if (m_bShuttingDown.load(std::memory_order_acquire))
	{
		m_bRunning = false;
		return false;
	}

	if (m_thread.joinable())
		m_thread.join();

	if (!m_ws)
		m_ws = std::make_unique<NetWsRuntime>();

	m_hSession = WinHttpOpen(g_wszNetUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!m_hSession)
	{
		m_bRunning = false;
		return false;
	}

	DWORD dwTimeout = (DWORD)NET_HTTP_TIMEOUT_MS;
	WinHttpSetOption(m_hSession, WINHTTP_OPTION_RESOLVE_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
	WinHttpSetOption(m_hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
	WinHttpSetOption(m_hSession, WINHTTP_OPTION_SEND_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
	WinHttpSetOption(m_hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &dwTimeout, sizeof(dwTimeout));

	m_hConnection = WinHttpConnect(m_hSession, NET_SERVER_HOST_W, INTERNET_DEFAULT_HTTPS_PORT, 0);

	m_thread = std::thread(&CNetClient::Worker, this);
	return true;
}

void CNetClient::Shutdown()
{
	bool bExpected = true;
	if (!m_bRunning.compare_exchange_strong(bExpected, false))
		return;

	m_bShuttingDown = true;
	m_cv.notify_all();

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_nSessionGeneration++;
	}

	HINTERNET hConnection = nullptr;
	HINTERNET hSession = nullptr;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		hConnection = m_hConnection;
		hSession = m_hSession;
		m_hConnection = nullptr;
		m_hSession = nullptr;
	}

	if (hConnection)
		WinHttpCloseHandle(hConnection);
	if (hSession)
		WinHttpCloseHandle(hSession);

	if (m_thread.joinable())
		m_thread.join();

	CloseWebSocket();

	if (m_ws)
	{
		static std::mutex s_mtxRetired;
		static std::vector<NetWsRuntime*> s_vecRetired;
		std::lock_guard<std::mutex> lockRetired(s_mtxRetired);
		s_vecRetired.push_back(m_ws.release());
	}
}

bool CNetClient::IsRunning() const
{
	return m_bRunning.load();
}

void CNetClient::SetToken(const std::string& strToken)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_strToken = strToken;
}

void CNetClient::ClearToken()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_strToken.clear();
}

std::string CNetClient::CopyToken() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_strToken;
}

void CNetClient::ResetState()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_strToken.clear();
		m_ownUser = {};
		m_vecFriends.clear();
		m_vecListing.clear();
		m_vecReqs.clear();
		m_mapUnread.clear();
		m_queueEvents.clear();
		m_strLastError.clear();
		m_bPolling = false;
		m_strPollState.clear();
		m_nPollElapsedMs = 0;
		m_bWsWanted = false;
		m_bPollMode = false;
		m_nWsFailCount = 0;
		m_eConnection = ENetConnection::DISCONNECTED;
	}
	QueueJob({ ENetJob::NONE, std::string() });
}

void CNetClient::BeginSteamPoll(const std::string& strState)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_strPollState = strState;
		m_bPolling = true;
		m_nPollElapsedMs = 0;
		m_tpNextPoll = NetClock::now();
	}
	m_cv.notify_all();
}

void CNetClient::CancelSteamPoll()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_bPolling = false;
	m_strPollState.clear();
}

void CNetClient::RequestRegister(const std::string& strUsername, const std::string& strPassword, const std::string& strPubkey, const std::string& strOpenIdState, const std::string& strTicket)
{
	CJsonValue jBody = NetJson::Object();
	jBody.Set("username", strUsername);
	jBody.Set("password", strPassword);
	jBody.Set("pubkey", strPubkey);
	if (!strOpenIdState.empty())
		jBody.Set("openid_state", strOpenIdState);
	else if (!strTicket.empty())
		jBody.Set("ticket", strTicket);
	QueueJob({ ENetJob::POST_REGISTER, jBody.Dump() });
}

void CNetClient::RequestLogin(const std::string& strUsername, const std::string& strPassword, const std::string& strPubkey, const std::string& strOpenIdState, const std::string& strTicket)
{
	CJsonValue jBody = NetJson::Object();
	jBody.Set("username", strUsername);
	jBody.Set("password", strPassword);
	jBody.Set("pubkey", strPubkey);
	if (!strOpenIdState.empty())
		jBody.Set("openid_state", strOpenIdState);
	else if (!strTicket.empty())
		jBody.Set("ticket", strTicket);
	QueueJob({ ENetJob::POST_LOGIN, jBody.Dump() });
}

void CNetClient::RequestMe()
{
	QueueJob({ ENetJob::POST_ME, NetJson::Object().Dump() });
}

void CNetClient::SetAccountPubkey(const std::string& strPubkey)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_strAccountPubkey = strPubkey;
}

void CNetClient::UpdateProfile(const NetProfilePatch& patch)
{
	CJsonValue jBody = NetJson::Object();
	if (patch.m_bSetName)
		jBody.Set("name", patch.m_strName);
	if (patch.m_bSetStatus)
		jBody.Set("status", (std::max)(0, (std::min)(NET_STATUS_COUNT - 1, patch.m_nStatus)));
	if (patch.m_bSetListed)
		jBody.Set("listed", patch.m_bListed);
	if (patch.m_bSetBio)
		jBody.Set("bio", patch.m_strBio);
	if (patch.m_bSetAvatar)
		jBody.Set("avatar", patch.m_strAvatar);
	QueueJob({ ENetJob::POST_PROFILE, jBody.Dump() });
}

void CNetClient::RequestListing()
{
	QueueJob({ ENetJob::POST_LISTING, NetJson::Object().Dump() });
}

void CNetClient::RequestUser(const std::string& strUsername)
{
	QueueJob({ ENetJob::POST_USER, NetJson::MakeObject({ { "username", strUsername } }).Dump() });
}

void CNetClient::RequestAvatar(const std::string& strUsername)
{
	QueueJob({ ENetJob::POST_AVATAR, NetJson::MakeObject({ { "username", strUsername } }).Dump() });
}

void CNetClient::SendFriendRequest(const std::string& strUsername)
{
	QueueJob({ ENetJob::POST_FREQ, NetJson::MakeObject({ { "username", strUsername } }).Dump() });
}

void CNetClient::RespondFriendRequest(long long llId, bool bAccept)
{
	QueueJob({ ENetJob::POST_FRESPOND, NetJson::MakeObject({ { "id", llId }, { "accept", bAccept } }).Dump() });
}

void CNetClient::RemoveFriend(const std::string& strUsername)
{
	QueueJob({ ENetJob::POST_FREMOVE, NetJson::MakeObject({ { "username", strUsername } }).Dump() });
}

void CNetClient::SendDm(const std::string& strTo, const std::string& strNonce, const std::string& strCt)
{
	if (strCt.size() > (size_t)NET_MAX_CT_B64)
	{
		Fail("message too large");
		NetEvent ev;
		ev.m_eKind = ENetEventKind::DM_SENT;
		ev.m_bOk = false;
		ev.m_strError = "message too large";
		PushEvent(ev);
		return;
	}
	QueueJob({ ENetJob::POST_DM, NetJson::MakeObject({ { "to", strTo }, { "nonce", strNonce }, { "ct", strCt } }).Dump() });
}

void CNetClient::RequestInbox()
{
	QueueJob({ ENetJob::POST_INBOX, NetJson::Object().Dump() });
}

void CNetClient::AckMessages(const std::vector<long long>& vecIds)
{
	for (size_t i = 0; i < vecIds.size(); i += (size_t)NET_ACK_MAX_IDS)
	{
		const size_t nEnd = (std::min)(vecIds.size(), i + (size_t)NET_ACK_MAX_IDS);
		CJsonValue jIds = NetJson::Array();
		for (size_t j = i; j < nEnd; j++)
			jIds.Push(CJsonValue(vecIds[j]));
		QueueJob({ ENetJob::POST_ACK, NetJson::MakeObject({ { "ids", jIds } }).Dump() });
	}
}

void CNetClient::RequestLogout()
{
	QueueJob({ ENetJob::POST_LOGOUT, NetJson::Object().Dump() });
}

void CNetClient::ConnectWebSocket()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_strToken.empty())
		{
			m_strLastError = "not logged in";
			m_eConnection = ENetConnection::DISCONNECTED;
		}
		else
		{
			m_bWsWanted = true;
			if (m_eConnection == ENetConnection::DISCONNECTED)
				m_eConnection = ENetConnection::CONNECTING;
		}
	}
	m_cv.notify_all();
}

void CNetClient::DisconnectWebSocket()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_bWsWanted = false;
	}
	m_cv.notify_all();
}

void CNetClient::SendTyping(const std::string& strTo)
{
	if (strTo.empty())
		return;

	if (m_bPollMode)
	{
		QueueJob({ ENetJob::POST_TYPING, NetJson::MakeObject({ { "to", strTo } }).Dump() });
		return;
	}

	QueueJob({ ENetJob::WS_SEND_TYPING, NetJson::MakeObject({ { "t", NET_WS_TYPING }, { "to", strTo } }).Dump() });
}

void CNetClient::RequestSync()
{
	QueueJob({ ENetJob::POST_SYNC, NetJson::Object().Dump() });
}

void CNetClient::SendAck(const std::vector<long long>& vecIds)
{
	for (size_t i = 0; i < vecIds.size(); i += (size_t)NET_ACK_MAX_IDS)
	{
		const size_t nEnd = (std::min)(vecIds.size(), i + (size_t)NET_ACK_MAX_IDS);
		CJsonValue jIds = NetJson::Array();
		for (size_t j = i; j < nEnd; j++)
			jIds.Push(CJsonValue(vecIds[j]));
		QueueJob({ ENetJob::WS_SEND_ACK, NetJson::MakeObject({ { "t", NET_WS_ACK }, { "ids", jIds } }).Dump() });
	}
}

unsigned long long CNetClient::GetDataRevision() const
{
	return m_nDataRevision.load(std::memory_order_acquire);
}

void CNetClient::SendStatus(int nStatus)
{
	const int nClamped = (std::max)(0, (std::min)(NET_STATUS_COUNT - 1, nStatus));
	QueueJob({ ENetJob::WS_SEND_STATUS, NetJson::MakeObject({ { "t", NET_WS_STATUS }, { "status", nClamped } }).Dump() });
}

void CNetClient::SendAvatarPush(const std::string& strTo, const std::string& strData, long long llTs)
{
	if (strData.empty() || strData.size() > static_cast<size_t>(NET_AVATAR_MAX_B64))
		return;

	CJsonValue jBody = strTo.empty()
		? NetJson::MakeObject({ { "t", NET_WS_AVATAR }, { "ts", llTs > 0 ? llTs : 0 }, { "data", strData } })
		: NetJson::MakeObject({ { "t", NET_WS_AVATAR }, { "to", strTo }, { "ts", llTs > 0 ? llTs : 0 }, { "data", strData } });
	QueueJob({ ENetJob::WS_SEND_AVATAR, jBody.Dump() });
}

void CNetClient::SetEventSink(TNetEventFn fnSink)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_fnEventSink = std::move(fnSink);
}

void CNetClient::SetHttpSink(TNetHttpResultFn fnSink)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_fnHttpSink = std::move(fnSink);
}

void CNetClient::BumpUnread(const std::string& strUsername)
{
	if (strUsername.empty())
		return;
	std::lock_guard<std::mutex> lock(m_mutex);
	++m_mapUnread[strUsername];
}

void CNetClient::ClearUnread(const std::string& strUsername)
{
	if (strUsername.empty())
		return;
	std::lock_guard<std::mutex> lock(m_mutex);
	m_mapUnread.erase(strUsername);
}

bool CNetClient::CopyOwnUser(NetUser& out) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	out = m_ownUser;
	return !out.m_strUsername.empty();
}

std::vector<NetUser> CNetClient::CopyFriends() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_vecFriends;
}

std::vector<NetUser> CNetClient::CopyListing() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_vecListing;
}

std::vector<NetFriendReq> CNetClient::CopyReqs() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_vecReqs;
}

std::map<std::string, int> CNetClient::CopyUnread() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_mapUnread;
}

int CNetClient::GetUnread(const std::string& strUsername) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto it = m_mapUnread.find(strUsername);
	return it != m_mapUnread.end() ? it->second : 0;
}

ENetConnection CNetClient::GetConnection() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_eConnection;
}

bool CNetClient::IsWebSocketConnected() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_bWsConnected;
}

std::string CNetClient::CopyLastError() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_strLastError;
}

bool CNetClient::PopEvent(NetEvent& out)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_queueEvents.empty())
		return false;
	out = std::move(m_queueEvents.front());
	m_queueEvents.pop_front();
	return true;
}

void CNetClient::Worker()
{
	while (m_bRunning)
	{
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			const NetTimePoint tpNow = NetClock::now();
			NetTimePoint tpDeadline = tpNow + std::chrono::milliseconds(NETCLI_WORKER_IDLE_MS);
			if (m_bPolling)
				tpDeadline = (std::min)(tpDeadline, m_tpNextPoll);
			if (m_eConnection == ENetConnection::CONNECTING || m_eConnection == ENetConnection::CONNECTED)
				tpDeadline = (std::min)(tpDeadline, tpNow + std::chrono::milliseconds(NETCLI_WS_PUMP_MS));
			else if (m_eConnection == ENetConnection::WAIT_RETRY)
				tpDeadline = (std::min)(tpDeadline, m_tpRetryAt);
			m_cv.wait_until(lock, tpDeadline, [&]() { return !m_bRunning.load() || !m_queueJobs.empty(); });
		}

		if (!m_bRunning)
			break;

		while (m_bRunning)
		{
			NetJob job;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (m_queueJobs.empty())
					break;
			job = std::move(m_queueJobs.front());
			m_queueJobs.pop_front();
		}
		try
		{
			RunJob(job);
		}
		catch (const std::exception& e)
		{
			Fail(e.what());
		}
		catch (...)
		{
			Fail("worker exception");
		}
	}

	if (!m_bRunning)
		break;

	if (m_bPollMode && !m_strToken.empty())
	{
		const NetTimePoint tpNow = NetClock::now();
		if (tpNow >= m_tpNextSync)
		{
			m_tpNextSync = tpNow + std::chrono::milliseconds(NETCLI_POLL_INTERVAL_MS);
			QueueJob({ ENetJob::POST_SYNC, NetJson::Object().Dump() });
		}
	}

	try
	{
		PumpSteamPoll();
		PumpWebSocket();
	}
	catch (const std::exception& e)
	{
		Fail(e.what());
	}
	catch (...)
	{
		Fail("worker exception");
	}
}

	CloseWebSocket();
}

void CNetClient::QueueJob(const NetJob& job)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_queueJobs.size() >= NETCLI_MAX_JOBS)
			m_queueJobs.pop_front();
		m_queueJobs.push_back(job);
	}
	m_cv.notify_one();
}

void CNetClient::PushEvent(const NetEvent& ev)
{
	TNetEventFn fnSink;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_queueEvents.size() >= NETCLI_MAX_EVENTS)
			m_queueEvents.pop_front();
		m_queueEvents.push_back(ev);
		fnSink = m_fnEventSink;
	}
	if (fnSink)
		fnSink(ev);
}

void CNetClient::Fail(const std::string& strError)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_strLastError = strError.size() > NETCLI_LASTERROR_MAX ? strError.substr(0, NETCLI_LASTERROR_MAX) : strError;
}

void CNetClient::SetConnection(ENetConnection eState)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_eConnection = eState;
}

void CNetClient::RunJob(const NetJob& job)
{
	switch (job.m_eKind)
	{
	case ENetJob::NONE:
	{
		CloseWebSocket();
		std::lock_guard<std::mutex> lock(m_mutex);
		m_eConnection = ENetConnection::DISCONNECTED;
		break;
	}
	case ENetJob::WS_SEND_TYPING:
	case ENetJob::WS_SEND_ACK:
	case ENetJob::WS_SEND_STATUS:
	case ENetJob::WS_SEND_AVATAR:
	{
		SendWebSocketText(job.m_strBody);
		break;
	}
	default:
	{
		const wchar_t* wszPath = NetHttpPathForJob(job.m_eKind);
		int nStatus = 0;
		std::string strBody;
		const bool bOk = wszPath ? HttpPost(wszPath, job.m_strBody, nStatus, strBody) : false;

		TNetHttpResultFn fnSink;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			fnSink = m_fnHttpSink;
		}
		if (fnSink)
			fnSink(bOk, nStatus, strBody);

		HandleResponse(job, bOk, nStatus, strBody);
		break;
	}
	}
}

bool CNetClient::HttpPost(const wchar_t* wszPath, const std::string& strBody, int& nStatusOut, std::string& strBodyOut)
{
	nStatusOut = 0;
	strBodyOut.clear();

	if (m_bShuttingDown.load(std::memory_order_acquire))
		return false;

	const unsigned long long nGeneration = m_nSessionGeneration.load(std::memory_order_acquire);

	HINTERNET hConnection = nullptr;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		hConnection = m_hConnection;
	}

	if (!hConnection || !wszPath)
		return false;

	std::string strHeaders = "Content-Type: application/json\r\n";
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_strToken.empty())
			strHeaders += "Authorization: Bearer " + m_strToken + "\r\n";
	}
	const std::wstring wszHeaders = NetWiden(strHeaders);

	HINTERNET hRequest = WinHttpOpenRequest(hConnection, L"POST", wszPath, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!hRequest)
		return false;

	bool bOk = false;
	if (WinHttpSendRequest(hRequest, wszHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wszHeaders.c_str(), (DWORD)-1, (LPVOID)strBody.data(), (DWORD)strBody.size(), (DWORD)strBody.size(), 0))
	{
		if (WinHttpReceiveResponse(hRequest, nullptr))
		{
			DWORD dwStatus = 0;
			DWORD dwSize = sizeof(dwStatus);
			if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &dwStatus, &dwSize, WINHTTP_NO_HEADER_INDEX))
				nStatusOut = (int)dwStatus;

			while (true)
			{
				DWORD dwAvailable = 0;
				if (!WinHttpQueryDataAvailable(hRequest, &dwAvailable))
					break;
				if (dwAvailable == 0)
				{
					bOk = true;
					break;
				}
				if (strBodyOut.size() + (size_t)dwAvailable > NETCLI_HTTP_MAX_BODY)
					break;

				std::vector<char> vecChunk((size_t)dwAvailable);
				DWORD dwRead = 0;
				if (!WinHttpReadData(hRequest, vecChunk.data(), dwAvailable, &dwRead))
					break;
				if (dwRead == 0)
				{
					bOk = true;
					break;
				}
				strBodyOut.append(vecChunk.data(), dwRead);
			}
		}
	}

	if (m_nSessionGeneration.load(std::memory_order_acquire) == nGeneration)
		WinHttpCloseHandle(hRequest);
	return bOk;
}

void CNetClient::HandleResponse(const NetJob& job, bool bOk, int nStatus, const std::string& strBody)
{
	auto EmitFailure = [&](const std::string& strError)
	{
		NetEvent ev;
		ev.m_eKind = NetEventKindForJob(job.m_eKind);
		ev.m_bOk = false;
		ev.m_nStatus = nStatus;
		ev.m_strError = strError;
		PushEvent(ev);
	};

	bool bParsed = false;
	CJsonValue jRoot = CJsonValue::Parse(strBody, &bParsed);

	if (!bParsed || !jRoot.IsObject())
	{
		const std::string strError = bOk ? "malformed response" : "connection failed";
		Fail(strError);
		EmitFailure(strError);
		return;
	}

	if (!bOk || !NetJsonBool(jRoot, "ok"))
	{
		std::string strError = NetJsonStr(jRoot, "e");
		if (strError.empty())
			strError = bOk ? "request failed (" + std::to_string(nStatus) + ")" : "connection failed";
		Fail(strError);
		EmitFailure(strError);
		return;
	}

	switch (job.m_eKind)
	{
	case ENetJob::POST_REGISTER:
	case ENetJob::POST_LOGIN:
	{
		ApplyAuthPayload(jRoot);
		break;
	}
	case ENetJob::POST_ME:
	{
		ApplyMePayload(jRoot);
		break;
	}
	case ENetJob::POST_PROFILE:
	{
		ApplyMePayload(jRoot);
		NetUser user;
		CopyOwnUser(user);
		NetEvent ev;
		ev.m_eKind = ENetEventKind::PROFILE;
		ev.m_bOk = true;
		ev.m_User = user;
		PushEvent(ev);
		break;
	}
	case ENetJob::POST_LISTING:
	{
		ApplyListingPayload(jRoot);
		break;
	}
	case ENetJob::POST_AVATAR:
	{
		NetUser user;
		user.m_strUsername = NetCappedStr(jRoot, "username", NET_MAX_NAME);
		user.m_strAvatar = NetCappedStr(jRoot, "avatar", NETCLI_MAX_AVATAR_B64 + 8);
		user.m_llAvatarTs = NetJsonInt(jRoot, "avatar_ts");
		NetEvent ev;
		ev.m_eKind = ENetEventKind::AVATAR;
		ev.m_bOk = !user.m_strUsername.empty();
		ev.m_User = user;
		if (!ev.m_bOk)
			ev.m_strError = "malformed avatar";
		PushEvent(ev);
		break;
	}
	case ENetJob::POST_USER:
	{
		NetUser user;
		const CJsonValue* pUser = jRoot.Find("user");
		NetEvent ev;
		ev.m_eKind = ENetEventKind::USER;
		if (pUser && ParseUser(*pUser, user))
		{
			ev.m_bOk = true;
			ev.m_User = user;
		}
		else
		{
			ev.m_bOk = false;
			ev.m_strError = "malformed user";
		}
		PushEvent(ev);
		break;
	}
	case ENetJob::POST_FREQ:
	{
		std::string strResult;
		bool bFlag = false;
		NetUser user;
		const CJsonValue* pFriend = jRoot.Find("friend");
		const bool bFriend = pFriend && ParseUser(*pFriend, user);
		const CJsonValue* pSent = jRoot.Find("sent");
		const CJsonValue* pPending = jRoot.Find("pending");
		const CJsonValue* pResult = jRoot.Find("result");
		const CJsonValue* pStatus = jRoot.Find("status");
		if (bFriend)
		{
			strResult = "added";
			bFlag = true;
		}
		else if (pSent && pSent->IsBoolean() && pSent->GetBool())
			strResult = "sent";
		else if (pPending && pPending->IsBoolean() && pPending->GetBool())
		{
			strResult = "pending";
			bFlag = true;
		}
		else if (pResult && pResult->IsString())
			strResult = pResult->GetString();
		else if (pStatus && pStatus->IsString())
			strResult = pStatus->GetString();
		if (strResult.empty())
			strResult = "sent";
		if (bFriend)
			ApplyUserUpsert(user);
		NetEvent ev;
		ev.m_eKind = ENetEventKind::FRIEND_REQ;
		ev.m_bOk = true;
		ev.m_bFlag = bFlag;
		ev.m_strText = strResult;
		if (bFriend)
			ev.m_User = user;
		PushEvent(ev);
		break;
	}
	case ENetJob::POST_FRESPOND:
	{
		long long llId = 0;
		bool bAccept = false;
		bool bBodyOk = false;
		CJsonValue jBody = CJsonValue::Parse(job.m_strBody, &bBodyOk);
		if (bBodyOk && jBody.IsObject())
		{
			llId = NetJsonInt(jBody, "id");
			bAccept = NetJsonBool(jBody, "accept");
		}
		NetUser user;
		const CJsonValue* pFriend = jRoot.Find("friend");
		const bool bFriend = pFriend && ParseUser(*pFriend, user);
		if (bFriend)
			ApplyUserUpsert(user);
		NetEvent ev;
		ev.m_eKind = ENetEventKind::FRIEND_RESPOND;
		ev.m_bOk = true;
		ev.m_bFlag = bAccept;
		ev.m_llId = llId;
		if (bFriend)
			ev.m_User = user;
		PushEvent(ev);
		break;
	}
	case ENetJob::POST_FREMOVE:
	{
		std::string strUsername;
		bool bBodyOk = false;
		CJsonValue jBody = CJsonValue::Parse(job.m_strBody, &bBodyOk);
		if (bBodyOk && jBody.IsObject())
			strUsername = NetJsonStr(jBody, "username");
		if (!strUsername.empty())
			ApplyFriendRemove(strUsername);
		NetEvent ev;
		ev.m_eKind = ENetEventKind::FRIEND_REMOVE;
		ev.m_bOk = true;
		ev.m_strUsername = strUsername;
		PushEvent(ev);
		break;
	}
	case ENetJob::POST_DM:
	{
		NetEvent ev;
		ev.m_eKind = ENetEventKind::DM_SENT;
		ev.m_bOk = true;
		ev.m_llId = NetJsonInt(jRoot, "id");
		PushEvent(ev);
		break;
	}
	case ENetJob::POST_INBOX:
	{
		ApplyInboxPayload(jRoot);
		break;
	}
	case ENetJob::POST_ACK:
	{
		bool bBodyOk = false;
		CJsonValue jBody = CJsonValue::Parse(job.m_strBody, &bBodyOk);
		NetEvent ev;
		ev.m_eKind = ENetEventKind::ACK;
		ev.m_bOk = true;
		ev.m_llId = NetJsonInt(jRoot, "deleted");
		if (bBodyOk && jBody.IsObject())
			ev.m_vecIds = NetParseIdArray(jBody, "ids");
		PushEvent(ev);
		break;
	}
	case ENetJob::POST_LOGOUT:
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_strToken.clear();
			m_bWsWanted = false;
		}
		NetEvent ev;
		ev.m_eKind = ENetEventKind::LOGOUT;
		ev.m_bOk = true;
		PushEvent(ev);
		break;
	}
	case ENetJob::POST_SYNC:
	{
		std::vector<NetUser> vecFriends;
		const CJsonValue* pFriends = jRoot.Find("friends");
		if (pFriends && pFriends->IsArray())
		{
			for (const CJsonValue& jFriend : pFriends->GetArray())
			{
				NetUser friendUser;
				if (ParseUser(jFriend, friendUser) && !friendUser.m_strUsername.empty())
					vecFriends.push_back(std::move(friendUser));
			}
		}

		std::vector<NetFriendReq> vecReqs;
		const CJsonValue* pReqs = jRoot.Find("reqs");
		if (pReqs && pReqs->IsArray())
		{
			for (const CJsonValue& jReq : pReqs->GetArray())
			{
				NetFriendReq req;
				if (ParseFriendReq(jReq, req) && req.m_llId != 0)
					vecReqs.push_back(std::move(req));
			}
		}

		const CJsonValue* pTyping = jRoot.Find("typing");
		if (pTyping && pTyping->IsArray())
		{
			for (const CJsonValue& jFrom : pTyping->GetArray())
			{
				if (!jFrom.IsString())
					continue;

				NetEvent ev;
				ev.m_eKind = ENetEventKind::WS_TYPING;
				ev.m_strFrom = jFrom.GetString();
				if (!ev.m_strFrom.empty())
					PushEvent(ev);
			}
		}

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_vecFriends = std::move(vecFriends);
			m_vecReqs = std::move(vecReqs);
		}
		m_nDataRevision.fetch_add(1, std::memory_order_release);

		NetEvent ev;
		ev.m_eKind = ENetEventKind::SYNC;
		ev.m_bOk = true;
		PushEvent(ev);

		ApplyInboxPayload(jRoot);
		break;
	}
	case ENetJob::POST_TYPING:
		break;
	default:
		break;
	}
}

void CNetClient::ApplyAuthPayload(const CJsonValue& jRoot)
{
	const CJsonValue* pToken = jRoot.Find("token");
	const std::string strToken = (pToken && pToken->IsString()) ? pToken->GetString() : std::string();

	NetUser user;
	const CJsonValue* pUser = jRoot.Find("user");
	const bool bUser = pUser && ParseUser(*pUser, user);

	std::vector<NetUser> vecFriends;
	const CJsonValue* pFriends = jRoot.Find("friends");
	if (pFriends && pFriends->IsArray())
	{
		for (const CJsonValue& jFriend : pFriends->GetArray())
		{
			NetUser friendUser;
			if (ParseUser(jFriend, friendUser) && !friendUser.m_strUsername.empty())
				vecFriends.push_back(std::move(friendUser));
		}
	}

	std::vector<NetFriendReq> vecReqs;
	const CJsonValue* pReqs = jRoot.Find("reqs");
	if (pReqs && pReqs->IsArray())
	{
		for (const CJsonValue& jReq : pReqs->GetArray())
		{
			NetFriendReq req;
			if (ParseFriendReq(jReq, req) && req.m_llId != 0)
				vecReqs.push_back(std::move(req));
		}
	}

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!strToken.empty())
			m_strToken = strToken;
		if (bUser)
			m_ownUser = user;
		m_vecFriends = std::move(vecFriends);
		m_vecReqs = std::move(vecReqs);
		m_mapUnread.clear();
		m_eConnection = ENetConnection::DISCONNECTED;
		m_nDataRevision.fetch_add(1, std::memory_order_release);
	}

	NetEvent ev;
	ev.m_eKind = ENetEventKind::AUTH;
	ev.m_bOk = true;
	if (bUser)
		ev.m_User = user;
	PushEvent(ev);
}

void CNetClient::ApplyMePayload(const CJsonValue& jRoot)
{
	NetUser user;
	const CJsonValue* pUser = jRoot.Find("user");
	const bool bUser = pUser && ParseUser(*pUser, user);

	std::vector<NetFriendReq> vecReqs;
	const CJsonValue* pReqs = jRoot.Find("reqs");
	if (pReqs && pReqs->IsArray())
	{
		for (const CJsonValue& jReq : pReqs->GetArray())
		{
			NetFriendReq req;
			if (ParseFriendReq(jReq, req) && req.m_llId != 0)
				vecReqs.push_back(std::move(req));
		}
	}

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (bUser)
			m_ownUser = user;
		m_vecReqs = std::move(vecReqs);
		m_nDataRevision.fetch_add(1, std::memory_order_release);
	}

	NetEvent ev;
	ev.m_eKind = ENetEventKind::ME;
	ev.m_bOk = true;
	if (bUser)
		ev.m_User = user;
	PushEvent(ev);
}

void CNetClient::ApplyListingPayload(const CJsonValue& jRoot)
{
	std::vector<NetUser> vecListing;
	const CJsonValue* pUsers = jRoot.Find("users");
	if (pUsers && pUsers->IsArray())
	{
		for (const CJsonValue& jUser : pUsers->GetArray())
		{
			NetUser user;
			if (ParseUser(jUser, user) && !user.m_strUsername.empty())
				vecListing.push_back(std::move(user));
		}
	}

	const int nCount = (int)vecListing.size();
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_vecListing = std::move(vecListing);
		m_nDataRevision.fetch_add(1, std::memory_order_release);
	}

	NetEvent ev;
	ev.m_eKind = ENetEventKind::LISTING;
	ev.m_bOk = true;
	ev.m_nStatus = nCount;
	PushEvent(ev);
}

void CNetClient::ApplyInboxPayload(const CJsonValue& jRoot)
{
	std::vector<NetDm> vecDms;
	const CJsonValue* pInbox = jRoot.Find("inbox");
	if (pInbox && pInbox->IsArray())
	{
		for (const CJsonValue& jDm : pInbox->GetArray())
		{
			NetDm dm;
			if (ParseDm(jDm, dm) && dm.m_llId != 0)
				vecDms.push_back(std::move(dm));
		}
	}

	const int nUnread = (int)NetJsonInt(jRoot, "unread");

	NetEvent ev;
	ev.m_eKind = ENetEventKind::INBOX;
	ev.m_bOk = true;
	ev.m_nStatus = nUnread;
	ev.m_vecDms = std::move(vecDms);
	PushEvent(ev);
}

bool CNetClient::ParseUser(const CJsonValue& jValue, NetUser& out)
{
	if (!jValue.IsObject())
		return false;
	out.m_strUsername = NetCappedStr(jValue, "username", NET_MAX_NAME);
	out.m_strName = NetCappedStr(jValue, "name", NET_MAX_NAME);
	out.m_nStatus = (int)NetJsonInt(jValue, "status");
	out.m_bListed = NetJsonBool(jValue, "listed");
	out.m_strBio = NetCappedStr(jValue, "bio", NET_MAX_BIO);
	out.m_bOnline = NetJsonBool(jValue, "online");
	out.m_llLastSeen = NetJsonInt(jValue, "last_seen");
	out.m_strAvatar = NetCappedStr(jValue, "avatar", NETCLI_MAX_AVATAR_B64 + 8);
	out.m_llAvatarTs = NetJsonInt(jValue, "avatar_ts");
	out.m_strPubkey = NetCappedStr(jValue, "pubkey", 128);
	return true;
}

bool CNetClient::ParseFriendReq(const CJsonValue& jValue, NetFriendReq& out)
{
	if (!jValue.IsObject())
		return false;
	out.m_llId = NetJsonInt(jValue, "id");
	out.m_strFrom = NetCappedStr(jValue, "from", 32);
	out.m_strUsername = NetCappedStr(jValue, "username", NET_MAX_NAME);
	out.m_strName = NetCappedStr(jValue, "name", NET_MAX_NAME);
	return true;
}

bool CNetClient::ParseDm(const CJsonValue& jValue, NetDm& out)
{
	if (!jValue.IsObject())
		return false;
	out.m_llId = NetJsonInt(jValue, "id");
	out.m_strFrom = NetCappedStr(jValue, "from", 32);
	out.m_strNonce = NetCappedStr(jValue, "nonce", 32);
	out.m_strCt = NetCappedStr(jValue, "ct", (size_t)NET_MAX_CT_B64);
	out.m_llTs = NetJsonInt(jValue, "ts");
	if (out.m_strNonce.size() < 16)
		return false;
	if (out.m_strCt.empty() || out.m_strCt.size() > (size_t)NET_MAX_CT_B64)
		return false;
	return true;
}

void CNetClient::ApplyUserUpsert(const NetUser& user)
{
	if (user.m_strUsername.empty())
		return;
	std::lock_guard<std::mutex> lock(m_mutex);
	for (NetUser& rFriend : m_vecFriends)
	{
		if (rFriend.m_strUsername == user.m_strUsername)
		{
			rFriend = user;
			m_nDataRevision.fetch_add(1, std::memory_order_release);
			return;
		}
	}
	m_vecFriends.push_back(user);
	m_nDataRevision.fetch_add(1, std::memory_order_release);
}

void CNetClient::ApplyFriendRemove(const std::string& strUsername)
{
	if (strUsername.empty())
		return;
	std::lock_guard<std::mutex> lock(m_mutex);
	for (auto it = m_vecFriends.begin(); it != m_vecFriends.end(); ++it)
	{
		if (it->m_strUsername == strUsername)
		{
			m_vecFriends.erase(it);
			m_nDataRevision.fetch_add(1, std::memory_order_release);
			break;
		}
	}
	m_mapUnread.erase(strUsername);
}

void CNetClient::PumpSteamPoll()
{
	std::string strState;
	NetTimePoint tpTick;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_bPolling)
			return;
		const NetTimePoint tpNow = NetClock::now();
		if (tpNow < m_tpNextPoll)
			return;
		tpTick = tpNow;
		strState = m_strPollState;
		m_tpNextPoll = tpNow + std::chrono::milliseconds(NET_STEAM_POLL_INTERVAL_MS);
	}

	int nStatus = 0;
	std::string strBody;
	const bool bOk = HttpPost(NET_PATH_STEAM_POLL_W, NetJson::MakeObject({ { "state", strState } }).Dump(), nStatus, strBody);

	int nElapsed = 0;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_bPolling)
			return;
		m_nPollElapsedMs += (int)std::chrono::duration_cast<std::chrono::milliseconds>(NetClock::now() - tpTick).count();
		nElapsed = m_nPollElapsedMs;
	}

	bool bParsed = false;
	CJsonValue jRoot = CJsonValue::Parse(strBody, &bParsed);

	if (bOk && bParsed && jRoot.IsObject() && NetJsonBool(jRoot, "ok") && NetJsonBool(jRoot, "ready"))
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_bPolling = false;
		}
		NetEvent ev;
		ev.m_eKind = ENetEventKind::STEAM_POLL;
		ev.m_bOk = true;
		ev.m_bFlag = true;
		ev.m_strSteamId = NetJsonStr(jRoot, "steamid");
		PushEvent(ev);
		return;
	}

	if (bOk && bParsed && jRoot.IsObject() && !NetJsonBool(jRoot, "ok"))
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_bPolling = false;
		}
		std::string strError = NetJsonStr(jRoot, "e");
		if (strError.empty())
			strError = "steam auth failed";
		NetEvent ev;
		ev.m_eKind = ENetEventKind::STEAM_POLL;
		ev.m_bOk = false;
		ev.m_strError = strError;
		PushEvent(ev);
		return;
	}

	if (nElapsed >= NET_STEAM_POLL_TIMEOUT_MS)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_bPolling = false;
		}
		NetEvent ev;
		ev.m_eKind = ENetEventKind::STEAM_POLL;
		ev.m_bOk = false;
		ev.m_strError = "steam auth timed out";
		PushEvent(ev);
	}
}

void CNetClient::PumpWebSocket()
{
	NetWsRuntime& ws = *m_ws;

	auto KillConnection = [&](const std::string& strReason)
	{
		Fail(strReason);
		CloseWebSocket();

		if (!m_bPollMode)
		{
			m_nWsFailCount++;
			if (m_nWsFailCount >= NETCLI_WS_FAILS_BEFORE_POLL)
			{
				m_bPollMode = true;
				m_tpNextSync = NetClock::now();
				SetConnection(ENetConnection::CONNECTED);
				NetEvent evUp;
				evUp.m_eKind = ENetEventKind::WS_OPENED;
				evUp.m_bOk = true;
				PushEvent(evUp);
			}
			else
			{
				ScheduleReconnect();
				NetEvent ev;
				ev.m_eKind = ENetEventKind::WS_CLOSED;
				ev.m_strError = strReason;
				PushEvent(ev);
			}
		}
	};

	auto AdvanceHandshake = [&]()
	{
		auto KillHandshake = [&](const std::string& strReason)
		{
			if (m_nWsProxyMode == 0)
				m_nWsProxyMode = 1;
			KillConnection(strReason);
		};

		if (NetClock::now() - ws.m_tpHandshake > std::chrono::milliseconds(NETCLI_HANDSHAKE_TIMEOUT_MS))
		{
			KillHandshake("websocket handshake timed out");
			return;
		}

		if (ws.m_bRequestError)
		{
			KillHandshake("websocket handshake failed (" + std::to_string(ws.m_dwError.load()) + ")");
			return;
		}

		if (!ws.m_bUpgraded || !ws.m_hWebSocket)
			return;

		HINTERNET hWebSocket = ws.m_hWebSocket;
		WinHttpCloseHandle(ws.m_hRequest);
		ws.m_hRequest = nullptr;
		ws.m_bStarted = false;
		ws.m_bReceiveArmed = false;
		ws.m_bSendOutstanding = false;
		ws.m_bCloseSeen = false;
		ws.m_bWsHandleClosed = false;

		std::string strToken;
		std::string strAccountPubkey;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_hWsRequest = hWebSocket;
			m_bWsConnected = true;
			m_tpLastReceive = NetClock::now();
			m_tpWsUpAt = m_tpLastReceive;
			m_tpNextPing = m_tpLastReceive + std::chrono::milliseconds(NET_WS_PING_INTERVAL_MS);
			strToken = m_strToken;
			strAccountPubkey = m_strAccountPubkey;
		}

		const std::string strHello = NetJson::MakeObject({ { "t", NET_WS_HELLO }, { "token", strToken }, { "pubkey", strAccountPubkey } }).Dump();
		if (!SendWebSocketText(strHello))
		{
			KillConnection("websocket hello failed");
			return;
		}

		SetConnection(ENetConnection::CONNECTED);

		NetEvent ev;
		ev.m_eKind = ENetEventKind::WS_OPENED;
		ev.m_bOk = true;
		PushEvent(ev);

		if (!NetArmReceive(ws, hWebSocket))
			KillConnection("receive failed");
	};

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_bWsWanted && !m_bWsConnected && m_eConnection == ENetConnection::DISCONNECTED)
			m_eConnection = ENetConnection::CONNECTING;
	}

	bool bWanted = false;
	bool bConnected = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		bWanted = m_bWsWanted;
		bConnected = m_bWsConnected;
	}

	if (!bWanted)
	{
		if (bConnected || ws.m_bStarted || ws.m_hSession || ws.m_hConnect || ws.m_hRequest || m_hWsRequest)
		{
			CloseWebSocket();
			SetConnection(ENetConnection::DISCONNECTED);
			NetEvent ev;
			ev.m_eKind = ENetEventKind::WS_CLOSED;
			ev.m_strError = "disconnected";
			PushEvent(ev);
		}
		return;
	}

	if (bConnected)
	{
		const NetTimePoint tpNow = NetClock::now();

		if (ws.m_bWsHandleClosed || ws.m_bOverflow || ws.m_bCloseSeen || ws.m_bRequestError)
		{
			std::string strReason = "connection lost";
			if (ws.m_bOverflow)
				strReason = "message overflow";
			else if (ws.m_bCloseSeen)
				strReason = "server closed connection";
			else if (ws.m_bRequestError)
				strReason = "connection error (" + std::to_string(ws.m_dwError.load()) + ")";
			KillConnection(strReason);
			return;
		}

		NetTimePoint tpLastReceive;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			tpLastReceive = m_tpLastReceive;
		}
		if (tpNow - tpLastReceive > std::chrono::milliseconds(NET_WS_SILENCE_TIMEOUT_MS))
		{
			KillConnection("connection timed out");
			return;
		}

		std::string strMessage;
		while (PollWebSocketText(strMessage))
		{
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_tpLastReceive = tpNow;
			}
			if ((m_nWsFailCount != 0 || m_bPollMode) && tpNow - m_tpWsUpAt > std::chrono::seconds(10))
			{
				m_nWsFailCount = 0;
				m_bPollMode = false;
			}
			DispatchWebSocketText(strMessage);
			strMessage.clear();
		}

		if (ws.m_bCloseSeen || ws.m_bOverflow || ws.m_bWsHandleClosed || ws.m_bRequestError)
		{
			std::string strReason = "connection lost";
			if (ws.m_bOverflow)
				strReason = "message overflow";
			else if (ws.m_bCloseSeen)
				strReason = "server closed connection";
			else if (ws.m_bRequestError)
				strReason = "connection error (" + std::to_string(ws.m_dwError.load()) + ")";
			KillConnection(strReason);
			return;
		}

		bool bPing = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const NetTimePoint tpCheck = NetClock::now();
			if (tpCheck >= m_tpNextPing)
			{
				m_tpNextPing = tpCheck + std::chrono::milliseconds(NET_WS_PING_INTERVAL_MS);
				bPing = true;
			}
		}
		if (bPing)
			SendWebSocketText(NetJson::MakeObject({ { "t", NET_WS_PING } }).Dump());

		while (!ws.m_bSendOutstanding && !ws.m_vecPendingSends.empty())
		{
			std::string strNext = std::move(ws.m_vecPendingSends.front());
			ws.m_vecPendingSends.pop_front();
			SendWebSocketText(strNext);
		}

		if (!NetArmReceive(ws, m_hWsRequest))
			KillConnection("receive failed");
		return;
	}

	if (ws.m_bStarted)
	{
		AdvanceHandshake();
		return;
	}

	bool bStart = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_eConnection == ENetConnection::WAIT_RETRY)
		{
			if (NetClock::now() >= m_tpRetryAt)
			{
				m_eConnection = ENetConnection::CONNECTING;
				bStart = true;
			}
		}
		else if (m_eConnection == ENetConnection::CONNECTING)
		{
			bStart = true;
		}
	}
	if (bStart)
		OpenWebSocket();
}

bool CNetClient::OpenWebSocket()
{
	NetWsRuntime& ws = *m_ws;

	CloseWebSocket();

	ws.m_bSendRequestDone = false;
	ws.m_bResponseRequested = false;
	ws.m_bHeadersAvailable = false;
	ws.m_bUpgraded = false;
	ws.m_bRequestError = false;
	ws.m_bReceiveArmed = false;
	ws.m_bSendOutstanding = false;
	ws.m_bCloseSeen = false;
	ws.m_bWsHandleClosed = false;
	ws.m_bOverflow = false;
	ws.m_dwError = 0;
	ws.m_strPartial.clear();
	ws.m_strSendBuffer.clear();
	ws.m_vecPendingSends.clear();

	const DWORD dwAccessType = m_nWsProxyMode == 0 ? WINHTTP_ACCESS_TYPE_DEFAULT_PROXY : WINHTTP_ACCESS_TYPE_NO_PROXY;
	HINTERNET hSession = WinHttpOpen(g_wszNetUserAgent, dwAccessType, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
	if (!hSession)
	{
		Fail("websocket init failed");
		ScheduleReconnect();
		return false;
	}

	const DWORD dwNotificationFlags = WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_STATUS_CLOSE_COMPLETE | WINHTTP_CALLBACK_STATUS_SHUTDOWN_COMPLETE | WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING;
	if (WinHttpSetStatusCallback(hSession, NetWsStatusCallback, dwNotificationFlags, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
	{
		WinHttpCloseHandle(hSession);
		Fail("websocket init failed");
		ScheduleReconnect();
		return false;
	}

	DWORD_PTR dwContext = reinterpret_cast<DWORD_PTR>(&ws);
	WinHttpSetOption(hSession, WINHTTP_OPTION_CONTEXT_VALUE, &dwContext, sizeof(dwContext));

	DWORD dwTimeout = (DWORD)NET_HTTP_TIMEOUT_MS;
	WinHttpSetOption(hSession, WINHTTP_OPTION_RESOLVE_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
	WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
	WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
	DWORD dwReceiveTimeout = (DWORD)NETCLI_WS_RECEIVE_TIMEOUT_MS;
	WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &dwReceiveTimeout, sizeof(dwReceiveTimeout));

	HINTERNET hConnect = WinHttpConnect(hSession, NET_SERVER_HOST_W, INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hConnect)
	{
		WinHttpCloseHandle(hSession);
		Fail("websocket connect failed");
		ScheduleReconnect();
		return false;
	}
	WinHttpSetOption(hConnect, WINHTTP_OPTION_CONTEXT_VALUE, &dwContext, sizeof(dwContext));

	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", NET_PATH_WS_W, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!hRequest)
	{
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		Fail("websocket request failed");
		ScheduleReconnect();
		return false;
	}
	WinHttpSetOption(hRequest, WINHTTP_OPTION_CONTEXT_VALUE, &dwContext, sizeof(dwContext));

	if (!WinHttpSetOption(hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0))
	{
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		Fail("websocket upgrade unsupported");
		ScheduleReconnect();
		return false;
	}

	ws.m_hSession = hSession;
	ws.m_hConnect = hConnect;
	ws.m_hRequest = hRequest;
	ws.m_tpHandshake = NetClock::now();
	ws.m_bStarted = true;

	if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
	{
		ws.m_bSendRequestDone = true;
		return true;
	}

	if (GetLastError() == ERROR_IO_PENDING)
		return true;

	ws.m_bStarted = false;
	CloseWebSocket();
	Fail("websocket handshake failed");
	ScheduleReconnect();
	return false;
}

void CNetClient::CloseWebSocket()
{
	if (m_ws)
	{
		NetWsRuntime& ws = *m_ws;
		ws.m_bStarted = false;
		ws.m_bSendRequestDone = false;
		ws.m_bResponseRequested = false;
		ws.m_bHeadersAvailable = false;
		ws.m_bUpgraded = false;
		ws.m_bRequestError = false;
		ws.m_bReceiveArmed = false;
		ws.m_bSendOutstanding = false;
		ws.m_bCloseSeen = false;
		ws.m_bWsHandleClosed = false;
		ws.m_bOverflow = false;
		ws.m_dwError = 0;
		ws.m_hWebSocket = nullptr;

		{
			std::lock_guard<std::mutex> lock(ws.m_mtxFrames);
			ws.m_vecFrames.clear();
		}
		ws.m_strPartial.clear();
		ws.m_strSendBuffer.clear();
		ws.m_vecPendingSends.clear();
	}

	if (m_hWsRequest)
	{
		WinHttpWebSocketClose(m_hWsRequest, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
		WinHttpCloseHandle(m_hWsRequest);
		m_hWsRequest = nullptr;
	}
	if (m_ws && m_ws->m_hRequest)
	{
		WinHttpCloseHandle(m_ws->m_hRequest);
		m_ws->m_hRequest = nullptr;
	}
	if (m_ws && m_ws->m_hConnect)
	{
		WinHttpCloseHandle(m_ws->m_hConnect);
		m_ws->m_hConnect = nullptr;
	}
	if (m_ws && m_ws->m_hSession)
	{
		WinHttpCloseHandle(m_ws->m_hSession);
		m_ws->m_hSession = nullptr;
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	m_bWsConnected = false;
}

bool CNetClient::SendWebSocketText(const std::string& strText)
{
	NetWsRuntime& ws = *m_ws;

	if (!m_hWsRequest || ws.m_bWsHandleClosed)
		return false;

	if (ws.m_bSendOutstanding)
	{
		if (ws.m_vecPendingSends.size() >= NETCLI_WS_MAX_PENDING)
			ws.m_vecPendingSends.pop_front();
		ws.m_vecPendingSends.push_back(strText);
		return true;
	}

	ws.m_strSendBuffer = strText;

	const DWORD dwResult = WinHttpWebSocketSend(m_hWsRequest, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (PVOID)ws.m_strSendBuffer.data(), (DWORD)ws.m_strSendBuffer.size());
	if (dwResult == ERROR_IO_PENDING)
	{
		ws.m_bSendOutstanding = true;
		return true;
	}
	return dwResult == NO_ERROR;
}

bool CNetClient::PollWebSocketText(std::string& strOut)
{
	NetWsRuntime& ws = *m_ws;

	while (true)
	{
		int eType = -1;
		std::string strChunk;
		{
			std::lock_guard<std::mutex> lock(ws.m_mtxFrames);
			if (ws.m_vecFrames.empty())
				return false;
			eType = ws.m_vecFrames.front().first;
			strChunk = std::move(ws.m_vecFrames.front().second);
			ws.m_vecFrames.pop_front();
		}

		if (eType == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE || eType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
		{
			ws.m_strPartial += strChunk;
			if (ws.m_strPartial.size() > NETCLI_WS_MAX_PARTIAL)
			{
				ws.m_strPartial.clear();
				ws.m_bOverflow = true;
				return false;
			}
			if (eType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
			{
				strOut = std::move(ws.m_strPartial);
				ws.m_strPartial.clear();
				return true;
			}
		}
		else if (eType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
		{
			ws.m_bCloseSeen = true;
		}
		else
		{
			ws.m_strPartial.clear();
		}
	}
}

void CNetClient::DispatchWebSocketText(const std::string& strText)
{
	bool bParsed = false;
	CJsonValue jRoot = CJsonValue::Parse(strText, &bParsed);
	if (!bParsed || !jRoot.IsObject())
		return;

	const CJsonValue* pType = jRoot.Find("t");
	const std::string strType = (pType && pType->IsString()) ? pType->GetString() : std::string();
	if (strType.empty())
		return;

	if (strType == NET_WS_WELCOME)
	{
		NetUser user;
		const CJsonValue* pUser = jRoot.Find("user");
		const bool bUser = pUser && ParseUser(*pUser, user);

		std::vector<NetUser> vecFriends;
		const CJsonValue* pFriends = jRoot.Find("friends");
		if (pFriends && pFriends->IsArray())
		{
			for (const CJsonValue& jFriend : pFriends->GetArray())
			{
				NetUser friendUser;
				if (ParseUser(jFriend, friendUser) && !friendUser.m_strUsername.empty())
					vecFriends.push_back(std::move(friendUser));
			}
		}

		std::vector<NetFriendReq> vecReqs;
		const CJsonValue* pReqs = jRoot.Find("reqs");
		if (pReqs && pReqs->IsArray())
		{
			for (const CJsonValue& jReq : pReqs->GetArray())
			{
				NetFriendReq req;
				if (ParseFriendReq(jReq, req) && req.m_llId != 0)
					vecReqs.push_back(std::move(req));
			}
		}

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (bUser)
				m_ownUser = user;
			m_vecFriends = std::move(vecFriends);
			m_vecReqs = std::move(vecReqs);
			m_nDataRevision.fetch_add(1, std::memory_order_release);
		}

		NetEvent ev;
		ev.m_eKind = ENetEventKind::SYNC;
		ev.m_bOk = true;
		PushEvent(ev);
		return;
	}

	if (strType == NET_WS_DM)
	{
		NetDm dm;
		if (!ParseDm(jRoot, dm) || dm.m_llId == 0)
			return;
		NetEvent ev;
		ev.m_eKind = ENetEventKind::WS_DM;
		ev.m_bOk = true;
		ev.m_llId = dm.m_llId;
		ev.m_llTs = dm.m_llTs;
		ev.m_strFrom = dm.m_strFrom;
		ev.m_strNonce = dm.m_strNonce;
		ev.m_strCt = dm.m_strCt;
		ev.m_vecDms.push_back(std::move(dm));
		PushEvent(ev);
		return;
	}

	if (strType == NET_WS_TYPING)
	{
		const std::string strFrom = NetJsonStr(jRoot, "from");
		if (strFrom.empty())
			return;
		NetEvent ev;
		ev.m_eKind = ENetEventKind::WS_TYPING;
		ev.m_bOk = true;
		ev.m_strFrom = strFrom;
		PushEvent(ev);
		return;
	}

	if (strType == NET_WS_AVATAR)
	{
		const std::string strFrom = NetJsonStr(jRoot, "from");
		const std::string strData = NetJsonStr(jRoot, "data");
		if (strFrom.empty() || strData.empty() || strData.size() > static_cast<size_t>(NET_AVATAR_MAX_B64))
			return;
		NetEvent ev;
		ev.m_eKind = ENetEventKind::WS_AVATAR;
		ev.m_bOk = true;
		ev.m_strFrom = strFrom;
		ev.m_strData = strData;
		ev.m_llTs = NetJsonInt(jRoot, "ts");
		PushEvent(ev);
		return;
	}

	if (strType == NET_WS_PRESENCE)
	{
		std::string strId;
		const CJsonValue* pU = jRoot.Find("u");
		if (pU && pU->IsString())
			strId = pU->GetString();
		else if (pU && pU->IsObject())
		{
			NetUser tmp;
			if (ParseUser(*pU, tmp))
				strId = tmp.m_strUsername;
		}
		if (strId.empty())
			return;

		const bool bOnline = NetJsonBool(jRoot, "online");
		bool bHasStatus = false;
		int nStatus = 0;
		const CJsonValue* pStatus = jRoot.Find("status");
		if (pStatus && pStatus->IsNumber())
		{
			bHasStatus = true;
			nStatus = (int)pStatus->GetInt();
		}

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			for (NetUser& rFriend : m_vecFriends)
			{
				if (rFriend.m_strUsername != strId)
					continue;
				rFriend.m_bOnline = bOnline;
				if (bHasStatus)
					rFriend.m_nStatus = nStatus;
				m_nDataRevision.fetch_add(1, std::memory_order_release);
				break;
			}
		}

		NetEvent ev;
		ev.m_eKind = ENetEventKind::WS_PRESENCE;
		ev.m_bOk = true;
		ev.m_bFlag = bOnline;
		ev.m_nStatus = nStatus;
		ev.m_strFrom = strId;
		PushEvent(ev);
		return;
	}

	if (strType == NET_WS_FRIEND_ADD || strType == NET_WS_FRIEND_UPD)
	{
		NetUser user;
		const CJsonValue* pU = jRoot.Find("u");
		if (!pU || !ParseUser(*pU, user) || user.m_strUsername.empty())
			return;
		ApplyUserUpsert(user);
		NetEvent ev;
		ev.m_eKind = (strType == NET_WS_FRIEND_ADD) ? ENetEventKind::WS_FRIEND_ADD : ENetEventKind::WS_FRIEND_UPD;
		ev.m_bOk = true;
		ev.m_User = user;
		PushEvent(ev);
		return;
	}

	if (strType == NET_WS_FRIEND_DEL)
	{
		std::string strId;
		const CJsonValue* pU = jRoot.Find("u");
		if (pU && pU->IsString())
			strId = pU->GetString();
		else if (pU && pU->IsObject())
		{
			NetUser tmp;
			if (ParseUser(*pU, tmp))
				strId = tmp.m_strUsername;
		}
		if (strId.empty())
			return;
		ApplyFriendRemove(strId);
		NetEvent ev;
		ev.m_eKind = ENetEventKind::WS_FRIEND_DEL;
		ev.m_bOk = true;
		ev.m_strUsername = strId;
		PushEvent(ev);
		return;
	}

	if (strType == NET_WS_FREQ)
	{
		NetFriendReq req;
		req.m_llId = NetJsonInt(jRoot, "id");
		req.m_strFrom = NetJsonStr(jRoot, "from");
		req.m_strUsername = NetJsonStr(jRoot, "username");
		req.m_strName = NetJsonStr(jRoot, "name");
		if (req.m_strFrom.empty() && req.m_strUsername.empty())
			return;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (req.m_llId != 0)
			{
				m_vecReqs.push_back(std::move(req));
				while (m_vecReqs.size() > NETCLI_MAX_REQS)
					m_vecReqs.erase(m_vecReqs.begin());
				m_nDataRevision.fetch_add(1, std::memory_order_release);
			}
		}
		NetEvent ev;
		ev.m_eKind = ENetEventKind::WS_FREQ;
		ev.m_bOk = true;
		ev.m_llId = NetJsonInt(jRoot, "id");
		ev.m_strFrom = NetJsonStr(jRoot, "from");
		ev.m_strUsername = NetJsonStr(jRoot, "username");
		ev.m_strName = NetJsonStr(jRoot, "name");
		PushEvent(ev);
		return;
	}

	if (strType == NET_WS_ACKD)
	{
		NetEvent ev;
		ev.m_eKind = ENetEventKind::WS_ACKD;
		ev.m_bOk = true;
		ev.m_vecIds = NetParseIdArray(jRoot, "ids");
		PushEvent(ev);
		return;
	}

	if (strType == NET_WS_ERR)
	{
		const std::string strError = NetJsonStr(jRoot, "e");
		Fail(strError.empty() ? "server error" : strError);
		NetEvent ev;
		ev.m_eKind = ENetEventKind::WS_ERROR;
		ev.m_bOk = false;
		ev.m_strError = strError;
		PushEvent(ev);
		return;
	}
}

void CNetClient::ScheduleReconnect()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_bWsWanted)
	{
		m_eConnection = ENetConnection::DISCONNECTED;
		return;
	}
	m_eConnection = ENetConnection::WAIT_RETRY;
	m_tpRetryAt = NetClock::now() + std::chrono::milliseconds(NET_RECONNECT_DELAY_MS);
}
