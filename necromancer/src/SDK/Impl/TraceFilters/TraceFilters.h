#pragma once
#include "../../TF2/IEngineTrace.h"
#include <vector>

// Amalgam-style enums for CTraceFilterCollideable
enum
{
	PLAYER_DEFAULT,
	PLAYER_NONE,
	PLAYER_ALL
};

enum
{
	OBJECT_DEFAULT,
	OBJECT_NONE,
	OBJECT_ALL
};

// Type enum for special trace behavior
enum
{
	SKIP_CHECK,   // Skip weapon-specific checks, just do basic collision
	FORCE_PASS,   // Force the trace to pass through
	FORCE_HIT     // Force the trace to hit
};

class CTraceFilterHitscan : public CTraceFilter
{
public:
	bool ShouldHitEntity(IHandleEntity *pServerEntity, int contentsMask) override;

	TraceType_t GetTraceType() const override
	{
		return TRACE_EVERYTHING;
	}

	C_BaseEntity *m_pIgnore = nullptr;
};

class CTraceFilterWorldCustom : public CTraceFilter
{
public:
	bool ShouldHitEntity(IHandleEntity *pServerEntity, int contentsMask) override;

	TraceType_t GetTraceType() const override
	{
		return TRACE_EVERYTHING;
	}

	C_BaseEntity *m_pTarget = nullptr;
	C_BaseEntity *m_pSkip = nullptr;  // Entity to skip (usually local player)
};

class CTraceFilterArc : public CTraceFilter
{
public:
	bool ShouldHitEntity(IHandleEntity* pServerEntity, int contentsMask) override;

	TraceType_t GetTraceType() const override
	{
		return TRACE_EVERYTHING;
	}

	C_BaseEntity* m_pSkip = nullptr;  // Entity to skip (usually local player)
};

// Amalgam-style CTraceFilterCollideable for projectile aimbot
class CTraceFilterCollideable : public CTraceFilter
{
public:
	bool ShouldHitEntity(IHandleEntity* pServerEntity, int contentsMask) override;

	TraceType_t GetTraceType() const override
	{
		return TRACE_EVERYTHING;
	}

	C_BaseEntity* pSkip = nullptr;
	int iTeam = -1;
	int iType = FORCE_HIT;         // SKIP_CHECK, FORCE_PASS, FORCE_HIT - controls special behavior
	int iPlayer = PLAYER_DEFAULT;  // PLAYER_DEFAULT, PLAYER_NONE, PLAYER_ALL
	int iObject = OBJECT_ALL;      // OBJECT_DEFAULT, OBJECT_NONE, OBJECT_ALL
	bool bMisc = false;            // Whether to hit misc entities like stickies
};

// Amalgam-style trace filter for projectile aimbot
// Only hits world geometry and props, not players/buildings
// Named differently to avoid conflict with CTraceFilterWorldAndPropsOnly in IEngineTrace.h
class CTraceFilterWorldAndPropsOnlyAmalgam : public CTraceFilter
{
public:
	bool ShouldHitEntity(IHandleEntity* pServerEntity, int contentsMask) override;

	TraceType_t GetTraceType() const override
	{
		return TRACE_EVERYTHING_FILTER_PROPS;
	}

	C_BaseEntity* pSkip = nullptr;
	int iTeam = -1;
};