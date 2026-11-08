#include "../../SDK/SDK.h"

#include "../Features/Materials/Materials.h"
#include "../Features/Outlines/Outlines.h"
#include "../Features/TF2Glow/TF2Glow.h"
#include "../Features/WorldModulation/WorldModulation.h"
#include "../Features/Paint/Paint.h"
#include "../Features/SeedPred/SeedPred.h"
#include "../Features/MovementSimulation/MovementSimulation.h"
#include "../Features/TickbaseManip/TickbaseManip.h"
#include "../Features/ChatESP/ChatESP.h"
#include "../Features/Misc/SkinChanger/SkinChanger.h"
#include "../Features/Models/Models.h"

MAKE_HOOK(IBaseClientDLL_LevelShutdown, Memory::GetVFunc(I::BaseClientDLL, 7), void, __fastcall,
	void* ecx)
{
	// Set level transition flag FIRST to prevent any entity access
	G::bLevelTransition = true;

	F::Materials->CleanUp();
	F::Outlines->CleanUp();
	F::TF2Glow->CleanUp();

	// Wait for render thread to finish any in-progress operations
	Sleep(100);

	CALL_ORIGINAL(ecx);

	H::Entities->ClearCache();
	H::Entities->ClearModelIndexes();
	H::Entities->ClearPlayerInfoCache();

	F::Paint->CleanUp();
	F::WorldModulation->LevelShutdown();
	F::SeedPred->Reset();

	for (int i = 0; i < G::MAX_VELFIX_SLOTS; i++)
		G::mapVelFixRecords[i].m_bActive = false;

	F::Ticks->Reset();
	F::MovementSimulation->ClearRecords();
	F::ChatESP->OnLevelShutdown();
	F::SkinChanger->OnLevelShutdown();
	F::Models->OnLevelShutdown();

	// Clear level transition flag - cleanup is done, menu should work again
	G::bLevelTransition = false;
}
