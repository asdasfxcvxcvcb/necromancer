#pragma once

#include "../../../SDK/SDK.h"

enum
{
	KILLA_NONE = -1,
	KILLA_SCATTER, KILLA_PISTOL, KILLA_BAT,
	KILLA_ROCKET, KILLA_SHOVEL,
	KILLA_BURN, KILLA_FLARE, KILLA_AXE,
	KILLA_STICKYARC, KILLA_BOTTLE,
	KILLA_MINIGUN, KILLA_GLOVES, KILLA_SHOTGUN,
	KILLA_WRENCH, KILLA_SENTRY,
	KILLA_MEDIC,
	KILLA_SKULL, KILLA_SKULLBOW, KILLA_SMG, KILLA_KUKRI,
	KILLA_REVOLVER, KILLA_KNIFE
};

class CStatusWindow
{
	void Drag();
	void StartSpree(int iAnim, const char* pszText, bool bFull, float flNow);
	void DrawSpree(int nX, int nY, int nW, int nBarH, int nRows, float flNow);
	void DrawKillAnim(int iAnim, int ax, int ay, int aw, int ah, float t);
	void DrawScrambledText(const CFont& font, int nX, int nY, Color_t tColor, const char* pszText, int nAlign, float flPhase);
	void DrawRainbowText(const CFont& font, int nX, int nY, const char* pszText, int nAlign, float flNow);

	float m_flSpreeStart = 0.f;
	float m_flSpreeEnd = 0.f;
	char m_szAnnounce[64] = {};
	int m_iKillAnim = KILLA_NONE;
	bool m_bSpreeFull = false;
	bool m_bSpreeHeadshot = false;
	bool m_bSpreeTeamRed = true;

	float m_flKillTimes[8] = {};
	int m_iKillIdx = 0;
	int m_iKillChain = 0;
	float m_flLastChainKill = 0.f;

	int m_iHealChain = 0;
	float m_flLastHealKill = 0.f;

	float m_flChaosEnd = 0.f;

public:
	void Run();
	void OnLocalKill(const char* pszWeapon, int iCustomKill, int nVictim, bool bCritKill);
	void OnHealTargetKill(int nAttackerIndex);
};

MAKE_SINGLETON_SCOPED(CStatusWindow, StatusWindow, F);
