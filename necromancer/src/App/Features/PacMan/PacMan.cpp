#include "PacMan.h"

#include "../CFG.h"

#include "PacManSoundsData.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstring>
#include <mutex>
#include <mmsystem.h>
#include <random>
#include <thread>
#include <vector>

#pragma comment(lib, "winmm.lib")

static constexpr float kPi = 3.14159265358979f;

// Classic arcade maze, 28 x 31.
// '#' wall, '.' pellet, 'o' power pellet, ' ' empty path, '-' ghost house door
static const char* const s_pszMaze[CPacMan::MAZE_H] =
{
	"############################",
	"#............##............#",
	"#.####.#####.##.#####.####.#",
	"#o####.#####.##.#####.####o#",
	"#.####.#####.##.#####.####.#",
	"#..........................#",
	"#.####.##.########.##.####.#",
	"#.####.##.########.##.####.#",
	"#......##....##....##......#",
	"######.##### ## #####.######",
	"     #.##### ## #####.#     ",
	"     #.##          ##.#     ",
	"     #.## ###--### ##.#     ",
	"######.## #      # ##.######",
	"      .   #      #   .      ",
	"######.## #      # ##.######",
	"     #.## ######## ##.#     ",
	"     #.##          ##.#     ",
	"     #.## ######## ##.#     ",
	"######.## ######## ##.######",
	"#............##............#",
	"#.####.#####.##.#####.####.#",
	"#.####.#####.##.#####.####.#",
	"#o..##.......  .......##..o#",
	"###.##.##.########.##.##.###",
	"###.##.##.########.##.##.###",
	"#......##....##....##......#",
	"#.##########.##.##########.#",
	"#.##########.##.##########.#",
	"#..........................#",
	"############################"
};

// Direction tables, indexed by EDir (UP, LEFT, DOWN, RIGHT)
static constexpr int DIR_DX[4] = { 0, -1, 0, 1 };
static constexpr int DIR_DY[4] = { -1, 0, 1, 0 };
static const float DIR_ANGLE[4] = { -kPi / 2.f, kPi, kPi / 2.f, 0.f };

static int OppositeDir(int nDir)
{
	return (nDir + 2) % 4;
}

static int WrapTileX(int nTileX)
{
	return ((nTileX % CPacMan::MAZE_W) + CPacMan::MAZE_W) % CPacMan::MAZE_W;
}

// ---------------------------------------------------------------------------
// Sound: tiny waveOut based synth. SFX are synthesized into a sample queue that
// a background thread drains into waveOut blocks; the siren is generated live.
// ---------------------------------------------------------------------------

namespace PacManSound
{
	enum ESfx { SFX_WAKA_A, SFX_WAKA_B, SFX_POWER, SFX_GHOST, SFX_FRUIT, SFX_EXTEND, SFX_DEATH, SFX_DEATH_END, SFX_START, SFX_CLEAR, SFX_CREDIT };

	constexpr int SAMPLE_RATE = 22050;
	constexpr int BLOCK_SAMPLES = 1024;	// ~46 ms per block
	constexpr int NUM_BLOCKS = 4;

	// the intro theme's length - the first READY pause lasts as long as it plays
	static constexpr float START_SECONDS = PacManSoundsData::nStart / float(SAMPLE_RATE);

	static std::atomic<bool> s_bThreadRunning{ false };
	static std::atomic<bool> s_bDeviceFailed{ false };
	static std::atomic<int> s_nSiren{ 0 };		// 0 = off, 1 = normal siren, 2 = frightened, 3 = eyes
	static std::atomic<int> s_nSirenStage{ 0 };	// normal siren pitch stage 0-4, rises as dots are eaten
	static std::mutex s_mutex;
	static std::vector<int16_t> s_vecQueue;
	static HWAVEOUT s_hWave = nullptr;
	static WAVEHDR s_arrHeaders[NUM_BLOCKS] = {};
	static int16_t* s_arrBlocks[NUM_BLOCKS] = {};
	static bool s_arrQueued[NUM_BLOCKS] = {};

	// nWave: 0 = sine (smooth), 1 = softened square (chippy), 2 = triangle (soft chippy),
	//        3 = 25% duty pulse (Namco WSG style) - every wave gets the 4-bit output crunch
	static float WaveSample(float flPhase, int nWave)
	{
		float flSample = sinf(flPhase);

		if (nWave == 1)
			flSample = flSample > 0.f ? 0.6f : -0.6f;
		else if (nWave == 2)
			flSample = 2.f / kPi * asinf(sinf(flPhase));
		else if (nWave == 3)
			flSample = fmodf(flPhase, 2.f * kPi) < (2.f * kPi * 0.25f) ? 0.6f : -0.6f;

		return flSample;
	}

	static void SynthTone(std::vector<int16_t>& vecOut, float flFrom, float flTo, float flDuration, float flVolume, int nWave = 0, float flVibratoRate = 0.f, float flVibratoDepth = 0.f, float flDetune = 0.f)
	{
		const int nSamples = static_cast<int>(flDuration * SAMPLE_RATE);
		float flPhase = 0.f;
		float flPhase2 = 0.f;

		for (int i = 0; i < nSamples; i++)
		{
			const float flT = float(i) / SAMPLE_RATE;
			const float flProgress = float(i) / nSamples;
			// exponential glide - arcade oscillators sweep by pitch ratio, not linearly
			float flFreq = flFrom * powf(flTo / flFrom, flProgress);

			// warble grows over the length of the note
			if (flVibratoDepth > 0.f)
				flFreq += sinf(2.f * kPi * flVibratoRate * flT) * flVibratoDepth * flProgress;

			flPhase += 2.f * kPi * flFreq / SAMPLE_RATE;

			float flSample = WaveSample(flPhase, nWave);

			// mix in a slightly detuned second voice for the thick arcade buzz
			if (flDetune > 0.f)
			{
				flPhase2 += 2.f * kPi * flFreq * (1.f + flDetune) / SAMPLE_RATE;
				flSample = (flSample + WaveSample(flPhase2, nWave)) * 0.5f;
			}

			// 8-level amplitude crunch on everything - the arcade's waveform generator is only 4-bit
			flSample = floorf(flSample * 4.f) / 4.f;

			const float flEnv = 1.f - 0.35f * flProgress;
			vecOut.push_back(static_cast<int16_t>(std::clamp(flSample, -1.f, 1.f) * flVolume * flEnv * 32767.f));
		}
	}

	static std::vector<int16_t> BuildSfx(int nSfx)
	{
		std::vector<int16_t> vecOut;

		switch (nSfx)
		{
		case SFX_WAKA_A:
			// arcade chomp - two detuned crunchy pulse voices sweeping down, then up on the next pellet
			SynthTone(vecOut, 620.f, 230.f, 0.08f, 0.16f, 3, 0.f, 0.f, 0.015f);
			break;
		case SFX_WAKA_B:
			SynthTone(vecOut, 230.f, 620.f, 0.08f, 0.16f, 3, 0.f, 0.f, 0.015f);
			break;
		case SFX_POWER:
			SynthTone(vecOut, 200.f, 720.f, 0.28f, 0.18f, 0);
			break;
		case SFX_GHOST:
			// a run of quick rising sweeps, like the arcade ghost-eaten sound
			for (int i = 0; i < 8; i++)
				SynthTone(vecOut, 380.f + i * 110.f, 1400.f + i * 90.f, 0.05f, 0.2f, 0);
			break;
		case SFX_FRUIT:
			// quick double blip for the bonus fruit
			SynthTone(vecOut, 480.f, 900.f, 0.08f, 0.2f, 2);
			SynthTone(vecOut, 640.f, 1200.f, 0.08f, 0.2f, 2);
			break;
		case SFX_DEATH:
			// descending undulating wail with a detuned second voice, then two final blips
			SynthTone(vecOut, 850.f, 170.f, 1.1f, 0.22f, 0, 26.f, 130.f, 0.02f);
			SynthTone(vecOut, 110.f, 420.f, 0.14f, 0.2f, 0);
			SynthTone(vecOut, 110.f, 420.f, 0.14f, 0.2f, 0);
			break;
		case SFX_START:
			// short arcade-style start jingle
			for (float f : { 659.26f, 783.99f, 987.77f, 1318.51f, 987.77f, 1318.51f, 1567.98f })
				SynthTone(vecOut, f, f, 0.11f, 0.16f, 2);
			SynthTone(vecOut, 2093.f, 2093.f, 0.3f, 0.16f, 2);
			break;
		case SFX_CLEAR:
			for (float f : { 523.25f, 659.26f, 783.99f, 1046.5f })
				SynthTone(vecOut, f, f, 0.09f, 0.16f, 2);
			break;
		}

		return vecOut;
	}

	// user-supplied arcade recordings embedded by embed_sounds.py - nullptr falls back to the synth
	struct Embed_t { const short* pData; int nSamples; };

	static Embed_t GetEmbed(int nSfx)
	{
		switch (nSfx)
		{
		case SFX_WAKA_A: return { PacManSoundsData::arrWaka0, PacManSoundsData::nWaka0 };
		case SFX_WAKA_B: return { PacManSoundsData::arrWaka1, PacManSoundsData::nWaka1 };
		case SFX_GHOST: return { PacManSoundsData::arrGhost, PacManSoundsData::nGhost };
		case SFX_FRUIT: return { PacManSoundsData::arrFruit, PacManSoundsData::nFruit };
		case SFX_EXTEND: return { PacManSoundsData::arrExtend, PacManSoundsData::nExtend };
		case SFX_DEATH: return { PacManSoundsData::arrDeath0, PacManSoundsData::nDeath0 };
		case SFX_DEATH_END: return { PacManSoundsData::arrDeath1, PacManSoundsData::nDeath1 };
		case SFX_START: return { PacManSoundsData::arrStart, PacManSoundsData::nStart };
		case SFX_CREDIT: return { PacManSoundsData::arrCredit, PacManSoundsData::nCredit };
		default: return { nullptr, 0 };
		}
	}

	// the looping background sirens - normal (5 pitch stages), frightened and eyes-returning
	static Embed_t GetLoop(int nMode, int nStage)
	{
		switch (nMode)
		{
		case 1:
		{
			static const Embed_t arrSirens[5] =
			{
				{ PacManSoundsData::arrSiren0, PacManSoundsData::nSiren0 },
				{ PacManSoundsData::arrSiren1, PacManSoundsData::nSiren1 },
				{ PacManSoundsData::arrSiren2, PacManSoundsData::nSiren2 },
				{ PacManSoundsData::arrSiren3, PacManSoundsData::nSiren3 },
				{ PacManSoundsData::arrSiren4, PacManSoundsData::nSiren4 },
			};
			return arrSirens[std::min(std::max(nStage, 0), 4)];
		}
		case 2: return { PacManSoundsData::arrFright, PacManSoundsData::nFright };
		case 3: return { PacManSoundsData::arrEyes, PacManSoundsData::nEyes };
		default: return { nullptr, 0 };
		}
	}

	static void FillBlock(int16_t* pDst, int nSamples)
	{
		static float s_flSirenPhase = 0.f;
		static double s_flTime = 0.0;
		static int s_nLoopMode = -1;	// loop bookkeeping lives on the audio thread only
		static int s_nLoopPos = 0;

		std::vector<int16_t> vecSfx;
		{
			std::lock_guard<std::mutex> lock(s_mutex);
			const size_t nTake = std::min(s_vecQueue.size(), static_cast<size_t>(nSamples));
			vecSfx.assign(s_vecQueue.begin(), s_vecQueue.begin() + nTake);
			s_vecQueue.erase(s_vecQueue.begin(), s_vecQueue.begin() + nTake);
		}

		const int nSiren = s_nSiren;

		// siren changed - restart the loop from the top
		if (nSiren != s_nLoopMode)
		{
			s_nLoopMode = nSiren;
			s_nLoopPos = 0;
		}

		const Embed_t loop = GetLoop(nSiren, s_nSirenStage);

		for (int i = 0; i < nSamples; i++)
		{
			float flSample = 0.f;

			if (i < static_cast<int>(vecSfx.size()))
				flSample += vecSfx[i] / 32767.f;

			s_flTime += 1.0 / SAMPLE_RATE;

			if (loop.pData)
			{
				// arcade siren recording, scaled under the sound effects
				// (the fright track is meant to stand out, like the arcade)
				const float flLoopGain = nSiren == 2 ? 0.2f : 0.1f;

				flSample += flLoopGain * loop.pData[s_nLoopPos] / 32767.f;
				s_nLoopPos = (s_nLoopPos + 1) % loop.nSamples;
			}
			else if (nSiren == 1)
			{
				// synth fallback: rising/falling siren sweep on a stepped 8-level wave
				const float flFreq = 420.f + 130.f * sinf(float(s_flTime) * 2.f * kPi * 2.f);
				s_flSirenPhase += 2.f * kPi * flFreq / SAMPLE_RATE;
				flSample += 0.035f * (floorf(sinf(s_flSirenPhase) * 4.f) / 4.f);
			}
			else if (nSiren == 2)
			{
				// synth fallback: frightened wobble
				const float flFreq = 550.f + 150.f * sinf(float(s_flTime) * 2.f * kPi * 8.f);
				s_flSirenPhase += 2.f * kPi * flFreq / SAMPLE_RATE;
				flSample += 0.04f * (floorf(sinf(s_flSirenPhase) * 4.f) / 4.f);
			}

			pDst[i] = static_cast<int16_t>(std::clamp(flSample, -1.f, 1.f) * 32767.f);
		}
	}

	static void AudioThread()
	{
		while (s_bThreadRunning)
		{
			bool bBusy = false;

			for (int i = 0; i < NUM_BLOCKS; i++)
			{
				WAVEHDR& hdr = s_arrHeaders[i];

				if (s_arrQueued[i] && !(hdr.dwFlags & WHDR_DONE))
					continue;

				if (s_arrQueued[i])
					waveOutUnprepareHeader(s_hWave, &hdr, sizeof(WAVEHDR));

				FillBlock(s_arrBlocks[i], BLOCK_SAMPLES);

				hdr.dwBufferLength = BLOCK_SAMPLES * sizeof(int16_t);
				hdr.dwFlags = 0;

				if (waveOutPrepareHeader(s_hWave, &hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR
					&& waveOutWrite(s_hWave, &hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR)
				{
					s_arrQueued[i] = true;
					bBusy = true;
				}
				else
				{
					s_arrQueued[i] = false;
				}
			}

			Sleep(bBusy ? 5 : 10);
		}
	}

	static void EnsureStarted()
	{
		if (s_bThreadRunning || s_bDeviceFailed)
			return;

		WAVEFORMATEX fmt = {};
		fmt.wFormatTag = WAVE_FORMAT_PCM;
		fmt.nChannels = 1;
		fmt.nSamplesPerSec = SAMPLE_RATE;
		fmt.wBitsPerSample = 16;
		fmt.nBlockAlign = 2;
		fmt.nAvgBytesPerSec = SAMPLE_RATE * 2;

		if (waveOutOpen(&s_hWave, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
		{
			s_bDeviceFailed = true;
			return;
		}

		for (int i = 0; i < NUM_BLOCKS; i++)
		{
			s_arrBlocks[i] = new int16_t[BLOCK_SAMPLES];
			memset(&s_arrHeaders[i], 0, sizeof(WAVEHDR));
			s_arrHeaders[i].lpData = reinterpret_cast<LPSTR>(s_arrBlocks[i]);
			s_arrHeaders[i].dwBufferLength = BLOCK_SAMPLES * sizeof(int16_t);
			s_arrQueued[i] = false;
		}

		s_bThreadRunning = true;
		std::thread(AudioThread).detach();
	}

	void Play(int nSfx)
	{
		EnsureStarted();

		if (!s_bThreadRunning || s_bDeviceFailed)
			return;

		std::vector<int16_t> vecSfx;
		const Embed_t embed = GetEmbed(nSfx);

		if (embed.pData)
		{
			// real arcade recording, scaled into the mix
			vecSfx.reserve(embed.nSamples);

			for (int i = 0; i < embed.nSamples; i++)
				vecSfx.push_back(static_cast<int16_t>(embed.pData[i] * 0.5f));
		}
		else
		{
			vecSfx = BuildSfx(nSfx);
		}

		std::lock_guard<std::mutex> lock(s_mutex);
		if (s_vecQueue.size() > SAMPLE_RATE)	// drop when over a second of backlog
			return;

		s_vecQueue.insert(s_vecQueue.end(), vecSfx.begin(), vecSfx.end());
	}

	void SetSiren(int nMode, int nStage = 0)
	{
		s_nSiren = nMode;
		s_nSirenStage = std::min(std::max(nStage, 0), 4);
	}

	void Quiet()
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		s_vecQueue.clear();
		s_nSiren = 0;
	}

	bool IsAvailable()
	{
		return !s_bDeviceFailed;
	}
}

// ---------------------------------------------------------------------------
// Game logic
// ---------------------------------------------------------------------------

bool CPacMan::Walkable(int nTileX, int nTileY) const
{
	if (nTileY < 0 || nTileY >= MAZE_H)
		return false;

	const char c = s_pszMaze[nTileY][WrapTileX(nTileX)];
	return c != '#' && c != '-';
}

void CPacMan::ResetPellets()
{
	m_nPelletsLeft = 0;
	m_nFruitEaten = 0;
	m_nFruitType = -1;

	for (int y = 0; y < MAZE_H; y++)
	{
		for (int x = 0; x < MAZE_W; x++)
		{
			const char c = s_pszMaze[y][x];
			m_arrPellets[y][x] = c == '.' ? 1 : c == 'o' ? 2 : 0;

			if (m_arrPellets[y][x])
				m_nPelletsLeft++;
		}
	}
}

void CPacMan::ResetActors()
{
	m_pac.x = 13.5f;
	m_pac.y = 23.5f;
	m_pac.nDir = DIR_NONE;
	m_pac.nQueued = DIR_NONE;
	m_flPacAngle = 0.f;
	m_flPacAnim = 0.f;

	m_bScatter = true;
	m_flModeTimer = 0.f;
	m_flFrightTimer = 0.f;
	m_nGhostChain = 0;
	m_flFreezeTimer = 0.f;

	// the fruit disappears when a life is lost
	m_nFruitType = -1;
	m_flFruitPopupTimer = 0.f;

	// respawns and fresh levels use the short READY pause
	m_flReadyTime = 2.f;

	const float flReleaseMul = std::max(0.4f, 1.f - (m_nLevel - 1) * 0.1f);
	static const Color_t arrColors[4] = { { 255, 70, 45, 255 }, { 255, 153, 255, 255 }, { 70, 255, 156, 255 }, { 255, 166, 66, 255 } };

	for (int i = 0; i < 4; i++)
	{
		Ghost_t& g = m_ghosts[i];
		g.nPersonality = i;
		g.clr = arrColors[i];
		g.bFrightened = false;
		g.bEaten = false;
		g.bLeaving = false;
		g.nDir = DIR_LEFT;

		if (i == 0)
		{
			// blinky starts outside the house straight away
			g.bActive = true;
			g.flRelease = 0.f;
			g.x = 13.5f;
			g.y = 11.5f;
		}
		else
		{
			// the others wait in the house (draw only until released)
			g.bActive = false;
			g.flRelease = (i == 1 ? 2.f : i == 2 ? 5.f : 8.f) * flReleaseMul;
			g.x = i == 1 ? 14.f : i == 2 ? 12.f : 16.f;
			g.y = 14.5f;
		}
	}
}

void CPacMan::AddScore(int nPoints)
{
	m_nScore += nPoints;

	if (m_nScore > CFG::PacMan_HighScore)
		CFG::PacMan_HighScore = m_nScore;

	// the arcade hands out a bonus life at 10,000 points, then every 10,000 after
	while (m_nScore >= m_nNextExtraLife)
	{
		m_nNextExtraLife += 10000;
		m_nLives++;
		PlaySfx(PacManSound::SFX_EXTEND);
	}
}

void CPacMan::StartGame()
{
	PacManSound::EnsureStarted();
	PacManSound::Quiet();

	m_bRunning = true;
	m_nScore = 0;
	m_nLives = 3;
	m_nLevel = 1;
	m_bWakaToggle = false;
	m_bDeathBlipPlayed = false;
	m_nNextExtraLife = 10000;

	ResetPellets();
	ResetActors();

	m_eState = READY;
	m_flStateTime = 0.f;
	m_flLastFrame = Plat_FloatTime();

	PlaySfx(PacManSound::SFX_START);

	// the first READY pause lasts as long as the intro theme, like the arcade -
	// respawns after a death fall back to the short 2 second pause
	m_flReadyTime = (CFG::PacMan_Sound && PacManSound::IsAvailable()) ? PacManSound::START_SECONDS + 0.25f : 2.f;
}

void CPacMan::StopGame()
{
	m_bRunning = false;
	PacManSound::Quiet();
}

void CPacMan::NotifyVisible(bool bVisible)
{
	// tab switched away or menu closed - the game is paused, so silence the siren
	if (!bVisible && m_bPageVisible)
		PacManSound::Quiet();

	m_bPageVisible = bVisible;
}

void CPacMan::PlaySfx(int nSfx)
{
	if (CFG::PacMan_Sound)
		PacManSound::Play(nSfx);
}

void CPacMan::TriggerFright()
{
	// seconds of fright per level, straight from the arcade table (0 = no fright)
	static const int arrFright[] = { 6, 5, 4, 3, 2, 5, 2, 2, 1, 5, 2, 1, 1, 3, 1, 1, 0, 1, 0 };
	const int nDuration = arrFright[std::min(m_nLevel, static_cast<int>(sizeof(arrFright) / sizeof(arrFright[0]))) - 1];

	m_flFrightTimer = std::max(m_flFrightTimer, static_cast<float>(nDuration));
	m_nGhostChain = 0;

	for (Ghost_t& g : m_ghosts)
	{
		if (g.bActive && !g.bEaten)
		{
			// a power pellet always turns the ghosts around
			g.nDir = OppositeDir(g.nDir);

			// cruise elroy blinky never turns blue
			const bool bElroy = g.nPersonality == 0 && m_nPelletsLeft <= 20;

			if (nDuration > 0 && !bElroy)
				g.bFrightened = true;
		}
	}
}

void CPacMan::EatPelletAt(int nTileX, int nTileY)
{
	const byte bType = m_arrPellets[nTileY][nTileX];
	m_arrPellets[nTileY][nTileX] = 0;
	m_nPelletsLeft--;
	m_nFruitEaten++;

	AddScore(bType == 2 ? 50 : 10);

	PlaySfx(m_bWakaToggle ? PacManSound::SFX_WAKA_A : PacManSound::SFX_WAKA_B);
	m_bWakaToggle = !m_bWakaToggle;

	if (bType == 2)
		TriggerFright();

	// bonus fruit shows up twice a level, after 70 and 170 pellets
	if ((m_nFruitEaten == 70 || m_nFruitEaten == 170) && m_nFruitType < 0)
		SpawnFruit();

	if (m_nPelletsLeft <= 0)
	{
		m_eState = LEVEL_CLEAR;
		m_flStateTime = 0.f;
		PlaySfx(PacManSound::SFX_CLEAR);
	}
}

void CPacMan::SpawnFruit()
{
	static const int arrFruitScore[8] = { 100, 300, 500, 700, 1000, 2000, 3000, 5000 };

	// cherry, strawberry, orange, apple, melon, galaxian, bell, key - by level
	int nType = 0;
	if (m_nLevel >= 13)
		nType = 7;
	else if (m_nLevel >= 11)
		nType = 6;
	else if (m_nLevel >= 9)
		nType = 5;
	else if (m_nLevel >= 7)
		nType = 4;
	else if (m_nLevel >= 5)
		nType = 3;
	else if (m_nLevel >= 3)
		nType = 2;
	else if (m_nLevel >= 2)
		nType = 1;

	m_nFruitType = nType;
	m_nFruitScore = arrFruitScore[nType];
	m_flFruitTimer = 9.5f;
	m_flFruitX = 13.5f;
	m_flFruitY = 17.5f;	// the open courtyard just below the ghost house
}

// classic arcade speeds in tiles per second (100% = 9.47 tiles/s)
float CPacMan::PacSpeed() const
{
	constexpr float flFull = 9.47f;

	if (m_nLevel == 1)
		return 0.8f * flFull;

	if (m_nLevel <= 4)
		return 0.9f * flFull;

	return flFull;
}

float CPacMan::GhostSpeed(const Ghost_t& g) const
{
	constexpr float flFull = 9.47f;

	// eyes race home no matter what
	if (g.bEaten)
		return 15.f;

	// the tunnel corridors slow everyone down
	const int nTileX = WrapTileX(static_cast<int>(g.x));

	if (static_cast<int>(g.y) == 14 && (nTileX <= 5 || nTileX >= 22))
		return (m_nLevel == 1 ? 0.4f : m_nLevel <= 4 ? 0.45f : 0.5f) * flFull;

	float flPct;

	if (g.bFrightened)
		flPct = m_nLevel == 1 ? 0.5f : m_nLevel <= 4 ? 0.55f : 0.6f;
	else
	{
		if (m_nLevel == 1)
			flPct = 0.75f;
		else if (m_nLevel <= 4)
			flPct = 0.85f;
		else if (m_nLevel <= 20)
			flPct = 0.95f;
		else
			flPct = 1.05f;

		// elroy: blinky speeds up when the maze is almost cleared
		if (g.nPersonality == 0)
		{
			if (m_nPelletsLeft <= 10)
				flPct += 0.1f;
			else if (m_nPelletsLeft <= 20)
				flPct += 0.05f;
		}
	}

	return flPct * flFull;
}

// Move along the current direction in tile units, making a turn decision
// every time a tile center is reached. Positions wrap through the tunnel.
void CPacMan::StepEntity(float& flX, float& flY, int& nDir, float flDist, bool bPac, Ghost_t* pGhost)
{
	while (flDist > 0.0001f && nDir != DIR_NONE)
	{
		const bool bHoriz = nDir == DIR_LEFT || nDir == DIR_RIGHT;
		const int nSign = nDir == DIR_RIGHT || nDir == DIR_DOWN ? 1 : -1;
		float& flPos = bHoriz ? flX : flY;

		const float flCenter = (floorf(flPos) + 0.5f);
		const float flToCenter = (flCenter - flPos) * nSign;
		const float flNext = flToCenter > 0.0001f ? flToCenter : flToCenter + 1.f;

		if (flDist < flNext - 0.0001f)
		{
			flPos += nSign * flDist;
			return;
		}

		flPos += nSign * flNext;
		flDist -= flNext;

		// tunnel wrap, then snap exactly onto the tile center we just reached
		if (flX < 0.f)
			flX += MAZE_W;
		else if (flX >= MAZE_W)
			flX -= MAZE_W;

		flX = (floorf(flX) + 0.5f);
		flY = (floorf(flY) + 0.5f);

		if (bPac)
			DecidePac(flX, flY, nDir);
		else if (pGhost)
			DecideGhost(*pGhost, flX, flY, nDir);
	}
}

void CPacMan::DecidePac(float& flX, float& flY, int& nDir)
{
	const int nTileX = WrapTileX(static_cast<int>(floorf(flX)));
	const int nTileY = static_cast<int>(floorf(flY));

	if (m_pac.nQueued != DIR_NONE
		&& Walkable(nTileX + DIR_DX[m_pac.nQueued], nTileY + DIR_DY[m_pac.nQueued]))
	{
		nDir = m_pac.nQueued;
		m_pac.nQueued = DIR_NONE;
		return;
	}

	if (!Walkable(nTileX + DIR_DX[nDir], nTileY + DIR_DY[nDir]))
		nDir = DIR_NONE;
}

void CPacMan::GhostTargetTile(const Ghost_t& g, int& nTileX, int& nTileY) const
{
	static constexpr int SCATTER[4][2] = { { 25, -2 }, { 2, -2 }, { 27, MAZE_H + 2 }, { 0, MAZE_H + 2 } };

	// eaten eyes head straight back into the ghost house
	if (g.bEaten)
	{
		nTileX = 13;
		nTileY = 14;
		return;
	}

	const int nPacX = WrapTileX(static_cast<int>(floorf(m_pac.x)));
	const int nPacY = static_cast<int>(floorf(m_pac.y));

	if (m_bScatter && !g.bFrightened)
	{
		nTileX = SCATTER[g.nPersonality][0];
		nTileY = SCATTER[g.nPersonality][1];
		return;
	}

	// pac never moves while stopped, fall back to facing right for the target math
	const int nPacDir = m_pac.nDir != DIR_NONE ? m_pac.nDir : DIR_RIGHT;

	switch (g.nPersonality)
	{
	case 0: // blinky - straight chase
		nTileX = nPacX;
		nTileY = nPacY;
		break;
	case 1: // pinky - four tiles ahead of pac
		nTileX = nPacX + DIR_DX[nPacDir] * 4;
		nTileY = nPacY + DIR_DY[nPacDir] * 4;
		break;
	case 2: // inky - mirrored through blinky
	{
		const Ghost_t& blinky = m_ghosts[0];
		nTileX = nPacX * 2 - WrapTileX(static_cast<int>(floorf(blinky.x)));
		nTileY = nPacY * 2 - static_cast<int>(floorf(blinky.y));
		break;
	}
	default: // clyde - chases when far, runs to his corner when close
	{
		const Ghost_t& self = g;
		const int dx = nPacX - WrapTileX(static_cast<int>(floorf(self.x)));
		const int dy = nPacY - static_cast<int>(floorf(self.y));

		if (dx * dx + dy * dy > 64)
		{
			nTileX = nPacX;
			nTileY = nPacY;
		}
		else
		{
			nTileX = SCATTER[3][0];
			nTileY = SCATTER[3][1];
		}
		break;
	}
	}
}

void CPacMan::DecideGhost(Ghost_t& g, float& flX, float& flY, int& nDir)
{
	const int nTileX = WrapTileX(static_cast<int>(floorf(flX)));
	const int nTileY = static_cast<int>(floorf(flY));

	int arrCandidates[4] = {};
	int nCount = 0;

	for (int d = 0; d < 4; d++)
	{
		if (nDir != DIR_NONE && d == OppositeDir(nDir))
			continue;

		const int nx = nTileX + DIR_DX[d];
		const int ny = nTileY + DIR_DY[d];

		// the eaten eyes may slip through the ghost house door
		const bool bDoor = g.bEaten && ny == 12 && (nx == 13 || nx == 14);

		if (!bDoor && !Walkable(nx, ny))
			continue;

		arrCandidates[nCount++] = d;
	}

	if (nCount == 0)
	{
		nDir = OppositeDir(nDir);	// dead end - turn around
		return;
	}

	if (g.bFrightened)
	{
		static std::mt19937 rng{ std::random_device{}() };
		nDir = arrCandidates[rng() % nCount];
		return;
	}

	int nTargetX = 0, nTargetY = 0;
	GhostTargetTile(g, nTargetX, nTargetY);

	int nBestDist = INT_MAX;
	for (int i = 0; i < nCount; i++)
	{
		const int nx = nTileX + DIR_DX[arrCandidates[i]];
		const int ny = nTileY + DIR_DY[arrCandidates[i]];
		const int nDist = (nx - nTargetX) * (nx - nTargetX) + (ny - nTargetY) * (ny - nTargetY);

		if (nDist < nBestDist)
		{
			nBestDist = nDist;
			nDir = arrCandidates[i];
		}
	}
}

void CPacMan::UpdatePac(float flDt)
{
	// reversing into the queued direction is always allowed
	if (m_pac.nQueued != DIR_NONE && m_pac.nDir != DIR_NONE && m_pac.nQueued == OppositeDir(m_pac.nDir))
	{
		m_pac.nDir = m_pac.nQueued;
		m_pac.nQueued = DIR_NONE;
	}
	else if (m_pac.nDir == DIR_NONE && m_pac.nQueued != DIR_NONE)
	{
		const int nTileX = WrapTileX(static_cast<int>(floorf(m_pac.x)));
		const int nTileY = static_cast<int>(floorf(m_pac.y));

		if (Walkable(nTileX + DIR_DX[m_pac.nQueued], nTileY + DIR_DY[m_pac.nQueued]))
		{
			m_pac.nDir = m_pac.nQueued;
			m_pac.nQueued = DIR_NONE;
		}
	}

	const float flSpeed = PacSpeed();
	StepEntity(m_pac.x, m_pac.y, m_pac.nDir, flSpeed * flDt, true);

	if (m_pac.nDir != DIR_NONE)
	{
		m_flPacAngle = DIR_ANGLE[m_pac.nDir];
		m_flPacAnim += flDt * 10.f;
	}

	const int nTileX = WrapTileX(static_cast<int>(floorf(m_pac.x)));
	const int nTileY = static_cast<int>(floorf(m_pac.y));

	if (nTileY >= 0 && nTileY < MAZE_H && m_arrPellets[nTileY][nTileX])
		EatPelletAt(nTileX, nTileY);

	// grabbing the bonus fruit
	if (m_nFruitType >= 0)
	{
		const float fdx = m_pac.x - m_flFruitX;
		const float fdy = m_pac.y - m_flFruitY;

		if (fdx * fdx + fdy * fdy < 0.7f * 0.7f)
		{
			AddScore(m_nFruitScore);
			m_nFruitPopupScore = m_nFruitScore;
			m_flFruitPopupX = m_flFruitX;
			m_flFruitPopupY = m_flFruitY;
			m_flFruitPopupTimer = 1.2f;
			m_nFruitType = -1;
			PlaySfx(PacManSound::SFX_FRUIT);
		}
	}
}

void CPacMan::UpdateGhosts(float flDt)
{
	for (Ghost_t& g : m_ghosts)
	{
		if (!g.bActive)
		{
			g.flRelease -= flDt;

			if (g.flRelease <= 0.f)
			{
				g.bActive = true;
				g.x = g.nPersonality % 2 ? 14.5f : 13.5f;
				g.y = 11.5f;
				g.nDir = DIR_LEFT;
			}

			continue;
		}

		// revived ghosts rise out of the house through the door
		if (g.bLeaving)
		{
			g.x = 13.5f;
			g.y -= 5.f * flDt;

			if (g.y <= 11.5f)
			{
				g.y = 11.5f;
				g.bLeaving = false;
				g.nDir = DIR_LEFT;
			}

			continue;
		}

		// eaten eyes race back into the house, everything else walks its AI
		const float flSpeed = GhostSpeed(g);
		StepEntity(g.x, g.y, g.nDir, flSpeed * flDt, false, &g);

		// made it inside the ghost house - revive and rise back out
		if (g.bEaten && g.y >= 13.f && g.y <= 16.f && fabsf(g.x - 13.5f) < 0.75f)
		{
			g.x = 13.5f;
			g.y = 14.5f;
			g.bEaten = false;
			g.bLeaving = true;
		}
	}
}

void CPacMan::CheckCollisions()
{
	for (Ghost_t& g : m_ghosts)
	{
		if (!g.bActive || g.bEaten)
			continue;

		const float dx = m_pac.x - g.x;
		const float dy = m_pac.y - g.y;

		if (dx * dx + dy * dy > 0.6f * 0.6f)
			continue;

		if (g.bFrightened)
		{
			// classic: everything freezes for a moment and the score shows where the ghost was
			g.bEaten = true;
			g.bFrightened = false;
			m_nFreezeScore = 200 << std::min(m_nGhostChain, 3);
			m_nGhostChain++;
			m_flFreezeTimer = 0.5f;
			m_flFreezeX = g.x;
			m_flFreezeY = g.y;
			AddScore(m_nFreezeScore);
			PlaySfx(PacManSound::SFX_GHOST);
		}
		else
		{
			m_eState = DYING;
			m_flStateTime = 0.f;
			PlaySfx(PacManSound::SFX_DEATH);
			return;
		}
	}
}

void CPacMan::UpdateGame(float flDt)
{
	m_flStateTime += flDt;

	switch (m_eState)
	{
	case READY:
		if (m_flStateTime >= m_flReadyTime)
		{
			m_eState = PLAY;
			m_flStateTime = 0.f;
		}
		break;

	case PLAY:
	{
		// the fruit score popup keeps ticking through the freeze
		if (m_flFruitPopupTimer > 0.f)
			m_flFruitPopupTimer -= flDt;

		// everything pauses while the ghost score is on screen, like the arcade
		if (m_flFreezeTimer > 0.f)
		{
			m_flFreezeTimer -= flDt;
			break;
		}

		// the fruit rots away if pac leaves it be
		if (m_nFruitType >= 0)
		{
			m_flFruitTimer -= flDt;

			if (m_flFruitTimer <= 0.f)
				m_nFruitType = -1;
		}

		// fright timer
		if (m_flFrightTimer > 0.f)
		{
			m_flFrightTimer -= flDt;

			if (m_flFrightTimer <= 0.f)
			{
				m_flFrightTimer = 0.f;

				for (Ghost_t& g : m_ghosts)
					g.bFrightened = false;
			}
		}
		else
		{
			// scatter / chase alternation (fright pauses it, like the arcade)
			m_flModeTimer += flDt;
			const float flLimit = m_bScatter ? 7.f : 20.f;

			if (m_flModeTimer >= flLimit)
			{
				m_flModeTimer = 0.f;
				m_bScatter = !m_bScatter;

				for (Ghost_t& g : m_ghosts)
				{
					if (g.bActive && !g.bEaten)
						g.nDir = OppositeDir(g.nDir);
				}
			}
		}

		UpdatePac(flDt);
		CheckCollisions();

		// pac may have died mid-frame
		if (m_eState != PLAY)
			break;

		UpdateGhosts(flDt);
		CheckCollisions();
		break;
	}

	case DYING:
		// the wail cuts off the moment pac's body is gone and the final blips fire
		if (!m_bDeathBlipPlayed && m_flStateTime >= 1.2f)
		{
			m_bDeathBlipPlayed = true;
			PacManSound::Quiet();
			PlaySfx(PacManSound::SFX_DEATH_END);
		}

		if (m_flStateTime >= 1.4f)
		{
			m_bDeathBlipPlayed = false;
			m_nLives--;

			if (m_nLives > 0)
			{
				ResetActors();
				m_eState = READY;
				m_flStateTime = 0.f;
			}
			else
			{
				m_eState = GAME_OVER;
				m_flStateTime = 0.f;
			}
		}
		break;

	case LEVEL_CLEAR:
		if (m_flStateTime >= 2.f)
		{
			m_nLevel++;

			ResetPellets();
			ResetActors();

			m_eState = READY;
			m_flStateTime = 0.f;
		}
		break;

	case GAME_OVER:
		break;
	}
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void CPacMan::DrawMaze(int nMazeX, int nMazeY, float flT) const
{
	const int nT = static_cast<int>(flT);
	const bool bFlash = m_eState == LEVEL_CLEAR && fmodf(m_flStateTime, 0.5f) < 0.25f;
	const Color_t clrWall = bFlash ? Color_t{ 80, 80, 255, 255 } : Color_t{ 20, 24, 96, 255 };
	const Color_t clrEdge = bFlash ? Color_t{ 200, 220, 255, 255 } : Color_t{ 62, 85, 255, 255 };
	const Color_t clrPellet = { 255, 183, 151, 255 };
	const bool bPowerVisible = fmodf(float(Plat_FloatTime()), 0.4f) < 0.25f;

	for (int y = 0; y < MAZE_H; y++)
	{
		for (int x = 0; x < MAZE_W; x++)
		{
			const char c = s_pszMaze[y][x];
			const int nPx = nMazeX + x * nT;
			const int nPy = nMazeY + y * nT;

			if (c == '#')
			{
				H::Draw->Rect(nPx, nPy, nT, nT, clrWall);

				// bright edge on every side that faces an open tile
				if (Walkable(x, y - 1) || (y > 0 && s_pszMaze[y - 1][WrapTileX(x)] == '-'))
					H::Draw->Rect(nPx, nPy, nT, 1, clrEdge);
				if (Walkable(x, y + 1) || (y < MAZE_H - 1 && s_pszMaze[y + 1][WrapTileX(x)] == '-'))
					H::Draw->Rect(nPx, nPy + nT - 1, nT, 1, clrEdge);
				if (Walkable(x - 1, y) || s_pszMaze[y][WrapTileX(x - 1)] == '-')
					H::Draw->Rect(nPx, nPy, 1, nT, clrEdge);
				if (Walkable(x + 1, y) || s_pszMaze[y][WrapTileX(x + 1)] == '-')
					H::Draw->Rect(nPx + nT - 1, nPy, 1, nT, clrEdge);
			}
			else if (c == '-')
			{
				H::Draw->Rect(nPx, nPy + nT / 2 - 1, nT, 2, { 255, 150, 180, 255 });
			}
			else if (m_arrPellets[y][x] == 1)
			{
				const int nS = std::max(2, nT / 4);
				H::Draw->Rect(nPx + nT / 2 - nS / 2, nPy + nT / 2 - nS / 2, nS, nS, clrPellet);
			}
			else if (m_arrPellets[y][x] == 2 && bPowerVisible)
			{
				H::Draw->FilledCircle(nPx + nT / 2, nPy + nT / 2, std::max(2, static_cast<int>(flT * 0.35f)), 10, clrPellet);
			}
		}
	}
}

void CPacMan::DrawPac(int nPx, int nPy, float flAngle, float flMouthHalf, int nRadius) const
{
	if (nRadius <= 0)
		return;

	const Color_t clrBody = { 255, 219, 0, 255 };
	const Color_t clrBg = { 8, 10, 24, 255 };

	H::Draw->FilledCircle(nPx, nPy, nRadius, 14, clrBody);

	if (flMouthHalf > 0.02f)
	{
		// cut the mouth wedge out in the background color
		const float flR = nRadius * 1.4f;
		const Vec2 vC{ float(nPx), float(nPy) };
		const Vec2 vA(nPx + cosf(flAngle - flMouthHalf) * flR, nPy + sinf(flAngle - flMouthHalf) * flR);
		const Vec2 vB(nPx + cosf(flAngle + flMouthHalf) * flR, nPy + sinf(flAngle + flMouthHalf) * flR);

		const std::array<Vec2, 3> arrWedge = { vC, vA, vB };
		H::Draw->FilledTriangle(arrWedge, clrBg);
	}
}

void CPacMan::DrawGhostEyes(int nPx, int nPy, float flT, int nDir) const
{
	const int nEyeR = std::max(2, static_cast<int>(flT * 0.22f));
	const int nPupR = std::max(1, nEyeR / 2);
	const int nEyeY = nPy - static_cast<int>(flT * 0.18f);
	const int nSpread = std::max(1, static_cast<int>(flT * 0.3f));
	const int nOff = std::max(1, static_cast<int>(flT * 0.1f));
	const int nDX = (nDir == DIR_LEFT ? -1 : nDir == DIR_RIGHT ? 1 : 0) * nOff;
	const int nDY = (nDir == DIR_UP ? -1 : nDir == DIR_DOWN ? 1 : 0) * nOff;

	H::Draw->FilledCircle(nPx - nSpread, nEyeY, nEyeR, 10, { 255, 255, 255, 255 });
	H::Draw->FilledCircle(nPx + nSpread, nEyeY, nEyeR, 10, { 255, 255, 255, 255 });
	H::Draw->FilledCircle(nPx - nSpread + nDX, nEyeY + nDY, nPupR, 8, { 33, 33, 255, 255 });
	H::Draw->FilledCircle(nPx + nSpread + nDX, nEyeY + nDY, nPupR, 8, { 33, 33, 255, 255 });
}

// simple arcade fruit sprites, drawn from primitives at tile size flT
void CPacMan::DrawFruit(int nType, int nPx, int nPy, float flT) const
{
	const int nR = std::max(2, static_cast<int>(flT * 0.4f));

	switch (nType)
	{
	case 0:	// cherries
		H::Draw->Line(nPx - nR / 2, nPy, nPx + nR, nPy - nR, { 40, 130, 40, 255 });
		H::Draw->Line(nPx + nR / 2, nPy + nR / 3, nPx + nR, nPy - nR, { 40, 130, 40, 255 });
		H::Draw->FilledCircle(nPx - nR / 2, nPy + nR / 3, nR / 2 + 1, 10, { 230, 30, 30, 255 });
		H::Draw->FilledCircle(nPx + nR / 2, nPy + nR / 2, nR / 2, 10, { 230, 30, 30, 255 });
		break;

	case 1:	// strawberry
		H::Draw->FilledCircle(nPx, nPy + nR / 4, nR, 12, { 235, 60, 70, 255 });
		H::Draw->Rect(nPx - nR / 2, nPy - nR / 2, nR, std::max(2, nR / 2), { 40, 180, 70, 255 });
		H::Draw->Rect(nPx - 1, nPy, 2, 2, { 255, 255, 255, 255 });
		H::Draw->Rect(nPx - nR / 3 - 1, nPy + nR / 3, 2, 2, { 255, 255, 255, 255 });
		H::Draw->Rect(nPx + nR / 3, nPy + nR / 3, 2, 2, { 255, 255, 255, 255 });
		break;

	case 2:	// orange
		H::Draw->FilledCircle(nPx, nPy + nR / 4, nR, 12, { 255, 150, 40, 255 });
		H::Draw->Rect(nPx - 1, nPy - nR / 2, 2, std::max(2, nR / 3), { 60, 130, 50, 255 });
		H::Draw->Rect(nPx + 1, nPy - nR / 2, std::max(2, nR / 3), std::max(2, nR / 4), { 60, 130, 50, 255 });
		break;

	case 3:	// apple
		H::Draw->FilledCircle(nPx, nPy + nR / 4, nR, 12, { 200, 30, 30, 255 });
		H::Draw->Line(nPx, nPy - nR / 2, nPx + nR / 2, nPy - nR, { 90, 60, 30, 255 });
		break;

	case 4:	// melon
		H::Draw->FilledCircle(nPx, nPy, nR, 12, { 120, 200, 80, 255 });
		H::Draw->Line(nPx - nR / 2, nPy - nR / 2, nPx - nR / 2, nPy + nR / 2, { 60, 130, 50, 255 });
		H::Draw->Line(nPx, nPy - nR, nPx, nPy + nR, { 60, 130, 50, 255 });
		H::Draw->Line(nPx + nR / 2, nPy - nR / 2, nPx + nR / 2, nPy + nR / 2, { 60, 130, 50, 255 });
		break;

	case 5:	// galaxian flagship
	{
		const std::array<Vec2, 3> arrWing = { Vec2(float(nPx - nR), float(nPy + nR)), Vec2(float(nPx + nR), float(nPy + nR)), Vec2(float(nPx), float(nPy - nR)) };
		H::Draw->FilledTriangle(arrWing, { 255, 210, 40, 255 });

		const std::array<Vec2, 3> arrBody = { Vec2(float(nPx - nR / 2), float(nPy + nR / 2)), Vec2(float(nPx + nR / 2), float(nPy + nR / 2)), Vec2(float(nPx), float(nPy - nR / 2)) };
		H::Draw->FilledTriangle(arrBody, { 230, 40, 40, 255 });
		break;
	}

	case 6:	// bell
		H::Draw->FilledCircle(nPx, nPy - nR / 4, nR, 12, { 255, 210, 40, 255 });
		H::Draw->Rect(nPx - nR, nPy - nR / 4, nR * 2, nR / 2 + 1, { 255, 210, 40, 255 });
		H::Draw->Rect(nPx - nR, nPy + nR / 4, nR * 2, std::max(2, nR / 4), { 255, 255, 255, 255 });
		H::Draw->Rect(nPx - 1, nPy + nR / 4 + std::max(2, nR / 4), 2, std::max(2, nR / 3), { 255, 255, 255, 255 });
		break;

	default:	// key
	{
		const int nBowR = std::max(2, nR / 2);
		H::Draw->FilledCircle(nPx - nR / 2, nPy - nR / 4, nBowR, 10, { 190, 225, 255, 255 });
		H::Draw->Rect(nPx - nR / 2, nPy - nR / 4, nR * 2, std::max(2, nR / 3), { 190, 225, 255, 255 });
		H::Draw->Rect(nPx + nR, nPy, std::max(2, nR / 3), std::max(2, nR / 2), { 190, 225, 255, 255 });
		break;
	}
	}
}

void CPacMan::DrawGhost(const Ghost_t& g, int nPx, int nPy, float flT, bool bFlash) const
{
	// classic ghosts are just a pair of eyes while returning home
	if (g.bEaten)
	{
		DrawGhostEyes(nPx, nPy, flT, g.nDir);
		return;
	}

	const bool bFright = g.bFrightened;
	const bool bFrightFlash = bFright && bFlash && m_flFrightTimer < 2.f;

	Color_t body = g.clr;
	if (bFright)
		body = bFrightFlash ? Color_t{ 222, 222, 255, 255 } : Color_t{ 33, 33, 220, 255 };

	const Color_t clrFace = bFrightFlash ? Color_t{ 255, 60, 60, 255 } : Color_t{ 255, 222, 200, 255 };

	const int nR = std::max(2, static_cast<int>(flT * 0.48f));
	const int nTop = nPy - static_cast<int>(flT * 0.35f);
	const int nBottom = nTop + static_cast<int>(flT * 0.75f);

	H::Draw->FilledCircle(nPx, nTop, nR, 14, body);
	H::Draw->Rect(nPx - nR, nTop, nR * 2, nBottom - nTop, body);

	// scalloped skirt
	const int nSkirt = std::max(2, static_cast<int>(flT * 0.16f));
	for (int i = 0; i < 3; i++)
	{
		const float fx0 = float(nPx - nR + nR * 2 * i / 3);
		const float fx1 = float(nPx - nR + nR * 2 * (i + 1) / 3);
		const float fxm = (fx0 + fx1) * 0.5f;

		const std::array<Vec2, 3> arrSkirt = { Vec2(fx0, float(nBottom)), Vec2(fx1, float(nBottom)), Vec2(fxm, float(nBottom + nSkirt)) };
		H::Draw->FilledTriangle(arrSkirt, body);
	}

	if (bFright)
	{
		// scared face: blank dot eyes and a zigzag mouth
		const int nDot = std::max(1, static_cast<int>(flT * 0.14f));
		const int nEyeY = nTop - nDot / 2;
		const int nEyeOff = std::max(2, static_cast<int>(flT * 0.3f));

		H::Draw->Rect(nPx - nEyeOff - nDot / 2, nEyeY - nDot / 2, nDot, nDot, clrFace);
		H::Draw->Rect(nPx + nEyeOff - nDot / 2, nEyeY - nDot / 2, nDot, nDot, clrFace);

		// wavy zigzag mouth, like the arcade's frightened ghosts - wide, shallow, 8 segments
		const int nMouthY = nTop + nR / 2;
		const int nAmp = std::max(1, nR / 5);
		const int nSegW = std::max(1, nR / 4);

		int nMx = nPx - 4 * nSegW;
		int nMy = nMouthY - nAmp;

		for (int i = 1; i <= 8; i++)
		{
			const int nNx = nMx + nSegW;
			const int nNy = (i % 2) ? (nMouthY + nAmp) : (nMouthY - nAmp);

			H::Draw->Line(nMx, nMy, nNx, nNy, clrFace);
			nMx = nNx;
			nMy = nNy;
		}
	}
	else
	{
		DrawGhostEyes(nPx, nPy, flT, g.nDir);
	}
}

void CPacMan::Render(int nX, int nY, int nW, int nH)
{
	if (!m_bRunning || nW < 60 || nH < 60)
		return;

	// fit the canvas inside the given region, integer tile size for crisp pixels
	const float flT = std::clamp(floorf(std::min((nW - 8) / float(MAZE_W), (nH - 8) / float(MAZE_H + 4))), 4.f, 16.f);
	const int nT = static_cast<int>(flT);
	const int nHudH = nT * 2;
	const int nLifeH = nT + 8;

	m_flTile = flT;
	m_nCanvasW = MAZE_W * nT + 6;
	m_nCanvasH = nHudH + MAZE_H * nT + nLifeH + 6;
	m_nCanvasX = nX + (nW - m_nCanvasW) / 2;
	m_nCanvasY = nY + (nH - m_nCanvasH) / 2;

	const int nMazeX = m_nCanvasX + 3;
	const int nMazeY = m_nCanvasY + nHudH;
	const int nMazeW = MAZE_W * nT;
	const int nMazeH = MAZE_H * nT;

	// delta time (clamped so pauses don't fast-forward the game)
	const float flNow = Plat_FloatTime();
	const float flDt = m_flLastFrame > 0.f ? std::min(flNow - m_flLastFrame, 0.05f) : 0.f;
	m_flLastFrame = flNow;

	// arrow keys or WASD queue a direction
	if (H::Input->IsDown(VK_UP) || H::Input->IsDown('W'))
		m_pac.nQueued = DIR_UP;
	else if (H::Input->IsDown(VK_DOWN) || H::Input->IsDown('S'))
		m_pac.nQueued = DIR_DOWN;
	else if (H::Input->IsDown(VK_LEFT) || H::Input->IsDown('A'))
		m_pac.nQueued = DIR_LEFT;
	else if (H::Input->IsDown(VK_RIGHT) || H::Input->IsDown('D'))
		m_pac.nQueued = DIR_RIGHT;

	if (flDt > 0.f)
		UpdateGame(flDt);

	const bool bClick = H::Input->IsPressed(VK_LBUTTON);
	const int nMx = H::Input->GetMouseX();
	const int nMy = H::Input->GetMouseY();

	// click the canvas on the game over screen to play again
	if (m_eState == GAME_OVER && bClick
		&& nMx >= m_nCanvasX && nMx <= m_nCanvasX + m_nCanvasW
		&& nMy >= m_nCanvasY && nMy <= m_nCanvasY + m_nCanvasH)
	{
		StartGame();
	}

	// sound toggle in the bottom right corner
	const int nSndBoxX = m_nCanvasX + m_nCanvasW - 58;
	const int nSndBoxY = m_nCanvasY + m_nCanvasH - nT / 2 - 11;

	if (bClick
		&& nMx >= nSndBoxX && nMx <= nSndBoxX + 54
		&& nMy >= nSndBoxY && nMy <= nSndBoxY + 12)
	{
		CFG::PacMan_Sound = !CFG::PacMan_Sound;

		if (!CFG::PacMan_Sound)
			PacManSound::Quiet();
	}

	// siren follows the game state (paused during the score freeze, like the arcade).
	// the eyes returning home have their own track, then fright, then the normal siren,
	// whose pitch stage rises as the maze is cleared
	int nSirenMode = 0;

	if (CFG::PacMan_Sound && m_eState == PLAY && m_flFreezeTimer <= 0.f)
	{
		bool bEyes = false;

		for (const Ghost_t& g : m_ghosts)
			bEyes = bEyes || g.bEaten;

		nSirenMode = bEyes ? 3 : (m_flFrightTimer > 0.f ? 2 : 1);
	}

	PacManSound::SetSiren(nSirenMode, m_nFruitEaten / 49);

	// draw (clipped to the region so nothing can escape the menu window)
	H::Draw->StartClipping(nX, nY, nW, nH);

	const Color_t clrBg = { 8, 10, 24, 255 };
	H::Draw->Rect(m_nCanvasX, m_nCanvasY, m_nCanvasW, m_nCanvasH, clrBg);
	H::Draw->OutlinedRect(m_nCanvasX, m_nCanvasY, m_nCanvasW, m_nCanvasH, { 62, 85, 255, 255 });

	// HUD
	H::Draw->String(H::Fonts->Get(EFonts::Menu), nMazeX, m_nCanvasY + 4, { 255, 219, 0, 255 }, POS_DEFAULT, "SCORE %d", m_nScore);
	H::Draw->String(H::Fonts->Get(EFonts::Menu), m_nCanvasX + m_nCanvasW - 100, m_nCanvasY + 4, { 255, 255, 255, 255 }, POS_DEFAULT, "HIGH %d", CFG::PacMan_HighScore);

	DrawMaze(nMazeX, nMazeY, flT);

	// ghosts (hidden while pac is dying, like the arcade)
	if (m_eState != DYING && m_eState != GAME_OVER)
	{
		const bool bFlash = fmodf(float(Plat_FloatTime()), 0.3f) < 0.15f;

		for (const Ghost_t& g : m_ghosts)
			DrawGhost(g, nMazeX + static_cast<int>(g.x * flT), nMazeY + static_cast<int>(g.y * flT), flT, bFlash);
	}

	// bonus fruit sitting below the ghost house
	if (m_nFruitType >= 0 && m_eState != DYING)
		DrawFruit(m_nFruitType, nMazeX + static_cast<int>(m_flFruitX * flT), nMazeY + static_cast<int>(m_flFruitY * flT), flT);

	// pac (hidden while the ghost score is showing, like the arcade)
	{
		const int nPx = nMazeX + static_cast<int>(m_pac.x * flT);
		const int nPy = nMazeY + static_cast<int>(m_pac.y * flT);

		if (m_eState == DYING)
		{
			const float flProg = std::clamp(m_flStateTime / 1.2f, 0.f, 1.f);
			DrawPac(nPx, nPy, -kPi / 2.f, 0.4f + 1.05f * flProg, static_cast<int>(flT * 0.45f * (1.f - flProg)));
		}
		else if (m_eState == GAME_OVER || (m_eState == PLAY && m_flFreezeTimer > 0.f))
		{
			// pac is gone
		}
		else
		{
			const float flMouth = m_eState == PLAY ? (0.08f + 0.42f * fabsf(sinf(m_flPacAnim))) * (kPi / 2.f) : 0.12f * (kPi / 2.f);
			DrawPac(nPx, nPy, m_flPacAngle, flMouth, static_cast<int>(flT * 0.45f));
		}
	}

	// the score popup where the ghost was eaten
	if (m_eState == PLAY && m_flFreezeTimer > 0.f)
		H::Draw->String(H::Fonts->Get(EFonts::Menu), nMazeX + static_cast<int>(m_flFreezeX * flT), nMazeY + static_cast<int>(m_flFreezeY * flT), { 0, 255, 255, 255 }, POS_CENTERXY, "%d", m_nFreezeScore);

	// the score popup where the fruit was eaten
	if (m_eState == PLAY && m_flFruitPopupTimer > 0.f)
		H::Draw->String(H::Fonts->Get(EFonts::Menu), nMazeX + static_cast<int>(m_flFruitPopupX * flT), nMazeY + static_cast<int>(m_flFruitPopupY * flT), { 255, 184, 255, 255 }, POS_CENTERXY, "%d", m_nFruitPopupScore);

	// state text over the maze
	const char* szBig = nullptr;
	Color_t clrBig = { 255, 219, 0, 255 };

	if (m_eState == READY)
		szBig = "READY!";
	else if (m_eState == GAME_OVER)
	{
		szBig = "GAME OVER";
		clrBig = { 255, 70, 70, 255 };
	}
	else if (m_eState == LEVEL_CLEAR && fmodf(m_flStateTime, 0.4f) < 0.2f)
		szBig = "LEVEL UP!";

	if (szBig)
		H::Draw->String(H::Fonts->Get(EFonts::Menu), nMazeX + nMazeW / 2, nMazeY + nMazeH / 2 + nT * 3, clrBig, POS_CENTERXY, szBig);

	if (m_eState == GAME_OVER)
		H::Draw->String(H::Fonts->Get(EFonts::Menu), nMazeX + nMazeW / 2, nMazeY + nMazeH / 2 + nT * 3 + 18, { 255, 255, 255, 255 }, POS_CENTERXY, "Click to play again");

	// remaining lives, bottom left
	for (int i = 0; i < m_nLives - 1; i++)
		DrawPac(nMazeX + 8 + i * (nT + 6), m_nCanvasY + m_nCanvasH - nT / 2 - 5, kPi, 0.35f, nT / 2 - 1);

	// level, bottom center
	H::Draw->String(H::Fonts->Get(EFonts::Menu), nMazeX + nMazeW / 2, m_nCanvasY + m_nCanvasH - nT / 2 - 12, { 255, 255, 255, 255 }, POS_CENTERXY, "LV %d", m_nLevel);

	// sound toggle
	const bool bSound = CFG::PacMan_Sound;
	H::Draw->OutlinedRect(nSndBoxX, nSndBoxY, 10, 10, { 255, 255, 255, 255 });

	if (bSound)
		H::Draw->Rect(nSndBoxX + 2, nSndBoxY + 2, 6, 6, { 255, 219, 0, 255 });

	H::Draw->String(H::Fonts->Get(EFonts::Menu), nSndBoxX + 14, nSndBoxY - 1, bSound ? Color_t{ 255, 255, 255, 255 } : Color_t{ 120, 120, 120, 255 }, POS_DEFAULT, "Sound");

	H::Draw->EndClipping();
}
