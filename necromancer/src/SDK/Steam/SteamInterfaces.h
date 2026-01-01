#pragma once

#include "../../Utils/InterfaceManager/InterfaceManager.h"
#include "ISteamClient.h"
#include "ISteamFriends.h"
#include "ISteamUtils.h"
#include "ISteamNetworkingUtils.h"

#define STEAMCLIENT_INTERFACE_VERSION "SteamClient017"
#define STEAMFRIENDS_INTERFACE_VERSION "SteamFriends015"
#define STEAMUTILS_INTERFACE_VERSION "SteamUtils007"

MAKE_INTERFACE_VERSION(ISteamClient, SteamClient, "steamclient64.dll", STEAMCLIENT_INTERFACE_VERSION);
MAKE_INTERFACE_NULL(ISteamFriends, SteamFriends);
MAKE_INTERFACE_NULL(ISteamUtils, SteamUtils);
MAKE_INTERFACE_NULL(ISteamNetworkingUtils, SteamNetworkingUtils);
