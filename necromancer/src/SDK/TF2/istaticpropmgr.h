#pragma once

#include "IEngineTrace.h"
#include "../../Utils/InterfaceManager/InterfaceManager.h"

class IPhysicsEnvironment;
class IVPhysicsKeyHandler;
typedef unsigned short MDLHandle_t;


class IStaticPropMgr
{
public:
	virtual bool Init() = 0;
	virtual void Shutdown() = 0;
	virtual void LevelInit() = 0;
	virtual void LevelInitClient() = 0;
	virtual void LevelShutdownClient() = 0;
	virtual void LevelShutdown() = 0;
	virtual void RecomputeStaticLighting() = 0;
	virtual bool IsPropInPVS(IHandleEntity* pHandleEntity, const byte* pVis) const = 0;
	virtual ICollideable* GetStaticProp(IHandleEntity* pHandleEntity) = 0;
	virtual LightCacheHandle_t GetLightCacheHandleForStaticProp(IHandleEntity* pHandleEntity) = 0;
	virtual bool IsStaticProp(IHandleEntity* pHandleEntity) const = 0;
	virtual bool IsStaticProp(CBaseHandle handle) const = 0;
	virtual int GetStaticPropIndex(IHandleEntity* pHandleEntity) const = 0;
	virtual bool PropHasBakedLightingDisabled(IHandleEntity* pHandleEntity) const = 0;
};

MAKE_INTERFACE_SIGNATURE(IStaticPropMgr, StaticPropMgr, "engine.dll", "48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 89 5C 24 ? 48 89 74 24 ? 57 48 81 EC ? ? ? ? 41 8B C0", 0x0, 0);
