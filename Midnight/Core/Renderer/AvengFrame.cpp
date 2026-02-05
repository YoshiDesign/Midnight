#include "AvengFrame.h"
#ifdef ENABLE_EDITOR
#include "Editor/Editor.h"
#endif
#include "Core/Renderer/ModelLibrary.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "Core/Renderer/Renderer.h"
#include "Services/IRenderSceneView.h"
#include <cassert>

namespace aveng {

	AvengFrame::AvengFrame(Renderer& renderer,
		ModelLibrary& modelLibrary,
		IRenderSceneView& sceneView,
		const IModelLibrary& modelLib,
		VkRenderData& renderData,
		GameData& gameData,
		EngineDevice& engineDevice,
		Editor* editor)
		: renderer{ renderer }
		, sceneView_{ sceneView }
		, modelLib_{ modelLib }
		, renderData{ renderData }
		, gameData { gameData }
		, engineDevice{ engineDevice }
		, pEditor{ editor }
		, modelLib__{ modelLibrary }
	{
	}

#ifdef ENABLE_EDITOR
	bool AvengFrame::hasEditorSelection()
	{
		return pEditor->hasSelection();
	}
#endif

	int AvengFrame::currentFrameIndex() { return renderer.getFrameIndex(); }

	bool AvengFrame::render(float deltaTime)
	{

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

  		int currentFrameIndex = renderer.getFrameIndex();

		/* start graphics rendering */
		// result = vkResetFences(engineDevice.device(), 1, &renderData.rdRenderFence.at(currentFrameIndex));
		// if (result != VK_SUCCESS) {
		// 	std::printf("%s error:  fence reset failed (error: %i)\n", __FUNCTION__, result);
		// 	throw std::runtime_error("Frame Failure 0");
		// }

		// GC
		renderer.destroyTrash();

		modelLib__.processPendingUnloads();
		modelLib__.processPendingModelLoads();

		renderer.updateCamera();

		/**
		* Update Model Buffer Data - Does not record commands
		* Side-effects: Buffers resizes cause descriptor sets to update
		*/
		int drawResult = renderer.draw(sceneView_, modelLib_, deltaTime);
		
		// If draw() returned early (e.g., deltaTime == 0 or frame not started),
		// the compute semaphore was never signaled. We must skip this frame entirely
		// to avoid graphics queue waiting on an unsignaled semaphore.
		// NOTE: We check this BEFORE resetting the render fence to avoid deadlock.
		if (drawResult == 0 || !renderer.isFrameInProgress()) {
			std::printf("%s: draw() skipped frame, aborting render\n", __FUNCTION__);
			renderer.endFrame();
			return false;
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
				pEditor->updateDescriptorSets(currentFrameIndex);
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
			pEditor->updateLights();

			// This does the exact same thing as renderer.drawModels, but with the editor's pipeline/framebuffers/renderpass.
			pEditor->drawModels(modelLib_, currentFrameIndex); 
			
		}
		else {
#endif

			renderer.updateLights();

			renderer.drawModels(
				modelLib_,
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

		/* submit command buffer */
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		// When graphics and compute use the same queue, skip compute semaphore sync
		// Submission order on the same queue already guarantees compute finishes before graphics uses its output
		std::vector<VkSemaphore> waitSemaphores;
		std::vector<VkPipelineStageFlags> waitStages;
		
		if (!engineDevice.sameGraphicsComputeQueue()) {
			// Different queues: wait for compute semaphore
			waitSemaphores.push_back(renderData.rdComputeSemaphore.at(currentFrameIndex));
			waitStages.push_back(VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);
		}
		// Always wait for present semaphore (swapchain image acquisition)
		waitSemaphores.push_back(renderData.rdPresentSemaphore.at(currentFrameIndex));
		waitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

		submitInfo.pWaitDstStageMask = waitStages.data();
		submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
		submitInfo.pWaitSemaphores = waitSemaphores.data();

		// Signal semaphores: always signal render semaphore for present
		// Only signal graphics semaphore if using separate queues (for compute to wait on)
		std::vector<VkSemaphore> signalSemaphores;
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
			pEditor->readPixelDataPos();
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

	}
}