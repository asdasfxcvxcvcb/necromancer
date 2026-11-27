#include "../../SDK/SDK.h"

MAKE_HOOK(ISurface_ResetFontCaches, Memory::GetVFunc(I::MatSystemSurface, 136), void, __fastcall,
	void* ecx)
{
	CALL_ORIGINAL(ecx);

	H::Fonts->Invalidate();
}
