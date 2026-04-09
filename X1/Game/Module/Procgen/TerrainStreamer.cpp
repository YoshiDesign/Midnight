#include <chrono>
#include "TerrainStreamer.h"
#include "Game/Module/Procgen/Helpers/Helpers.h"
#include "Utils/Logger.h"
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>

namespace xone {

    using namespace aveng;

    TerrainStreamer::TerrainStreamer(
        TerrainController& terrain,
        const TerrainStreamPolicy& policy
    )
        : terrain_(terrain)
        , policy_(policy)
        , linear_(policy.linear)
        , allRange_(policy.allRange)
    {
        streamed_.reserve(15);
        // Logger::log(1, "TerrainStreamer Initialized\n");
    }

    void TerrainStreamer::setPolicy(const TerrainStreamPolicy& policy)
    {
        policy_ = policy;
        linear_.setPolicy(policy_.linear);
        allRange_.setPolicy(policy_.allRange);
        reset();
    }

    void TerrainStreamer::reset()
    {
        streamed_.clear();
        pendingEvictions_.clear();
        linear_.reset();
        allRange_.reset();
    }

    void TerrainStreamer::update(const AvengCamera& cam, uint64_t frameIndex)
    {
        StreamUpdateContext ctx{
            terrain_.getDrawCenter(),
            frameIndex
        };

#ifdef M_DEBUG
        if (streamSize_ != lastStreamSize_) {
            lastStreamSize_ = streamSize_;
            std::printf("Latest stream_ size:\t%d\n", streamSize_);
        }
#endif

        StreamCommandBuffer cmds{};

        switch (policy_.mode) {
        case TerrainStreamMode::LinearFlight:
            // Logger::log(1, "Updating with LinearFlight Policy");
            linear_.update(ctx, terrain_, streamed_, cmds);

            // DEBUG
            streamSize_ = streamed_.size();

            break;

        case TerrainStreamMode::AllRange:
            // Logger::log(1, "Updating with AllRange Policy");
            allRange_.update(ctx, terrain_, streamed_, cmds);
            break;
        }

        applyRequests(cmds.requestCenters, frameIndex);
        applyEvictions(cmds.evictCenters);
    }

    void TerrainStreamer::applyRequests(const std::vector<ChunkCoord>& centers, uint64_t frameIndex)
    {
        for (const ChunkCoord& c : centers) {
            Logger::log(1, "Requesting {%d, %d}\n", c.x, c.z);
            terrain_.generateChunks(c);
        }
    }

    void TerrainStreamer::applyEvictions(const std::vector<ChunkCoord>& evictCenters)
    {
//// #region agent log
//        auto _batch_t0 = std::chrono::steady_clock::now();
//// #endregion
        int evicted = 0;
        for (const ChunkCoord& c : evictCenters) {
            if (evicted >= kMaxEvictionsPerFrame) { break; }

            auto it = streamed_.find(c);
            if (it == streamed_.end()) { continue; }

            terrain_.evictChunk(c);
            streamed_.erase(it);

            // DEBUG
            streamSize_ = streamed_.size();


            ++evicted;
        }
//// #region agent log
//        if (evicted > 0) {
//            auto _batch_t1 = std::chrono::steady_clock::now();
//            float _batchMs = std::chrono::duration<float,std::milli>(_batch_t1-_batch_t0).count();
//            FILE* _f; fopen_s(&_f, "c:/Users/Yoshi/dev/Midnight/debug-ed8025.log", "a");
//            if(_f){ std::fprintf(_f,"{\"sessionId\":\"ed8025\",\"runId\":\"batch-fix\",\"hypothesisId\":\"D\",\"location\":\"TerrainStreamer.cpp:applyEvictions\",\"message\":\"frame eviction batch\",\"data\":{\"evictedCount\":%d,\"candidateCount\":%d,\"batchMs\":%.3f},\"timestamp\":%lld}\n",evicted,(int)evictCenters.size(),_batchMs,(long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()); std::fclose(_f);} }
//// #endregion
    }

    void LinearFlightStreamer::reset()
    {
        state_ = {};
    }

    void LinearFlightStreamer::update(
        const StreamUpdateContext& ctx,
        TerrainController& terrain,
        std::unordered_map<ChunkCoord, StreamedChunkState, ChunkCoordHash>& streamed,
        StreamCommandBuffer& outCmds
    )
    {
#ifdef M_DEBUG
        // // Logger::log(1, "Player Chunk Coord {%d, %d}\n", ctx.playerChunk.x, ctx.playerChunk.z);
        //for (auto& [k, v] : streamed) {
        //    // Logger::log(1, "Has Streamed Record[%d, %d] = %d\n", k.x, k.z, v.status);
        //}
#endif

        /*
        * ctx contains the player's current chunk coordinate.
        * Here we're synchronizing this to the LinearFlightStreamer's state_
        */

        // Acquire stream starting point: snap player position to the
        // nearest chunk center and lock it as the fixed streaming origin.
        // All wave coordinates are computed relative to this origin so that
        // absolute wave numbers always map to the same spatial coordinates.
        if (!state_.initialized) {
            state_.initialized = true;
            state_.maxCenterWaveRequested = -1;
            state_.maxFanWaveRequested = -1;

            ChunkCoord cdelta = procgen::centerOfRegion_linearPolicy(ctx.playerChunk.x, ctx.playerChunk.z);
            state_.baseX = ctx.playerChunk.x + cdelta.x;
            state_.baseZ = ctx.playerChunk.z + cdelta.z;
        }

        if (state_.maxCenterWaveRequested < 0) {
            requestInitialCenter(streamed, outCmds);
        }

        advanceFrontier(ctx, terrain, streamed, outCmds);
        enqueueEvictions(ctx.playerChunk, streamed, outCmds);
    }

    void LinearFlightStreamer::requestInitialCenter(
        std::unordered_map<ChunkCoord, StreamedChunkState, ChunkCoordHash>& streamed,
        StreamCommandBuffer& outCmds
    )
    {
        const LinearWaveCoords w0 = makeLinearWave(0, state_.baseX, state_.baseZ);

        requestIfNeeded(w0.center, streamed, outCmds);

        state_.maxCenterWaveRequested = 0; 
    }

    void LinearFlightStreamer::advanceFrontier(
        const StreamUpdateContext& ctx,
        TerrainController& terrain,
        std::unordered_map<ChunkCoord, StreamedChunkState, ChunkCoordHash>& streamed,
        StreamCommandBuffer& outCmds
    )
    {
        const int originX = state_.baseX;
        const int originZ = state_.baseZ;

        // std::printf("[LinearFlightStreamer] Streaming Origin: {%d, %d}\n", originX, originZ);

        // Rule 1:
        // Request the two side fan chunks for a wave once the center is ready.
        const int nextFanWave = state_.maxFanWaveRequested + 1;
        if (nextFanWave <= state_.maxCenterWaveRequested) {
            // std::printf("[F1] Ready for FAN candidates.\n");
            
            const LinearWaveCoords wave = makeLinearWave(nextFanWave, originX, originZ);

            if (wave.leftFan.z - ctx.playerChunk.z > policy_.forwardRows) {
                //std::printf("[F] Beyond forward limit (fan z=%d, player z=%d, limit=%d)\n",
                //    wave.leftFan.z, ctx.playerChunk.z, policy_.forwardRows);
            }
            else if (auto it = streamed.find(wave.leftFan); it == streamed.end()) {
                /*std::printf("[F2] Wave...\nLeftFan\t{%d, %d}\nCenter\t{%d, %d}\nRightFan\t{%d, %d}\n",
                    wave.leftFan.x, wave.leftFan.z,
                    wave.center.x, wave.center.z,
                    wave.rightFan.x, wave.rightFan.z);

                std::printf("[F3] Is last center ready?\t\n");*/

                // Check that an entire 5x5 region has completed up to the spatial grid
                if (terrain.hasRegionReady(wave.center)) {
                    // std::printf("[F4] Yes!\n");
                    requestIfNeeded(wave.leftFan, streamed, outCmds);
                    requestIfNeeded(wave.rightFan, streamed, outCmds);

                    state_.maxFanWaveRequested = nextFanWave;
                }
                else {
                   // std::printf("[F4] ...Nope...\n");
                }
            }
            else {
                // std::printf("[F] Early Exit\n");
            }
        }

        // Rule 2:
        // Once both fan regions' 5x5 support have completed spatial grids, request the next center wave.
        // The next center's runGenerate handles its own mesh-readiness checks internally via CAS retry,
        // so we only need the support region's spatial data to be available, not the full pipeline.
        if (state_.maxFanWaveRequested >= 0 &&
            state_.maxCenterWaveRequested == state_.maxFanWaveRequested)
        {
            // std::printf("[C1] Ready for CENTER candidates.\n");

            const int nextCenterWave = state_.maxCenterWaveRequested + 1;
            const LinearWaveCoords currentWave = makeLinearWave(state_.maxFanWaveRequested, originX, originZ);
            const LinearWaveCoords nextWave = makeLinearWave(nextCenterWave, originX, originZ);

            if (nextWave.center.z - ctx.playerChunk.z > policy_.forwardRows) {
                //std::printf("[C] Beyond forward limit (center z=%d, player z=%d, limit=%d)\n",
                //    nextWave.center.z, ctx.playerChunk.z, policy_.forwardRows);
            }
            else if (auto it = streamed.find(nextWave.center); it == streamed.end()) {
            
               /* std::printf("[C2] Wave...\nLeftFan\t{%d, %d}\nCenter\t{%d, %d}\nRightFan\t{%d, %d}\n",
                    nextWave.leftFan.x, nextWave.leftFan.z,
                    nextWave.center.x, nextWave.center.z,
                    nextWave.rightFan.x, nextWave.rightFan.z);*/

                // std::printf("[C3] Is last fan ready?\t");

                // Gate on the CURRENT wave's fans being ready
                if (terrain.hasRegionReady(currentWave.leftFan) &&
                    terrain.hasRegionReady(currentWave.rightFan))
                {
                    // std::printf("[C4] Yes!\n");
                    requestIfNeeded(nextWave.center, streamed, outCmds);
                    state_.maxCenterWaveRequested = nextCenterWave;
                }
                else {
                    // std::printf("[C4] ...Nope...\n");
                }
            }
            else {
                // std::printf("[C] Early Exit\n");
            }
        }

        // std::printf("[5 LinearFlightStreamer] Exiting Request Stage...\n");
    }

    void LinearFlightStreamer::enqueueEvictions(
        ChunkCoord playerChunk,
        std::unordered_map<ChunkCoord,
        StreamedChunkState,
        ChunkCoordHash>& streamed,
        StreamCommandBuffer& outCmds
    )
    {
        for (auto& [coord, state] : streamed) {
            if (state.status == StreamedChunkState::Status::Evicting) {
                continue;
            }

            if (shouldEvict(coord, playerChunk)) {
                outCmds.evictCenters.push_back(coord);
                // Logger::log(1, "Evictions pending: {%d}\n", outCmds.evictCenters.size());
            }
        }
    }

    bool LinearFlightStreamer::shouldEvict(ChunkCoord c, ChunkCoord playerChunk) const
    {
        const int dx = std::abs(c.x - playerChunk.x);
        const int dz = c.z - playerChunk.z;  // signed: positive = ahead, negative = behind

        if (dx > policy_.evictRadiusX) { return true; }
        // if (dz > policy_.evictRadiusZ) return true;       // too far ahead
        if (dz < -policy_.evictBackwardZ) { return true; }   // too far behind

        return false;
    }

    /* Add to the command's requestCenters. Requests are assumed to be validated given regional readiness */
    void LinearFlightStreamer::requestIfNeeded(
        ChunkCoord coord,
        std::unordered_map<ChunkCoord, StreamedChunkState, ChunkCoordHash>& streamed,
        StreamCommandBuffer& outCmds
    )
    {
        auto [it, inserted] = streamed.try_emplace(
            coord,
            StreamedChunkState{ StreamedChunkState::Status::Requested }
        );

        if (inserted) {
            // Logger::log(1, "[LinearFlightStreamer] Adding Request Cmd for {%d, %d}\n", coord.x, coord.z);
            outCmds.requestCenters.push_back(coord);
        }
    }

    void AllRangeStreamer::reset()
    {
    }

    void AllRangeStreamer::update(
        const StreamUpdateContext& ctx,
        TerrainController& terrain,
        std::unordered_map<ChunkCoord, StreamedChunkState, ChunkCoordHash>& streamed,
        StreamCommandBuffer& outCmds
    )
    {
        // Logger::log(1, "AllRangeStreamer::update()\n");
        // Stub for later.
    }

}