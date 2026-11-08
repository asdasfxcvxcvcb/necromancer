#include "SoundpadDecode.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO
// vendored decoder: its size_t/int conversions are internal and benign -
// silence them instead of patching the third-party header
#pragma warning(push)
#pragma warning(disable: 4244 4267)
#include "../../../../../third_party/minimp3_ex.h"
#pragma warning(pop)

#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "../../../../../third_party/dr_wav.h"

#define STB_VORBIS_IMPLEMENTATION
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "../../../../../third_party/stb_vorbis.c"

namespace
{
	constexpr unsigned TARGET_RATE = 48000;

	std::string ToLowerExt(const std::filesystem::path& fsPath)
	{
		std::wstring strExt = fsPath.extension().wstring();
		std::transform(strExt.begin(), strExt.end(), strExt.begin(), [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
		std::string strOut;
		for (wchar_t c : strExt)
		{
			if (c != L'.')
				strOut += static_cast<char>(c);
		}
		return strOut;
	}

	void DownmixToMono(const float* pSamples, size_t nTotalSamples, int nChannels, std::vector<float>& vecOut)
	{
		vecOut.resize(nTotalSamples / nChannels);
		for (size_t i = 0; i < vecOut.size(); i++)
		{
			float flSum = 0.0f;
			for (int c = 0; c < nChannels; c++)
				flSum += pSamples[i * nChannels + c];
			vecOut[i] = flSum / static_cast<float>(nChannels);
		}
	}

	void ResampleLinear(const std::vector<float>& vecIn, unsigned nInRate, std::vector<float>& vecOut)
	{
		if (nInRate == TARGET_RATE || vecIn.empty())
		{
			vecOut = vecIn;
			return;
		}
		const size_t nOutSamples = static_cast<size_t>(static_cast<double>(vecIn.size()) * TARGET_RATE / nInRate);
		vecOut.resize(nOutSamples);
		const double flStep = static_cast<double>(nInRate) / TARGET_RATE;
		for (size_t i = 0; i < nOutSamples; i++)
		{
			const double flPos = i * flStep;
			const size_t nPos = static_cast<size_t>(flPos);
			const float flFrac = static_cast<float>(flPos - nPos);
			const float flA = vecIn[nPos];
			const float flB = (nPos + 1 < vecIn.size()) ? vecIn[nPos + 1] : vecIn.back();
			vecOut[i] = flA + (flB - flA) * flFrac;
		}
	}
}

bool SoundpadDecodeToMono48k(const std::filesystem::path& fsPath, std::vector<float>& vecPcm)
{
	vecPcm.clear();

	std::error_code ec;
	if (!std::filesystem::exists(fsPath, ec) || ec)
		return false;

	std::ifstream file(fsPath, std::ios::binary);
	if (!file.is_open())
		return false;

	std::vector<unsigned char> vecData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	file.close();
	if (vecData.empty())
		return false;

	const std::string strExt = ToLowerExt(fsPath);
	std::vector<float> vecRaw;
	unsigned nRate = 0;
	int nChannels = 0;

	if (strExt == "mp3")
	{
		mp3dec_t dec;
		mp3dec_init(&dec);
		mp3dec_file_info_t info = {};
		if (mp3dec_load_buf(&dec, vecData.data(), vecData.size(), &info, nullptr, nullptr) != 0 || !info.buffer || !info.samples)
		{
			if (info.buffer)
				free(info.buffer);
			return false;
		}
		nRate = static_cast<unsigned>(info.hz);
		nChannels = info.channels > 0 ? info.channels : 1;
		vecRaw.resize(info.samples);
		for (size_t i = 0; i < info.samples; i++)
			vecRaw[i] = info.buffer[i] / 32768.0f;
		free(info.buffer);
	}
	else if (strExt == "ogg")
	{
		short* pOutput = nullptr;
		int nChannelsOut = 0;
		int nRateOut = 0;
		const int nSamples = stb_vorbis_decode_memory(vecData.data(), static_cast<int>(vecData.size()), &nChannelsOut, &nRateOut, &pOutput);
		if (nSamples <= 0 || !pOutput)
		{
			if (pOutput)
				free(pOutput);
			return false;
		}
		nRate = static_cast<unsigned>(nRateOut);
		nChannels = nChannelsOut > 0 ? nChannelsOut : 1;
		vecRaw.resize(static_cast<size_t>(nSamples) * nChannels);
		for (size_t i = 0; i < vecRaw.size(); i++)
			vecRaw[i] = pOutput[i] / 32768.0f;
		free(pOutput);
	}
	else if (strExt == "wav")
	{
		drwav wav{};
		if (!drwav_init_memory(&wav, vecData.data(), vecData.size(), nullptr))
			return false;
		nRate = wav.sampleRate;
		nChannels = wav.channels > 0 ? wav.channels : 1;
		const drwav_uint64 nFrames = wav.totalPCMFrameCount;
		vecRaw.resize(static_cast<size_t>(nFrames) * nChannels);
		const drwav_uint64 nRead = drwav_read_pcm_frames_f32(&wav, nFrames, vecRaw.data());
		drwav_uninit(&wav);
		if (nRead == 0)
			return false;
		vecRaw.resize(static_cast<size_t>(nRead) * nChannels);
	}
	else
	{
		return false;
	}

	if (nRate == 0)
		nRate = TARGET_RATE;

	std::vector<float> vecMono;
	DownmixToMono(vecRaw.data(), vecRaw.size(), nChannels, vecMono);
	ResampleLinear(vecMono, nRate, vecPcm);
	return !vecPcm.empty();
}
