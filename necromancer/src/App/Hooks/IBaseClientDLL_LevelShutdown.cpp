#include "../../SDK/SDK.h"

#include "../Features/Materials/Materials.h"
#include "../Features/Outlines/Outlines.h"
#include "../Features/WorldModulation/WorldModulation.h"
#include "../Features/Paint/Paint.h"
#include "../Features/SeedPred/SeedPred.h"
#include "../Features/MovementSimulation/MovementSimulation.h"
#include "../Features/Resolver/Resolver.h"

// Quick debug log
static void DebugLog(const char* msg)
{
	FILE* f = nullptr;
	fopen_s(&f, "C:\\necromancer_tf2\\crashlog\\levelchange.txt", "a");
	if (f) { fprintf(f, "%s\n", msg); fflush(f); fclose(f); }
}

MAKE_HOOK(IBaseClientDLL_LevelShutdown, Memory::GetVFunc(I::BaseClientDLL, 7), void, __fastcall,
	void* ecx)
{
	DebugLog("LevelShutdown: START");
	
	// Set level transition flag FIRST to prevent any entity access
	G::bLevelTransition = true;
	
	// Signal that we're shutting down - this will prevent rendering from using materials
	DebugLog("LevelShutdown: Materials CleanUp");
	F::Materials->CleanUp();
	
	DebugLog("LevelShutdown: Outlines CleanUp");
	F::Outlines->CleanUp();

	// Wait for render thread to finish any in-progress operations
	// This gives the render thread time to complete before we actually destroy resources
	DebugLog("LevelShutdown: Sleep");
	Sleep(100);

	DebugLog("LevelShutdown: CALL_ORIGINAL");
	CALL_ORIGINAL(ecx);

	DebugLog("LevelShutdown: ClearCache");
	H::Entities->ClearCache();
	
	DebugLog("LevelShutdown: ClearModelIndexes");
	H::Entities->ClearModelIndexes();
	
	DebugLog("LevelShutdown: ClearPlayerInfoCache");
	H::Entities->ClearPlayerInfoCache(); // Clear F2P and party cache on level change

	DebugLog("LevelShutdown: Paint CleanUp");
	F::Paint->CleanUp();
	
	DebugLog("LevelShutdown: WorldModulation");
	F::WorldModulation->LevelShutdown();

	DebugLog("LevelShutdown: SeedPred Reset");
	F::SeedPred->Reset();

	DebugLog("LevelShutdown: Clear velfix");
	G::mapVelFixRecords.clear();

	DebugLog("LevelShutdown: Shifting Reset");
	Shifting::Reset();
	
	DebugLog("LevelShutdown: Clear player behaviors");
	F::MovementSimulation->ClearBehaviors();
	
	DebugLog("LevelShutdown: Resolver Reset");
	F::Resolver->Reset();
	
	// Clear level transition flag - cleanup is done, menu should work again
	G::bLevelTransition = false;
	
	DebugLog("LevelShutdown: END");
}
