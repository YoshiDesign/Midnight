#include "AvengFrame.h"
#ifdef ENABLE_EDITOR
#include "Editor/Editor.h"
#endif
#include "Core/Renderer/ModelLibrary.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "Core/Renderer/Renderer.h"
#include "Services/IRenderSceneView.h"
#include "CoreVK/Resources/platform.h"
#include <cassert>

namespace aveng {

	AvengFrame::AvengFrame(Renderer& renderer,
		ModelLibrary& modelLibrary,
		IRenderSceneView& sceneView, // TODO: Interface. I don't enjoy the fact that this is an interface
		VkRenderData& renderData,
		GameData& gameData,
		EngineDevice& engineDevice,
		Editor* editor)
		: renderer{ renderer }
		, sceneView_{ sceneView }
		, renderData{ renderData }
		, gameData { gameData }
		, engineDevice{ engineDevice }
		, pEditor{ editor }
		, modelLib__{ modelLibrary } // TODO ...?
	{
		/* framePacketBuilder_ dependencies */
		framePacketBuilder_.setModelQuery(&modelLib__.query());
		framePacketBuilder_.setAnimQuery(&modelLib__.animQuery());
		framePacketBuilder_.setFramesInFlight(SwapChain::MAX_FRAMES_IN_FLIGHT);
	}

#ifdef ENABLE_EDITOR
	bool AvengFrame::hasEditorSelection()
	{
		return pEditor->hasSelection();
	}
#endif

	// 
	int AvengFrame::currentFrameIndex() { return renderer.getFrameIndex(); }

	// 
	void AvengFrame::reset_timers()
	{
		renderData.rdFramePacketTime = 0.0f;
	}

	//
	bool AvengFrame::start_frame() {

		mFrameTimer.start();

		// Clear the Frame's vector of command buffer handles that we'll be submitting to the graphics queue
		commandBuffers.clear();

		// Wait for fences, vkAcquireNextImageKH
		if (!renderer.beginFrame())
		{
			// Window was resized
#ifdef ENABLE_EDITOR
			pEditor->recreateFrameBuffers(renderer.pGetSwapChain());
#endif
			return false;
		}

		/* start graphics rendering */
		// result = vkResetFences(engineDevice.device(), 1, &renderData.rdRenderFence.at(currentFrameIndex));
		// if (result != VK_SUCCESS) {
		// 	std::printf("%s error:  fence reset failed (error: %i)\n", __FUNCTION__, result);
		// 	throw std::runtime_error("Frame Failure 0");
		// }

		// GC
		renderer.destroyTrash();

		modelLib__.processPendingUnloads();
		modelLib__.processPendingModelLoads(renderer.getFrameIndex());

		renderer.updateCamera();
	}
	
	// 
	FramePacket& AvengFrame::frame_packet(float deltaTime, int currentFrameIndex) {

#ifdef M_DEBUG
		reset_timers();
#endif

		auto stat = sceneView_.staticPoolInputs();
		auto anim = sceneView_.animatedPoolInputs();
		auto& stat_slots = *stat.slots;
		auto& anim_slots = *anim.slots;

		// animatedModelLoaded = anim_slots.size() > 0;

		// Fetch the next frame packet and construct it
		return framePacketBuilder_.build<AvengInstance, AssimpInstance>(
				stat,
				anim,
				currentFrameIndex,
				0, // tmp/unused frame number
				deltaTime,
				FramePacketBuildOptions{} // opts
#ifdef M_DEBUG
				, renderData
#endif
			);

	}

	//
	bool AvengFrame::render(float deltaTime)
	{

		if (!start_frame()) { return false;  }

		const int currentFrameIndex = renderer.getFrameIndex();


		// Build Frame Packet using AssetLibrary
		const auto& pkt = frame_packet(deltaTime, currentFrameIndex);

		/**
		* Update Model Buffer Data - Does not record commands
		* Side-effects: If Buffers resize, this causes descriptor sets to update
		*/
		int drawResult = renderer.update(pkt, modelLib__, deltaTime);
		
		// Not Fatal upon failure
		// Note: We check this BEFORE resetting the render fence to avoid deadlock.
		if (drawResult == WTF_BOOM || !renderer.isFrameInProgress()) {
			std::printf("%s: draw() dropped frame, aborting render\n", __FUNCTION__);
			renderer.endFrame();
			return false;
		}

		// Compute bone transforms for skinning mat's
		int animComputeResult = renderer.dispatchCompute(modelLib__, pkt);
		if (animComputeResult == WTF_BOOM) {
			// Terrible sync issue. Die.
			throw std::runtime_error("Failed to dispatch animation compute.");
		}

		/* start graphics rendering - reset fence only after we're committed to submitting */
		result = vkResetFences(engineDevice.device(), 1, &renderData.rdRenderFence.at(currentFrameIndex));
		if (result != VK_SUCCESS) {
			std::printf("%s error:  fence reset failed (error: %i)\n", __FUNCTION__, result);
			throw std::runtime_error("Frame Failure 0");
		}

#ifdef ENABLE_EDITOR
		if (gameData.currentAppMode == AppMode::Editor) {

			pEditor->destroyTrash();

			// if (hasEditorSelection()) {
				// The editor always updates its descriptors before each frame

				// Validation errors are occuring once here during the pipeline transition
				// bcause we're updating both descriptors in one go while there could be 
				// another cmd buffer executing in the background. This loop should only update
				// One frame's descriptor set(s) at a time
				// pEditor->updateDescriptorSets(currentFrameIndex);
			//}

			/**
			* Update Model Buffer Data - Does not record commands
			* Side-effects: Buffers resizes cause descriptor sets to update (again)
			*/

			pEditor->update(deltaTime, currentFrameIndex);

		}
#endif 

		// Start graphics command recording
		renderer.beginGraphicsCommands(currentFrameIndex);

#ifdef ENABLE_EDITOR
		if (gameData.currentAppMode == AppMode::Editor)
		{
		
			// Begin model + selection renderpass
			renderer.beginSwapChainRenderPass(
				renderData.rdCommandBuffersGraphics.at(currentFrameIndex), 
				renderer.getCurrentSelectionFramebuffer(),
				renderer.getSelectionRenderPass(),
				true);

		}
		else {
#endif

			// Begin the basic model rendering renderpass
			renderer.beginSwapChainRenderPass(
				renderData.rdCommandBuffersGraphics.at(currentFrameIndex),
				renderer.getCurrentFramebuffer(), // 
				renderer.getSwapChainRenderPass()
			);

#ifdef ENABLE_EDITOR
		}

		if (gameData.currentAppMode == AppMode::Editor) {

			// Editor takes over lighting
			pEditor->renderLights();

			// This does the exact same thing as renderer.drawModels, but with the editor's pipeline/framebuffers/renderpass.
			pEditor->drawModels(modelLib__, pkt, currentFrameIndex); 
			
		}
		else {
#endif

			renderer.renderLights();

			renderer.drawModels(
				framePacketBuilder_.getFramePacket(currentFrameIndex),
				modelLib__,
				renderData.rdCommandBuffersGraphics.at(currentFrameIndex),
				renderData.rdAvengPipeline,
				renderData.rdAvengAnimationPipeline,
				renderData.rdAvengPipelineLayout,
				renderData.rdAvengAnimationPipelineLayout,
				renderData.rdAvengDescriptorSets.at(currentFrameIndex),
				renderData.rdAvengAnimationDescriptorSets.at(currentFrameIndex),
				currentFrameIndex);

#ifdef ENABLE_EDITOR
		}
#endif

		// End model drawing renderpass (normal or selection-enabled)
		renderer.endSwapChainRenderPass(renderData.rdCommandBuffersGraphics.at(currentFrameIndex));
		renderer.endGraphicsCommands(currentFrameIndex);

#ifdef ENABLE_EDITOR
		
		if (gameData.currentAppMode == AppMode::Editor) {

			pEditor->beginGUICommands(currentFrameIndex);

			// Begin the ImGUI renderpass
			renderer.beginSwapChainRenderPass(
				renderData.rdGUICommandBuffers.at(currentFrameIndex),
				renderer.getCurrentFramebuffer(), // 
				renderData.rdImguiRenderpass
			);

			// This is where the editor updates its current frame index - TODO - Maybe this index is better passed into it as an arg here
			pEditor->renderGUI(deltaTime);
			// End ImGUI renderpass & Command recording
			pEditor->endGUIRenderPass(renderData.rdGUICommandBuffers.at(currentFrameIndex));
			pEditor->endGUICommands(currentFrameIndex);

		}

#endif

		// First in queue - Graphics commands
		commandBuffers.push_back(renderData.rdCommandBuffersGraphics.at(currentFrameIndex));

#ifdef ENABLE_EDITOR

		if (gameData.currentAppMode == AppMode::Editor) {

			// NOTE: drawInstanceGizmo begins / ends its own renderpass
			if (pEditor->drawInstanceGizmo()) {

				// Next in queue - Line drawing commands
				commandBuffers.push_back(renderData.rdLineCommandBuffers.at(currentFrameIndex));
			
			}

			// Last in Queue - ensures the editor always renders on top
			commandBuffers.push_back(renderData.rdGUICommandBuffers.at(currentFrameIndex));
		}
#endif

		end_frame(pkt, currentFrameIndex);

	}

	void AvengFrame::end_frame(const FramePacket& pkt, int currentFrameIndex) {

		/*
		* TODO: The logic below can be cleaned up based on whether or not
		* the graphics and compute queues belong to the same family.
		* Note the use of engineDevice.sameGraphicsComputeQueue(). When this
		* is the case, we can take a no-semaphore, no-cross-submission approach.
		*
		* This has higher reaching implications. If graphics and compute use the same queue family:
		* - Record a single primary graphics command buffer.
		* - Update to vkCmdPipelineBarrier2 while you're at it...
		* - Keep the secondary compute command buffer but don't apply
		*
		* I still need to understand this better. But to reiterate:
		* - vkCmdPipelineBarrier creates a dependency for commands before/after it in the same command buffer
		* This is why we use semaphores to sync with compute shader read/writes
		*/

		/* submit command buffer */
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		// When graphics and compute use the same queue, skip compute semaphore sync
		// Submission order on the same queue already guarantees compute finishes before graphics uses its output
		std::vector<VkSemaphore> waitSemaphores; // TODO: Move these to member var's - we're allocating vectors every frame!!
		std::vector<VkPipelineStageFlags> waitStages;

		if (!engineDevice.sameGraphicsComputeQueue()) {
			// Different queues: wait for compute semaphore
			waitSemaphores.push_back(renderData.rdComputeSemaphore.at(currentFrameIndex));
			waitStages.push_back(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT); // not VK_PIPELINE_STAGE_VERTEX_INPUT_BIT
		}
		// Always wait for present semaphore (swapchain image acquisition)
		waitSemaphores.push_back(renderData.rdPresentSemaphore.at(currentFrameIndex));
		waitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

		submitInfo.pWaitDstStageMask = waitStages.data();
		submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
		submitInfo.pWaitSemaphores = waitSemaphores.data();

		// Signal semaphores: always signal render semaphore for present
		// Only signal graphics semaphore if using separate queues (for compute to wait on)
		std::vector<VkSemaphore> signalSemaphores; // TODO
		signalSemaphores.push_back(renderData.rdRenderSemaphore.at(currentFrameIndex));
		if (!engineDevice.sameGraphicsComputeQueue()) {
			signalSemaphores.push_back(renderData.rdGraphicSemaphore.at(currentFrameIndex));
		}

		submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
		submitInfo.pSignalSemaphores = signalSemaphores.data();
		submitInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
		submitInfo.pCommandBuffers = commandBuffers.data();

		result = vkQueueSubmit(engineDevice.graphicsQueue(), 1, &submitInfo, renderData.rdRenderFence.at(currentFrameIndex));
		if (result != VK_SUCCESS) {
			std::printf("%s error: failed to submit draw command buffer (%i)\n", __FUNCTION__, result);
			throw std::runtime_error("Frame Failure 1");
		}

#ifdef ENABLE_EDITOR
		if (gameData.currentAppMode == AppMode::Editor) {

			// Get the selected instance ID after the frag shader writes it to the Selection FrameBuffer texture
			// Side-Effect : Sets editor's clicked flag to false
			pEditor->readPixelDataPos(pkt);
		}
#endif
		/* trigger swapchain image presentation */
		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &renderData.rdRenderSemaphore.at(currentFrameIndex);

		VkSwapchainKHR swapchain = renderer.getVkSwapchain();
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapchain;
		presentInfo.pImageIndices = renderer.pGetCurrentImageIndex();

		result = vkQueuePresentKHR(engineDevice.presentQueue(), &presentInfo);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
			renderer.recreateSwapChain();
#ifdef ENABLE_EDITOR
			pEditor->recreateFrameBuffers(renderer.pGetSwapChain());
#endif
			// return false;
		}
		else if (result != VK_SUCCESS) {
			std::printf("%s error: failed to present swapchain image\n", __FUNCTION__);
			throw std::runtime_error("Frame Failure 2");
		}
		vkDeviceWaitIdle(engineDevice.device()); // TEMPORARY - removes all parallelism

		renderer.endFrame();
		renderData.rdFrameTime = mFrameTimer.stop();

	}

}