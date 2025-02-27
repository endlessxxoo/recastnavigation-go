#include "detour.h"
#include "detour_util.h"
#include <Detour/Include/DetourAssert.h>
#include <Detour/Include/DetourCommon.h>
#include <Detour/Include/DetourNavMeshQuery.h>
#include <stdio.h>
#include <string>
#include <string.h>

struct NavMeshSetHeader
{
    int32_t magic;
    int32_t version;
    int32_t numTiles;
    dtNavMeshParams params;
    float boundsMinX;
    float boundsMinY;
    float boundsMinZ;
    float boundsMaxX;
    float boundsMaxY;
    float boundsMaxZ;
};

struct NavMeshTileHeader
{
    dtTileRef tileRef;
    int32_t dataSize;
};

struct TileCacheSetHeader
{
    int32_t magic;
    int32_t version;
    int32_t numTiles;
    dtNavMeshParams meshParams;
    dtTileCacheParams cacheParams;
    float boundsMinX;
    float boundsMinY;
    float boundsMinZ;
    float boundsMaxX;
    float boundsMaxY;
    float boundsMaxZ;
};

struct TileCacheTileHeader
{
    dtCompressedTileRef tileRef;
    int32_t dataSize;
};

static const int32_t NAVMESHSET_MAGIC = 'M' << 24 | 'S' << 16 | 'A' << 8 | 'T';
static const int32_t NAVMESHSET_VERSION = 1;
static const int32_t TILECACHESET_MAGIC = 'T' << 24 | 'S' << 16 | 'A' << 8 | 'T';
static const int32_t TILECACHESET_VERSION = 1;



class FileReader {
public:
    FileReader(const char* path)
        :fp(nullptr)
    {
#pragma warning(push)
#pragma warning(disable: 4996)
        fp = fopen(path, "rb");
#pragma warning(pop)
    }
    ~FileReader() {
        if (fp) {
            fclose(fp);
            fp = nullptr;
        }
    }
    operator FILE*() {
        return fp;
    }

private:
    FILE * fp;
};


dtNavMesh* LoadStaticMesh(const char*path, int& errCode) {
    errCode = 0;
    FileReader fp(path);
    if (fp == 0) {
        errCode = 101;
        return nullptr;
    }

    // Read header.
    NavMeshSetHeader header;
    size_t readLen = fread(&header, sizeof(NavMeshSetHeader), 1, fp);
    if (readLen != 1)
    {
        errCode = 102;
        return nullptr;
    }
    if (header.magic != NAVMESHSET_MAGIC)
    {
        errCode = 103;
        return nullptr;
    }
    if (header.version != NAVMESHSET_VERSION)
    {
        errCode = 104;
        return nullptr;
    }

    printf("boundsMin: %f, %f, %f\n", header.boundsMinX, header.boundsMinY, header.boundsMinZ);
    printf("boundsMax: %f, %f, %f\n", header.boundsMaxX, header.boundsMaxY, header.boundsMaxZ);

    dtNavMesh* mesh = dtAllocNavMesh();
    if (!mesh)
    {
        errCode = 105;
        return nullptr;
    }
    dtStatus status = mesh->init(&header.params);
    if (!dtStatusSucceed(status))
    {
        dtFreeNavMesh(mesh);
        errCode = 106;
        return nullptr;
    }

    // Read tiles.
    for (int i = 0; i < header.numTiles; ++i)
    {
        NavMeshTileHeader tileHeader;
        readLen = fread(&tileHeader, sizeof(tileHeader), 1, fp);
        if (readLen != 1)
        {
            dtFreeNavMesh(mesh);
            errCode = 107;
            return nullptr;
        }

        if (!tileHeader.tileRef || !tileHeader.dataSize)
            break;

        unsigned char* data = (unsigned char*)dtAlloc(tileHeader.dataSize, DT_ALLOC_PERM);
        if (!data) break;
        memset(data, 0, tileHeader.dataSize);
        readLen = fread(data, tileHeader.dataSize, 1, fp);
        if (readLen != 1)
        {
            dtFree(data);
            dtFreeNavMesh(mesh);
            errCode = 108;
            return nullptr;
        }

        mesh->addTile(data, tileHeader.dataSize, DT_TILE_FREE_DATA, tileHeader.tileRef, 0);
    }
    return mesh;
}


dtNavMeshQuery* CreateQuery(dtNavMesh* mesh, int maxNode) {
    auto query = dtAllocNavMeshQuery();
    if (!query) {
        return nullptr;
    }
    dtStatus status = query->init(mesh, maxNode);
    if (!dtStatusSucceed(status)) {
        return nullptr;
    }
    return query;
}


dtTileCache* mTileCache;
LinearAllocator* mTalloc;
FastLZCompressor* mTcomp;
MeshProcess* mTmproc;

dtNavMesh* LoadDynamicMesh(const char*path, int& errCode) {
    errCode = 0;
    FileReader fp(path);
    if (fp == 0) {
        errCode = 201;
        return nullptr;
    }

    // Read header.
    TileCacheSetHeader header;
    size_t headerReadReturnCode = fread(&header, sizeof(TileCacheSetHeader), 1, fp);
    if (headerReadReturnCode != 1)
    {
        // Error or early EOF
        errCode = 202;
        return nullptr;
    }
    if (header.magic != TILECACHESET_MAGIC)
    {
        errCode = 203;
        return nullptr;
    }
    if (header.version != TILECACHESET_VERSION)
    {
        errCode = 204;
        return nullptr;
    }

    printf("boundsMin: %f, %f, %f\n", header.boundsMinX, header.boundsMinY, header.boundsMinZ);
    printf("boundsMax: %f, %f, %f\n", header.boundsMaxX, header.boundsMaxY, header.boundsMaxZ);

    dtNavMesh* mMesh = dtAllocNavMesh();
    if (!mMesh)
    {
        errCode = 205;
        return nullptr;
    }
    dtStatus status = mMesh->init(&header.meshParams);
    if (!dtStatusSucceed(status))
    {
        errCode = 206;
        return nullptr;
    }

    mTileCache = dtAllocTileCache();
    if (!mTileCache)
    {
        errCode = 207;
        return nullptr;
    }

    mTalloc = new LinearAllocator(32 * 1024);
    mTcomp = new FastLZCompressor();
    mTmproc = new MeshProcess();
    status = mTileCache->init(&header.cacheParams, mTalloc, mTcomp, mTmproc);
    if (!dtStatusSucceed(status))
    {
        errCode = 208;
        return nullptr;
    }

    // Read tiles.
    for (int i = 0; i < header.numTiles; ++i)
    {
        TileCacheTileHeader tileHeader;
        size_t tileHeaderReadReturnCode = fread(&tileHeader, sizeof(tileHeader), 1, fp);
        if (tileHeaderReadReturnCode != 1)
        {
            // Error or early EOF
            errCode = 209;
            return nullptr;
        }
        if (!tileHeader.tileRef || !tileHeader.dataSize)
            break;

        unsigned char* data = (unsigned char*)dtAlloc(tileHeader.dataSize, DT_ALLOC_PERM);
        if (!data) break;
        memset(data, 0, tileHeader.dataSize);
        size_t tileDataReadReturnCode = fread(data, tileHeader.dataSize, 1, fp);
        if (tileDataReadReturnCode != 1)
        {
            // Error or early EOF
            dtFree(data);
            errCode = 210;
            return nullptr;
        }

        dtCompressedTileRef tile = 0;
        dtStatus addTileStatus = mTileCache->addTile(data, tileHeader.dataSize, DT_COMPRESSEDTILE_FREE_DATA, &tile);
        if (dtStatusFailed(addTileStatus))
        {
            dtFree(data);
            errCode = 211;
            return nullptr;
        }

        if (tile) {
            mTileCache->buildNavMeshTile(tile, mMesh);
        }
        else {
            errCode = 212;
            return nullptr;
        }
    }
    return mMesh;
}


dtStatus FindRandomPoint(const dtNavMeshQuery *query,
    const dtQueryFilter* filter, float(*frand)(),
    dtPolyRef* randomRef, float* randomPt)
{
    const dtNavMesh* m_nav = query->getAttachedNavMesh();
    dtAssert(m_nav);

    // Randomly pick one tile. Assume that all tiles cover roughly the same area.
    int tileIndex = int(frand() * m_nav->getMaxTiles());
    const dtMeshTile* tile = 0;
    for (int i = tileIndex; true; i++)
    {
        i = i%m_nav->getMaxTiles();
        tile = m_nav->getTile(i);
        if (tile && tile->header) {
            break;
        }
    }
    if (!tile)
        return DT_FAILURE;

    // Randomly pick one polygon weighted by polygon area.
    const dtPoly* poly = 0;
    dtPolyRef polyRef = 0;
    const dtPolyRef base = m_nav->getPolyRefBase(tile);

    float areaSum = 0.0f;
    for (int i = 0; i < tile->header->polyCount; ++i)
    {
        const dtPoly* p = &tile->polys[i];
        // Do not return off-mesh connection polygons.
        if (p->getType() != DT_POLYTYPE_GROUND)
            continue;
        // Must pass filter
        const dtPolyRef ref = base | (const dtPolyRef)i;
        if (!filter->passFilter(ref, tile, p))
            continue;

        // Calc area of the polygon.
        float polyArea = 0.0f;
        for (int j = 2; j < p->vertCount; ++j)
        {
            const float* va = &tile->verts[p->verts[0] * 3];
            const float* vb = &tile->verts[p->verts[j - 1] * 3];
            const float* vc = &tile->verts[p->verts[j] * 3];
            polyArea += dtTriArea2D(va, vb, vc);
        }

        // Choose random polygon weighted by area, using reservoi sampling.
        areaSum += polyArea;
        const float u = frand();
        if (u*areaSum <= polyArea)
        {
            poly = p;
            polyRef = ref;
        }
    }

    if (!poly)
        return DT_FAILURE;

    // Randomly pick point on polygon.
    const float* v = &tile->verts[poly->verts[0] * 3];
    float verts[3 * DT_VERTS_PER_POLYGON];
    float areas[DT_VERTS_PER_POLYGON];
    dtVcopy(&verts[0 * 3], v);
    for (int j = 1; j < poly->vertCount; ++j)
    {
        v = &tile->verts[poly->verts[j] * 3];
        dtVcopy(&verts[j * 3], v);
    }

    const float s = frand();
    const float t = frand();

    float pt[3];
    dtRandomPointInConvexPoly(verts, poly->vertCount, areas, s, t, pt);

    float h = 0.0f;
    dtStatus status = query->getPolyHeight(polyRef, pt, &h);
    if (dtStatusFailed(status))
        return status;
    pt[1] = h;

    dtVcopy(randomPt, pt);
    *randomRef = polyRef;

    return DT_SUCCESS;
}