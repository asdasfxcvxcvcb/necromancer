#pragma once

#include <unordered_map>
#include <cstdint>
#include <vector>
#include <array>
#include "../Fonts/Fonts.h"
#include "../../../Utils/Vector/Vector.h"
#include "../../../Utils/Color/Color.h"

#define POS_DEFAULT (1 << 0)
#define POS_LEFT (1 << 1)
#define POS_TOP (1 << 2)
#define POS_CENTERX (1 << 3)
#define POS_CENTERY (1 << 4)
#define POS_CENTERXY POS_CENTERX | POS_CENTERY

class CDraw
{
	int m_nScreenW = 0, m_nScreenH = 0;
	VMatrix m_WorldToProjection = {};
	std::unordered_map<uint64_t, int> m_mapAvatars = {};

public:
	void UpdateScreenSize();
	void UpdateW2SMatrix();

	int GetScreenW() { return m_nScreenW; }
	int GetScreenH() { return m_nScreenH; }

	bool W2S(const Vec3 &vOrigin, Vec3 &vScreen);
	bool ClipTransformWithProjection(const matrix3x4_t &worldToScreen, const Vec3 &point, Vec3 *pClip);
	bool ClipTransform(const Vector &point, Vector *pClip);
	bool ScreenPosition(const Vec3 &vPoint, Vec3 &vScreen);
	void String(const CFont &font, int x, int y, Color_t clr, short pos, const char *str, ...);
	void String(const CFont &font, int x, int y, Color_t clr, short pos, const wchar_t *str, ...);
	void Line(int x, int y, int x1, int y1, Color_t clr);
	void Rect(int x, int y, int w, int h, Color_t clr);
	void OutlinedRect(int x, int y, int w, int h, Color_t clr);
	void GradientRect(int x, int y, int w, int h, Color_t top_clr, Color_t bottom_clr, bool horizontal);
	void OutlinedCircle(int x, int y, int radius, int segments, Color_t clr);
	void FilledCircle(int x, int y, int radius, int segments, Color_t clr);
	void Texture(int x, int y, int w, int h, int id, short pos);
	void Polygon(int count, Vertex_t *vertices, Color_t clr);
	void FilledTriangle(const std::array<Vec2, 3> &points, Color_t clr);
	void Arc(int x, int y, int radius, float thickness, float start, float end, Color_t col);
	void StartClipping(int x, int y, int w, int h);
	void EndClipping();
	void FillRectRounded(int x, int y, int w, int h, int radius, Color_t col);
	void Avatar(int x, int y, int w, int h, uint32_t nFriendID);

	// Amalgam-style 3D rendering functions
	void RenderLine(const Vec3& vStart, const Vec3& vEnd, Color_t tColor, bool bZBuffer = false);
	void RenderPath(const std::vector<Vec3>& vPath, Color_t tColor, bool bZBuffer = false,
		int iStyle = 1, float flTime = 0.f, int iSeparatorSpacing = 5, float flSeparatorLength = 5.f);
	void RenderBox(const Vec3& vOrigin, const Vec3& vMins, const Vec3& vMaxs, const Vec3& vAngles, Color_t tColor, bool bZBuffer = false);

	// Draw all stored paths/lines/boxes
	void DrawStoredPaths();
};

MAKE_SINGLETON_SCOPED(CDraw, Draw, H);