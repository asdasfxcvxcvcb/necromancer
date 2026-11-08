#include "../../SDK/SDK.h"

#include "../Features/Materials/Materials.h"
#include "../Features/Outlines/Outlines.h"
#include "../../Utils/CrashHandler/CrashHandler.h"

MAKE_SIGNATURE(CBaseWorldView_DrawExecute, "client.dll", "40 53 55 56 41 56 41 57 48 81 EC", 0x0);
MAKE_SIGNATURE(CParticleSystemMgr_DrawRenderCache, "client.dll", "48 8B C4 88 50 ? 48 89 48 ? 55 57", 0x0);

bool isDrawingWorld = false;

MAKE_HOOK(CBaseWorldView_DrawExecute, Signatures::CBaseWorldView_DrawExecute.Get(), void, __fastcall,
	void* ecx, float waterHeight, view_id_t viewID, float waterZAdjust)
{
	isDrawingWorld = true;
	CALL_ORIGINAL(ecx, waterHeight, viewID, waterZAdjust);
	isDrawingWorld = false;
}

namespace
{
	// Materials/Outlines run here mid world-draw, and (re)creating their render
	// targets/materials in that state is known to fault inside materialsystem/
	// shaderapidx9 (READ at 0xffffffffffffffff) on some driver states. Swallow
	// the fault and skip this frame's pass instead of taking the game down -
	// the buffers get retried next frame. Static free function with no C++
	// locals: required for __try/__except (C2712).
	void RunOutlinePassesGuarded()
	{
		CCrashHandler::s_iSuppressDepth++;
		__try
		{
			F::Materials->Run();
			F::Outlines->RunModels();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			OutputDebugStringA("[Necromancer] outline/material pass faulted mid-draw, skipping it\n");
		}
		CCrashHandler::s_iSuppressDepth--;
	}
}

MAKE_HOOK(CParticleSystemMgr_DrawRenderCache, Signatures::CParticleSystemMgr_DrawRenderCache.Get(), void, __fastcall,
	void* ecx, bool bShadowDepth)
{
	if (isDrawingWorld)
	{
		if (const auto rc = I::MaterialSystem->GetRenderContext())
		{
			rc->ClearBuffers(false, false, true);
		}

		RunOutlinePassesGuarded();
	}

	CALL_ORIGINAL(ecx, bShadowDepth);
}
