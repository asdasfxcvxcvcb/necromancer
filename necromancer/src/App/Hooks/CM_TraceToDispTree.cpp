#include "../../SDK/SDK.h"

#include <cstdint>
#include <optional>

// Amalgam's displacement tracing. The engine rejects displacement triangle
// hits one-sidedly, so a fast swept projectile (pipes/stickies move up to
// 36 units per tick) can tunnel through steep or thin displacement terrain
// within a single tick. These hooks make disp triangles two-sided for traces
// whose mask carries our custom CONTENTS_DISPSOLID bit - ProjSim::SetupTrace
// sets it for drag projectiles so the aimbot's pipe/sticky validation matches
// what the real server's collision does.

MAKE_SIGNATURE(CM_TraceToDispTree_True, "engine.dll", "48 8B C4 48 89 58 ? 48 89 70 ? 57 48 81 EC ? ? ? ? F3 0F 10 01", 0x0);
MAKE_SIGNATURE(CM_TraceToDispTree_False, "engine.dll", "48 8B C4 48 89 58 ? 48 89 70 ? 57 48 83 EC ? F3 0F 10 01", 0x0);
MAKE_SIGNATURE(IntersectRayWithTriangle, "engine.dll", "48 8B C4 48 81 EC ? ? ? ? 80 BC 24", 0x0);
MAKE_SIGNATURE(CDispCollTree_SweepAABBTriIntersect, "engine.dll", "48 8B C4 48 89 70 ? 48 89 78 ? 55 41 56 41 57 48 8D 68", 0x0);
MAKE_SIGNATURE(CDispCollTree_EdgeCrossAxis_0, "engine.dll", "48 83 EC ? B8 ? ? ? ? 66 44 3B C0 75 ? B0 ? 48 83 C4 ? C3 48 8B 89 ? ? ? ? F3 0F 10 05 ? ? ? ? 0F 29 74 24 ? 0F 29 7C 24 ? 44 0F 29 04 24 41 0F B7 C0 66 45 85 C0 79 ? 25 ? ? ? ? 48 8D 04 40 F3 44 0F 10 04 81", 0x0);

#define DISPCOLL_DIST_EPSILON 0.03125f

// engine-internal structures (layouts as reverse engineered by Amalgam)
#define MAX_CHECK_COUNT_DEPTH 2
typedef uint32_t TraceCounter_t;

struct TraceInfo_t
{
	Vec3 m_start;
	Vec3 m_end;
	Vec3 m_mins;
	Vec3 m_maxs;
	Vec3 m_extents;
	Vec3 m_delta;
	Vec3 m_invDelta;
	CGameTrace m_trace;
	CGameTrace m_stabTrace;
	int m_contents;
	bool m_ispoint;
	bool m_isswept;
	void* m_pBSPData;
	Vec3 m_DispStabDir;
	int m_bDispHit;
	bool m_bCheckPrimary;
	int m_nCheckDepth;
	TraceCounter_t m_Count[MAX_CHECK_COUNT_DEPTH];
	CUtlVector<TraceCounter_t> m_BrushCounters[MAX_CHECK_COUNT_DEPTH];
	CUtlVector<TraceCounter_t> m_DispCounters[MAX_CHECK_COUNT_DEPTH];
};

class CDispCollTri
{
public:
	struct index_t
	{
		union
		{
			struct
			{
				unsigned short uiVert : 9;
				unsigned short uiMin : 2;
				unsigned short uiMax : 2;
			} m_Index;
			unsigned short m_IndexDummy;
		};
	};

	index_t m_TriData[3];
	unsigned short m_ucSignBits : 3;
	unsigned short m_ucPlaneType : 3;
	unsigned short m_uiFlags : 5;
	Vec3 m_vecNormal;
	float m_flDist;
};

static int s_iMask = 0;
static std::optional<Vec3> s_vOriginal = {};
static CDispCollTri* s_pTri = nullptr;

// the two TraceToDispTree variants only differ in their prologue stack
// reservation; both carry the trace mask in TraceInfo_t
MAKE_HOOK(CM_TraceToDispTree_True, Signatures::CM_TraceToDispTree_True.Get(), float, __fastcall,
	TraceInfo_t* pTraceInfo, void* pDispTree, float startFrac, float endFrac)
{
	s_iMask = pTraceInfo->m_contents;

	return CALL_ORIGINAL(pTraceInfo, pDispTree, startFrac, endFrac);
}

MAKE_HOOK(CM_TraceToDispTree_False, Signatures::CM_TraceToDispTree_False.Get(), float, __fastcall,
	TraceInfo_t* pTraceInfo, void* pDispTree, float startFrac, float endFrac)
{
	s_iMask = pTraceInfo->m_contents;

	return CALL_ORIGINAL(pTraceInfo, pDispTree, startFrac, endFrac);
}

MAKE_HOOK(IntersectRayWithTriangle, Signatures::IntersectRayWithTriangle.Get(), float, __fastcall,
	const Ray_t& ray, const Vec3& v1, const Vec3& v2, const Vec3& v3, bool oneSided)
{
	if (s_iMask & CONTENTS_DISPSOLID)
		oneSided = false;

	return CALL_ORIGINAL(ray, v1, v2, v3, oneSided);
}

MAKE_HOOK(CDispCollTree_SweepAABBTriIntersect, Signatures::CDispCollTree_SweepAABBTriIntersect.Get(), void, __fastcall,
	void* pDispTree, const Ray_t& ray, const Vec3& rayDir, int iTri, CDispCollTri* pTri, void* pTrace)
{
	// a triangle facing the same way the ray travels is a backface for the
	// sweep - flip it so the hit registers, and restore it once done
	if (s_iMask & CONTENTS_DISPSOLID && pTri->m_vecNormal.Dot(ray.m_Delta) > DISPCOLL_DIST_EPSILON)
	{
		s_vOriginal = pTri->m_vecNormal, s_pTri = pTri;
		pTri->m_vecNormal = -pTri->m_vecNormal;
	}

	CALL_ORIGINAL(pDispTree, ray, rayDir, iTri, pTri, pTrace);

	if (s_vOriginal)
		pTri->m_vecNormal = *s_vOriginal, s_vOriginal = std::nullopt;
}

// the sweep test can bail into the edge-cross-axis path before the restore
// above runs - put the normal back there instead
MAKE_HOOK(CDispCollTree_EdgeCrossAxis_0, Signatures::CDispCollTree_EdgeCrossAxis_0.Get(), void, __fastcall,
	void* pDispTree, const Ray_t& ray, unsigned short iPlane, void* pHelper)
{
	if (s_vOriginal)
		s_pTri->m_vecNormal = *s_vOriginal, s_vOriginal = std::nullopt;

	CALL_ORIGINAL(pDispTree, ray, iPlane, pHelper);
}
