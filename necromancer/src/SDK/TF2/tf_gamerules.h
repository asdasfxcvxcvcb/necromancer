#pragma once

#include "shareddefs.h"
#include "../../Utils/SignatureManager/SignatureManager.h"
#include "../../Utils/Memory/Memory.h"

MAKE_SIGNATURE(TFGameRules, "client.dll", "48 8B 0D ? ? ? ? 4C 8B C3 48 8B D7 48 8B 01 FF 90 ? ? ? ? 84 C0", 0x0);

class CTFGameRules
{
public:
	CViewVectors* GetViewVectors()
	{
		return reinterpret_cast<CViewVectors*(__thiscall*)(void*)>(Memory::GetVFunc(this, 31))(this);
	}
};

namespace I
{
	inline CTFGameRules* TFGameRules()
	{
		static auto addr = Signatures::TFGameRules.Get();
		if (!addr)
			return nullptr;
		return *reinterpret_cast<CTFGameRules**>(Memory::RelToAbs(addr));
	}
}
