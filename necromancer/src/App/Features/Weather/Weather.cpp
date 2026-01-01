#include "Weather.h"

#include "../CFG.h"

// Credits to:
// blizzman
// inrepathy
// narrow the uploader (even tho hes an asshole but give credit where credit is due)


void CWeather::Rain()
{
	constexpr auto PRECIPITATION_INDEX = (MAX_EDICTS - 1);

	// If weather is off, clean up the entity
	if (!CFG::Visuals_Weather)
	{
		if (m_pRainEntity && m_pRainEntity->GetClientNetworkable())
		{
			static const auto dwOff = NetVars::GetNetVar("CPrecipitation", "m_nPrecipType");
			*reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(m_pRainEntity) + dwOff) = 0;
			m_pRainEntity->m_vecMins() = Vec3();
			m_pRainEntity->m_vecMaxs() = Vec3();
			m_pRainEntity->GetClientNetworkable()->PreDataUpdate(DATA_UPDATE_CREATED);
			m_pRainEntity->GetClientNetworkable()->OnPreDataChanged(DATA_UPDATE_CREATED);
			m_pRainEntity->GetClientNetworkable()->OnDataChanged(DATA_UPDATE_CREATED);
			m_pRainEntity->GetClientNetworkable()->PostDataUpdate(DATA_UPDATE_CREATED);
		}
		return;
	}

	// Find the CPrecipitation client class
	static ClientClass* pClass = nullptr;
	if (!pClass)
	{
		for (auto pReturn = I::BaseClientDLL->GetAllClasses(); pReturn; pReturn = pReturn->m_pNext)
		{
			if (pReturn->m_ClassID == static_cast<int>(ETFClassIds::CPrecipitation))
			{
				pClass = pReturn;
				break;
			}
		}
	}

	// Check if entity already exists at our index
	const auto* pRainEntity = I::ClientEntityList->GetClientEntity(PRECIPITATION_INDEX);

	if (!pRainEntity)
	{
		// Create the precipitation entity
		if (!pClass || !pClass->m_pCreateFn)
			return;

		m_pRainNetworkable = reinterpret_cast<IClientNetworkable* (__cdecl*)(int, int)>(pClass->m_pCreateFn)(PRECIPITATION_INDEX, 0);

		if (!m_pRainNetworkable)
			return;

		m_pRainEntity = static_cast<C_BaseEntity*>(I::ClientEntityList->GetClientEntity(PRECIPITATION_INDEX));

		if (!m_pRainEntity || !m_pRainEntity->GetClientNetworkable())
			return;
	}
	else if (!m_pRainEntity)
	{
		m_pRainEntity = static_cast<C_BaseEntity*>(I::ClientEntityList->GetClientEntity(PRECIPITATION_INDEX));
		m_pRainNetworkable = m_pRainEntity ? m_pRainEntity->GetClientNetworkable() : nullptr;
	}

	// Update the precipitation entity
	if (m_pRainEntity && m_pRainEntity->GetClientNetworkable())
	{
		static const auto dwOff = NetVars::GetNetVar("CPrecipitation", "m_nPrecipType");
		

		*reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(m_pRainEntity) + dwOff) = CFG::Visuals_Weather - 1;

		m_pRainEntity->GetClientNetworkable()->PreDataUpdate(DATA_UPDATE_CREATED);
		m_pRainEntity->GetClientNetworkable()->OnPreDataChanged(DATA_UPDATE_CREATED);

		// Set bounds to cover the entire map
		m_pRainEntity->m_vecMins() = Vec3(-32768.0f, -32768.0f, -32768.0f);
		m_pRainEntity->m_vecMaxs() = Vec3(32768.0f, 32768.0f, 32768.0f);

		m_pRainEntity->GetClientNetworkable()->OnDataChanged(DATA_UPDATE_CREATED);
		m_pRainEntity->GetClientNetworkable()->PostDataUpdate(DATA_UPDATE_CREATED);
	}
}
