#include "TerrainStreamer.h"
#include "Utils/Logger.h"

namespace xone {

    using namespace aveng;

    /*
    * Operational Success Criteria:
    *   update() is called every frame
    *   Terrain.hasAllPointsReady(coord) eventually flips to true for requested chunks.
    *   Streamed remains the authoritative dedupe set for already-requested centers.
    *   MakeLinearWave(n) produces the intended forward progression pattern.
    *   Eviction does not remove chunks too aggressively before downstream waves can use them.
    */

    TerrainStreamer::TerrainStreamer(
        TerrainController& terrain,
        const TerrainStreamPolicy& policy
    )
        : terrain_(terrain)
        , policy_(policy)
        , linear_(policy.linear)
        , allRange_(policy.allRange)
    {
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
            worldToChunk(cam.transform().translation),
            frameIndex
        };

        StreamCommandBuffer cmds{};

        switch (policy_.mode) {
        case TerrainStreamMode::LinearFlight:
            // // Logger::log(1, "Updating with LinearFlight Policy");
            linear_.update(ctx, terrain_, streamed_, cmds);
            break;

        case TerrainStreamMode::AllRange:
            // Logger::log(1, "Updating with AllRange Policy");
            allRange_.update(ctx, terrain_, streamed_, cmds);
            break;
        }

        applyRequests(cmds.requestCenters, frameIndex);
        applyEvictions(cmds.evictCenters);
    }

    ChunkCoord TerrainStreamer::worldToChunk(glm::vec3 pos) const
    {

        float chunk_size = terrain_.getChunkSize();

        // Assuming x/z world plane and chunk coordinates are based on floor division.
        const int cx = static_cast<int>(std::floor(pos.x / chunk_size));
        const int cz = static_cast<int>(std::floor(pos.z / chunk_size));

        return { cx, cz };
    }

    void TerrainStreamer::applyRequests(const std::vector<ChunkCoord>& centers, uint64_t frameIndex)
    {
        for (const ChunkCoord& c : centers) {
            // // Logger::log(1, "Requesting {%d, %d}\n", c.x, c.z);
            terrain_.generateChunks(c);
        }
    }

    void TerrainStreamer::applyEvictions(const std::vector<ChunkCoord>& evictCenters)
    {
        for (const ChunkCoord& c : evictCenters) {
            auto it = streamed_.find(c);
            if (it == streamed_.end()) {
                continue;
            }

            terrain_.evictChunk(c);
            streamed_.erase(it);
        }
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
        if (!state_.initialized) {
            state_.baseX = ctx.playerChunk.x;
            state_.baseZ = ctx.playerChunk.z;
            state_.initialized = true;
        }
        else if (ctx.playerChunk.x != state_.baseX) {
            state_.baseX = ctx.playerChunk.x;
            // baseZ intentionally preserved -- forward wave progress is kept
            state_.maxCenterWaveRequested = -1;
            state_.maxFanWaveRequested = -1;
        }

        requestInitialCenter(streamed, outCmds);
        advanceFrontier(ctx, terrain, streamed, outCmds);
        enqueueEvictions(ctx.playerChunk, streamed, outCmds);
    }

    void LinearFlightStreamer::requestInitialCenter(
        std::unordered_map<ChunkCoord, StreamedChunkState, ChunkCoordHash>& streamed,
        StreamCommandBuffer& outCmds
    )
    {
        if (state_.maxCenterWaveRequested >= 0) {
            return;
        }

        const LinearWaveCoords w0 = makeLinearWave(0, state_.baseX, state_.baseZ);
        //// Logger::log(1, "Center: {%d,%d}\n", w0.center.x, w0.center.z);
        //// Logger::log(1, "LeftFan: {%d,%d}\n", w0.leftFan.x, w0.leftFan.z);
        //// Logger::log(1, "RightFan: {%d,%d}\n", w0.rightFan.x, w0.rightFan.z);
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
        bool progressed = false;

        do {
            progressed = false;

            // Rule 1:
            // If the next center wave has already been requested and its entire 5x5 support
            // region is all-points-ready, request the two side fan chunks for that wave.
            const int nextFanWave = state_.maxFanWaveRequested + 1;
            if (nextFanWave <= state_.maxCenterWaveRequested) {
                const LinearWaveCoords wave = makeLinearWave(nextFanWave, state_.baseX, state_.baseZ);

                if (terrain.hasRegionReady(wave.center)) {
                    requestIfNeeded(wave.leftFan, streamed, outCmds);
                    requestIfNeeded(wave.rightFan, streamed, outCmds);

                    state_.maxFanWaveRequested = nextFanWave;
                    progressed = true;
                }
            }

            // Rule 2:
            // Once both fan regions' 5x5 support have completed spatial grids, request the next center wave.
            // The next center's runGenerate handles its own mesh-readiness checks internally via CAS retry,
            // so we only need the support region's spatial data to be available, not the full pipeline.
            if (state_.maxFanWaveRequested >= 0 &&
                state_.maxCenterWaveRequested == state_.maxFanWaveRequested)
            {
                const LinearWaveCoords currentFanWave = makeLinearWave(state_.maxFanWaveRequested, state_.baseX, state_.baseZ);

                if (terrain.hasRegionReady(currentFanWave.leftFan) &&
                    terrain.hasRegionReady(currentFanWave.rightFan))
                {
                    const int nextCenterWave = state_.maxCenterWaveRequested + 1;
                    const LinearWaveCoords nextWave = makeLinearWave(nextCenterWave, state_.baseX, state_.baseZ);

                    const int leadZ = nextWave.center.z - ctx.playerChunk.z;
                    if (leadZ > policy_.forwardRows) {
                        break;
                    }

                    requestIfNeeded(nextWave.center, streamed, outCmds);
                    state_.maxCenterWaveRequested = nextCenterWave;
                    progressed = true;
                }
            }

        } while (progressed);
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

        if (dx > policy_.evictRadiusX) return true;
        // if (dz > policy_.evictRadiusZ) return true;       // too far ahead
        if (dz < -policy_.evictBackwardZ) return true;     // too far behind

        return false;
    }

    void LinearFlightStreamer::evictChunks(StreamCommandBuffer& outCmds, TerrainController& terrain) 
    {
        for (const ChunkCoord coord : outCmds.evictCenters) {
            terrain.evictChunk(coord);
        }
    }


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

        // // Logger::log(1, "Request center chunk? <%s>", inserted ? "true" : "false");

        if (inserted) {
            outCmds.requestCenters.push_back(coord);
            // Logger::log(1, "RequestCenters {%d}\n", outCmds.requestCenters.size());
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