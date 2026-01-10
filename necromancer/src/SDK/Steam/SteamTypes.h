#pragma once

typedef unsigned char uint8;
typedef signed char int8;
typedef short int16;
typedef unsigned short uint16;
typedef int int32;
typedef unsigned int uint32;
typedef long long int64;
typedef unsigned long long uint64;

typedef int32 HSteamPipe;
typedef int32 HSteamUser;

enum EUniverse
{
	k_EUniverseInvalid = 0,
	k_EUniversePublic = 1,
	k_EUniverseBeta = 2,
	k_EUniverseInternal = 3,
	k_EUniverseDev = 4,
	k_EUniverseMax
};

enum EAccountType
{
	k_EAccountTypeInvalid = 0,
	k_EAccountTypeIndividual = 1,
	k_EAccountTypeMultiseat = 2,
	k_EAccountTypeGameServer = 3,
	k_EAccountTypeAnonGameServer = 4,
	k_EAccountTypePending = 5,
	k_EAccountTypeContentServer = 6,
	k_EAccountTypeClan = 7,
	k_EAccountTypeChat = 8,
	k_EAccountTypeConsoleUser = 9,
	k_EAccountTypeAnonUser = 10,
	k_EAccountTypeMax
};

enum EFriendFlags
{
	k_EFriendFlagNone = 0x00,
	k_EFriendFlagBlocked = 0x01,
	k_EFriendFlagFriendshipRequested = 0x02,
	k_EFriendFlagImmediate = 0x04,
	k_EFriendFlagClanMember = 0x08,
	k_EFriendFlagOnGameServer = 0x10,
	k_EFriendFlagRequestingFriendship = 0x80,
	k_EFriendFlagRequestingInfo = 0x100,
	k_EFriendFlagIgnored = 0x200,
	k_EFriendFlagIgnoredFriend = 0x400,
	k_EFriendFlagChatMember = 0x1000,
	k_EFriendFlagAll = 0xFFFF,
};

#pragma pack(push, 1)
class CSteamID
{
public:
	CSteamID() : m_unAccountID(0), m_unAccountInstance(1), m_EAccountType(k_EAccountTypeInvalid), m_EUniverse(k_EUniverseInvalid) {}
	
	CSteamID(uint32 unAccountID, EUniverse eUniverse, EAccountType eAccountType)
		: m_unAccountID(unAccountID), m_unAccountInstance(1), m_EAccountType(eAccountType), m_EUniverse(eUniverse) {}

	CSteamID(uint64 ulSteamID)
	{
		m_unAccountID = static_cast<uint32>(ulSteamID & 0xFFFFFFFF);
		m_unAccountInstance = static_cast<uint32>((ulSteamID >> 32) & 0xFFFFF);
		m_EAccountType = static_cast<EAccountType>((ulSteamID >> 52) & 0xF);
		m_EUniverse = static_cast<EUniverse>((ulSteamID >> 56) & 0xFF);
	}

	uint64 ConvertToUint64() const
	{
		return static_cast<uint64>(m_unAccountID) |
			(static_cast<uint64>(m_unAccountInstance) << 32) |
			(static_cast<uint64>(m_EAccountType) << 52) |
			(static_cast<uint64>(m_EUniverse) << 56);
	}

	uint32 GetAccountID() const { return m_unAccountID; }

private:
	uint32 m_unAccountID : 32;
	uint32 m_unAccountInstance : 20;
	EAccountType m_EAccountType : 4;
	EUniverse m_EUniverse : 8;
};
#pragma pack(pop)
