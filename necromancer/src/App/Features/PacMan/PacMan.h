#pragma once

#include "../../../SDK/SDK.h"

class CPacMan
{
public:
	// Tick + draw the mini game into the given menu region (clipped to it)
	void Render(int nX, int nY, int nW, int nH);

	// The menu calls this every frame with whether the Pac-Man page is showing,
	// so the game can silence itself when the tab or the menu goes away
	void NotifyVisible(bool bVisible);

	void StartGame();
	void StopGame();

	bool IsRunning() const { return m_bRunning; }

	static constexpr int MAZE_W = 28;
	static constexpr int MAZE_H = 31;

private:
	enum EGameState { READY, PLAY, DYING, LEVEL_CLEAR, GAME_OVER };
	enum EDir { DIR_NONE = -1, DIR_UP = 0, DIR_LEFT, DIR_DOWN, DIR_RIGHT };

	struct Ghost_t
	{
		float x{}, y{};			// position in tile units (tile centers are X.5)
		int nDir{ DIR_LEFT };
		int nPersonality{};		// 0 blinky, 1 pinky, 2 inky, 3 clyde
		bool bActive{};			// has left the house
		float flRelease{};		// seconds until it leaves the house
		bool bFrightened{};
		bool bEaten{};			// reduced to eyes, traveling back into the ghost house
		bool bLeaving{};		// revived inside the house, rising back out through the door
		Color_t clr{};
	};

	void UpdateGame(float flDt);
	void UpdatePac(float flDt);
	void UpdateGhosts(float flDt);
	void CheckCollisions();
	void EatPelletAt(int nTileX, int nTileY);
	void TriggerFright();
	void ResetPellets();
	void ResetActors();
	void AddScore(int nPoints);
	void SpawnFruit();

	// classic arcade speeds, in tiles per second
	float PacSpeed() const;
	float GhostSpeed(const Ghost_t& g) const;

	bool Walkable(int nTileX, int nTileY) const;
	void StepEntity(float& flX, float& flY, int& nDir, float flDist, bool bPac, Ghost_t* pGhost = nullptr);
	void DecidePac(float& flX, float& flY, int& nDir);
	void DecideGhost(Ghost_t& g, float& flX, float& flY, int& nDir);
	void GhostTargetTile(const Ghost_t& g, int& nTileX, int& nTileY) const;
	void PlaySfx(int nSfx);

	void DrawMaze(int nMazeX, int nMazeY, float flT) const;
	void DrawPac(int nPx, int nPy, float flAngle, float flMouthHalf, int nRadius) const;
	void DrawGhost(const Ghost_t& g, int nPx, int nPy, float flT, bool bFlash) const;
	void DrawGhostEyes(int nPx, int nPy, float flT, int nDir) const;
	void DrawFruit(int nType, int nPx, int nPy, float flT) const;

	bool m_bRunning = false;
	bool m_bPageVisible = false;
	EGameState m_eState = READY;
	float m_flStateTime = 0.f;	// time spent in the current state
	float m_flReadyTime = 2.f;	// how long the READY pause lasts (intro theme on a fresh game)
	bool m_bDeathBlipPlayed = false;
	int m_nNextExtraLife = 10000;	// next score threshold that grants a bonus life (then every 10,000)
	float m_flLastFrame = 0.f;
	bool m_bWakaToggle = false;

	int m_nScore = 0;
	int m_nLives = 3;
	int m_nLevel = 1;
	int m_nPelletsLeft = 0;
	int m_nGhostChain = 0;
	float m_flFrightTimer = 0.f;
	bool m_bScatter = true;
	float m_flModeTimer = 0.f;

	// bonus fruit - spawns twice a level after 70 and 170 pellets, like the arcade
	int m_nFruitEaten = 0;
	int m_nFruitType = -1;			// -1 = no fruit on the maze
	int m_nFruitScore = 0;
	float m_flFruitTimer = 0.f;
	float m_flFruitX = 0.f, m_flFruitY = 0.f;
	float m_flFruitPopupTimer = 0.f;
	int m_nFruitPopupScore = 0;
	float m_flFruitPopupX = 0.f, m_flFruitPopupY = 0.f;

	// classic score-display freeze after eating a ghost
	float m_flFreezeTimer = 0.f;
	int m_nFreezeScore = 0;
	float m_flFreezeX = 0.f, m_flFreezeY = 0.f;

	// 0 = empty, 1 = pellet, 2 = power pellet
	byte m_arrPellets[MAZE_H][MAZE_W] = {};

	float m_flPacAngle = 0.f;
	float m_flPacAnim = 0.f;
	struct { float x{}, y{}; int nDir{ DIR_NONE }; int nQueued{ DIR_NONE }; } m_pac;

	Ghost_t m_ghosts[4];

	// layout cache so click handling matches what was drawn
	int m_nCanvasX = 0, m_nCanvasY = 0, m_nCanvasW = 0, m_nCanvasH = 0;
	float m_flTile = 8.f;
};

MAKE_SINGLETON_SCOPED(CPacMan, PacMan, F);
