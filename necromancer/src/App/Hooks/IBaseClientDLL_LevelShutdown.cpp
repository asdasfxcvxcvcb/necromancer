#include "../../SDK/SDK.h"

#include "../Features/Materials/Materials.h"
#include "../Features/Outlines/Outlines.h"
#include "../Features/WorldModulation/WorldModulation.h"
#include "../Features/Paint/Paint.h"
#include "../Features/SeedPred/SeedPred.h"

MAKE_HOOK(IBaseClientDLL_LevelShutdown, Memory::GetVFunc(I::BaseClientDLL, 7), void, __fastcall,
	void* ecx)
{
	// Signal that we're shutting down - this will prevent rendering from using materials
	F::Materials->CleanUp();
	F::Outlines->CleanUp();

	// Wait for render thread to finish any in-progress operations
	// This gives the render thread time to complete before we actually destroy resources
	Sleep(100);

	CALL_ORIGINAL(ecx);

	H::Entities->ClearCache();
	H::Entities->ClearModelIndexes();
	H::Entities->ClearPlayerInfoCache(); // Clear F2P and party cache on level change

	F::Paint->CleanUp();
	F::WorldModulation->LevelShutdown();

	F::SeedPred->Reset();

	G::mapVelFixRecords.clear();

	Shifting::Reset();
}
