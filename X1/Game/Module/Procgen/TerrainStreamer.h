#pragma once
#include "Runtime/Play/Controller/TerrainController.h"
#include <Module/Procgen/Types.h>
#include "Core/Camera/aveng_camera.h"
#include "Utils/glm_includes.h"

namespace xone {

    // Use this if you need to debug, otherwise we just use LinearWaveCoords
    //struct LinearWave {
    //    aveng::ChunkCoord center;
    //    aveng::ChunkCoord leftFan;
    //    aveng::ChunkCoord rightFan;

    //    bool centerRequested = false;
    //    bool leftRequested = false;
    //    bool rightRequested = false;

    //    bool leftUnlocked = false;
    //    bool rightUnlocked = false;
    //    bool nextCenterUnlocked = false;
    //};

    struct LinearWaveCoords {
        aveng::ChunkCoord center;
        aveng::ChunkCoord leftFan;
        aveng::ChunkCoord rightFan;
    };

    inline LinearWaveCoords makeLinearWave(int k, int baseX, int baseZ) {
        const int z = baseZ + 3 * k;
        return {
            { baseX, z },
            { baseX - 2, z + 2 },
            { baseX + 2, z + 2 }
        };
    }

    class LinearFlightStreamer {
    public:
        explicit LinearFlightStreamer(const aveng::LinearStreamPolicy& policy = {})
            : policy_(policy) {
        }

        void setPolicy(const aveng::LinearStreamPolicy& policy) { policy_ = policy; }
        void reset();

        void update(
            const aveng::StreamUpdateContext& ctx,
            aveng::TerrainController& terrain,
            std::unordered_map<aveng::ChunkCoord,
            aveng::StreamedChunkState,
            aveng::ChunkCoordHash>& streamed,
            aveng::StreamCommandBuffer& outCmds
        );

    private:
        void requestInitialCenter(
            std::unordered_map<aveng::ChunkCoord,
            aveng::StreamedChunkState,
            aveng::ChunkCoordHash>& streamed,
            aveng::StreamCommandBuffer& outCmds
        );

        void advanceFrontier(
            const aveng::StreamUpdateContext& ctx,
            aveng::TerrainController& terrain,
            std::unordered_map<aveng::ChunkCoord,
            aveng::StreamedChunkState,
            aveng::ChunkCoordHash>& streamed,
            aveng::StreamCommandBuffer& outCmds
        );

        void enqueueEvictions(
            aveng::ChunkCoord playerChunk,
            std::unordered_map<aveng::ChunkCoord,
            aveng::StreamedChunkState,
            aveng::ChunkCoordHash>& streamed,
            aveng::StreamCommandBuffer& outCmds
        );

        bool shouldEvict(aveng::ChunkCoord c, aveng::ChunkCoord playerChunk) const;

        void evictChunks(aveng::StreamCommandBuffer& outCmds, aveng::TerrainController& terrain);

        static void requestIfNeeded(
            aveng::ChunkCoord coord,
            std::unordered_map<aveng::ChunkCoord,
            aveng::StreamedChunkState,
            aveng::ChunkCoordHash>& streamed,
            aveng::StreamCommandBuffer& outCmds
        );

    private:
        aveng::LinearStreamPolicy policy_;
        aveng::LinearFlightState state_{};
    };

    class AllRangeStreamer {
    public:
        explicit AllRangeStreamer(const aveng::AllRangeStreamPolicy& policy = {})
            : policy_(policy) {
        }

        void setPolicy(const aveng::AllRangeStreamPolicy& policy) { policy_ = policy; }
        void reset();

        void update(
            const aveng::StreamUpdateContext& ctx,
            aveng::TerrainController& terrain,
            std::unordered_map<aveng::ChunkCoord,
            aveng::StreamedChunkState,
            aveng::ChunkCoordHash>& streamed,
            aveng::StreamCommandBuffer& outCmds
        );

    private:
        aveng::AllRangeStreamPolicy policy_;
    };

    class TerrainStreamer {
    public:
        TerrainStreamer(
            aveng::TerrainController& terrain,
            const aveng::TerrainStreamPolicy& policy = {}
        );

        void setPolicy(const aveng::TerrainStreamPolicy& policy);
        void reset();

        void update(const aveng::AvengCamera& cam, uint64_t frameIndex);
        aveng::ChunkCoord worldToChunk(glm::vec3 pos) const;

    private:
        void applyRequests(const std::vector<aveng::ChunkCoord>& centers, uint64_t frameIndex);
        void applyEvictions(const std::vector<aveng::ChunkCoord>& evictCenters);

    private:
        aveng::TerrainController& terrain_;
        aveng::TerrainStreamPolicy policy_;
        LinearFlightStreamer linear_;
        AllRangeStreamer allRange_;
        std::unordered_map<aveng::ChunkCoord, aveng::StreamedChunkState, aveng::ChunkCoordHash> streamed_;
        std::vector<aveng::ChunkCoord> pendingEvictions_;
    };

}