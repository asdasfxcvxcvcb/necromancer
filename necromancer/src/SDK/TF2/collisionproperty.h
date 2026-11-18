#pragma once

#include "../../Utils/Vector/Vector.h"
#include "../../Utils/SignatureManager/SignatureManager.h"

MAKE_SIGNATURE(CCollisionProperty_CalcNearestPoint, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 49 8B D8 48 8B F9 4C 8D 44 24", 0x0);

class CCollisionProperty
{
public:
	void CalcNearestPoint(const Vec3& vecWorldPt, Vec3* pVecNearestWorldPt)
	{
		reinterpret_cast<void(__thiscall*)(void*, const Vec3&, Vec3*)>(Signatures::CCollisionProperty_CalcNearestPoint.Get())(this, vecWorldPt, pVecNearestWorldPt);
	}
};
