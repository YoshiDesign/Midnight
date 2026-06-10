#pragma once
#include <cstdint>
#include "Runtime/Threading/Job.h"

namespace procgen {

    /**
    * Terrain Generation. Version 2 Support
    * 
    * The Docs:
    * Terrain is requested by the TerrainStreamer.
    * A completed renderable touches 7x7 (49) chunks. This allows us to generate large amounts of terrain
    * and not pester CPU cores with small jobs, in favor of doing more work up front.
    * 
    * Each stage of terrain generation completes one of the structs pointed to within ChunkRecord2.
	* Some stages impose a countdown to synchronize between stages while work is done in parallel. 
    * E.g. BuildPointsRow counts down on pointsReady, and the next stage waits for pointsReady to hit 0 before it kicks off.
	* The thread to bring the counters to 0 is responsible for launching the next stage's job.
    * 
    * 
    */

    struct TerrainRequest;

    struct TerrainController;

    enum class JobKind : uint8_t {
        Empty = 0,

        // Request-level control job.
        t_RequestLaunch,

        // 7x7 footprint: 49 chunks.
        t_BuildPointsRow,

        // 5x5 footprint: 25 chunks.
        t_BuildAllPoints,
        t_BuildHeightField,
        t_Triangulate,

        // 3x3 footprint: 9 chunks.
        t_SpatialGrid,
        t_ErosionBatch,
        t_ThermalPass,

        // Request-level / renderable-level publish.
        t_FinalizeTerrain, // a single request-level terminal assembly job that depends on the inner 3x3 being complete
        t_TerrainComplete,
    };

    struct RequestLaunchPayload {
        TerrainController* terrain;
        TerrainRequest* request;
        uint16_t requestSlot;
        uint32_t requestId;
    };

    struct BuildPointsRowPayload {
        TerrainController* terrain;
        TerrainRequest* request;
        uint16_t requestSlot;
        uint16_t chunkSlot;
        uint8_t localIdx;
        uint8_t row;
        uint32_t requestId;
    };

    struct TriangulatePayload {
        TerrainController* terrain;
        TerrainRequest* request;
        uint16_t requestSlot;
        uint16_t chunkSlot;
        uint8_t localIdx;
        uint32_t requestId;
    };

    //void executeRequestLaunch(JobContext& ctx, const RequestLaunchPayload& payload);
    //void executeBuildPointsRow(JobContext& ctx, const BuildPointsRowPayload& payload);
    //void executeTriangulate(JobContext& ctx, const TriangulatePayload& payload);

}
