#include "TerrainStreamer.h"

namespace xone {

    /*
    * Operational Success Criteria:
    *   update() is called every frame
    *   Terrain.hasAllPointsReady(coord) eventually flips to true for requested chunks.
    *   Streamed remains the authoritative dedupe set for already-requested centers.
    *   MakeLinearWave(n) produces the intended forward progression pattern.
    *   Eviction does not remove chunks too aggressively before downstream waves can use them.
    */

    TerrainStreamer::TerrainStreamer(
        aveng::TerrainController& terrain,
        const aveng::TerrainStreamPolicy& policy
    )
        : terrain_(terrain)
        , policy_(policy)
        , linear_(policy.linear)
        , allRange_(policy.allRange)
    {
    }

    void TerrainStreamer::setPolicy(const aveng::TerrainStreamPolicy& policy)
    {
        policy_ = policy;
        linear_.setPolicy(policy_.linear);
        allRange_.setPolicy(policy_.allRange);
        reset();
    }

    void TerrainStreamer::reset()
    {
        streamed_.clear();
        linear_.reset();
        allRange_.reset();
    }

    void TerrainStreamer::update(const aveng::AvengCamera& cam, uint64_t frameIndex)
    {
        aveng::StreamUpdateContext ctx{
            worldToChunk(cam.transform().translation),
            frameIndex
        };

        aveng::StreamCommandBuffer cmds{};

        switch (policy_.mode) {
        case aveng::TerrainStreamMode::LinearFlight:
            linear_.update(ctx, terrain_, streamed_, cmds);
            break;

        case aveng::TerrainStreamMode::AllRange:
            allRange_.update(ctx, terrain_, streamed_, cmds);
            break;
        }

        applyRequests(cmds.requestCenters, frameIndex);
        applyEvictions(cmds.evictCenters);
    }

    aveng::ChunkCoord TerrainStreamer::worldToChunk(glm::vec3 pos) const
    {
        constexpr float CHUNK_SIZE = 256.0f;

        // Assuming x/z world plane and chunk coordinates are based on floor division.
        const int cx = static_cast<int>(std::floor(pos.x / CHUNK_SIZE));
        const int cz = static_cast<int>(std::floor(pos.z / CHUNK_SIZE));

        return { cx, cz };
    }

    void TerrainStreamer::applyRequests(const std::vector<aveng::ChunkCoord>& centers, uint64_t frameIndex)
    {
        for (const aveng::ChunkCoord& c : centers) {
            terrain_.generateChunks(c);
        }
    }

    void TerrainStreamer::applyEvictions(const std::vector<aveng::ChunkCoord>& evictCenters)
    {
        for (const aveng::ChunkCoord& c : evictCenters) {
            auto it = streamed_.find(c);
            if (it == streamed_.end()) {
                continue;
            }

            it->second.status = aveng::StreamedChunkState::Status::Evicting;

            // If/when you expose this:
            // terrain_.evictChunk(c);
        }
    }

    void LinearFlightStreamer::reset()
    {
        state_ = {};
    }

    void LinearFlightStreamer::update(
        const aveng::StreamUpdateContext& ctx,
        aveng::TerrainController& terrain,
        std::unordered_map<aveng::ChunkCoord, aveng::StreamedChunkState, aveng::ChunkCoordHash>& streamed,
        aveng::StreamCommandBuffer& outCmds
    )
    {
        requestInitialCenter(streamed, outCmds);
        advanceFrontier(terrain, streamed, outCmds);
        enqueueEvictions(ctx.playerChunk, streamed, outCmds);
    }

    void LinearFlightStreamer::requestInitialCenter(
        std::unordered_map<aveng::ChunkCoord, aveng::StreamedChunkState, aveng::ChunkCoordHash>& streamed,
        aveng::StreamCommandBuffer& outCmds
    )
    {
        if (state_.maxCenterWaveRequested >= 0) {
            return;
        }

        const LinearWaveCoords w0 = makeLinearWave(0);
        requestIfNeeded(w0.center, streamed, outCmds);
        state_.maxCenterWaveRequested = 0;
    }

    void LinearFlightStreamer::advanceFrontier(
        aveng::TerrainController& terrain,
        std::unordered_map<aveng::ChunkCoord, aveng::StreamedChunkState, aveng::ChunkCoordHash>& streamed,
        aveng::StreamCommandBuffer& outCmds
    )
    {
        bool progressed = false;

        do {
            progressed = false;

            // Rule 1:
            // If the next center wave has already been requested and its center is all-points-ready,
            // request the two side fan chunks for that wave.
            const int nextFanWave = state_.maxFanWaveRequested + 1;
            if (nextFanWave <= state_.maxCenterWaveRequested) {
                const LinearWaveCoords wave = makeLinearWave(nextFanWave);

                if (terrain.hasAllPointsReady(wave.center)) {
                    requestIfNeeded(wave.leftFan, streamed, outCmds);
                    requestIfNeeded(wave.rightFan, streamed, outCmds);

                    state_.maxFanWaveRequested = nextFanWave;
                    progressed = true;
                }
            }

            // Rule 2:
            // Once the latest fan pair is all-points-ready, request the next center wave.
            if (state_.maxFanWaveRequested >= 0 &&
                state_.maxCenterWaveRequested == state_.maxFanWaveRequested)
            {
                const LinearWaveCoords currentFanWave = makeLinearWave(state_.maxFanWaveRequested);

                if (terrain.hasAllPointsReady(currentFanWave.leftFan) &&
                    terrain.hasAllPointsReady(currentFanWave.rightFan))
                {
                    const int nextCenterWave = state_.maxCenterWaveRequested + 1;
                    const LinearWaveCoords nextWave = makeLinearWave(nextCenterWave);

                    requestIfNeeded(nextWave.center, streamed, outCmds);
                    state_.maxCenterWaveRequested = nextCenterWave;
                    progressed = true;
                }
            }

        } while (progressed);
    }

    void LinearFlightStreamer::enqueueEvictions(
        aveng::ChunkCoord playerChunk,
        std::unordered_map<aveng::ChunkCoord,
        aveng::StreamedChunkState,
        aveng::ChunkCoordHash>& streamed,
        aveng::StreamCommandBuffer& outCmds
    )
    {
        for (auto& [coord, state] : streamed) {
            if (state.status == aveng::StreamedChunkState::Status::Evicting) {
                continue;
            }

            if (shouldEvict(coord, playerChunk)) {
                outCmds.evictCenters.push_back(coord);
            }
        }
    }

    bool LinearFlightStreamer::shouldEvict(aveng::ChunkCoord c, aveng::ChunkCoord playerChunk) const
    {
        const int dx = std::abs(c.x - playerChunk.x);
        const int dz = std::abs(c.z - playerChunk.z);

        return dx > policy_.evictRadiusX || dz > policy_.evictRadiusZ;
    }

    void LinearFlightStreamer::requestIfNeeded(
        aveng::ChunkCoord coord,
        std::unordered_map<aveng::ChunkCoord, aveng::StreamedChunkState, aveng::ChunkCoordHash>& streamed,
        aveng::StreamCommandBuffer& outCmds
    )
    {
        auto [it, inserted] = streamed.try_emplace(
            coord,
            aveng::StreamedChunkState{ aveng::StreamedChunkState::Status::Requested }
        );

        if (inserted) {
            outCmds.requestCenters.push_back(coord);
        }
    }

    void AllRangeStreamer::reset()
    {
    }

    void AllRangeStreamer::update(
        const aveng::StreamUpdateContext& ctx,
        aveng::TerrainController& terrain,
        std::unordered_map<aveng::ChunkCoord, aveng::StreamedChunkState, aveng::ChunkCoordHash>& streamed,
        aveng::StreamCommandBuffer& outCmds
    )
    {
        // Stub for later.
    }

}