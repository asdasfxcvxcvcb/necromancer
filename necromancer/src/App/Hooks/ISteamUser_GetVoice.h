#pragma once

namespace Hooks
{
	namespace ISteamUser_GetVoice
	{
		void Init();
	}
	namespace ISteamUser_GetAvailableVoice
	{
		void Init();
	}
	namespace IMMDevice_Activate
	{
		void Init();
	}
	namespace IAudioClient_Initialize
	{
		void Init();
	}
	namespace IAudioClient_GetService
	{
		void Init();
	}
	namespace IAudioCaptureClient_GetBuffer
	{
		void Init();
	}
	namespace DirectSoundCaptureCreate_Mine
	{
		void Init();
	}
	namespace IDirectSoundCapture_CreateCaptureBuffer
	{
		void Init();
	}
	namespace IDirectSoundCaptureBuffer_Lock
	{
		void Init();
	}
}
