#pragma once

#include "../AmalgamCompat.h"
#include "../../../../SDK/TF2/collisionbspdata.h"
#include "../../../../SDK/TF2/dispcolltree.h"
#include "../../../../SDK/TF2/icollideable.h"

#include <functional>
#include <unordered_map>
#include <vector>
#include <string>

// ============================================
// Amalgam's F::World feature - map geometry face cache
// ============================================
// Walks the engine's collision BSP (box + plane brushes), displacement trees
// and spatial partition props/entities and exposes their polygons as Face_t's.
// The splash aimbot's Face mode samples these polygons for splash points that
// the trace-sphere sampler can never produce (splash surfaces around cover).
// Ported from Amalgam's Features/World/World.cpp; props are cached lazily per
// vcollide instead of pre-walking MDLCache, everything else is 1:1.

namespace FaceTypeEnum
{
	enum
	{
		None = 0,
		BoxBrush = 1 << 0,
		PlaneBrush = 1 << 1,
		Displacement = 1 << 2,
		Prop = 1 << 3,
		Entity = 1 << 4,
		All = BoxBrush | PlaneBrush | Displacement | Prop | Entity,
		Cache = BoxBrush | PlaneBrush | Prop | Entity
	};
}

using NormalValidCallback = const std::function<bool(const std::vector<Vec3>&, const Vec3&)>;

struct Plane_t
{
	Vec3 m_vNormal;
	float m_flDist;
};

struct Face_t
{
	std::vector<Vec3> m_vVertices;
	Vec3 m_vNormal;
	int8_t m_iType;
};

MAKE_INTERFACE_VERSION(ISpatialPartition, SpatialPartition, "engine.dll", "SpatialPartition001");

class CAmalgamWorld
{
private:
	bool FaceValid(const std::vector<Vec3>& vVertices, const Vec3* pNormal, const Vec3* pMins = nullptr, const Vec3* pMaxs = nullptr);

	// only the normal callback is settable - the splash setup filters faces by
	// whether their surface can receive a splash from the shot direction
	NormalValidCallback* m_pNormalValidCallback = nullptr;

	int m_iNeedsCache = FaceTypeEnum::Cache;
	std::unordered_map<cbrush_t*, std::vector<Face_t>> m_mBrushCache = {};
	std::unordered_map<vcollide_t*, std::vector<Face_t>> m_mCollideCache = {};
	std::string m_sMapName;

public:
	void Cache();
	void CacheBoxBrushes();
	void CachePlaneBrushes();
	void CacheEntities();
	void Uncache();

	std::vector<Face_t> GetFacesInAABB(const Vec3& vMins, const Vec3& vMaxs, int iMask = MASK_SOLID, ITraceFilter* pFilter = nullptr, int iFlags = FaceTypeEnum::All);

	void SetNormalValidCallback(NormalValidCallback* pCallback = nullptr)
	{
		m_pNormalValidCallback = pCallback;
	}
};

MAKE_SINGLETON_SCOPED(CAmalgamWorld, AmalgamWorld, F);
