#pragma once

#include "../../../../SDK/SDK.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "SoundpadDecode.h"

inline constexpr bool SoundpadEnabled = false;

class CSoundpad
{
public:
	void Run();
	void Shutdown();

	void RefreshFiles();
	void PlayFile(const std::string& strName);
	void PlaySelected();
	void StopAll();
	void Draw();

	bool IsPlaying() const { return m_bPlaying.load(std::memory_order_relaxed); }

	struct SoundEntry_t
	{
		std::string m_strName;
		std::filesystem::path m_fsPath;
		bool m_bSelected = false;
	};

	std::vector<SoundEntry_t> CopySounds();
	void ToggleSelected(const std::string& strName);
	int GetSelectedCount();

	bool GetVoiceHooked() const { return m_bVoiceHooked.load(std::memory_order_relaxed); }
	void MarkVoiceHooked() { m_bVoiceHooked.store(true, std::memory_order_relaxed); }

	bool ShouldReplaceVoice() const { return m_bVoiceActive.load(std::memory_order_relaxed) && !m_bMicMix.load(std::memory_order_relaxed); }
	bool MarkMicMixArmed() { return m_bMicMix.exchange(true, std::memory_order_relaxed); }
	void PushMix(const float* pSrc, size_t nSamples);
	size_t PopMix(float* pDst, size_t nNeeded);
	void MixMicCapture(unsigned char* pData, UINT32 nFrames, unsigned nRate, int nChannels, bool bFloat);
	bool MarkQueueEmptyPulled() { return m_bEverEmpty.exchange(true, std::memory_order_relaxed); }
	bool MarkAvailOverridden() { return m_bEverAvail.exchange(true, std::memory_order_relaxed); }
	bool MarkRealBlobSeen() { return m_bEverReal.exchange(true, std::memory_order_relaxed); }
	void SelfTestBlob(const void* pBlob, uint32 cbBlob, uint32 nRate);
	bool BuildVoiceBlob(void* pDest, uint32 cbDest, uint32& nWritten);
	uint32 GetPendingVoiceSize();
	void SetVoiceOptimalRate(uint32 nRate) { m_nVoiceRate.store(nRate, std::memory_order_relaxed); }
	void SetSteamUser(void* pUser);
	void FeedMicAudio(void* pSteamUser, const void* pCompressed, uint32 cbCompressed, uint32 nDesiredRate);

private:
	void EnsureFolder();
	void StartWorker();
	void WorkerThread();
	int ResolveVoiceKey();
	std::vector<unsigned char> BuildVoicePacket(const unsigned char* pOpus, int nOpusBytes);

	struct PlayRequest_t
	{
		std::vector<std::string> m_vecFiles;
		bool m_bQueueMode = false;
	};

	std::mutex m_mutexData;
	std::vector<SoundEntry_t> m_vecSounds;

	std::mutex m_mutexReq;
	std::deque<PlayRequest_t> m_queueRequests;

	std::mutex m_mutexVoice;
	std::deque<std::vector<unsigned char>> m_queueVoice;
	std::deque<std::vector<float>> m_queueMic;

	std::atomic<bool> m_bVoiceHooked{ false };
	std::atomic<bool> m_bVoiceActive{ false };
	std::atomic<bool> m_bMicMix{ false };
	std::atomic<bool> m_bMixActivePrinted{ false };

	std::mutex m_mutexMix;
	std::deque<float> m_queueMix;
	double m_flMixPos = 0.0;
	std::atomic<bool> m_bPlaying{ false };
	std::atomic<bool> m_bEverDelivered{ false };
	std::atomic<bool> m_bEverQueued{ false };
	std::atomic<bool> m_bEverSelfTest{ false };
	std::atomic<bool> m_bEverEmpty{ false };
	std::atomic<bool> m_bEverAvail{ false };
	std::atomic<bool> m_bEverReal{ false };
	std::atomic<uint32> m_nVoiceRate{ 0 };
	void* m_pSteamUser = nullptr;
	uint64 m_ullSteamId = 0;
	std::atomic<bool> m_bWorkerStop{ false };
	std::atomic<bool> m_bStopRequested{ false };
	std::atomic<bool> m_bWorkerStarted{ false };
	std::atomic<bool> m_bVoiceCmdSent{ false };
	std::atomic<bool> m_bShutDown{ false };
	std::string m_strCurrentSound;
	int m_nVoiceKey = 0;
	float m_flNextVoiceAssert = 0.0f;
	bool m_bWasVoiceKeyDown = false;
	std::thread m_thread;
};

MAKE_SINGLETON_SCOPED(CSoundpad, Soundpad, F);
