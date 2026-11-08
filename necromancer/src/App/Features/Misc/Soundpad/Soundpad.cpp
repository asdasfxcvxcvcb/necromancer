#include "Soundpad.h"

#include "../../CFG.h"
#include "../../../Hooks/ISteamUser_GetVoice.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>

#include "../../../../../third_party/opus/include/opus.h"

#include <mmdeviceapi.h>
#include <audiopolicy.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "opus.lib")

namespace
{
	constexpr const char* SOUND_FOLDER = "C:\\necromancer_tf2\\soundpad";
	constexpr size_t MAX_CACHE_FLOATS = 48u * 48000u * 120u;
	constexpr size_t SP_MAX_STREAMS = 8;
	constexpr uint32 SP_VOICE_RATE = 24000;

	double NowSeconds()
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	bool IsAudioFile(const std::string& strName)
	{
		const size_t nDot = strName.find_last_of('.');
		if (nDot == std::string::npos)
			return false;
		std::string strExt = strName.substr(nDot + 1);
		std::transform(strExt.begin(), strExt.end(), strExt.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return strExt == "mp3" || strExt == "ogg" || strExt == "wav";
	}

	int OverlapLimit()
	{
		if (CFG::Soundpad_Overlap == 1)
			return 2;
		if (CFG::Soundpad_Overlap == 2)
			return 3;
		if (CFG::Soundpad_Overlap >= 3)
			return INT_MAX;
		return 1;
	}

	uint32 SoundpadCrc32(const unsigned char* pData, size_t nSize)
	{
		uint32 nCrc = 0xFFFFFFFFu;
		for (size_t i = 0; i < nSize; i++)
		{
			nCrc ^= pData[i];
			for (int b = 0; b < 8; b++)
				nCrc = (nCrc >> 1) ^ (0xEDB88320u & (0u - (nCrc & 1u)));
		}
		return ~nCrc;
	}

	void PushU16(std::vector<unsigned char>& vec, uint16 v)
	{
		vec.push_back(static_cast<unsigned char>(v & 0xFF));
		vec.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
	}

	void PushU32(std::vector<unsigned char>& vec, uint32 v)
	{
		vec.push_back(static_cast<unsigned char>(v & 0xFF));
		vec.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
		vec.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
		vec.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
	}

	void PushU64(std::vector<unsigned char>& vec, uint64 v)
	{
		for (int b = 0; b < 8; b++)
			vec.push_back(static_cast<unsigned char>((v >> (b * 8)) & 0xFF));
	}
}

void CSoundpad::EnsureFolder()
{
	std::error_code ec;
	const std::filesystem::path path(SOUND_FOLDER);
	if (!std::filesystem::exists(path, ec))
		std::filesystem::create_directories(path, ec);

	const std::filesystem::path readme = path / "README.txt";
	if (!std::filesystem::exists(readme, ec))
	{
		std::ofstream out(readme);
		if (out.is_open())
		{
			out << "NECROMANCER SOUNDPAD\nPut your sound files in this folder.\n\nSupported formats:\n  .mp3\n  .ogg\n  .wav\n\n";
			out << "Click a sound's name in the menu (Soundpad tab) to play it over voice chat.\n";
			out << "Use the checkboxes to multi-select sounds, then save them as a playlist.\n";
		}
	}
}

void CSoundpad::RefreshFiles()
{
	EnsureFolder();

	std::vector<std::pair<std::string, std::filesystem::path>> vecFound;
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(SOUND_FOLDER), ec))
	{
		if (ec)
			break;
		if (!entry.is_regular_file(ec))
			continue;
		const std::string strName = entry.path().filename().string();
		if (!IsAudioFile(strName))
			continue;
		vecFound.push_back({ strName, entry.path() });
	}
	std::sort(vecFound.begin(), vecFound.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

	std::lock_guard<std::mutex> lock(m_mutexData);
	std::vector<SoundEntry_t> vecNew;
	vecNew.reserve(vecFound.size());
	for (const auto& [strName, fsPathEntry] : vecFound)
	{
		SoundEntry_t entry;
		entry.m_strName = strName;
		entry.m_fsPath = fsPathEntry;
		for (const auto& old : m_vecSounds)
		{
			if (old.m_strName == strName)
			{
				entry.m_bSelected = old.m_bSelected;
				break;
			}
		}
		vecNew.push_back(std::move(entry));
	}
	m_vecSounds = std::move(vecNew);
}

std::vector<CSoundpad::SoundEntry_t> CSoundpad::CopySounds()
{
	std::lock_guard<std::mutex> lock(m_mutexData);
	return m_vecSounds;
}

void CSoundpad::ToggleSelected(const std::string& strName)
{
	std::lock_guard<std::mutex> lock(m_mutexData);
	for (auto& sound : m_vecSounds)
	{
		if (sound.m_strName == strName)
		{
			sound.m_bSelected = !sound.m_bSelected;
			return;
		}
	}
}

int CSoundpad::GetSelectedCount()
{
	std::lock_guard<std::mutex> lock(m_mutexData);
	int nCount = 0;
	for (const auto& sound : m_vecSounds)
	{
		if (sound.m_bSelected)
			nCount++;
	}
	return nCount;
}

void CSoundpad::PlayFile(const std::string& strName)
{
	std::lock_guard<std::mutex> lock(m_mutexReq);
	PlayRequest_t req;
	req.m_vecFiles.push_back(strName);
	m_queueRequests.push_back(std::move(req));
}

void CSoundpad::PlaySelected()
{
	std::vector<std::string> vecFiles;
	{
		std::lock_guard<std::mutex> lock(m_mutexData);
		for (const auto& sound : m_vecSounds)
		{
			if (sound.m_bSelected)
				vecFiles.push_back(sound.m_strName);
		}
	}
	if (vecFiles.empty())
		return;

	if (CFG::Soundpad_Randomize)
	{
		std::random_device rd;
		std::mt19937 gen(rd());
		std::shuffle(vecFiles.begin(), vecFiles.end(), gen);
	}

	std::lock_guard<std::mutex> lock(m_mutexReq);
	PlayRequest_t req;
	req.m_vecFiles = std::move(vecFiles);
	m_queueRequests.push_back(std::move(req));
}

void CSoundpad::StopAll()
{
	std::lock_guard<std::mutex> lock(m_mutexReq);
	m_queueRequests.clear();
	m_bStopRequested.store(true, std::memory_order_relaxed);
	{
		std::lock_guard<std::mutex> lockVoice(m_mutexVoice);
		m_queueVoice.clear();
		m_queueMic.clear();
	}
	m_bVoiceActive.store(false, std::memory_order_relaxed);
	m_bPlaying.store(false, std::memory_order_relaxed);
}

bool CSoundpad::BuildVoiceBlob(void* pDest, uint32 cbDest, uint32& nWritten)
{
	nWritten = 0;
	if (cbDest < 16 || !m_ullSteamId)
		return false;

	unsigned char* pOut = static_cast<unsigned char*>(pDest);
	uint32 nBody = 0;
	{
		std::lock_guard<std::mutex> lock(m_mutexVoice);
		const bool bFlush = !m_bPlaying.load(std::memory_order_relaxed);
		if (m_queueVoice.empty() || (!bFlush && m_queueVoice.size() < 6))
			return false;
		int nPulled = 0;
		while (nPulled < 10 && !m_queueVoice.empty())
		{
			const auto& vec = m_queueVoice.front();
			if (vec.empty() || vec.size() > cbDest - 15 - nBody)
				break;
			memcpy(pOut + 11 + nBody, vec.data(), vec.size());
			nBody += static_cast<uint32>(vec.size());
			m_queueVoice.pop_front();
			nPulled++;
		}
	}
	if (!nBody)
		return false;

	for (int b = 0; b < 8; b++)
		pOut[b] = static_cast<unsigned char>((m_ullSteamId >> (b * 8)) & 0xFF);
	pOut[8] = 0x0B;
	pOut[9] = static_cast<unsigned char>(SP_VOICE_RATE & 0xFF);
	pOut[10] = static_cast<unsigned char>((SP_VOICE_RATE >> 8) & 0xFF);
	const uint32 nCrc = SoundpadCrc32(pOut, 11 + nBody);
	pOut[11 + nBody] = static_cast<unsigned char>(nCrc & 0xFF);
	pOut[11 + nBody + 1] = static_cast<unsigned char>((nCrc >> 8) & 0xFF);
	pOut[11 + nBody + 2] = static_cast<unsigned char>((nCrc >> 16) & 0xFF);
	pOut[11 + nBody + 3] = static_cast<unsigned char>((nCrc >> 24) & 0xFF);
	nWritten = 11 + nBody + 4;

	return true;
}

void CSoundpad::SelfTestBlob(const void* pBlob, uint32 cbBlob, uint32 nRate)
{
	if (m_bEverSelfTest.exchange(true, std::memory_order_relaxed))
		return;
	if (!m_pSteamUser || !pBlob || cbBlob < 16)
		return;

	alignas(16) static unsigned char s_out[65536];
	uint32 nPcmBytes = 0;
	const auto pDecompress = reinterpret_cast<int(__fastcall*)(void*, const void*, uint32, void*, uint32, uint32*, uint32)>((*reinterpret_cast<void***>(m_pSteamUser))[11]);
	const int nResult = pDecompress(m_pSteamUser, pBlob, cbBlob, s_out, sizeof(s_out), &nPcmBytes, nRate);
	(void)nResult;
}

uint32 CSoundpad::GetPendingVoiceSize()
{
	std::lock_guard<std::mutex> lock(m_mutexVoice);
	if (m_queueVoice.empty())
		return 0;
	const bool bFlush = !m_bPlaying.load(std::memory_order_relaxed);
	if (!bFlush && m_queueVoice.size() < 6)
		return 0;
	uint32 nBody = 0;
	int nCount = 0;
	for (const auto& vec : m_queueVoice)
	{
		if (nCount++ >= 10)
			break;
		nBody += static_cast<uint32>(vec.size());
	}
	return nBody ? nBody + 15 : 0;
}

void CSoundpad::FeedMicAudio(void* pSteamUser, const void* pCompressed, uint32 cbCompressed, uint32 nDesiredRate)
{
	if (CFG::Soundpad_MuteMic || !pSteamUser || !pCompressed || cbCompressed == 0)
		return;

	const uint32 nUseRate = SP_VOICE_RATE;

	alignas(16) static unsigned char s_buf[524288];
	uint32 nPcmBytes = 0;
	const auto pDecompress = reinterpret_cast<int(__fastcall*)(void*, const void*, uint32, void*, uint32, uint32*, uint32)>((*reinterpret_cast<void***>(pSteamUser))[11]);
	const int nResult = pDecompress(pSteamUser, pCompressed, cbCompressed, s_buf, sizeof(s_buf), &nPcmBytes, nUseRate);
	if (nResult != 0 || nPcmBytes < 2)
		return;

	const size_t nSamples = nPcmBytes / sizeof(short);
	if (nSamples > nUseRate)
		return;

	std::vector<float> vecMic(nSamples);
	const short* pSamples = reinterpret_cast<const short*>(s_buf);
	for (size_t i = 0; i < nSamples; i++)
		vecMic[i] = pSamples[i] / 32768.0f;

	std::lock_guard<std::mutex> lock(m_mutexVoice);
	if (m_queueMic.size() < 8)
		m_queueMic.push_back(std::move(vecMic));
}

void CSoundpad::PushMix(const float* pSrc, size_t nSamples)
{
	std::lock_guard<std::mutex> lock(m_mutexMix);
	for (size_t s = 0; s < nSamples; s++)
		m_queueMix.push_back(pSrc[s]);
	while (m_queueMix.size() > 24000 / 2)
		m_queueMix.pop_front();
}

size_t CSoundpad::PopMix(float* pDst, size_t nNeeded)
{
	std::lock_guard<std::mutex> lock(m_mutexMix);
	const size_t nHave = (std::min<size_t>)(nNeeded, m_queueMix.size());
	for (size_t i = 0; i < nHave; i++)
	{
		pDst[i] = m_queueMix.front();
		m_queueMix.pop_front();
	}
	for (size_t i = nHave; i < nNeeded; i++)
		pDst[i] = 0.0f;
	return nHave;
}

void CSoundpad::MixMicCapture(unsigned char* pData, UINT32 nFrames, unsigned nRate, int nChannels, bool bFloat)
{
	const double flStep = 24000.0 / static_cast<double>(nRate);
	const size_t nNeeded = static_cast<size_t>(static_cast<double>(nFrames) * flStep) + 2;
	if (!pData || !nFrames || nNeeded > 16384)
	{
		m_flMixPos = 0.0;
		return;
	}
	std::vector<float> vecMusic(nNeeded, 0.0f);
	const size_t nFilled = PopMix(vecMusic.data(), nNeeded);
	if (!nFilled)
	{
		m_flMixPos = 0.0;
		return;
	}
	const bool bMusicCovers = nFilled >= nNeeded * 3 / 4;
	double flPos = m_flMixPos;
	if (bFloat)
	{
		float* pF = reinterpret_cast<float*>(pData);
		for (UINT32 f = 0; f < nFrames; f++)
		{
			const size_t nPos = static_cast<size_t>(flPos);
			float flMusic = 0.0f;
			if (nPos + 1 < nNeeded)
			{
				const float flFrac = static_cast<float>(flPos - nPos);
				flMusic = vecMusic[nPos] + (vecMusic[nPos + 1] - vecMusic[nPos]) * flFrac;
			}
			float flSample = pF[f * nChannels];
			if (CFG::Soundpad_MuteMic && bMusicCovers)
				flSample = 0.0f;
			pF[f * nChannels] = std::clamp(flSample + flMusic, -1.0f, 1.0f);
			flPos += flStep;
		}
	}
	else
	{
		short* pS = reinterpret_cast<short*>(pData);
		for (UINT32 f = 0; f < nFrames; f++)
		{
			const size_t nPos = static_cast<size_t>(flPos);
			float flMusic = 0.0f;
			if (nPos + 1 < nNeeded)
			{
				const float flFrac = static_cast<float>(flPos - nPos);
				flMusic = vecMusic[nPos] + (vecMusic[nPos + 1] - vecMusic[nPos]) * flFrac;
			}
			float flSample = static_cast<float>(pS[f * nChannels]) / 32768.0f;
			if (CFG::Soundpad_MuteMic && bMusicCovers)
				flSample = 0.0f;
			pS[f * nChannels] = static_cast<short>(std::clamp(flSample + flMusic, -1.0f, 1.0f) * 32767.0f);
			flPos += flStep;
		}
	}
	m_flMixPos = flPos - std::floor(flPos);
}

void CSoundpad::StartWorker()
{
	if (m_bShutDown.load(std::memory_order_seq_cst))
		return;

	bool bExpected = false;
	if (!m_bWorkerStarted.compare_exchange_strong(bExpected, true))
		return;
	m_bWorkerStop.store(false, std::memory_order_relaxed);
	m_thread = std::thread(&CSoundpad::WorkerThread, this);
}

void CSoundpad::Run()
{
	if (!SoundpadEnabled)
		return;
	StartWorker();

	if (!m_bVoiceHooked.load(std::memory_order_relaxed))
	{
		static float flNextTry = 0.0f;
		const float flNow = I::EngineClient->Time();
		if (flNow > flNextTry)
		{
			flNextTry = flNow + 2.0f;
			Hooks::ISteamUser_GetVoice::Init();
			Hooks::ISteamUser_GetAvailableVoice::Init();
		}
	}
	if (!m_bMicMix.load(std::memory_order_relaxed))
	{
		static float flNextMixTry = 0.0f;
		const float flNow = I::EngineClient->Time();
		if (flNow > flNextMixTry)
		{
			flNextMixTry = flNow + 2.0f;
			Hooks::IMMDevice_Activate::Init();
			Hooks::IAudioClient_Initialize::Init();
			Hooks::IAudioClient_GetService::Init();
			Hooks::IAudioCaptureClient_GetBuffer::Init();
			Hooks::DirectSoundCaptureCreate_Mine::Init();
			Hooks::IDirectSoundCapture_CreateCaptureBuffer::Init();
			Hooks::IDirectSoundCaptureBuffer_Lock::Init();
		}
	}

	static bool s_bFolderReady = false;
	if (!s_bFolderReady)
	{
		EnsureFolder();
		RefreshFiles();
		s_bFolderReady = true;
	}

	const bool bInGame = I::EngineClient->IsInGame() && I::EngineClient->IsConnected();

	if (!m_nVoiceKey)
		m_nVoiceKey = ResolveVoiceKey();
	bool bVoiceKeyHeld = false;
	if (m_nVoiceKey)
		bVoiceKeyHeld = (GetAsyncKeyState(m_nVoiceKey) & 0x8000) != 0;
	if (m_nVoiceKey && m_bVoiceActive.load(std::memory_order_relaxed) && CFG::Soundpad_KeyStops)
	{
		if (bVoiceKeyHeld && !m_bWasVoiceKeyDown)
			StopAll();
		m_bWasVoiceKeyDown = bVoiceKeyHeld;
	}
	else
		m_bWasVoiceKeyDown = false;

	if (!CFG::Soundpad_Active)
	{
		if (m_bVoiceCmdSent.load(std::memory_order_relaxed) && bInGame)
		{
			I::EngineClient->ClientCmd_Unrestricted("-voicerecord");
			m_bVoiceCmdSent.store(false, std::memory_order_relaxed);
		}
		return;
	}

	if (m_bVoiceActive.load(std::memory_order_relaxed) && bInGame && !m_bVoiceCmdSent.load(std::memory_order_relaxed))
	{
		I::EngineClient->ClientCmd_Unrestricted("+voicerecord");
		m_bVoiceCmdSent.store(true, std::memory_order_relaxed);
		m_flNextVoiceAssert = I::EngineClient->Time() + 1.0f;
	}
	if (m_bVoiceCmdSent.load(std::memory_order_relaxed) && bInGame && m_bVoiceActive.load(std::memory_order_relaxed) && !bVoiceKeyHeld)
	{
		const float flNowT = I::EngineClient->Time();
		if (flNowT >= m_flNextVoiceAssert)
		{
			I::EngineClient->ClientCmd_Unrestricted("+voicerecord");
			m_flNextVoiceAssert = flNowT + 1.0f;
		}
	}
	else if (m_bVoiceCmdSent.load(std::memory_order_relaxed) && (!bInGame || (!m_bVoiceActive.load(std::memory_order_relaxed) && !bVoiceKeyHeld)))
	{
		I::EngineClient->ClientCmd_Unrestricted("-voicerecord");
		m_bVoiceCmdSent.store(false, std::memory_order_relaxed);
		m_flNextVoiceAssert = 0.0f;
	}
}

void CSoundpad::Shutdown()
{
	m_bShutDown.store(true, std::memory_order_seq_cst);
	m_bVoiceActive.store(false, std::memory_order_relaxed);
	if (m_bVoiceCmdSent.exchange(false))
		I::EngineClient->ClientCmd_Unrestricted("-voicerecord");
	m_bWorkerStop.store(true, std::memory_order_relaxed);
	if (m_thread.joinable())
		m_thread.join();
	m_bWorkerStarted.store(false, std::memory_order_relaxed);
}

int CSoundpad::ResolveVoiceKey()
{
	const char* pszBind = I::EngineClient->Key_LookupBinding("+voicerecord");
	if (!pszBind || !pszBind[0])
		return 0;

	std::string strKey = pszBind;
	std::transform(strKey.begin(), strKey.end(), strKey.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	if (strKey == "capslock") return VK_CAPITAL;
	if (strKey == "space") return VK_SPACE;
	if (strKey == "tab") return VK_TAB;
	if (strKey == "enter") return VK_RETURN;
	if (strKey == "shift") return VK_SHIFT;
	if (strKey == "ctrl") return VK_CONTROL;
	if (strKey == "alt") return VK_MENU;
	if (strKey == "ins") return VK_INSERT;
	if (strKey == "home") return VK_HOME;
	if (strKey == "end") return VK_END;
	if (strKey == "pgup") return VK_PRIOR;
	if (strKey == "pgdn") return VK_NEXT;
	if (strKey == "mouse1") return VK_LBUTTON;
	if (strKey == "mouse2") return VK_RBUTTON;
	if (strKey == "mouse3") return VK_MBUTTON;
	if (strKey == "mouse4") return VK_XBUTTON1;
	if (strKey == "mouse5") return VK_XBUTTON2;
	if (strKey == "uparrow") return VK_UP;
	if (strKey == "downarrow") return VK_DOWN;
	if (strKey == "leftarrow") return VK_LEFT;
	if (strKey == "rightarrow") return VK_RIGHT;
	if (strKey.size() == 2 && strKey[0] == 'f' && strKey[1] >= '1' && strKey[1] <= '9')
		return VK_F1 + (strKey[1] - '1');
	if (strKey.size() == 1)
	{
		const SHORT nScan = VkKeyScanA(strKey[0]);
		if (nScan != -1 && (nScan & 0xFF) > 0)
			return nScan & 0xFF;
	}
	return 0;
}

void CSoundpad::Draw()
{
	if (!SoundpadEnabled || !CFG::Soundpad_Active || !CFG::Soundpad_Indicator || !m_bPlaying.load(std::memory_order_relaxed))
		return;

	std::string strName;
	{
		std::lock_guard<std::mutex> lock(m_mutexData);
		strName = m_strCurrentSound;
	}
	if (strName.empty())
		return;
	if (strName.size() > 40)
		strName = strName.substr(0, 39) + "..";

	const auto& font = H::Fonts->Get(EFonts::Menu);
	const std::string strLabel = "Soundpad: " + strName;
	int nW = 0;
	int nH = 0;
	I::MatSystemSurface->GetTextSize(font.m_dwFont, Utils::ConvertUtf8ToWide(strLabel).c_str(), nW, nH);
	if (nW <= 0)
		return;

	const int nPadX = 6;
	const int nPadY = 4;
	const int nBoxW = nW + nPadX * 2;
	const int nBoxH = nH + nPadY * 2;
	const int nX = (H::Draw->GetScreenW() - nBoxW) / 2;
	const int nY = 8;

	Color_t clrBg = CFG::Menu_Background;
	clrBg.a = 220;
	H::Draw->Rect(nX, nY, nBoxW, nBoxH, clrBg);
	H::Draw->OutlinedRect(nX, nY, nBoxW, nBoxH, CFG::Menu_Accent_Primary);
	H::Draw->String(font, nX + nPadX, nY + nPadY, CFG::Menu_Text_Active, POS_DEFAULT, "%s", strLabel.c_str());
}

void CSoundpad::SetSteamUser(void* pUser)
{
	if (pUser && !m_pSteamUser)
	{
		m_pSteamUser = pUser;
		m_ullSteamId = 0;
		const auto pfnGetSteamID = reinterpret_cast<void(__fastcall*)(void*, void*)>((*reinterpret_cast<void***>(pUser))[2]);
		if (pfnGetSteamID)
			pfnGetSteamID(pUser, &m_ullSteamId);
	}
}

std::vector<unsigned char> CSoundpad::BuildVoicePacket(const unsigned char* pOpus, int nOpusBytes)
{
	std::vector<unsigned char> vec;
	vec.reserve(static_cast<size_t>(nOpusBytes) + 4);
	vec.push_back(0x06);
	PushU16(vec, static_cast<uint16>(nOpusBytes));
	vec.insert(vec.end(), pOpus, pOpus + nOpusBytes);
	return vec;
}

void CSoundpad::WorkerThread()
{
	struct ActiveStream_t
	{
		std::shared_ptr<const std::vector<float>> m_pPcm;
		double m_flPos = 0.0;
	};

	std::vector<ActiveStream_t> vecStreams;
	std::deque<std::string> dequePending;
	std::deque<std::string> dequeImmediate;
	std::map<std::filesystem::path, std::shared_ptr<const std::vector<float>>> mapCache;
	std::vector<std::filesystem::path> vecCacheOrder;

	OpusEncoder* pEncoder = nullptr;
	uint32 nEncRate = 0;
	bool bCoOwned = false;

	IAudioClient* pAudioClient = nullptr;
	IAudioRenderClient* pAudioRender = nullptr;
	UINT32 nAudioBufferFrames = 0;
	unsigned nAudioRate = 48000;
	int nAudioChannels = 2;
	bool bAudioFloat = true;

	std::vector<float> vecMicAccum;
	int nQuietFrames = 0;
	int nEmitGrace = 0;
	double flNextChunk = NowSeconds();
	double flLastMixPush = NowSeconds();


	auto ResolvePath = [&](const std::string& strName) -> std::filesystem::path {
		std::lock_guard<std::mutex> lock(m_mutexData);
		for (const auto& sound : m_vecSounds)
		{
			if (sound.m_strName == strName)
				return sound.m_fsPath;
		}
		return {};
	};

	auto DecodeAndCache = [&](const std::filesystem::path& fsPath, std::shared_ptr<const std::vector<float>>* ppPcm) -> bool {
		auto it = mapCache.find(fsPath);
		if (it != mapCache.end())
		{
			*ppPcm = it->second;
			return true;
		}
		std::vector<float> vecPcm;
		if (!SoundpadDecodeToMono48k(fsPath, vecPcm) || vecPcm.empty())
			return false;
		auto pPcm = std::make_shared<const std::vector<float>>(std::move(vecPcm));
		size_t nTotal = 0;
		for (const auto& [str, vec] : mapCache)
			nTotal += vec->size();
		while (nTotal + pPcm->size() > MAX_CACHE_FLOATS && !vecCacheOrder.empty())
		{
			const std::filesystem::path fsEvict = vecCacheOrder.front();
			vecCacheOrder.erase(vecCacheOrder.begin());
			auto itEvict = mapCache.find(fsEvict);
			if (itEvict != mapCache.end())
			{
				nTotal -= itEvict->second->size();
				mapCache.erase(itEvict);
			}
		}
		vecCacheOrder.push_back(fsPath);
		*ppPcm = pPcm;
		mapCache[fsPath] = std::move(pPcm);
		return true;
	};

	auto StartStream = [&](const std::string& strName, bool bForce) {
		const std::filesystem::path fsPath = ResolvePath(strName);
		if (fsPath.empty())
			return;
		std::shared_ptr<const std::vector<float>> pPcm;
		if (!DecodeAndCache(fsPath, &pPcm))
			return;
		const int nLimit = OverlapLimit();
		if (static_cast<int>(vecStreams.size()) >= nLimit)
		{
			if (!bForce)
				return;
			vecStreams.erase(vecStreams.begin());
		}
		if (static_cast<int>(vecStreams.size()) >= SP_MAX_STREAMS)
			vecStreams.erase(vecStreams.begin());
		ActiveStream_t stream;
		stream.m_pPcm = pPcm;
		stream.m_flPos = 0.0;
		vecStreams.push_back(std::move(stream));
		nQuietFrames = 0;
		{
			std::lock_guard<std::mutex> lock(m_mutexData);
			m_strCurrentSound = strName;
		}
	};


	while (!m_bWorkerStop.load(std::memory_order_relaxed))
	{
		Sleep(4);

		if (m_bStopRequested.exchange(false, std::memory_order_relaxed))
		{
			vecStreams.clear();
			dequePending.clear();
			dequeImmediate.clear();
			vecMicAccum.clear();
			{
				std::lock_guard<std::mutex> lock(m_mutexVoice);
				m_queueVoice.clear();
				m_queueMic.clear();
			}
			m_bVoiceActive.store(false, std::memory_order_relaxed);
			m_bPlaying.store(false, std::memory_order_relaxed);
			nQuietFrames = 100;
			nEmitGrace = 0;
		}

		PlayRequest_t req;
		{
			std::lock_guard<std::mutex> lock(m_mutexReq);
			if (!m_queueRequests.empty())
			{
				req = std::move(m_queueRequests.front());
				m_queueRequests.pop_front();
			}
		}
		if (!req.m_vecFiles.empty())
		{
			if (req.m_bQueueMode)
			{
				for (const auto& strFile : req.m_vecFiles)
					dequePending.push_back(strFile);
			}
			else
			{
				for (const auto& strFile : req.m_vecFiles)
					dequeImmediate.push_back(strFile);
			}
		}

		while (!dequeImmediate.empty() && static_cast<int>(vecStreams.size()) < SP_MAX_STREAMS)
		{
			StartStream(dequeImmediate.front(), true);
			dequeImmediate.pop_front();
		}

		const int nLimit = OverlapLimit();
		while (!dequePending.empty() && static_cast<int>(vecStreams.size()) < (std::min)(nLimit, static_cast<int>(SP_MAX_STREAMS)))
		{
			StartStream(dequePending.front(), false);
			dequePending.pop_front();
		}

		const double flNow = NowSeconds();
		if (flNow < flNextChunk)
			continue;
		flNextChunk = (std::max)(flNextChunk + 0.020, flNow - 0.020);

		if (!dequePending.empty())
		{
			const std::filesystem::path fsPath = ResolvePath(dequePending.front());
			if (!fsPath.empty())
			{
				std::shared_ptr<const std::vector<float>> pPre;
				DecodeAndCache(fsPath, &pPre);
			}
		}

		const uint32 nUseRate = SP_VOICE_RATE;
		const size_t nFrameSamples = static_cast<size_t>(nUseRate / 50);

		if (pEncoder && nEncRate != nUseRate)
		{
			opus_encoder_destroy(pEncoder);
			pEncoder = nullptr;
			nEncRate = 0;
		}
		if (!pEncoder)
		{
			int nErr = 0;
			pEncoder = opus_encoder_create(static_cast<int>(nUseRate), 1, OPUS_APPLICATION_AUDIO, &nErr);
			if (nErr == 0 && pEncoder)
			{
				nEncRate = nUseRate;
				opus_encoder_ctl(pEncoder, OPUS_SET_BITRATE(64000));
			}
			else
				pEncoder = nullptr;
		}

		float aChunk[2048] = {};
		const float flSpeed = std::clamp(CFG::Soundpad_Speed, 0.25f, 3.0f);
		const double flStep = (48000.0 * flSpeed) / nUseRate;
		const size_t nSamples = (std::min<size_t>)(nFrameSamples, 2048);

		for (int i = static_cast<int>(vecStreams.size()) - 1; i >= 0; i--)
		{
			const std::vector<float>& vecPcm = *vecStreams[i].m_pPcm;
			const size_t nPcm = vecPcm.size();
			bool bFinished = (nPcm < 2);
			if (!bFinished)
			{
				for (size_t s = 0; s < nSamples; s++)
				{
					const size_t nPos = static_cast<size_t>(vecStreams[i].m_flPos);
					if (nPos + 1 >= nPcm)
					{
						bFinished = true;
						break;
					}
					const float flFrac = static_cast<float>(vecStreams[i].m_flPos - nPos);
					aChunk[s] += vecPcm[nPos] + (vecPcm[nPos + 1] - vecPcm[nPos]) * flFrac;
					vecStreams[i].m_flPos += flStep;
				}
			}
			if (bFinished || vecStreams[i].m_flPos + 1 >= nPcm)
				vecStreams.erase(vecStreams.begin() + i);
		}

		const bool bStreamActive = !vecStreams.empty() || !dequePending.empty();
		if (bStreamActive)
			nQuietFrames = 0;
		else
			nQuietFrames++;
		m_bPlaying.store(bStreamActive, std::memory_order_relaxed);

		if (bStreamActive)
			nEmitGrace = 30;
		else if (nEmitGrace > 0)
			nEmitGrace--;

		const float flVolume = std::clamp(CFG::Soundpad_Volume, 0.0f, 300.0f) / 100.0f;
		const bool bVoiceWanted = CFG::Soundpad_Active && pEncoder;
		const bool bEmitVoice = bVoiceWanted && nEmitGrace > 0;

		if (bEmitVoice && m_bMicMix.load(std::memory_order_relaxed))
		{
			const double flNowPush = NowSeconds();
			double flElapsed = flNowPush - flLastMixPush;
			flLastMixPush = flNowPush;
			if (flElapsed < 0.0)
				flElapsed = 0.0;
			size_t nOut = static_cast<size_t>(flElapsed * 24000.0);
			if (nOut == 0)
				nOut = 1;
			if (nOut > 4096)
				nOut = 4096;
			float aMix[4096] = {};
			const double flInStep = static_cast<double>(nSamples) / nOut;
			double flInPos = 0.0;
			for (size_t s = 0; s < nOut; s++)
			{
				const size_t nPos = static_cast<size_t>(flInPos);
				const float flFrac = static_cast<float>(flInPos - nPos);
				const size_t nNext = (std::min<size_t>)(nPos + 1, nSamples - 1);
				aMix[s] = std::clamp((aChunk[nPos] + (aChunk[nNext] - aChunk[nPos]) * flFrac) * flVolume, -1.0f, 1.0f);
				flInPos += flInStep;
			}
			PushMix(aMix, nOut);
			m_bVoiceActive.store(true, std::memory_order_relaxed);
		}
		else if (bEmitVoice)
		{
			{
				std::vector<float> vecMicLeftover;
				{
					std::lock_guard<std::mutex> lock(m_mutexVoice);
					while (!m_queueMic.empty())
					{
						vecMicLeftover.insert(vecMicLeftover.end(), m_queueMic.front().begin(), m_queueMic.front().end());
						m_queueMic.pop_front();
					}
				}
				if (!vecMicLeftover.empty())
					vecMicAccum.insert(vecMicAccum.end(), vecMicLeftover.begin(), vecMicLeftover.end());
			}
			const size_t nMicTake = (std::min<size_t>)(nSamples, vecMicAccum.size());
			for (size_t s = 0; s < nMicTake; s++)
				aChunk[s] += vecMicAccum[s];
			vecMicAccum.erase(vecMicAccum.begin(), vecMicAccum.begin() + nMicTake);

			for (size_t s = 0; s < nSamples; s++)
				aChunk[s] = std::clamp(aChunk[s] * flVolume, -1.0f, 1.0f);

			unsigned char aOpus[2048] = {};
			const int nOpusBytes = opus_encode_float(pEncoder, aChunk, static_cast<int>(nSamples), aOpus, sizeof(aOpus));
			if (nOpusBytes > 0)
			{
				std::vector<unsigned char> vecPacket;
				if (nOpusBytes > 3)
					vecPacket = BuildVoicePacket(aOpus, nOpusBytes);

				bool bQueued = false;
				{
				std::lock_guard<std::mutex> lock(m_mutexVoice);
				while (m_queueVoice.size() >= 12)
					m_queueVoice.pop_front();
					if (!vecPacket.empty())
					{
						m_queueVoice.push_back(std::move(vecPacket));
						bQueued = true;
					}
				}
				if (bQueued && !m_bVoiceActive.load(std::memory_order_relaxed))
					m_bVoiceActive.store(true, std::memory_order_relaxed);
			}
		}
		else if (m_bVoiceActive.load(std::memory_order_relaxed))
		{
			if (nEmitGrace <= 0)
			{
				std::lock_guard<std::mutex> lock(m_mutexVoice);
				if (m_queueVoice.empty())
					m_bVoiceActive.store(false, std::memory_order_relaxed);
			}
		}

		if (CFG::Soundpad_Active && CFG::Soundpad_Monitor && bStreamActive)
		{
			if (!pAudioClient)
			{
				HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
				bCoOwned = SUCCEEDED(hrCo);
				IMMDeviceEnumerator* pEnum = nullptr;
				IMMDevice* pDevice = nullptr;
				do
				{
					if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&pEnum))) || !pEnum)
						break;
					if (FAILED(pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice)) || !pDevice)
						break;
					if (FAILED(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&pAudioClient))) || !pAudioClient)
						break;
					WAVEFORMATEX* pMix = nullptr;
					if (FAILED(pAudioClient->GetMixFormat(&pMix)) || !pMix)
					{
						if (pMix)
							CoTaskMemFree(pMix);
						pAudioClient->Release();
						pAudioClient = nullptr;
						break;
					}
					nAudioRate = pMix->nSamplesPerSec ? pMix->nSamplesPerSec : 48000;
					nAudioChannels = pMix->nChannels > 0 ? pMix->nChannels : 2;
					bAudioFloat = (pMix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) || (pMix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && pMix->wBitsPerSample == 32);
					if (FAILED(pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, pMix, nullptr)))
					{
						CoTaskMemFree(pMix);
						pAudioClient->Release();
						pAudioClient = nullptr;
						break;
					}
					CoTaskMemFree(pMix);
					if (FAILED(pAudioClient->GetBufferSize(&nAudioBufferFrames)) || FAILED(pAudioClient->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&pAudioRender))) || !pAudioRender)
					{
						pAudioClient->Release();
						pAudioClient = nullptr;
						break;
					}
					pAudioClient->Start();
				} while (false);
				if (pDevice)
					pDevice->Release();
				if (pEnum)
					pEnum->Release();
			}
			if (pAudioClient && pAudioRender)
			{
				UINT32 nPadding = 0;
				if (SUCCEEDED(pAudioClient->GetCurrentPadding(&nPadding)))
				{
					const UINT32 nChunkFrames = static_cast<UINT32>(nAudioRate / 50);
					const UINT32 nMaxQueue = static_cast<UINT32>(nAudioRate * 200 / 1000);
					if (nAudioBufferFrames - nPadding >= nChunkFrames && nPadding <= nMaxQueue)
					{
						BYTE* pData = nullptr;
						if (SUCCEEDED(pAudioRender->GetBuffer(nChunkFrames, &pData)))
						{
							const double flMonStep = static_cast<double>(nUseRate) / nAudioRate;
							double flPos = 0.0;
							for (UINT32 f = 0; f < nChunkFrames; f++)
							{
								const size_t nPos = static_cast<size_t>(flPos);
								float flSample = 0.0f;
								if (nPos < nSamples)
								{
									const float flFrac = static_cast<float>(flPos - nPos);
									const size_t nNext = (std::min<size_t>)(nPos + 1, nSamples - 1);
									flSample = aChunk[nPos] + (aChunk[nNext] - aChunk[nPos]) * flFrac;
								}
								const float flOut = std::clamp(flSample * flVolume, -1.0f, 1.0f);
								if (bAudioFloat)
								{
									float* pFrame = reinterpret_cast<float*>(pData + f * (nAudioChannels * sizeof(float)));
									for (int c = 0; c < nAudioChannels; c++)
										pFrame[c] = flOut;
								}
								else
								{
									short* pFrame = reinterpret_cast<short*>(pData + f * (nAudioChannels * sizeof(short)));
									const short v = static_cast<short>(flOut * 32767.0f);
									for (int c = 0; c < nAudioChannels; c++)
										pFrame[c] = v;
								}
								flPos += flMonStep;
							}
							pAudioRender->ReleaseBuffer(nChunkFrames, 0);
						}
					}
				}
			}
		}
		else if (pAudioClient)
		{
			pAudioClient->Stop();
			pAudioRender->Release();
			pAudioRender = nullptr;
			pAudioClient->Release();
			pAudioClient = nullptr;
			if (bCoOwned)
			{
				CoUninitialize();
				bCoOwned = false;
			}
		}

	}

	if (pEncoder)
		opus_encoder_destroy(pEncoder);
	if (pAudioClient)
	{
		pAudioClient->Stop();
		if (pAudioRender)
			pAudioRender->Release();
		pAudioClient->Release();
		if (bCoOwned)
			CoUninitialize();
	}
}
