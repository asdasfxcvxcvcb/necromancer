#include "../../SDK/SDK.h"

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <dsound.h>
#include <unordered_map>
#include <mutex>

#include "../Features/Misc/Soundpad/Soundpad.h"
#include "ISteamUser_GetVoice.h"

namespace
{
	void* ResolveSteamUser()
	{
		if (!I::SteamClient)
			return nullptr;

		static HSteamPipe s_hPipe = 0;
		static HSteamUser s_hUser = 0;
		if (!s_hPipe)
			s_hPipe = I::SteamClient->CreateSteamPipe();
		if (!s_hUser)
			s_hUser = I::SteamClient->ConnectToGlobalUser(s_hPipe);
		if (!s_hUser)
			return nullptr;

		return I::SteamClient->GetISteamUser(s_hUser, s_hPipe, "SteamUser023");
	}

	void* ResolveGetVoiceTarget()
	{
		if (!SoundpadEnabled)
			return nullptr;
		static void* s_pTarget = nullptr;
		if (s_pTarget)
			return s_pTarget;

		void* pUser = ResolveSteamUser();
		if (!pUser)
			return nullptr;

		const auto pfnOptimalRate = reinterpret_cast<uint32(__fastcall*)(void*)>((*reinterpret_cast<void***>(pUser))[12]);
		if (pfnOptimalRate)
			F::Soundpad->SetVoiceOptimalRate(pfnOptimalRate(pUser));

		s_pTarget = Memory::GetVFunc(pUser, 10);
		F::Soundpad->SetSteamUser(pUser);
		F::Soundpad->MarkVoiceHooked();
		return s_pTarget;
	}

	void* ResolveGetAvailableTarget()
	{
		if (!SoundpadEnabled)
			return nullptr;
		static void* s_pTarget = nullptr;
		if (s_pTarget)
			return s_pTarget;

		void* pUser = ResolveSteamUser();
		if (!pUser)
			return nullptr;

		s_pTarget = Memory::GetVFunc(pUser, 9);
		F::Soundpad->SetSteamUser(pUser);
		F::Soundpad->MarkVoiceHooked();
		return s_pTarget;
	}

}

MAKE_HOOK(ISteamUser_GetVoice, ResolveGetVoiceTarget(), int, __fastcall,
	void* ecx, bool bWantCompressed, void* pDestBuffer, uint32 cbDestBufferSize, uint32* nBytesWritten,
	bool bWantUncompressed, void* pUncompressedDestBuffer, uint32 cbUncompressedDestBufferSize,
	uint32* nUncompressBytesWritten, uint32 nUncompressedVoiceDesiredSampleRate)
{
	uint32 nOrigWritten = 0;
	const int nResult = CALL_ORIGINAL(ecx, bWantCompressed, pDestBuffer, cbDestBufferSize, &nOrigWritten,
		bWantUncompressed, pUncompressedDestBuffer, cbUncompressedDestBufferSize, nUncompressBytesWritten,
		nUncompressedVoiceDesiredSampleRate);

	if (F::Soundpad->ShouldReplaceVoice())
	{
		F::Soundpad->FeedMicAudio(ecx, pDestBuffer, nResult == 0 ? nOrigWritten : 0, nUncompressedVoiceDesiredSampleRate);
		if (cbDestBufferSize <= 2048)
		{
			uint32 nWritten = 0;
			if (F::Soundpad->BuildVoiceBlob(pDestBuffer, cbDestBufferSize, nWritten))
			{
				if (nBytesWritten)
					*nBytesWritten = nWritten;
				F::Soundpad->SelfTestBlob(pDestBuffer, nWritten, nUncompressedVoiceDesiredSampleRate);
				return 0;
			}
			if (nBytesWritten)
				*nBytesWritten = 0;
			return 0;
		}
	}

	if (nResult != 0)
	{
		if (nBytesWritten)
			*nBytesWritten = 0;
		return nResult;
	}

	if (nBytesWritten)
		*nBytesWritten = nOrigWritten;
	return nResult;
}

MAKE_HOOK(ISteamUser_GetAvailableVoice, ResolveGetAvailableTarget(), int, __fastcall,
	void* ecx, uint32* pcbCompressed, uint32* pcbUncompressed, uint32 nUncompressVoiceDesiredSampleRate)
{
	const int nResult = CALL_ORIGINAL(ecx, pcbCompressed, pcbUncompressed, nUncompressVoiceDesiredSampleRate);

	if (!F::Soundpad->ShouldReplaceVoice())
		return nResult;

	const uint32 nPending = F::Soundpad->GetPendingVoiceSize();
	if (!nPending)
	{
		if (pcbCompressed)
			*pcbCompressed = 0;
		if (pcbUncompressed)
			*pcbUncompressed = 0;
		return 0;
	}

	if (pcbCompressed)
		*pcbCompressed = nPending;
	if (pcbUncompressed)
		*pcbUncompressed = 0;
	return 0;
}

namespace
{
	struct CaptureFormat_t
	{
		unsigned nRate = 48000;
		int nChannels = 2;
		bool bFloat = true;
	};

	std::mutex s_mutexCapture;
	std::unordered_map<void*, CaptureFormat_t> s_mapCaptureFormats;
	void* s_pfnAudioClientInitialize = nullptr;
	void* s_pfnAudioClientGetService = nullptr;
	void* s_pfnCaptureGetBuffer = nullptr;
	bool s_bClientHooksCreated = false;
	bool s_bCaptureHookCreated = false;

	void* s_pfnDSCreate = nullptr;
	void* s_pfnDSCaptureBufferCreate = nullptr;
	void* s_pfnDSLock = nullptr;
	bool s_bDSBufferHookCreated = false;

	struct DSCBUFFERDESC_Mine
	{
		DWORD dwSize;
		DWORD dwFlags;
		DWORD dwBufferBytes;
		DWORD dwReserved;
		const WAVEFORMATEX* lpwfxFormat;
	};

	void* ResolveDSCreate()
	{
		if (!SoundpadEnabled)
			return nullptr;
		HMODULE hDsound = GetModuleHandleW(L"dsound.dll");
		if (!hDsound)
			hDsound = LoadLibraryW(L"dsound.dll");
		if (!hDsound)
			return nullptr;
		return reinterpret_cast<void*>(GetProcAddress(hDsound, "DirectSoundCaptureCreate"));
	}

	void* ResolveDeviceActivate()
	{
		if (!SoundpadEnabled)
			return nullptr;
		const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		void* pFn = nullptr;
		IMMDeviceEnumerator* pEnum = nullptr;
		IMMDevice* pDevice = nullptr;
		if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&pEnum))) && pEnum)
		{
			if (SUCCEEDED(pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice)) && pDevice)
			{
				void** pVtable = *reinterpret_cast<void***>(pDevice);
				pFn = pVtable[3];
				pDevice->Release();
			}
			pEnum->Release();
		}
		if (SUCCEEDED(hrCo))
			CoUninitialize();
		return pFn;
	}
}

MAKE_HOOK(IAudioCaptureClient_GetBuffer, s_pfnCaptureGetBuffer, HRESULT, __stdcall,
	void* ecx, BYTE** ppData, UINT32* pNumFramesToRead, DWORD* pdwFlags)
{
	const HRESULT hr = CALL_ORIGINAL(ecx, ppData, pNumFramesToRead, pdwFlags);
	if (hr == S_OK && ppData && *ppData && pNumFramesToRead && *pNumFramesToRead > 0)
	{
		CaptureFormat_t fmt;
		{
			std::lock_guard<std::mutex> lock(s_mutexCapture);
			const auto it = s_mapCaptureFormats.find(ecx);
			if (it == s_mapCaptureFormats.end())
				return hr;
			fmt = it->second;
		}
		F::Soundpad->MixMicCapture(*ppData, *pNumFramesToRead, fmt.nRate, fmt.nChannels, fmt.bFloat);
		if (pdwFlags)
			*pdwFlags &= ~AUDCLNT_BUFFERFLAGS_SILENT;
	}
	return hr;
}

MAKE_HOOK(IAudioClient_Initialize, s_pfnAudioClientInitialize, HRESULT, __stdcall,
	void* ecx, AUDCLNT_SHAREMODE shareMode, DWORD streamFlags, REFERENCE_TIME bufferDuration, REFERENCE_TIME periodicity,
	const WAVEFORMATEX* pFormat, LPCGUID audioSessionGuid)
{
	const HRESULT hr = CALL_ORIGINAL(ecx, shareMode, streamFlags, bufferDuration, periodicity, pFormat, audioSessionGuid);
	if (hr == S_OK && pFormat)
	{
		CaptureFormat_t fmt;
		fmt.nRate = pFormat->nSamplesPerSec ? pFormat->nSamplesPerSec : 48000;
		fmt.nChannels = pFormat->nChannels > 0 ? pFormat->nChannels : 2;
		fmt.bFloat = (pFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) || (pFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && pFormat->wBitsPerSample == 32);
		std::lock_guard<std::mutex> lock(s_mutexCapture);
		s_mapCaptureFormats[ecx] = fmt;
	}
	return hr;
}

MAKE_HOOK(IAudioClient_GetService, s_pfnAudioClientGetService, HRESULT, __stdcall,
	void* ecx, REFIID iid, void** ppInterface)
{
	const HRESULT hr = CALL_ORIGINAL(ecx, iid, ppInterface);
	if (hr == S_OK && ppInterface && *ppInterface && iid == __uuidof(IAudioCaptureClient) && !s_bCaptureHookCreated)
	{
		{
			std::lock_guard<std::mutex> lock(s_mutexCapture);
			if (s_mapCaptureFormats.find(ecx) == s_mapCaptureFormats.end())
				return hr;
		}
		void** pVtable = *reinterpret_cast<void***>(*ppInterface);
		s_pfnCaptureGetBuffer = pVtable[3];
		Hooks::IAudioCaptureClient_GetBuffer::Hook.Create(s_pfnCaptureGetBuffer, Hooks::IAudioCaptureClient_GetBuffer::Func);
		s_bCaptureHookCreated = true;
		F::Soundpad->MarkMicMixArmed();
	}
	return hr;
}

MAKE_HOOK(IDirectSoundCaptureBuffer_Lock, s_pfnDSLock, HRESULT, __stdcall,
	void* ecx, DWORD dwOffset, DWORD dwBytes, void** ppvAudioPtr1, DWORD* pdwAudioBytes1, void** ppvAudioPtr2, DWORD* pdwAudioBytes2, DWORD dwFlags)
{
	const HRESULT hr = CALL_ORIGINAL(ecx, dwOffset, dwBytes, ppvAudioPtr1, pdwAudioBytes1, ppvAudioPtr2, pdwAudioBytes2, dwFlags);
	if (hr == DS_OK && ppvAudioPtr1 && *ppvAudioPtr1 && pdwAudioBytes1 && *pdwAudioBytes1)
	{
		CaptureFormat_t fmt;
		bool bFound = false;
		{
			std::lock_guard<std::mutex> lock(s_mutexCapture);
			const auto it = s_mapCaptureFormats.find(ecx);
			if (it != s_mapCaptureFormats.end())
			{
				fmt = it->second;
				bFound = true;
			}
		}
		if (bFound)
		{
			const int nBlock = fmt.nChannels * (fmt.bFloat ? 4 : 2);
			const UINT32 nFrames = *pdwAudioBytes1 / static_cast<DWORD>(nBlock);
			F::Soundpad->MixMicCapture(static_cast<unsigned char*>(*ppvAudioPtr1), nFrames, fmt.nRate, fmt.nChannels, fmt.bFloat);
		}
	}
	return hr;
}

MAKE_HOOK(IDirectSoundCapture_CreateCaptureBuffer, s_pfnDSCaptureBufferCreate, HRESULT, __stdcall,
	void* ecx, const DSCBUFFERDESC_Mine* pDesc, void** ppDSCBuffer, void* pUnkOuter)
{
	const HRESULT hr = CALL_ORIGINAL(ecx, pDesc, ppDSCBuffer, pUnkOuter);
	if (hr == DS_OK && ppDSCBuffer && *ppDSCBuffer && pDesc && pDesc->lpwfxFormat)
	{
		CaptureFormat_t fmt;
		fmt.nRate = pDesc->lpwfxFormat->nSamplesPerSec ? pDesc->lpwfxFormat->nSamplesPerSec : 48000;
		fmt.nChannels = pDesc->lpwfxFormat->nChannels > 0 ? pDesc->lpwfxFormat->nChannels : 2;
		fmt.bFloat = (pDesc->lpwfxFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) || (pDesc->lpwfxFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && pDesc->lpwfxFormat->wBitsPerSample == 32);
		{
			std::lock_guard<std::mutex> lock(s_mutexCapture);
			s_mapCaptureFormats[*ppDSCBuffer] = fmt;
		}
		if (!s_bDSBufferHookCreated)
		{
			void** pVtable = *reinterpret_cast<void***>(*ppDSCBuffer);
			s_pfnDSLock = pVtable[8];
			Hooks::IDirectSoundCaptureBuffer_Lock::Hook.Create(s_pfnDSLock, Hooks::IDirectSoundCaptureBuffer_Lock::Func);
			s_bDSBufferHookCreated = true;
			F::Soundpad->MarkMicMixArmed();
		}
	}
	return hr;
}

MAKE_HOOK(IMMDevice_Activate, ResolveDeviceActivate(), HRESULT, __stdcall,
	void* ecx, REFIID iid, DWORD dwClsCtx, PROPVARIANT* pActivationParams, void** ppInterface)
{
	const HRESULT hr = CALL_ORIGINAL(ecx, iid, dwClsCtx, pActivationParams, ppInterface);
	if (hr == S_OK && ppInterface && *ppInterface && iid == __uuidof(IAudioClient) && !s_bClientHooksCreated)
	{
		void** pVtable = *reinterpret_cast<void***>(*ppInterface);
		s_pfnAudioClientInitialize = pVtable[3];
		s_pfnAudioClientGetService = pVtable[14];
		Hooks::IAudioClient_Initialize::Hook.Create(s_pfnAudioClientInitialize, Hooks::IAudioClient_Initialize::Func);
		Hooks::IAudioClient_GetService::Hook.Create(s_pfnAudioClientGetService, Hooks::IAudioClient_GetService::Func);
		s_bClientHooksCreated = true;
	}
	return hr;
}

MAKE_HOOK(DirectSoundCaptureCreate_Mine, s_pfnDSCreate, HRESULT, __stdcall,
	REFGUID guidDevice, void** ppDSC, void* pUnkOuter)
{
	const HRESULT hr = CALL_ORIGINAL(guidDevice, ppDSC, pUnkOuter);
	if (hr == DS_OK && ppDSC && *ppDSC)
	{
		void** pVtable = *reinterpret_cast<void***>(*ppDSC);
		Hooks::IDirectSoundCapture_CreateCaptureBuffer::Hook.Create(pVtable[3], Hooks::IDirectSoundCapture_CreateCaptureBuffer::Func);
	}
	return hr;
}
