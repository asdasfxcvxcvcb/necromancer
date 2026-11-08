#pragma once
#include "interface.h"
#include "trace.h"
#include "cmodel.h"
#include "vcollide.h"

struct cmodel_t;

#ifndef MAX_QPATH
#define MAX_QPATH 96
#endif

#define NUMSIDES_BOXBRUSH 0xFFFFFFFF

// ----------------------------------------------------------------------------
// CCollisionBSPData - the engine's collision BSP (engine.dll global, signature
// bound below). Layout is member-for-member identical to Amalgam's, which runs
// against the same engine build. The helper templates below are byte-exact
// layout stand-ins for the engine's containers - the members are never used,
// only their sizes/offsets matter.
// ----------------------------------------------------------------------------

template <class T>
class CRangeValidatedArray
{
public:
	T& operator[](int i) { return m_pArray[i]; }
	const T& operator[](int i) const { return m_pArray[i]; }
	T* Base() { return m_pArray; }

	T* m_pArray;
	int m_nCount;
};

// engine CUtlBuffer has no vtable - overflow handlers are member function
// pointers and CByteswap is two bitfields in an unsigned int, so this stand-in
// is 64 bytes exactly like the real one
class CSPUtlBuffer
{
public:
	byte* m_pMemory;
	int m_nAllocationCount;
	int m_nGrowSize;
	int m_Get;
	int m_Put;
	byte m_Error;
	byte m_Flags;
	byte m_Reserved;
	int m_nTab;
	int m_nMaxPut;
	int m_nOffset;
	bool (CSPUtlBuffer::*m_GetOverflowFunc)(int);
	bool (CSPUtlBuffer::*m_PutOverflowFunc)(int);
	unsigned int m_ByteswapBits;
};

template <class T>
class CDiscardableArray
{
public:
	int m_nCount;
	CSPUtlBuffer m_buf;
	char m_pFilename[256];
	int m_nOffset;
};

struct cnode_t
{
	cplane_t* plane;
	int children[2];
};

struct cleaf_t
{
	int contents;
	short cluster;
	short area : 9;
	short flags : 7;
	unsigned short firstleafbrush;
	unsigned short numleafbrushes;
	unsigned short dispListStart;
	unsigned short dispCount;
};

struct carea_t
{
	int numareaportals;
	int firstareaportal;
	int floodnum;
	int floodvalid;
};

struct dareaportal_t
{
	unsigned short m_PortalKey;
	unsigned short otherarea;
	unsigned short m_FirstClipPortalVert;
	unsigned short m_nClipPortalVerts;
	int planenum;
};

struct dvis_t
{
	int numclusters;
	int bitofs[8][2];
};

struct cbrush_t
{
	int contents;
	int numsides;
	int firstbrushside;

	inline int GetBox() const { return firstbrushside; }
	inline bool IsBox() const { return numsides == NUMSIDES_BOXBRUSH; }
};

struct cboxbrush_t
{
	VectorAligned mins;
	VectorAligned maxs;
	unsigned short surfaceIndex[6];
	unsigned short pad2[2];
};

struct cbrushside_t
{
	cplane_t* plane;
	unsigned short surfaceIndex;
	unsigned short bBevel;
};

class CCollisionBSPData
{
public:
	cnode_t* map_rootnode;
	char map_name[MAX_QPATH];
	int numbrushsides;
	CRangeValidatedArray<cbrushside_t> map_brushsides;
	int numboxbrushes;
	CRangeValidatedArray<cboxbrush_t> map_boxbrushes;
	int numplanes;
	CRangeValidatedArray<cplane_t> map_planes;
	int numnodes;
	CRangeValidatedArray<cnode_t> map_nodes;
	int numleafs;
	CRangeValidatedArray<cleaf_t> map_leafs;
	int emptyleaf, solidleaf;
	int numleafbrushes;
	CRangeValidatedArray<unsigned short> map_leafbrushes;
	int numcmodels;
	CRangeValidatedArray<cmodel_t> map_cmodels;
	int numbrushes;
	CRangeValidatedArray<cbrush_t> map_brushes;
	int numdisplist;
	CRangeValidatedArray<unsigned short> map_dispList;
	int numvisibility;
	dvis_t* map_vis;
	int numentitychars;
	CDiscardableArray<char> map_entitystring;
	int numareas;
	CRangeValidatedArray<carea_t> map_areas;
	int numareaportals;
	CRangeValidatedArray<dareaportal_t> map_areaportals;
	int numclusters;
	char* map_nullname;
	int numtextures;
	char* map_texturenames;
	CRangeValidatedArray<csurface_t> map_surfaces;
	int floodvalid;
	int numportalopen;
	CRangeValidatedArray<bool> portalopen;
};

MAKE_INTERFACE_SIGNATURE(CCollisionBSPData, BSPData, "engine.dll", "48 8D 15 ? ? ? ? 4C 03 05", 0x0, 0);
