#pragma once

#include <functional>
#include <string>
#include <vector>

inline constexpr const char* NET_SERVER_HOST = "necromancer.matsurimagic6.workers.dev";
inline constexpr wchar_t NET_SERVER_HOST_W[] = L"necromancer.matsurimagic6.workers.dev";
inline constexpr const char* NET_SERVER_BASE = "https://necromancer.matsurimagic6.workers.dev";

inline constexpr const char* NET_PATH_REGISTER = "/register";
inline constexpr const char* NET_PATH_LOGIN = "/login";
inline constexpr const char* NET_PATH_ME = "/me";
inline constexpr const char* NET_PATH_LISTING = "/listing";
inline constexpr const char* NET_PATH_USER = "/user";
inline constexpr const char* NET_PATH_AVATAR = "/avatar";
inline constexpr const char* NET_PATH_FREQ = "/freq";
inline constexpr const char* NET_PATH_FRESPOND = "/frespond";
inline constexpr const char* NET_PATH_FREMOVE = "/fremove";
inline constexpr const char* NET_PATH_DM = "/dm";
inline constexpr const char* NET_PATH_INBOX = "/inbox";
inline constexpr const char* NET_PATH_ACK = "/ack";
inline constexpr const char* NET_PATH_LOGOUT = "/logout";
inline constexpr const char* NET_PATH_SYNC = "/sync";
inline constexpr const char* NET_PATH_TYPING = "/typing";
inline constexpr const char* NET_PATH_STEAM_POLL = "/steam/poll";
inline constexpr const char* NET_PATH_STEAM_START = "/steam/start";
inline constexpr const char* NET_PATH_WS = "/ws";

inline constexpr wchar_t NET_PATH_REGISTER_W[] = L"/register";
inline constexpr wchar_t NET_PATH_LOGIN_W[] = L"/login";
inline constexpr wchar_t NET_PATH_ME_W[] = L"/me";
inline constexpr wchar_t NET_PATH_LISTING_W[] = L"/listing";
inline constexpr wchar_t NET_PATH_USER_W[] = L"/user";
inline constexpr wchar_t NET_PATH_AVATAR_W[] = L"/avatar";
inline constexpr wchar_t NET_PATH_FREQ_W[] = L"/freq";
inline constexpr wchar_t NET_PATH_FRESPOND_W[] = L"/frespond";
inline constexpr wchar_t NET_PATH_FREMOVE_W[] = L"/fremove";
inline constexpr wchar_t NET_PATH_DM_W[] = L"/dm";
inline constexpr wchar_t NET_PATH_INBOX_W[] = L"/inbox";
inline constexpr wchar_t NET_PATH_ACK_W[] = L"/ack";
inline constexpr wchar_t NET_PATH_LOGOUT_W[] = L"/logout";
inline constexpr wchar_t NET_PATH_SYNC_W[] = L"/sync";
inline constexpr wchar_t NET_PATH_TYPING_W[] = L"/typing";
inline constexpr wchar_t NET_PATH_STEAM_POLL_W[] = L"/steam/poll";
inline constexpr wchar_t NET_PATH_WS_W[] = L"/ws";

inline constexpr int NET_STATUS_COUNT = 6;
inline constexpr int NET_USERNAME_MIN = 3;
inline constexpr int NET_USERNAME_MAX = 16;
inline constexpr int NET_PASSWORD_MIN = 6;
inline constexpr int NET_PASSWORD_MAX = 128;
inline constexpr int NET_MAX_NAME = 32;
inline constexpr int NET_MAX_BIO = 200;
inline constexpr int NET_MAX_MESSAGE_CHARS = 4000;
	inline constexpr int NET_MAX_CT_B64 = 8192;
	inline constexpr int NET_ACK_MAX_IDS = 500;
inline constexpr int NET_HTTP_TIMEOUT_MS = 15000;
inline constexpr int NET_STEAM_POLL_INTERVAL_MS = 2000;
inline constexpr int NET_STEAM_POLL_TIMEOUT_MS = 180000;
inline constexpr int NET_WS_PING_INTERVAL_MS = 30000;
inline constexpr int NET_WS_SILENCE_TIMEOUT_MS = 75000;
inline constexpr int NET_TYPING_THROTTLE_MS = 2000;
inline constexpr int NET_TYPING_VISIBLE_MS = 6000;
inline constexpr int NET_RECONNECT_DELAY_MS = 3000;
inline constexpr int NET_AVATAR_MAX_B64 = 32768;

inline constexpr const char* NET_STATUS_NAMES[NET_STATUS_COUNT] = { "Online", "Invisible", "Busy", "Looking", "Duel", "Away" };

inline constexpr const char* NET_WS_HELLO = "hello";
inline constexpr const char* NET_WS_TYPING = "typing";
inline constexpr const char* NET_WS_ACK = "ack";
inline constexpr const char* NET_WS_STATUS = "status";
inline constexpr const char* NET_WS_PING = "ping";
inline constexpr const char* NET_WS_PONG = "pong";
inline constexpr const char* NET_WS_WELCOME = "welcome";
inline constexpr const char* NET_WS_DM = "dm";
inline constexpr const char* NET_WS_PRESENCE = "presence";
inline constexpr const char* NET_WS_FRIEND_ADD = "friend_add";
inline constexpr const char* NET_WS_FRIEND_UPD = "friend_upd";
inline constexpr const char* NET_WS_FRIEND_DEL = "friend_del";
inline constexpr const char* NET_WS_FREQ = "freq";
inline constexpr const char* NET_WS_AVATAR = "avatar";
inline constexpr const char* NET_WS_ACKD = "ackd";
inline constexpr const char* NET_WS_ERR = "err";

enum class ENetStatus { ONLINE = 0, INVISIBLE = 1, BUSY = 2, LOOKING = 3, DUEL = 4, AWAY = 5 };
enum class ENetConnection { DISCONNECTED, CONNECTING, CONNECTED, WAIT_RETRY };

enum class ENetJob
{
	NONE,
	POST_REGISTER,
	POST_LOGIN,
	POST_ME,
	POST_PROFILE,
	POST_LISTING,
	POST_USER,
	POST_AVATAR,
	POST_FREQ,
	POST_FRESPOND,
	POST_FREMOVE,
	POST_DM,
	POST_INBOX,
	POST_ACK,
	POST_LOGOUT,
	POST_SYNC,
	POST_TYPING,
	WS_SEND_TYPING,
	WS_SEND_ACK,
	WS_SEND_STATUS,
	WS_SEND_AVATAR,
};

enum class ENetEventKind
{
	NONE,
	STEAM_POLL,
	AUTH,
	ME,
	PROFILE,
	LISTING,
	USER,
	AVATAR,
	FRIEND_REQ,
	FRIEND_RESPOND,
	FRIEND_REMOVE,
	DM_SENT,
	INBOX,
	ACK,
	LOGOUT,
	SYNC,
	WS_OPENED,
	WS_CLOSED,
	WS_DM,
	WS_TYPING,
	WS_PRESENCE,
	WS_FRIEND_ADD,
	WS_FRIEND_UPD,
	WS_FRIEND_DEL,
	WS_FREQ,
	WS_ACKD,
	WS_ERROR,
	WS_AVATAR,
};

struct NetUser
{
	std::string m_strUsername;
	std::string m_strName;
	int m_nStatus = 0;
	bool m_bListed = false;
	std::string m_strBio;
	bool m_bOnline = false;
	long long m_llLastSeen = 0;
	std::string m_strAvatar;
	long long m_llAvatarTs = 0;
	std::string m_strPubkey;
};

struct NetFriendReq
{
	long long m_llId = 0;
	std::string m_strFrom;
	std::string m_strUsername;
	std::string m_strName;
};

struct NetDm
{
	long long m_llId = 0;
	std::string m_strFrom;
	std::string m_strNonce;
	std::string m_strCt;
	long long m_llTs = 0;
};

struct NetHistoryLine
{
	bool m_bOut = false;
	std::string m_strFrom;
	std::string m_strText;
	long long m_llTs = 0;
};

struct NetProfile
{
	std::string m_strToken;
	std::string m_strSteamId;
	std::string m_strUsername;
	std::string m_strName;
	std::string m_strPubkey;
	std::string m_strPrivKey;
};

struct NetProfilePatch
{
	bool m_bSetName = false;
	std::string m_strName;
	bool m_bSetStatus = false;
	int m_nStatus = 0;
	bool m_bSetListed = false;
	bool m_bListed = false;
	bool m_bSetBio = false;
	std::string m_strBio;
	bool m_bSetAvatar = false;
	std::string m_strAvatar;
};

struct NetEvent
{
	ENetEventKind m_eKind = ENetEventKind::NONE;
	bool m_bOk = false;
	bool m_bFlag = false;
	int m_nStatus = 0;
	long long m_llId = 0;
	long long m_llTs = 0;
	std::string m_strError;
	std::string m_strFrom;
	std::string m_strSteamId;
	std::string m_strUsername;
	std::string m_strName;
	std::string m_strNonce;
	std::string m_strCt;
	std::string m_strText;
	std::string m_strData;
	std::vector<long long> m_vecIds;
	std::vector<NetDm> m_vecDms;
	NetUser m_User = {};
};

using TNetEventFn = std::function<void(const NetEvent&)>;
using TNetHttpResultFn = std::function<void(bool bOk, int nStatus, const std::string& strBody)>;

inline std::string NetMakeSteamStartUrl(const std::string& strState)
{
	return std::string(NET_SERVER_BASE) + NET_PATH_STEAM_START + "?state=" + strState;
}

class CJsonValue;
class CNetCrypto;
class CNetClient;
class CNetStore;
class CNetSteam;
class CNecroNet;
