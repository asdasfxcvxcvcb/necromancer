#pragma once

#include "SteamTypes.h"
#include "../../Utils/Memory/Memory.h"

// Signature to get the SteamNetworkingUtils interface
MAKE_SIGNATURE(Get_SteamNetworkingUtils, "client.dll", "40 53 48 83 EC ? 48 8B D9 48 8D 15 ? ? ? ? 33 C9 FF 15 ? ? ? ? 33 C9", 0x0);

// Steam Networking POP ID (Point of Presence / Data Center ID)
// This is a 4-character code packed into a uint32
typedef uint32_t SteamNetworkingPOPID;

// Minimal ISteamNetworkingUtils interface - only what we need for region selector
class ISteamNetworkingUtils
{
public:
	// Virtual function table - we only need GetPingToDataCenter at index 8
	// Indices 0-7 are other functions we don't need
	virtual void* _vfunc0() = 0; // AllocateMessage
	virtual void* _vfunc1() = 0; // GetRelayNetworkStatus  
	virtual void* _vfunc2() = 0; // GetLocalPingLocation
	virtual void* _vfunc3() = 0; // EstimatePingTimeBetweenTwoLocations
	virtual void* _vfunc4() = 0; // EstimatePingTimeFromLocalHost
	virtual void* _vfunc5() = 0; // ConvertPingLocationToString
	virtual void* _vfunc6() = 0; // ParsePingLocationString
	virtual void* _vfunc7() = 0; // CheckPingDataUpToDate
	
	// Index 8 - This is what we hook
	virtual int GetPingToDataCenter(SteamNetworkingPOPID popID, SteamNetworkingPOPID* pViaRelayPoP) = 0;
	
	// Index 9
	virtual int GetDirectPingToPOP(SteamNetworkingPOPID popID) = 0;
	
	// Index 10
	virtual int GetPOPCount() = 0;
	
	// Index 11
	virtual int GetPOPList(SteamNetworkingPOPID* list, int nListSz) = 0;
};
