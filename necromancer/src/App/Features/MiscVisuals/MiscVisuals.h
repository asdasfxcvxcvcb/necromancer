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

	// Freecam state
	bool m_bFreecamActive = false;
	Vec3 m_vFreecamPos = {};
	Vec3 m_vFreecamAngles = {};
	Vec3 m_vSavedPlayerAngles = {}; // Saved player angles to restore when exiting freecam

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
	void Freecam(CViewSetup* pSetup);
	
	bool IsFreecamActive() const { return m_bFreecamActive; }
	
	void CleanUpBloom();
};

MAKE_SINGLETON_SCOPED(CMiscVisuals, MiscVisuals, F);
