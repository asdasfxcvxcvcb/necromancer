#include "World.h"

#include <algorithm>
#include <numeric>

class CEntityEnumerator : public IPartitionEnumerator
{
public:
	IterationRetval_t EnumElement(IHandleEntity* pHandleEntity) override
	{
		m_vEntities.push_back(pHandleEntity);
		return ITERATION_CONTINUE;
	}

	std::vector<IHandleEntity*> m_vEntities = {};
};

static CPhysConvex* s_aConvexes[1024];

static void GetFacesForConvex(CPhysConvex* pConvex, std::vector<Face_t>& vFaces, float flResize = 0.f, int8_t iType = FaceTypeEnum::None)
{
	CPolyhedron* pPolyhedron = I::PhysicsCollision->PolyhedronFromConvex(pConvex, true);
	if (!pPolyhedron)
		return;

	if (!flResize)
	{
		std::vector<std::pair<std::vector<int>, Vec3>> vTemp = {};
		for (int iPolygon = 0; iPolygon < pPolyhedron->iPolygonCount; iPolygon++)
		{
			Polyhedron_IndexedPolygon_t* pPolygon = &pPolyhedron->pPolygons[iPolygon];

			std::vector<int>* pVertices = nullptr;
			for (auto& [vIndices, vNormal] : vTemp)
			{
				if (pPolygon->polyNormal.Dot(vNormal) > 1 - CALC_EPSILON)
				{
					pVertices = &vIndices;
					break;
				}
			}
			if (!pVertices)
			{
				vTemp.emplace_back(std::vector<int>(), pPolygon->polyNormal);
				pVertices = &vTemp.back().first;
			}

			for (int iLineRef = 0; iLineRef < pPolygon->iIndexCount; iLineRef++)
			{
				Polyhedron_IndexedLineReference_t* pLineReference = &pPolyhedron->pIndices[pPolygon->iFirstIndex + iLineRef];
				Polyhedron_IndexedLine_t* pLine = &pPolyhedron->pLines[pLineReference->iLineIndex];
				int iVertex = pLine->iPointIndices[pLineReference->iEndPointIndex];
				if (std::find(pVertices->begin(), pVertices->end(), iVertex) == pVertices->end())
					pVertices->push_back(iVertex);
			}
		}

		for (auto& [vIndices, vNormal] : vTemp)
		{
			if (vIndices.size() < 3)
				continue;

			std::vector<Vec3> vVertices = {};
			for (auto& iVertex : vIndices)
				vVertices.push_back(pPolyhedron->pVertices[iVertex]);

			if (vVertices.size() > 3)
			{	// wind the polygon around its center so the triangle fan covers it
				Vec3 vCenter = {};
				for (auto& vVertex : vVertices)
					vCenter += vVertex;
				vCenter /= float(vVertices.size());

				Vec3 vDir1 = (vVertices.front() - vCenter).Normalized();
				Vec3 vDir2 = vDir1.Cross(vNormal);
				std::sort(vVertices.begin(), vVertices.end(), [&](const Vec3& a, const Vec3& b) -> bool
				{
					return atan2f(vDir2.Dot(a - vCenter), vDir1.Dot(a - vCenter)) < atan2f(vDir2.Dot(b - vCenter), vDir1.Dot(b - vCenter));
				});
			}

			vFaces.emplace_back(vVertices, vNormal, iType);
		}
	}
	else
	{	// rough approximation, not too sure why this is needed in the first place
		std::vector<Plane_t> vPlanes = {}; vPlanes.reserve(pPolyhedron->iPolygonCount);
		for (int iPolygon = 0; iPolygon < pPolyhedron->iPolygonCount; iPolygon++)
		{
			Polyhedron_IndexedPolygon_t* pPolygon = &pPolyhedron->pPolygons[iPolygon];

			if (pPolygon->iIndexCount)
			{
				Polyhedron_IndexedLineReference_t* pLineReference = &pPolyhedron->pIndices[pPolygon->iFirstIndex];
				Polyhedron_IndexedLine_t* pLine = &pPolyhedron->pLines[pLineReference->iLineIndex];
				Vec3& vVertex = pPolyhedron->pVertices[pLine->iPointIndices[pLineReference->iEndPointIndex]];
				vPlanes.emplace_back(pPolygon->polyNormal, pPolygon->polyNormal.Dot(vVertex + pPolygon->polyNormal * flResize));
			}
		}

		if (CPhysConvex* pConvex2 = I::PhysicsCollision->ConvexFromPlanes(reinterpret_cast<float*>(vPlanes.data()), int(vPlanes.size()), 0.f))
		{
			GetFacesForConvex(pConvex2, vFaces, 0.f, iType);
			I::PhysicsCollision->ConvexFree(pConvex2);
		}
	}

	pPolyhedron->Release();
}

static void GetFacesForCollision(CPhysCollide* pCollision, std::vector<Face_t>& vFaces, float flResize = 0.f, int8_t iType = FaceTypeEnum::None)
{
	int iConvexes = I::PhysicsCollision->GetConvexesUsedInCollideable(pCollision, s_aConvexes, sizeof(s_aConvexes) / sizeof(CPhysConvex*));
	for (int iConvex = 0; iConvex < iConvexes; iConvex++)
		GetFacesForConvex(s_aConvexes[iConvex], vFaces, flResize, iType);
}

bool CAmalgamWorld::FaceValid(const std::vector<Vec3>& vVertices, const Vec3* pNormal, const Vec3* pMins, const Vec3* pMaxs)
{
	if (vVertices.size() < 3)
		return false;

	if (pMins && pMaxs)
	{
		Vec3 vVerticeMins = { FLT_MAX, FLT_MAX, FLT_MAX }, vVerticeMaxs = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
		for (auto& vVertex : vVertices)
		{
			vVerticeMins.x = std::min(vVerticeMins.x, vVertex.x);
			vVerticeMins.y = std::min(vVerticeMins.y, vVertex.y);
			vVerticeMins.z = std::min(vVerticeMins.z, vVertex.z);
			vVerticeMaxs.x = std::max(vVerticeMaxs.x, vVertex.x);
			vVerticeMaxs.y = std::max(vVerticeMaxs.y, vVertex.y);
			vVerticeMaxs.z = std::max(vVerticeMaxs.z, vVertex.z);
		}
		if (!Math::IsBoxIntersectingBox(vVerticeMins, vVerticeMaxs, *pMins, *pMaxs))
			return false;
	}

	if (pNormal && m_pNormalValidCallback && !(*m_pNormalValidCallback)(vVertices, *pNormal))
		return false;

	return true;
}

void CAmalgamWorld::Cache()
{
	if (!m_iNeedsCache)
		return;

	CacheBoxBrushes();
	CachePlaneBrushes();
	CacheEntities();
}

void CAmalgamWorld::CacheBoxBrushes()
{
	if (!(m_iNeedsCache & FaceTypeEnum::BoxBrush))
		return;

	m_iNeedsCache &= ~FaceTypeEnum::BoxBrush;

	for (int iBrush = 0; iBrush < I::BSPData->numbrushes; iBrush++)
	{
		cbrush_t* pBrush = &I::BSPData->map_brushes[iBrush];
		if (!pBrush->IsBox())
			continue;

		cboxbrush_t* pBoxBrush = &I::BSPData->map_boxbrushes[pBrush->GetBox()];

		Vec3 aVertices[8];
		for (int i = 0; i != 8; i++)
		{
			aVertices[i].x = i & (1 << 0) ? pBoxBrush->maxs.x : pBoxBrush->mins.x;
			aVertices[i].y = i & (1 << 1) ? pBoxBrush->maxs.y : pBoxBrush->mins.y;
			aVertices[i].z = i & (1 << 2) ? pBoxBrush->maxs.z : pBoxBrush->mins.z;
		}
		Vec3 aNormals[6] = {
			{ 1, 0, 0 },
			{ -1, 0, 0 },
			{ 0, 1, 0 },
			{ 0, -1, 0 },
			{ 0, 0, 1 },
			{ 0, 0, -1 }
		};

		for (int i = 0; i < 6; i++)
		{
			if (I::BSPData->map_surfaces[pBoxBrush->surfaceIndex[i]].flags & SURF_SKY)
				continue;

			std::vector<Vec3> vVertices;
			switch (i)
			{
			case 0: vVertices = { aVertices[1], aVertices[5], aVertices[7], aVertices[3] }; break;
			case 1: vVertices = { aVertices[2], aVertices[6], aVertices[4], aVertices[0] }; break;
			case 2: vVertices = { aVertices[3], aVertices[7], aVertices[6], aVertices[2] }; break;
			case 3: vVertices = { aVertices[0], aVertices[4], aVertices[5], aVertices[1] }; break;
			case 4: vVertices = { aVertices[6], aVertices[7], aVertices[5], aVertices[4] }; break;
			case 5: vVertices = { aVertices[0], aVertices[1], aVertices[3], aVertices[2] }; break;
			}
			Vec3& vNormal = aNormals[i];

			m_mBrushCache[pBrush].emplace_back(vVertices, vNormal, FaceTypeEnum::BoxBrush);
		}
	}
}

void CAmalgamWorld::CachePlaneBrushes()
{
	if (!(m_iNeedsCache & FaceTypeEnum::PlaneBrush))
		return;

	m_iNeedsCache &= ~FaceTypeEnum::PlaneBrush;

	for (int iBrush = 0; iBrush < I::BSPData->numbrushes; iBrush++)
	{
		cbrush_t* pBrush = &I::BSPData->map_brushes[iBrush];
		if (pBrush->IsBox())
			continue;

		std::vector<Plane_t> vPlanes = {}; vPlanes.reserve(pBrush->numsides);
		for (int iSide = 0; iSide != pBrush->numsides; iSide++)
		{
			cbrushside_t* pBrushSide = &I::BSPData->map_brushsides[pBrush->firstbrushside + iSide];
			if (I::BSPData->map_surfaces[pBrushSide->surfaceIndex].flags & SURF_SKY)
				continue;

			cplane_t* pPlane = pBrushSide->plane;
			vPlanes.emplace_back(pPlane->normal, pPlane->dist);
		}
		CPhysConvex* pConvex = I::PhysicsCollision->ConvexFromPlanes(reinterpret_cast<float*>(vPlanes.data()), int(vPlanes.size()), 0.f);
		if (!pConvex)
			continue;

		GetFacesForConvex(pConvex, m_mBrushCache[pBrush], 0.f, FaceTypeEnum::PlaneBrush);
		I::PhysicsCollision->ConvexFree(pConvex);
	}
}

void CAmalgamWorld::CacheEntities()
{
	if (!(m_iNeedsCache & FaceTypeEnum::Entity))
		return;

	m_iNeedsCache &= ~FaceTypeEnum::Entity;

	for (int iCModel = 0; iCModel < I::BSPData->numcmodels; iCModel++)
	{
		cmodel_t* pCModel = &I::BSPData->map_cmodels[iCModel];
		vcollide_t* pCollide = &pCModel->vcollisionData;

		for (int iSolid = 0; iSolid < pCollide->solidCount; iSolid++)
		{
			CPhysCollide* pCollision = pCollide->solids[iSolid];
			if (!pCollision)
				continue;

			GetFacesForCollision(pCollision, m_mCollideCache[pCollide], 0.5f, FaceTypeEnum::Entity);
		}
	}
}

void CAmalgamWorld::Uncache()
{
	if (m_iNeedsCache == FaceTypeEnum::Cache)
		return;

	m_mBrushCache.clear();
	m_mCollideCache.clear();

	m_iNeedsCache = FaceTypeEnum::Cache;
}

std::vector<Face_t> CAmalgamWorld::GetFacesInAABB(const Vec3& vMins, const Vec3& vMaxs, int iMask, ITraceFilter* pFilter, int iFlags)
{
	std::vector<Face_t> vFaces;

	// rebuild everything on map change - brush pointers and vcollides go stale
	const char* sLevelName = I::EngineClient ? I::EngineClient->GetLevelName() : nullptr;
	if (m_sMapName != (sLevelName ? sLevelName : ""))
	{
		m_sMapName = sLevelName ? sLevelName : "";
		Uncache();
	}

	if (!I::BSPData || !I::EngineTrace || !I::SpatialPartition || !I::PhysicsCollision)
		return vFaces;

	Cache();

	if (iFlags & (FaceTypeEnum::BoxBrush | FaceTypeEnum::PlaneBrush))
	{
		CUtlVector<int> vBrushes; I::EngineTrace->GetBrushesInAABB(vMins, vMaxs, &vBrushes, iMask);
		for (int iBrush = 0; iBrush < vBrushes.Count(); iBrush++)
		{
			cbrush_t* pBrush = &I::BSPData->map_brushes[vBrushes[iBrush]];
			if (!m_mBrushCache.contains(pBrush))
				continue;

			bool bBox = pBrush->IsBox();
			if (!(iFlags & (bBox ? FaceTypeEnum::BoxBrush : FaceTypeEnum::PlaneBrush)))
				continue;

			auto& vCache = m_mBrushCache[pBrush];
			for (auto& tFace : vCache)
			{
				if (!FaceValid(tFace.m_vVertices, &tFace.m_vNormal, &vMins, &vMaxs))
					continue;

				vFaces.push_back(tFace);
				if (bBox)
				{	// clamp box brush faces to the query so points stay in range
					for (auto& vVertex : vFaces.back().m_vVertices)
					{
						vVertex.x = std::clamp(vVertex.x, vMins.x, vMaxs.x);
						vVertex.y = std::clamp(vVertex.y, vMins.y, vMaxs.y);
						vVertex.z = std::clamp(vVertex.z, vMins.z, vMaxs.z);
					}
				}
			}
		}
	}

	if (iFlags & FaceTypeEnum::Displacement && I::DispCollTrees && *I::DispCollTrees)
	{
		for (int iDisplacement = 0; iDisplacement < *I::DispCollTreeCount; iDisplacement++)
		{
			CDispCollTree* pDispTree = &(*I::DispCollTrees)[iDisplacement];
			// displacement power is 0-4; anything else means the tree read is
			// corrupt (an engine update shifting the layout, say) and its
			// derived face count would be a runaway loop
			if (pDispTree->m_nPower < 0 || pDispTree->m_nPower > 4)
				continue;

			if (!Math::IsBoxIntersectingBox(vMins, vMaxs, pDispTree->m_mins, pDispTree->m_maxs))
				continue;

			const int iFaces = std::min(pDispTree->GetFaceCount(), pDispTree->m_aTris.Count());
			for (int iFace = 0; iFace < iFaces; iFace++)
			{
				CDispCollTri* pTri = &pDispTree->m_aTris[iFace];
				if (pTri->GetVert(0) >= pDispTree->m_aVerts.Count() || pTri->GetVert(1) >= pDispTree->m_aVerts.Count() || pTri->GetVert(2) >= pDispTree->m_aVerts.Count())
					continue;

				std::vector<Vec3> vVertices = { pDispTree->m_aVerts[pTri->GetVert(0)], pDispTree->m_aVerts[pTri->GetVert(1)], pDispTree->m_aVerts[pTri->GetVert(2)] };

				if (FaceValid(vVertices, &pTri->m_vecNormal, &vMins, &vMaxs))
					vFaces.emplace_back(vVertices, pTri->m_vecNormal, FaceTypeEnum::Displacement);
			}
		}
	}

	if (iFlags & (FaceTypeEnum::Entity | FaceTypeEnum::Prop))
	{
		// two enumerations stand in for Amalgam's single SpatialPartitionMask +
		// IsStaticProp pass - the list mask itself splits props from entities
		auto fHandleHandle = [&](IHandleEntity* pHandleEntity, const bool bProp)
		{
			if (pFilter && !pFilter->ShouldHitEntity(pHandleEntity, iMask))
				return;

			ICollideable* pCollideable = nullptr;
			if (!bProp)
			{
				C_BaseEntity* pEntity = reinterpret_cast<C_BaseEntity*>(pHandleEntity);
				if (!pEntity->GetAbsVelocity().IsZero())
					return;

				pCollideable = pEntity->GetCollideable();
			}
			else
				pCollideable = reinterpret_cast<IClientUnknown*>(pHandleEntity)->GetCollideable();
			if (!pCollideable || !pCollideable->IsSolid())
				return;

			vcollide_t* pCollide = I::ModelInfoClient->GetVCollide(pCollideable->GetCollisionModel());
			if (!pCollide)
				return;

			// props build lazily per vcollide (Amalgam pre-walks all of MDLCache
			// for the same result); brush entities come from CacheEntities with
			// resized faces
			if (bProp && !m_mCollideCache.contains(pCollide))
			{
				for (int iSolid = 0; iSolid < pCollide->solidCount; iSolid++)
				{
					CPhysCollide* pCollision = pCollide->solids[iSolid];
					if (pCollision)
						GetFacesForCollision(pCollision, m_mCollideCache[pCollide], 0.f, FaceTypeEnum::Prop);
				}
			}
			if (!m_mCollideCache.contains(pCollide))
				return;

			auto& vCache = m_mCollideCache[pCollide];
			for (auto& tFace : vCache)
			{
				std::vector<Vec3> vVertices = tFace.m_vVertices;
				Vec3 vNormal = tFace.m_vNormal;
				if (pCollideable->GetCollisionAngles().IsZero())
				{
					for (auto& vVertex : vVertices)
						vVertex += pCollideable->GetCollisionOrigin();
				}
				else
				{
					for (auto& vVertex : vVertices)
						vVertex = Math::VectorTransform(vVertex, pCollideable->CollisionToWorldTransform());
					vNormal = (vVertices[0] - vVertices[2]).Cross(vVertices[0] - vVertices[1]).Normalized();
				}

				if (FaceValid(vVertices, &vNormal, &vMins, &vMaxs))
					vFaces.emplace_back(vVertices, vNormal, tFace.m_iType);
			}
		};

		CEntityEnumerator tProps;
		I::SpatialPartition->EnumerateElementsInBox(PARTITION_CLIENT_STATIC_PROPS, vMins, vMaxs, false, &tProps);
		for (auto pHandleEntity : tProps.m_vEntities)
			fHandleHandle(pHandleEntity, true);

		CEntityEnumerator tEntities;
		I::SpatialPartition->EnumerateElementsInBox(PARTITION_CLIENT_SOLID_EDICTS | PARTITION_CLIENT_NON_STATIC_EDICTS | PARTITION_CLIENT_RESPONSIVE_EDICTS, vMins, vMaxs, false, &tEntities);
		for (auto pHandleEntity : tEntities.m_vEntities)
			fHandleHandle(pHandleEntity, false);
	}

	return vFaces;
}
