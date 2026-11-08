#pragma once
#include "../../../SDK/SDK.h"

#include <string>

bool RefreshSteamSession();

class INetSteamUser
{
public:
	virtual HSteamUser GetHSteamUser() = 0;
	virtual bool BLoggedOn() = 0;
	virtual CSteamID GetSteamID() = 0;
	virtual int InitiateGameConnection(void* pAuthBlob, int cbMaxAuthBlob, CSteamID steamIDGameServer, uint32 unIPServer, uint16 usPortServer, bool bSecure) = 0;
	virtual void TerminateGameConnection(uint32 unIPServer, uint16 usPortServer) = 0;
	virtual void TrackAppUsageEvent(CSteamID gameIDOfEvent, int eAppUsageEvent, const char* pchExtraInfo) = 0;
	virtual bool GetUserDataFolder(char* pchBuffer, int cubBuffer) = 0;
	virtual void StartVoiceRecording() = 0;
	virtual void StopVoiceRecording() = 0;
	virtual int GetAvailableVoice(uint32* pcbCompressed, uint32* pcbUncompressed, uint32 nUncompressedVoiceDesired) = 0;
	virtual int GetVoice(bool bWantCompressed, void* pDestBuffer, uint32 cbDestBufferSize, uint32* nBytesWritten, bool bWantUncompressed, void* pUncompressedDestBuffer, uint32 cbUncompressedDestBufferSize, uint32* nUncompressBytesWritten, uint32 nUncompressedVoiceDesired) = 0;
	virtual int DecompressVoice(const void* pCompressed, uint32 cbCompressed, void* pDestBuffer, uint32 cbDestBufferSize, uint32* nBytesWritten, uint32 nExpectedSampleRate) = 0;
	virtual uint32 GetVoiceOptimalSampleRate() = 0;
};

class CNetSteam
{
public:
	bool Refresh();
	bool IsAvailable() const;
	std::string GetSteamId() const;
	bool OpenOverlayPage(const std::string& strUrl) const;
	bool OpenInBrowser(const std::string& strUrl) const;

private:
	INetSteamUser* AcquireSteamUser();
	bool IsOverlayAvailable() const;

	HMODULE m_hSteamApi = nullptr;
	INetSteamUser* m_pSteamUser = nullptr;
	std::string m_strSteamId;
	bool m_bAvailable = false;
};
