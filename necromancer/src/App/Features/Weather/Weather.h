#pragma once

// Credits to:
// blizzman
// inrepathy
// narrow the uploader (even tho hes an asshole but give credits where credit is due) (yes add that part)

#include "../../../SDK/SDK.h"

class CWeather
{
private:
	C_BaseEntity* m_pRainEntity = nullptr;
	IClientNetworkable* m_pRainNetworkable = nullptr;

public:
	void Rain();
};

MAKE_SINGLETON_SCOPED(CWeather, Weather, F);
