#pragma once

#include "../../../SDK/SDK.h"

class CMaterials
{
	void Initialize();

	std::unordered_map<C_BaseEntity*, bool> m_mapDrawnEntities = {};
	bool m_bRendering = false;
	bool m_bRenderingOriginalMat = false;
	bool m_bCleaningUp = false;

	void DrawEntity(C_BaseEntity* pEntity);
	void RunFakeAngle();

public:
	IMaterial* m_pFlat = nullptr;
	IMaterial* m_pShaded = nullptr;
	IMaterial* m_pGlossy = nullptr;
	IMaterial* m_pGlow = nullptr;
	IMaterial* m_pPlastic = nullptr;
	IMaterialVar* m_pGlowEnvmapTint = nullptr;
	IMaterialVar* m_pGlowSelfillumTint = nullptr;

	IMaterial* m_pSheen = nullptr;
	IMaterialVar* m_pSheenIndex = nullptr;
	IMaterialVar* m_pSheenTint = nullptr;
	IMaterial* m_pFlatNoInvis = nullptr;
	IMaterial* m_pShadedNoInvis = nullptr;

	void Run();
	void CleanUp();
	void ValidateSheen();

	// Called by the DrawModelExecute hook to provide the original function pointer
	void SetDrawModelExecuteFn(IVModelRender::DrawModelExecuteFn fn) { m_fnDrawModelExecute = fn; }

	// Original DrawModelExecute function pointer — set by the hook, used by RenderLagRecords
	IVModelRender::DrawModelExecuteFn m_fnDrawModelExecute = nullptr;

	// Check if lag records should be rendered for this player (called from DrawModelExecute hook)
	bool ShouldRenderLagRecords(C_TFPlayer* pPlayer);

	// Check weapon/record conditions without Materials-specific config — shared with Outlines
	static bool ShouldRenderLagRecordsBase(C_TFPlayer* pPlayer);

	// Render lag records by calling DrawModelExecute directly with each record's BoneMatrix
	// (Amalgam approach — no Set/Restore/DrawModel, no SetupBones recalculation)
	void RenderLagRecords(C_TFPlayer* pPlayer, const DrawModelState_t& state, ModelRenderInfo_t& pInfo);

	bool HasDrawn(C_BaseEntity* pEntity)
	{
		return m_mapDrawnEntities.contains(pEntity);
	}

	bool IsRendering()
	{
		return m_bRendering;
	}

	bool IsRenderingOriginalMat()
	{
		return m_bRenderingOriginalMat;
	}

	bool IsUsedMaterial(const IMaterial* pMaterial)
	{
		return pMaterial == m_pFlat
			|| pMaterial == m_pShaded
			|| pMaterial == m_pGlossy
			|| pMaterial == m_pGlow
			|| pMaterial == m_pPlastic
			|| pMaterial == m_pSheen
			|| pMaterial == m_pFlatNoInvis
			|| pMaterial == m_pShadedNoInvis;
	}

	bool IsCleaningUp() { return m_bCleaningUp; }
};

MAKE_SINGLETON_SCOPED(CMaterials, Materials, F);
