#include "../../SDK/SDK.h"

#include "../Features/CFG.h"
#include "../Features/Outlines/Outlines.h"
#include "../Features/TF2Glow/TF2Glow.h"
#include "../Features/Paint/Paint.h"
#include "../../Utils/CrashHandler/CrashHandler.h"

MAKE_SIGNATURE(CViewRender_DrawViewModels, "client.dll", "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 8B FA", 0x0);

namespace
{
	// Outlines::Run composites its render targets mid-draw via
	// DrawScreenSpaceRectangle, which can fault inside materialsystem/
	// shaderapidx9 the same way the model pass can - skip this frame's
	// composite instead of crashing. Static free function with no C++ locals:
	// required for __try/__except (C2712).
	void RunOutlineCompositeGuarded()
	{
		CCrashHandler::s_iSuppressDepth++;
		__try
		{
			F::Outlines->Run();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			OutputDebugStringA("[Necromancer] outline composite faulted mid-draw, skipping it\n");
		}
		CCrashHandler::s_iSuppressDepth--;
	}
}

MAKE_HOOK(CViewRender_DrawViewModels, Signatures::CViewRender_DrawViewModels.Get(), void, __fastcall,
	void* ecx, const CViewSetup& viewRender, bool drawViewmodel)
{
	CALL_ORIGINAL(ecx, viewRender, drawViewmodel);

	// Only run Outlines::Run if outlines are actually enabled and have entities to draw
	// EXTREME: Skip outlines entirely — saves GPU work
	if (!CFG::Perf_Extreme_Skip_Outlines && !CFG::Perf_Extreme_Skip_All_Visuals)
	{
		if (CFG::Outlines_Active && (CFG::Outlines_Players_Active || CFG::Outlines_Buildings_Active || CFG::Outlines_World_Active) && !(CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot()))
		{
			if (CFG::Outlines_Style == 4)
			{
				F::TF2Glow->Run(); // Register entities with glow manager (rendering happens in DoPostScreenSpaceEffects)
			}
			else
				RunOutlineCompositeGuarded(); // Custom outline rendering
		}
	}

	// EXTREME: Skip paint rendering
	if (!CFG::Perf_Extreme_Skip_All_Visuals)
		F::Paint->Run();
}
