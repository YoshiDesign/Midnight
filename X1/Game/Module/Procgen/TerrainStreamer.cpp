#include "TerrainStreamer.h"
#include "Game/Module/Procgen/Helpers/Helpers.h"
#include "Utils/Logger.h"

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

    void TerrainStreamer::applyRequests(const std::vector<ChunkCoord>& centers, uint64_t frameIndex)
    {
        for (const ChunkCoord& c : centers) {
            // // Logger::log(1, "Requesting {%d, %d}\n", c.x, c.z);
            terrain_.generateChunks(c);
        }
    }

    void TerrainStreamer::applyEvictions(const std::vector<ChunkCoord>& evictCenters)
    {
        int evicted = 0;
        for (const ChunkCoord& c : evictCenters) {
            if (evicted >= kMaxEvictionsPerFrame) { break; }

            auto it = streamed_.find(c);
            if (it == streamed_.end()) { continue; }

            terrain_.evictChunk(c);
            streamed_.erase(it);
            ++evicted;
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

        /*
        * ctx contains the player's current chunk coordinate.
        * Here we're synchronizing this to the LinearFlightStreamer's state_
        */

        // Acquire stream starting point
        if (!state_.initialized) {
            state_.baseX = ctx.playerChunk.x;
            state_.baseZ = ctx.playerChunk.z;
            state_.initialized = true;
        }

        // Update the authoritative state
        if (ctx.playerChunk.x != state_.baseX) {
            state_.baseX = ctx.playerChunk.x;
            // baseZ intentionally preserved -- forward wave progress is kept
            state_.maxCenterWaveRequested = -1;
            state_.maxFanWaveRequested = -1;
        }

        if (ctx.playerChunk.z != state_.baseZ) {
            state_.baseZ = ctx.playerChunk.z;
        }

        // Request chunks based on (X % 3 == 0 || Z % 3 == 0)
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
        // Designate the first set of chunkCoords to be generated.
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

        bool progressed = false;
        int iter = 0;

        ChunkCoord nearestCenter = procgen::centerOfRegion_linearPolicy_wrap(ctx.playerChunk.x, ctx.playerChunk.z);

        std::printf("Nearest Center: {%d, %d}\n", nearestCenter.x, nearestCenter.z);

        do {
            progressed = false;

            //if (iter++ == policy_.kMaxRequests) { break; }
            // Updated Algo:
            /// Get the players chunk X/Z
            /// 
            /// - Using the new algo, derive the closest central (chunkX/Z%3 == 0) chunk based on the player's region
            /// - 
            /// 
            /// Invariant: We only request chunks from centers that are at least 3 units apart

            // Rule 1:
            // Request the two side fan chunks for that wave once the center is ready enough.
            const int nextFanWave = state_.maxFanWaveRequested + 1;
            if (nextFanWave <= state_.maxCenterWaveRequested) {

                // Based on the player's current state_.baseX/Z - Create a LinearWaveCoords based on that location
                const LinearWaveCoords wave = makeLinearWave(nextFanWave, state_.baseX, state_.baseZ);

                // Check that an entire 5x5 region has completed up to the spatial grid
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

                // Check that an entire 5x5 region has completed up to the spatial grid
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
            Logger::log(1, "[LinearFlightStreamer] Adding Request Cmd for {%d, %d}", coord.x, coord.z);
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