#include <cstdio>
#include "TerrainController.h"
#include "Module/Procgen/Terrain/ChunkManager.h"


namespace aveng {
	
	TerrainController::TerrainController(ChunkManager& chunks) noexcept 
		: chunks_(&chunks) 
	{
		std::printf("[%s] Constructing TerrainController\n", __FUNCTION__);
	}

    // TODO - this isn't the only mgr which will need a frame index. Put it in 1 place and read it every frame.
    void TerrainController::setFrameIndex(uint64_t frameIndex) noexcept {
        frameIndex_ = frameIndex;
    }

    std::vector<std::shared_future<FinalMeshCPU const*>> const& TerrainController::lastRequestedMeshes() const noexcept {
        return lastRequested_;
    }

    ChunkCoord TerrainController::offsetCoord(ChunkCoord base, int dx, int dz) noexcept {
        // Assumes ChunkCoord has members named x and z.
        // If yours are different, adjust here.
        base.x += dx;
        base.z += dz;
        return base;
    }

    void TerrainController::generateChunks(ChunkCoord start_coord, int cols, int rows) {
        if (!chunks_ || cols <= 0 || rows <= 0) {
            lastRequested_.clear();
            return;
        }

        std::printf("[%s] ChunkCoord{%d, %d}", __FUNCTION__, start_coord.x, start_coord.z);
        chunks_->requestMesh(start_coord, frameIndex_);
        return;
        
        //lastRequested_.clear();
        //lastRequested_.reserve(static_cast<size_t>(cols) * static_cast<size_t>(rows));

        //// Convention: cols increases +x, rows increases +z (adjust if your grid differs).
        //for (int r = 0; r < rows; ++r) {
        //    for (int c = 0; c < cols; ++c) {
        //        const ChunkCoord coord = offsetCoord(start_coord, c, r);

        //        // This schedules the whole pipeline via call_once inside ChunkManager::requestMesh.
        //        // It should be non-blocking unless requestMesh itself blocks (it shouldn't).
        //        // lastRequested_.push_back(chunks_->test(coord, frameIndex_));
        //        chunks_->test(coord, frameIndex_);
        //    }
        //}
    }
}