#include "../../SDK/SDK.h"
#include "../../Utils/HookManager/HookManager.h"

#include "../Features/CFG.h"

// Region bit flags
namespace RegionSelector
{
	enum ERegion : uint32_t
	{
		// North America
		ATL = 1 << 0,  // Atlanta
		ORD = 1 << 1,  // Chicago
		DFW = 1 << 2,  // Dallas
		LAX = 1 << 3,  // Los Angeles
		SEA = 1 << 4,  // Seattle
		IAD = 1 << 5,  // Virginia
		// Europe
		AMS = 1 << 6,  // Amsterdam
		FSN = 1 << 7,  // Falkenstein
		FRA = 1 << 8,  // Frankfurt
		HEL = 1 << 9,  // Helsinki
		LHR = 1 << 10, // London
		MAD = 1 << 11, // Madrid
		PAR = 1 << 12, // Paris
		STO = 1 << 13, // Stockholm
		VIE = 1 << 14, // Vienna
		WAW = 1 << 15, // Warsaw
		// South America
		EZE = 1 << 16, // Buenos Aires
		LIM = 1 << 17, // Lima
		SCL = 1 << 18, // Santiago
		GRU = 1 << 19, // Sao Paulo
		// Asia
		MAA = 1 << 20, // Chennai
		DXB = 1 << 21, // Dubai
		HKG = 1 << 22, // Hong Kong
		BOM = 1 << 23, // Mumbai
		SEO = 1 << 24, // Seoul
		SGP = 1 << 25, // Singapore
		TYO = 1 << 26, // Tokyo
		// Australia
		SYD = 1 << 27, // Sydney
		// Africa
		JNB = 1 << 28, // Johannesburg
	};

	// FNV-1a hash for datacenter code lookup
	constexpr uint32_t FNV1a_32(const char* str)
	{
		uint32_t hash = 2166136261u;
		while (*str)
		{
			hash ^= static_cast<uint32_t>(*str++);
			hash *= 16777619u;
		}
		return hash;
	}

	// Get datacenter region from POP ID code
	inline uint32_t GetDatacenter(uint32_t uHash)
	{
		// Pre-computed hashes for datacenter codes
		constexpr uint32_t HASH_ATL = FNV1a_32("atl");
		constexpr uint32_t HASH_ORD = FNV1a_32("ord");
		constexpr uint32_t HASH_DFW = FNV1a_32("dfw");
		constexpr uint32_t HASH_LAX = FNV1a_32("lax");
		constexpr uint32_t HASH_SEA = FNV1a_32("sea");
		constexpr uint32_t HASH_EAT = FNV1a_32("eat");
		constexpr uint32_t HASH_IAD = FNV1a_32("iad");
		constexpr uint32_t HASH_AMS = FNV1a_32("ams");
		constexpr uint32_t HASH_AMS4 = FNV1a_32("ams4");
		constexpr uint32_t HASH_FSN = FNV1a_32("fsn");
		constexpr uint32_t HASH_FRA = FNV1a_32("fra");
		constexpr uint32_t HASH_HEL = FNV1a_32("hel");
		constexpr uint32_t HASH_LHR = FNV1a_32("lhr");
		constexpr uint32_t HASH_MAD = FNV1a_32("mad");
		constexpr uint32_t HASH_PAR = FNV1a_32("par");
		constexpr uint32_t HASH_STO = FNV1a_32("sto");
		constexpr uint32_t HASH_STO2 = FNV1a_32("sto2");
		constexpr uint32_t HASH_VIE = FNV1a_32("vie");
		constexpr uint32_t HASH_WAW = FNV1a_32("waw");
		constexpr uint32_t HASH_EZE = FNV1a_32("eze");
		constexpr uint32_t HASH_LIM = FNV1a_32("lim");
		constexpr uint32_t HASH_SCL = FNV1a_32("scl");
		constexpr uint32_t HASH_GRU = FNV1a_32("gru");
		constexpr uint32_t HASH_MAA2 = FNV1a_32("maa2");
		constexpr uint32_t HASH_DXB = FNV1a_32("dxb");
		constexpr uint32_t HASH_HKG = FNV1a_32("hkg");
		constexpr uint32_t HASH_BOM2 = FNV1a_32("bom2");
		constexpr uint32_t HASH_SEO = FNV1a_32("seo");
		constexpr uint32_t HASH_SGP = FNV1a_32("sgp");
		constexpr uint32_t HASH_TYO = FNV1a_32("tyo");
		constexpr uint32_t HASH_SYD = FNV1a_32("syd");
		constexpr uint32_t HASH_JNB = FNV1a_32("jnb");

		switch (uHash)
		{
		case HASH_ATL: return ATL;
		case HASH_ORD: return ORD;
		case HASH_DFW: return DFW;
		case HASH_LAX: return LAX;
		case HASH_SEA:
		case HASH_EAT: return SEA;
		case HASH_IAD: return IAD;
		case HASH_AMS:
		case HASH_AMS4: return AMS;
		case HASH_FSN: return FSN;
		case HASH_FRA: return FRA;
		case HASH_HEL: return HEL;
		case HASH_LHR: return LHR;
		case HASH_MAD: return MAD;
		case HASH_PAR: return PAR;
		case HASH_STO:
		case HASH_STO2: return STO;
		case HASH_VIE: return VIE;
		case HASH_WAW: return WAW;
		case HASH_EZE: return EZE;
		case HASH_LIM: return LIM;
		case HASH_SCL: return SCL;
		case HASH_GRU: return GRU;
		case HASH_MAA2: return MAA;
		case HASH_DXB: return DXB;
		case HASH_HKG: return HKG;
		case HASH_BOM2: return BOM;
		case HASH_SEO: return SEO;
		case HASH_SGP: return SGP;
		case HASH_TYO: return TYO;
		case HASH_SYD: return SYD;
		case HASH_JNB: return JNB;
		}
		return 0;
	}

	// Get enabled regions from config
	inline uint32_t GetEnabledRegions()
	{
		if (!CFG::Exploits_Region_Selector_Active)
			return 0;

		uint32_t flags = 0;
		// North America
		if (CFG::Exploits_Region_ATL) flags |= ATL;
		if (CFG::Exploits_Region_ORD) flags |= ORD;
		if (CFG::Exploits_Region_DFW) flags |= DFW;
		if (CFG::Exploits_Region_LAX) flags |= LAX;
		if (CFG::Exploits_Region_SEA) flags |= SEA;
		if (CFG::Exploits_Region_IAD) flags |= IAD;
		// Europe
		if (CFG::Exploits_Region_AMS) flags |= AMS;
		if (CFG::Exploits_Region_FRA) flags |= FRA;
		if (CFG::Exploits_Region_HEL) flags |= HEL;
		if (CFG::Exploits_Region_LHR) flags |= LHR;
		if (CFG::Exploits_Region_MAD) flags |= MAD;
		if (CFG::Exploits_Region_PAR) flags |= PAR;
		if (CFG::Exploits_Region_STO) flags |= STO;
		if (CFG::Exploits_Region_VIE) flags |= VIE;
		if (CFG::Exploits_Region_WAW) flags |= WAW;
		// South America
		if (CFG::Exploits_Region_EZE) flags |= EZE;
		if (CFG::Exploits_Region_LIM) flags |= LIM;
		if (CFG::Exploits_Region_SCL) flags |= SCL;
		if (CFG::Exploits_Region_GRU) flags |= GRU;
		// Asia
		if (CFG::Exploits_Region_MAA) flags |= MAA;
		if (CFG::Exploits_Region_DXB) flags |= DXB;
		if (CFG::Exploits_Region_HKG) flags |= HKG;
		if (CFG::Exploits_Region_BOM) flags |= BOM;
		if (CFG::Exploits_Region_SEO) flags |= SEO;
		if (CFG::Exploits_Region_SGP) flags |= SGP;
		if (CFG::Exploits_Region_TYO) flags |= TYO;
		// Australia
		if (CFG::Exploits_Region_SYD) flags |= SYD;
		// Africa
		if (CFG::Exploits_Region_JNB) flags |= JNB;

		return flags;
	}
}

// Convert POP ID to string
static void PopIdName(SteamNetworkingPOPID popID, char* out)
{
	out[0] = static_cast<char>(popID >> 16);
	out[1] = static_cast<char>(popID >> 8);
	out[2] = static_cast<char>(popID);
	out[3] = static_cast<char>(popID >> 24);
	out[4] = 0;
}

// FNV-1a hash at runtime
static uint32_t FNV1a_Hash(const char* str)
{
	uint32_t hash = 2166136261u;
	while (*str)
	{
		hash ^= static_cast<uint32_t>(*str++);
		hash *= 16777619u;
	}
	return hash;
}

// We need to handle the case where SteamNetworkingUtils might be null
// Use a conditional hook that only creates if the interface exists
namespace Hooks
{
	namespace ISteamNetworkingUtils_GetPingToDataCenter
	{
		void Init();
		inline CHook Hook(Init);
		using fn = int(__fastcall*)(void*, SteamNetworkingPOPID, SteamNetworkingPOPID*);
		int __fastcall Func(void* rcx, SteamNetworkingPOPID popID, SteamNetworkingPOPID* pViaRelayPoP);
	}
}

void Hooks::ISteamNetworkingUtils_GetPingToDataCenter::Init()
{
	if (I::SteamNetworkingUtils)
	{
		Hook.Create(Memory::GetVFunc(I::SteamNetworkingUtils, 8), Func);
	}
}

int __fastcall Hooks::ISteamNetworkingUtils_GetPingToDataCenter::Func(void* rcx, SteamNetworkingPOPID popID, SteamNetworkingPOPID* pViaRelayPoP)
{
	int iReturn = Hook.Original<fn>()(rcx, popID, pViaRelayPoP);
	
	uint32_t uEnabledRegions = RegionSelector::GetEnabledRegions();
	if (!uEnabledRegions || iReturn < 0)
		return iReturn;

	char sPopID[5];
	PopIdName(popID, sPopID);
	
	uint32_t uHash = FNV1a_Hash(sPopID);
	if (auto uDatacenter = RegionSelector::GetDatacenter(uHash))
	{
		// If this datacenter is in our enabled list, return low ping (1ms = preferred)
		// Otherwise return high ping (1000ms = avoided)
		bool bEnabled = (uEnabledRegions & uDatacenter) != 0;
		return bEnabled ? 1 : 1000;
	}

	return iReturn;
}
