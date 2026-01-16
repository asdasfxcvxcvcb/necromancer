#pragma once

#include "../../../SDK/SDK.h"
#include <vector>

struct TF2GlowEntity_t
{
	C_BaseEntity* m_pEntity;
	int m_nGlowIndex;
	Color_t m_Color;
	float m_flAlpha;
};

class CTF2Glow
{
private:
	std::vector<TF2GlowEntity_t> m_vecGlowEntities;

public:
	void Run();
	void Render(const CViewSetup* pViewSetup);
	void CleanUp();
};

MAKE_SINGLETON_SCOPED(CTF2Glow, TF2Glow, F);
