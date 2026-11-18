#pragma once

#include "../Vector/Vector.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <vector>

#ifndef DIST_EPSILON
#define DIST_EPSILON 0.03125f
#endif
#ifndef CALC_EPSILON
#define CALC_EPSILON 0.001f
#endif

namespace Math
{
	inline bool IsBoxIntersectingBox(const Vec3& vMins1, const Vec3& vMaxs1, const Vec3& vMins2, const Vec3& vMaxs2)
	{
		if (vMins1.x > vMaxs2.x || vMaxs1.x < vMins2.x)
			return false;
		if (vMins1.y > vMaxs2.y || vMaxs1.y < vMins2.y)
			return false;
		if (vMins1.z > vMaxs2.z || vMaxs1.z < vMins2.z)
			return false;
		return true;
	}

	inline float AABBLine(Vec3 vMins, Vec3 vMaxs, Vec3 vStart, Vec3 vDir)
	{
		Vec3 a = { (vMins.x - vStart.x) / vDir.x, (vMins.y - vStart.y) / vDir.y, (vMins.z - vStart.z) / vDir.z };
		Vec3 b = { (vMaxs.x - vStart.x) / vDir.x, (vMaxs.y - vStart.y) / vDir.y, (vMaxs.z - vStart.z) / vDir.z };
		Vec3 c = { std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z) };
		return std::max(std::max(c.x, c.y), c.z);
	}

	inline Vec3 PullPoint(Vec3 vFrom, Vec3 vTo, Vec3 vOrigin, Vec3 vMins, Vec3 vMaxs)
	{
		const float flT = fabsf(AABBLine(vOrigin + vMins, vOrigin + vMaxs, vTo, vFrom - vTo));
		return vTo + (vFrom - vTo) * flT;
	}

	inline float Dot2(const Vec2& a, const Vec2& b)
	{
		return a.x * b.x + a.y * b.y;
	}

	inline Vec2 Norm2(const Vec2& v)
	{
		const float flLength = v.Length();
		return flLength ? Vec2(v.x / flLength, v.y / flLength) : v;
	}

	inline void ExpandPolygon(std::vector<Vec3>& vOldVertices, std::vector<Vec3>& vNewVertices, const Vec3& vNormal, float flOffset, const Vec3* pTarget = nullptr)
	{
		Vec3 vForward = (vNewVertices.front() - vNewVertices.back()).Normalized();
		Vec3 vRight = vForward.Cross(vNormal).Normalized();

		vOldVertices = vNewVertices;
		for (int i = 0, n = int(vNewVertices.size()); i < n; i++)
		{
			Vec3& vVertex1 = vOldVertices[i], &vVertex2 = vOldVertices[(i + 1) % n], &vVertex3 = vOldVertices[(i + 2) % n];

			Vec2 vPoint1 = { vVertex1.Dot(vForward), vVertex1.Dot(vRight) };
			Vec2 vPoint2 = { vVertex2.Dot(vForward), vVertex2.Dot(vRight) };
			Vec2 vPoint3 = { vVertex3.Dot(vForward), vVertex3.Dot(vRight) };

			Vec2 vDelta21 = vPoint2 - vPoint1;
			Vec2 vDelta32 = vPoint3 - vPoint2;

			Vec2 vNormal1 = Norm2(Vec2(vDelta21.y, -vDelta21.x));
			Vec2 vNormal2 = Norm2(Vec2(vDelta32.y, -vDelta32.x));

			Vec2 vBisect = Norm2(Vec2(vNormal1.x + vNormal2.x, vNormal1.y + vNormal2.y));
			float flDistance = flOffset / Dot2(vNormal1, vBisect);
			vBisect *= flDistance;

			if (!pTarget)
				vNewVertices[(i + 1) % n] += vForward * vBisect.x + vRight * vBisect.y;
			else
			{
				Vec3 vToTarget = *pTarget - vVertex2;
				if (Vec3 vDisplacementX = vForward * vBisect.x; vDisplacementX.Dot(vToTarget) > 0.f)
					vNewVertices[(i + 1) % n] += vDisplacementX;
				if (Vec3 vDisplacementY = vRight * vBisect.y; vDisplacementY.Dot(vToTarget) > 0.f)
					vNewVertices[(i + 1) % n] += vDisplacementY;
			}
		}
	}

	inline void ExpandPolygon(std::vector<Vec3>& vVertices, const Vec3& vNormal, float flOffset, const Vec3* pTarget = nullptr)
	{
		std::vector<Vec3> vTemporary;
		ExpandPolygon(vTemporary, vVertices, vNormal, flOffset, pTarget);
	}

	inline Vec3 ClosestPointOnLine(const Vec3& vPoint, const Vec3& vPoint1, const Vec3& vPoint2)
	{
		Vec3 vDelta = vPoint2 - vPoint1;

		float flRatio = std::clamp((vPoint - vPoint1).Dot(vDelta) / vDelta.Dot(vDelta), 0.f, 1.f);

		return vPoint1 + vDelta * flRatio;
	}

	inline Vec3 ClosestPointOnTriangle(const Vec3& vPoint, const Vec3& vPoint1, const Vec3& vPoint2, const Vec3& vPoint3, bool* pInside = nullptr)
	{
		Vec3 vDelta21 = vPoint2 - vPoint1;
		Vec3 vDelta31 = vPoint3 - vPoint1;
		if (pInside) *pInside = false;

		Vec3 vDeltaPT = vPoint - vPoint1;
		float flDot1 = vDelta21.Dot(vDeltaPT);
		float flDot2 = vDelta31.Dot(vDeltaPT);
		if (flDot1 <= 0 && flDot2 <= 0)
			return vPoint1;

		vDeltaPT = vPoint - vPoint2;
		float flDot3 = vDelta21.Dot(vDeltaPT);
		float flDot4 = vDelta31.Dot(vDeltaPT);
		if (flDot3 >= 0 && flDot4 <= flDot3)
			return vPoint2;

		vDeltaPT = vPoint - vPoint3;
		float flDot5 = vDelta21.Dot(vDeltaPT);
		float flDot6 = vDelta31.Dot(vDeltaPT);
		if (flDot6 >= 0 && flDot5 <= flDot6)
			return vPoint3;

		float flSideC = flDot1 * flDot4 - flDot3 * flDot2;
		if (flSideC <= 0 && flDot1 >= 0 && flDot3 <= 0)
		{
			float flRatio = flDot1 / (flDot1 - flDot3);
			return vPoint1 + vDelta21 * flRatio;
		}

		float flSideB = flDot5 * flDot2 - flDot1 * flDot6;
		if (flSideB <= 0 && flDot2 >= 0 && flDot6 <= 0)
		{
			float flRatio = flDot2 / (flDot2 - flDot6);
			return vPoint1 + vDelta31 * flRatio;
		}

		float flSideA = flDot3 * flDot6 - flDot5 * flDot4;
		if (flSideA <= 0 && (flDot4 - flDot3) >= 0 && (flDot5 - flDot6) >= 0)
		{
			float flRatio = (flDot4 - flDot3) / (flDot4 - flDot3 + flDot5 - flDot6);
			return vPoint2 + (vPoint3 - vPoint2) * flRatio;
		}

		float flDenominator = 1 / (flSideA + flSideB + flSideC);
		float flV = flSideB * flDenominator;
		float flW = flSideC * flDenominator;
		if (pInside) *pInside = true;
		return vPoint1 + vDelta21 * flV + vDelta31 * flW;
	}

	inline Vec3 ClosestPointOnPolygon(const Vec3& vPoint, const std::vector<Vec3>& vVertices, const Vec3& vNormal, bool* pInside = nullptr)
	{
		float flDot = vNormal.Dot(vPoint - vVertices.front());
		Vec3 vProjection = vPoint - vNormal * flDot;

		for (int i = 0, n = int(vVertices.size()); i < n; i++)
		{
			const Vec3& vVertex1 = vVertices[i], &vVertex2 = vVertices[(i + 1) % n];

			Vec3 vEdge = vVertex2 - vVertex1;
			Vec3 vToProjection = vProjection - vVertex1;

			if (vToProjection.Cross(vEdge).Dot(vNormal) < 0)
				goto outside;
		}
		if (pInside) *pInside = true;
		return vProjection;

	outside:
		float flLowestDistance = std::numeric_limits<float>::max();
		for (int i = 0, n = int(vVertices.size()); i < n; i++)
		{
			const Vec3& vVertex1 = vVertices[i], &vVertex2 = vVertices[(i + 1) % n];

			Vec3 vLine = ClosestPointOnLine(vPoint, vVertex1, vVertex2);
			float flDistance = vLine.DistToSqr(vPoint);
			if (flDistance < flLowestDistance)
				vProjection = vLine, flLowestDistance = flDistance;
		}
		if (pInside) *pInside = false;
		return vProjection;
	}

	inline std::vector<float> SolveQuadratic(float a, float b, float c)
	{
		float flRoot = powf(b, 2.f) - 4 * a * c;
		if (flRoot < 0)
			return {};

		a *= 2;
		b = -b;
		return { (b + sqrtf(flRoot)) / a, (b - sqrtf(flRoot)) / a };
	}
}
