#include <cstdio>
#include <memory>
#include <mutex>
#include "TerrainController.h"
//#include "Module/Procgen/Rendering/BasicTerrainAsset.h"
#include "Module/Procgen/Terrain/ChunkManager.h"
#include "Module/Procgen/Rendering/BasicTerrainAsset.h"


namespace aveng {
	
	TerrainController::TerrainController(ChunkManager& chunks) noexcept 
		: chunks_(&chunks) 
	{
        // Clearly, the Controller must outlive the Chunk Manager
		std::printf("[%s] Constructing TerrainController\n", __FUNCTION__);
        chunks_->initManagers(&erosionMgr_);
	}

    // TODO - this isn't the only mgr which will need a frame index. Put it in 1 place and read it every frame.
    void TerrainController::setFrameIndex(uint64_t frameIndex) noexcept {
        frameIndex_ = frameIndex;
    }

    //std::unique_ptr<procgen::TerrainRenderable> const& TerrainController::lastRequestedRenderable() const noexcept {
    //    return lastRequested_;
    //}

    ChunkCoord TerrainController::offsetCoord(ChunkCoord base, int dx, int dz) noexcept {
        // Assumes ChunkCoord has members named x and z.
        // If yours are different, adjust here.
        base.x += dx;
        base.z += dz;
        return base;
    }

    void TerrainController::generateChunks(ChunkCoord start_coord, int cols, int rows) {

        std::printf("Generating Chunks...\nStart: {%d, %d} \n", start_coord.x, start_coord.z);

        ensureChunkRequested(start_coord);
        return;
        
    }

    void TerrainController::ensureChunkRequested(ChunkCoord coord)
    {
        auto& slot = slots_[coord];
        slot.coord = coord;

        if (slot.state == TerrainRuntimeState::Requested ||
            slot.state == TerrainRuntimeState::CpuReady ||
            slot.state == TerrainRuntimeState::Uploading ||
            slot.state == TerrainRuntimeState::Resident)
        {
            return;
        }

        slot.requestId = chunks_->requestRenderableAsync(coord, frameIndex_);
        slot.state = TerrainRuntimeState::Requested;
    }

    void TerrainController::drainCompletedTerrain()
    {
        chunks_->drainCompletedRenderables([this](const procgen::RenderableCompletion& completedChunk) {

            auto it = slots_.find(completedChunk.coord);
            if (it == slots_.end()) {
                return; // no longer desired
            }

            auto& slot = it->second;

            // Ignore stale completions
            if (slot.requestId != completedChunk.requestId) {
                return;
            }

            if (!completedChunk.success) {
                slot.state = TerrainRuntimeState::Failed;
                return;
            }

            std::unique_ptr<procgen::TerrainRenderable> cpuRenderable;
            /**
             *   Note: the streaming architecture doesn't need the ChunkRecord to cache its renderable at all
             *   if we never revisit old chunks. In fact, that would make tryTakeRenderable unnecessary
             *   as we could simply deliver the renderable into the RenderableCompletion directly.
             *   The queue's ownership transfers would become larger, however.
             */
            if (!chunks_->tryTakeRenderable(completedChunk.coord, completedChunk.requestId, cpuRenderable)) {
                return; // could log this; usually indicates stale or already consumed
            }

            slot.cpuRenderable = std::move(cpuRenderable);
            slot.state = TerrainRuntimeState::CpuReady;
        });
    }

    // For higher perf, no cache lookups on renderables - Direct access on TerrainRenderable
    //void TerrainController::drainCompletedTerrainALT()
    //{
    //    chunks_->drainCompletedRenderables([this](RenderableCompletion& done) {
    //        auto it = slots_.find(done.coord);
    //        if (it == slots_.end()) return;

    //        auto& slot = it->second;
    //        if (slot.requestId != done.requestId) return;

    //        if (!done.success || !done.renderable) {
    //            slot.state = TerrainRuntimeState::Failed;
    //            return;
    //        }

    //        slot.cpuRenderable = std::move(done.renderable);
    //        slot.state = TerrainRuntimeState::CpuReady;
    //    });
    //}



}