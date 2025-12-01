#include "AvengFrame.h"
#ifdef ENABLE_EDITOR
#include "Editor.h"
#endif
#include "Core/Renderer/Renderer.h"
#include <cassert>


namespace aveng {

	AvengFrame::AvengFrame(Renderer& renderer,
		VkRenderData& renderData,
		GameData& gameData,
		EngineDevice& engineDevice,
		ModelAndInstanceData& modelInstanceData,
		Editor* editor)
		: renderer{ renderer }
		, renderData{ renderData }
		, gameData { gameData }
		, engineDevice{ engineDevice }
		, modelInstanceData{ modelInstanceData }
		, pEditor{ editor }
	{
	}

#ifdef ENABLE_EDITOR
	bool AvengFrame::hasEditorSelection()
	{
		return pEditor->hasSelection();
	}
#endif

	bool AvengFrame::render(float deltaTime)
	{

		// Clear the vector of buffers we'll be submitting to the graphics queue
		commandBuffers.clear();

		// Load new models if any are pending. Side-Effect: Creates descriptor sets for this model
		renderer.processPendingModelLoads();

		// Wait for fences, vkAcquireNextImageKH
		if (!renderer.beginFrame())
		{
			// Window was resized
			return false;
		}

  		int currentFrameIndex = renderer.getFrameIndex();

		/* start graphics rendering */
		result = vkResetFences(engineDevice.device(), 1, &renderData.rdRenderFence[currentFrameIndex]);
		if (result != VK_SUCCESS) {
			std::printf("%s error:  fence reset failed (error: %i)\n", __FUNCTION__, result);
			throw std::runtime_error("Frame Failure 0");
		}

		//if (pEditor->hasClicked()) {
		//	std::cout << "2 -----------[EDITOR DEBUG BEGIN]---------" << std::endl;
		//	pEditor->debug();
		//	std::cout << "3 -----------[EDITOR DEBUG END]---------" << std::endl;
		//}

		renderer.updateCamera();
		/**
		* Update Model Buffer Data - Does not record commands
		* Side-effects: Buffers resizes cause descriptor sets to update
		*/
		renderer.draw(deltaTime);

#ifdef ENABLE_EDITOR
		if (gameData.currentAppMode == AppMode::Editor) {

			if (hasEditorSelection()) {
				// The editor always updates its descriptors before each frame

				// Validation errors are occuring once here during the pipeline transition
				// bcause we're updating both descriptors in one go while there could be 
				// another cmd buffer executing in the background. This loop should only update
				// One frame's descriptor set(s) at a time
				pEditor->updateDescriptorSets(currentFrameIndex);
			}

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
		if (gameData.currentAppMode == AppMode::Editor && hasEditorSelection())
		{
			// Begin model + selection renderpass
			renderer.beginSwapChainRenderPass(
				renderData.rdCommandBuffersGraphics[currentFrameIndex], 
				renderer.getCurrentSelectionFramebuffer(),
				renderer.getSelectionRenderPass(),
				true);

		}
		else {
#endif
			// Begin the basic model rendering renderpass
			renderer.beginSwapChainRenderPass(
				renderData.rdCommandBuffersGraphics[currentFrameIndex],
				renderer.getCurrentFramebuffer(), // 
				renderer.getSwapChainRenderPass()
			);

#ifdef ENABLE_EDITOR
		}

		if (gameData.currentAppMode == AppMode::Editor && hasEditorSelection()) {

			// This does the exact same thing as renderer.drawModels, but with the editor's pipeline/framebuffers/renderpass.
			pEditor->drawSelectedModels(currentFrameIndex); 
			
		}
		else {
#endif
			renderer.drawModels(
				renderData.rdCommandBuffersGraphics[currentFrameIndex],
				renderData.rdAvengPipeline,
				renderData.rdAvengAnimationPipeline,
				renderData.rdAvengPipelineLayout,
				renderData.rdAvengAnimationPipelineLayout,
				renderData.rdAvengDescriptorSets[currentFrameIndex],
				renderData.rdAvengAnimationDescriptorSets[currentFrameIndex],
				currentFrameIndex);

#ifdef ENABLE_EDITOR
		}
#endif

		// End model drawing renderpass (normal or selection-enabled)
		renderer.endSwapChainRenderPass(renderData.rdCommandBuffersGraphics[currentFrameIndex]);
		renderer.endGraphicsCommands(currentFrameIndex);

#ifdef ENABLE_EDITOR
		
		pEditor->beginGUICommands(currentFrameIndex);

		// Begin the ImGUI renderpass
		renderer.beginSwapChainRenderPass(
			renderData.rdGUICommandBuffers[currentFrameIndex],
			renderer.getCurrentFramebuffer(), // 
			renderData.rdImguiRenderpass
		);

		if (gameData.currentAppMode == AppMode::Editor) {

			// This is where the editor updates its current frame index - TODO - Maybe this index is better passed into it as an arg here
			pEditor->renderGUI(deltaTime);
		}

		// End ImGUI renderpass & Command recording
		pEditor->endGUIRenderPass(renderData.rdGUICommandBuffers[currentFrameIndex]);
		pEditor->endGUICommands(currentFrameIndex);
#endif

		// First in queue - Graphics commands
		commandBuffers.push_back(renderData.rdCommandBuffersGraphics[currentFrameIndex]);

#ifdef ENABLE_EDITOR

		if (gameData.currentAppMode == AppMode::Editor && hasEditorSelection()) {

			// NOTE: drawInstanceGizmo begins / ends its own renderpass
			if (pEditor->drawInstanceGizmo()) {

				// Next in queue - Line drawing commands
				commandBuffers.push_back(renderData.rdLineCommandBuffers[currentFrameIndex]);
			
			}
		}

		// Last in Queue - ensures the editor always renders on top
		commandBuffers.push_back(renderData.rdGUICommandBuffers[currentFrameIndex]);
#endif

		/* submit command buffer */
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		std::vector<VkSemaphore> waitSemaphores = { renderData.rdComputeSemaphore[currentFrameIndex], renderData.rdPresentSemaphore[currentFrameIndex] };
		std::vector<VkPipelineStageFlags> waitStages = { VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

		/* compute shader: contine if in vertex input ready
		 * vertex shader: wait for color attachment output ready */
		submitInfo.pWaitDstStageMask = waitStages.data();
		submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
		submitInfo.pWaitSemaphores = waitSemaphores.data();

		std::vector<VkSemaphore> signalSemaphores = { renderData.rdRenderSemaphore[currentFrameIndex], renderData.rdGraphicSemaphore[currentFrameIndex] };

		submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
		submitInfo.pSignalSemaphores = signalSemaphores.data();
		submitInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
		submitInfo.pCommandBuffers = commandBuffers.data();

		result = vkQueueSubmit(engineDevice.graphicsQueue(), 1, &submitInfo, renderData.rdRenderFence[currentFrameIndex]);
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
		presentInfo.pWaitSemaphores = &renderData.rdRenderSemaphore[currentFrameIndex];

		VkSwapchainKHR swapchain = renderer.getVkSwapchain();
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapchain;
		presentInfo.pImageIndices = renderer.pGetCurrentImageIndex();

		result = vkQueuePresentKHR(engineDevice.presentQueue(), &presentInfo);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
			renderer.recreateSwapChain();
		}
		else {
			if (result != VK_SUCCESS) {
				std::printf("%s error: failed to present swapchain image\n", __FUNCTION__);
				throw std::runtime_error("Frame Failure 2");
			}
		}

		renderer.endFrame();

	}
}