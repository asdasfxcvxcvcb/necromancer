#pragma once

#include "../../../SDK/SDK.h"

class CMiscVisuals
{
	// Bloom rendering system (same as Paint)
	IMaterial* m_pMatGlowColor = nullptr;
	IMaterial* m_pMatHaloAddToScreen = nullptr;
	ITexture* m_pRtFullFrame = nullptr;
	ITexture* m_pRenderBuffer0 = nullptr;
	ITexture* m_pRenderBuffer1 = nullptr;
	IMaterial* m_pMatBlurX = nullptr;
	IMaterial* m_pMatBlurY = nullptr;
	IMaterialVar* m_pBloomAmount = nullptr;
	bool m_bBloomInitialized = false;

	void InitializeBloom();

public:
	void AimbotFOVCircle();
	void AimbotFOVCircleBloom();  // Bloom version using shaders
	void ViewModelSway();
	void DetailProps();
	void ShiftBar();

	void SniperLines();
	void CritIndicator();

	void CustomFOV(CViewSetup* pSetup);
	void Thirdperson(CViewSetup* pSetup);
	
	void CleanUpBloom();
};

MAKE_SINGLETON_SCOPED(CMiscVisuals, MiscVisuals, F);
