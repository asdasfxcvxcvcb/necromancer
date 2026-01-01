#pragma once
#include "../../../SDK/SDK.h"

class CAutoQueue
{
public:
	void Run();
};

MAKE_SINGLETON_SCOPED(CAutoQueue, AutoQueue, F);
