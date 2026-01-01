#pragma once

#include "SteamTypes.h"

class ISteamUtils
{
public:
	virtual uint32 GetSecondsSinceAppActive() = 0;
	virtual uint32 GetSecondsSinceComputerActive() = 0;
	virtual int GetConnectedUniverse() = 0;
	virtual uint32 GetServerRealTime() = 0;
	virtual const char* GetIPCountry() = 0;
	virtual bool GetImageSize(int iImage, uint32* pnWidth, uint32* pnHeight) = 0;
	virtual bool GetImageRGBA(int iImage, uint8* pubDest, int nDestBufferSize) = 0;
};
