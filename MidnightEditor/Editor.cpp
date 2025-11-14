#include "Core/Renderer/Renderer.h"
#include "Editor.h"


namespace aveng {

	Editor::Editor(VkRenderData& _renderData, Renderer& _renderer, GameData& _gameData, EngineDevice& _engineDevice, AvengWindow& window, ModelAndInstanceData& modelInstanceData)
		: renderData{ _renderData }, renderer{_renderer}, gameData{ _gameData }, engineDevice{ _engineDevice }, window{ window }, mModelInstanceData{ modelInstanceData }
	{
		editorData.eSelectedInstanceBuffers = std::vector<VkShaderStorageBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		renderData.rdLineCommandBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

		if (!createCommandBuffers())
		{
			throw std::runtime_error("editor failed to create command buffers");
		}

		if (!createSSBOs())
		{
			throw std::runtime_error("editor failed to create storage buffers");
		}

		if (!createDescriptorLayouts())
		{
			throw std::runtime_error("editor failed to create descriptor layouts");
		}
		if (!createDescriptorSets())
		{
			throw std::runtime_error("editor failed to create descriptor sets");
		}
	}

	Editor::~Editor() 
	{
		cleanup();
	}

	void Editor::render(int frameIndex)
	{
		aveng_imgui.newFrame();
		aveng_imgui.runGUI();
		drawSelectedInstanceGizmo(frameIndex);
		aveng_imgui.render(frameIndex);
	}

	void Editor::init(SwapChain* swapchain) 
	{
		// The primary renderpass, just in case
		// VkRenderPass renderPass = swapchain->getRenderPass();

		// Create the secondary renderpass used by the line rendering pipeline
		if (!swapchain->createSecondaryRenderpass(renderData.rdLineRenderpass))
		{
			Logger::log(1, "%s error; could not create secondary renderpass\n", __FUNCTION__);
			throw std::runtime_error("noob");
		}

		// Create the secondary renderpass used by the selection highlight
		if (!swapchain->createSecondaryRenderpass(renderData.rdSelectionRenderpass))
		{
			Logger::log(1, "%s error; could not create secondary renderpass\n", __FUNCTION__);
			throw std::runtime_error("noob");
		}

		std::string vertexShaderFile = "shader/line.vert.spv";
		std::string fragmentShaderFile = "shader/line.frag.spv";
		if (!LinePipeline::init(engineDevice, renderData.rdLineRenderpass, renderData.rdLinePipelineLayout, renderData.rdLinePipeline,
			vertexShaderFile, fragmentShaderFile)) {
			Logger::log(1, "%s error: could not init Assimp line drawing shader pipeline\n", __FUNCTION__);
			throw std::runtime_error("noob");
		}

		vertexShaderFile = "shader/aveng_selection.vert.spv";
		fragmentShaderFile = "shader/aveng_selection.frag.spv";
		if (!SkinningPipeline::init(engineDevice, renderData.rdSelectionRenderpass, renderData.rdAvengSelectionPipelineLayout,
			renderData.rdAvengSelectionPipeline, vertexShaderFile, fragmentShaderFile)) {
			Logger::log(1, "%s error: could not init Aveng Selection shader pipeline\n", __FUNCTION__);
			throw std::runtime_error("noob");
		}

		vertexShaderFile = "shader/aveng_skinning_selection.vert.spv";
		fragmentShaderFile = "shader/aveng_skinning_selection.frag.spv";
		if (!SkinningPipeline::init(engineDevice, renderData.rdSelectionRenderpass, renderData.rdAvengAnimationSelectionPipelineLayout,
			renderData.rdAvengAnimationSelectionPipeline, vertexShaderFile, fragmentShaderFile)) {
			Logger::log(1, "%s error: could not init Assimp Skinning Selection shader pipeline\n", __FUNCTION__);
			throw std::runtime_error("noob");
		}

		aveng_imgui.init(
			swapchain->getRenderPass(),
			swapchain->imageCount()
		);
	}

	bool Editor::createCommandBuffers()
	{
		renderData.rdLineCommandBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		if (!engineDevice.initCommandBuffers(renderData.rdLineCommandBuffers))
		{
			return false;
		}
		return true;
	}

	bool Editor::createSSBOs()
	{
		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
			if (!ShaderStorageBuffer::init(engineDevice, editorData.eSelectedInstanceBuffers[i])) {
				Logger::log(1, "%s error: could not create selection SSBO\n", __FUNCTION__);
				return false;
			}
		}
	}

	void AvengImgui::setupFrame(float dt) {
		editorData.eSelectedInstance.clear();
		editorData.eSelectedInstance.resize(modInstData.miAssimpInstances.size());

		// Potential Improvement: Use a non - owning pointer or a reference to the shared_ptr in the container :
		// E.g.
		// AssimpInstance * currentSelectedInstance = nullptr;
		// if (mRenderData.rdHighlightSelectedInstance) {
		//     auto& sp = mModelInstData.miAssimpInstances[mModelInstData.miSelectedInstance]; // reference, no refcount change
		//     currentSelectedInstance = sp.get(); // raw, non-owning
		// }
		/*
		* From ChatGuy:
		* If you prefer to keep safety when selection changes, cache a weak_ptr
		* once and lock only when you need it (ideally when selection changes, not per frame).
		*/

		/* save the selected instance for color highlight */
		editorData.eCurrentSelectedInstance = nullptr;
		if (editorData.eHighlightSelectedInstance) {
			editorData.eCurrentSelectedInstance = modInstData.miAssimpInstances[modInstData.miSelectedInstance];
			editorData.eSelectHighlightValue += dt * 4.0f;
			if (editorData.eSelectHighlightValue > 2.0f) {
				editorData.eSelectHighlightValue = 0.1f;
			}
		}
	}

	void Editor::setSelectedInstance(const std::shared_ptr<AssimpInstance>& instance, size_t instanceToStore, unsigned int i)
	{

		if (editorData.eCurrentSelectedInstance == instance) {
			mSelectedInstance.at(instanceToStore + i).x = editorData.eSelectHighlightValue;
		}
		else {
			mSelectedInstance.at(instanceToStore + i).x = 1.0f;
		}

		if (editorData.eMousePick) {
			InstanceSettings instSettings = instance->getInstanceSettings();
			mSelectedInstance.at(instanceToStore + i).y = static_cast<float>(instSettings.isInstanceIndexPosition);
		}
	}

	bool Editor::drawSelectedInstanceGizmo(int frameIndex) {

		/* draw coordinate lines */
		mCoordArrowsLineIndexCount = 0;
		mLineMesh->vertices.clear();
		if (mModelInstanceData.miSelectedInstance > 0) {
			InstanceSettings instSettings = mModelInstanceData.miAssimpInstances.at(mModelInstanceData.miSelectedInstance)->getInstanceSettings();

			/* draw coordiante arrows at origin of selected instance */
			switch (renderData.rdInstanceEditMode) {
			case instanceEditMode::move:
				mCoordArrowsMesh = mCoordArrowsModel.getVertexData();
				break;
			case instanceEditMode::rotate:
				mCoordArrowsMesh = mRotationArrowsModel.getVertexData();
				break;
			case instanceEditMode::scale:
				mCoordArrowsMesh = mScaleArrowsModel.getVertexData();
				break;
			}

			mCoordArrowsLineIndexCount += mCoordArrowsMesh.vertices.size();
			std::for_each(mCoordArrowsMesh.vertices.begin(), mCoordArrowsMesh.vertices.end(),
				[=](auto& n) {
				n.color /= 2.0f;
				n.position = glm::quat(glm::radians(instSettings.isWorldRotation)) * n.position;
				n.position += instSettings.isWorldPosition;
			});
			mLineMesh->vertices.insert(mLineMesh->vertices.end(),
				mCoordArrowsMesh.vertices.begin(), mCoordArrowsMesh.vertices.end());
		}

		if (!engineDevice.resetCommandBuffer(renderData.rdLineCommandBuffers[frameIndex], 0)) {
			Logger::log(1, "%s error: failed to reset line drawing command buffer\n", __FUNCTION__);
			return false;
		}

		if (!engineDevice.beginSingleShotCommand(renderData.rdLineCommandBuffers[frameIndex])) {
			Logger::log(1, "%s error: failed to begin line drawing command buffer\n", __FUNCTION__);
			return false;
		}

		rpInfo.renderPass = renderData.rdLineRenderpass;
		rpInfo.framebuffer = getFrameBuffer(imageIndex);

		vkCmdBeginRenderPass(renderData.rdLineCommandBuffers[frameIndex], &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdSetViewport(renderData.rdLineCommandBuffers[frameIndex], 0, 1, &viewport);
		vkCmdSetScissor(renderData.rdLineCommandBuffers[frameIndex], 0, 1, &scissor);

		if (mCoordArrowsLineIndexCount > 0) {
			mUploadToVBOTimer.start();
			VertexBuffer::uploadData(engineDevice, mLineVertexBuffer, *mLineMesh);
			renderData.rdUploadToVBOTime += mUploadToVBOTimer.stop();

			vkCmdBindPipeline(renderData.rdLineCommandBuffers[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, renderData.rdLinePipeline);

			vkCmdBindDescriptorSets(renderData.rdLineCommandBuffers[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
				renderData.rdLinePipelineLayout, 0, 1, &renderData.rdLineDescriptorSets[frameIndex], 0, nullptr);

			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(renderData.rdLineCommandBuffers[frameIndex], 0, 1, &mLineVertexBuffer.buffer, &offset);
			vkCmdSetLineWidth(renderData.rdLineCommandBuffers[frameIndex], 3.0f);
			vkCmdDraw(renderData.rdLineCommandBuffers[frameIndex], static_cast<uint32_t>(mLineMesh->vertices.size()), 1, 0, 0);
		}

		vkCmdEndRenderPass(renderData.rdLineCommandBuffers[frameIndex]);

		if (!engineDevice.endCommandBuffer(renderData.rdLineCommandBuffers[frameIndex])) {
			Logger::log(1, "%s error: failed to end line drawing command buffer\n", __FUNCTION__);
			return false;
		}

	}

	void Editor::updateStorageBuffers(int frameIndex)
	{
		bool bufferResized = false;
		bufferResized |= ShaderStorageBuffer::uploadSsboData(engineDevice, editorData.eSelectedInstanceBuffers[frameIndex], mSelectedInstance);
	}

	bool Editor::createDescriptorLayouts()
	{
		{
			/* non-animated selection shader */
			VkDescriptorSetLayoutBinding assimpSelUboBind{};
			assimpSelUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			assimpSelUboBind.binding = 0;
			assimpSelUboBind.descriptorCount = 1;
			assimpSelUboBind.pImmutableSamplers = nullptr;
			assimpSelUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			VkDescriptorSetLayoutBinding assimpSelSsboBind{};
			assimpSelSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpSelSsboBind.binding = 1;
			assimpSelSsboBind.descriptorCount = 1;
			assimpSelSsboBind.pImmutableSamplers = nullptr;
			assimpSelSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			VkDescriptorSetLayoutBinding assimpSelSsboBind2{};
			assimpSelSsboBind2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpSelSsboBind2.binding = 2;
			assimpSelSsboBind2.descriptorCount = 1;
			assimpSelSsboBind2.pImmutableSamplers = nullptr;
			assimpSelSsboBind2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			std::vector<VkDescriptorSetLayoutBinding> assimpBindings = { assimpSelUboBind, assimpSelSsboBind, assimpSelSsboBind2 };

			VkDescriptorSetLayoutCreateInfo assimpSelCreateInfo{};
			assimpSelCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			assimpSelCreateInfo.bindingCount = static_cast<uint32_t>(assimpBindings.size());
			assimpSelCreateInfo.pBindings = assimpBindings.data();

			result = vkCreateDescriptorSetLayout(engineDevice.device(), &assimpSelCreateInfo,
				nullptr, &renderData.rdAvengSelectionDescriptorLayout);
			if (result != VK_SUCCESS) {
				Logger::log(1, "%s error: could not create Assimp selection buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
				return false;
			}
		}

		{
			/* animated selection shader */
			VkDescriptorSetLayoutBinding assimpSelUboBind{};
			assimpSelUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			assimpSelUboBind.binding = 0;
			assimpSelUboBind.descriptorCount = 1;
			assimpSelUboBind.pImmutableSamplers = nullptr;
			assimpSelUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			VkDescriptorSetLayoutBinding assimpSkinningSelSsboBind{};
			assimpSkinningSelSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpSkinningSelSsboBind.binding = 1;
			assimpSkinningSelSsboBind.descriptorCount = 1;
			assimpSkinningSelSsboBind.pImmutableSamplers = nullptr;
			assimpSkinningSelSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			VkDescriptorSetLayoutBinding assimpSkinningSelSsboBind2{};
			assimpSkinningSelSsboBind2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpSkinningSelSsboBind2.binding = 2;
			assimpSkinningSelSsboBind2.descriptorCount = 1;
			assimpSkinningSelSsboBind2.pImmutableSamplers = nullptr;
			assimpSkinningSelSsboBind2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			VkDescriptorSetLayoutBinding assimpSkinningSelSsboBind3{};
			assimpSkinningSelSsboBind3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpSkinningSelSsboBind3.binding = 3;
			assimpSkinningSelSsboBind3.descriptorCount = 1;
			assimpSkinningSelSsboBind3.pImmutableSamplers = nullptr;
			assimpSkinningSelSsboBind3.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			std::vector<VkDescriptorSetLayoutBinding> assimpSkinningBindings =
			{ assimpSelUboBind, assimpSkinningSelSsboBind, assimpSkinningSelSsboBind2, assimpSkinningSelSsboBind3 };

			VkDescriptorSetLayoutCreateInfo assimpSkinningCreateInfo{};
			assimpSkinningCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			assimpSkinningCreateInfo.bindingCount = static_cast<uint32_t>(assimpSkinningBindings.size());
			assimpSkinningCreateInfo.pBindings = assimpSkinningBindings.data();

			result = vkCreateDescriptorSetLayout(engineDevice.device(), &assimpSkinningCreateInfo,
				nullptr, &renderData.rdAvengAnimationSelectionDescriptorLayout);
			if (result != VK_SUCCESS) {
				Logger::log(1, "%s error: could not create Assimp skinning selection buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
				return false;
			}
		}
	
		{
			/* line shader */
			VkDescriptorSetLayoutBinding assimpUboBind{};
			assimpUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			assimpUboBind.binding = 0;
			assimpUboBind.descriptorCount = 1;
			assimpUboBind.pImmutableSamplers = nullptr;
			assimpUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			std::vector<VkDescriptorSetLayoutBinding> assimpBindings = { assimpUboBind };

			VkDescriptorSetLayoutCreateInfo assimpCreateInfo{};
			assimpCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			assimpCreateInfo.bindingCount = static_cast<uint32_t>(assimpBindings.size());
			assimpCreateInfo.pBindings = assimpBindings.data();

			result = vkCreateDescriptorSetLayout(engineDevice.device(), &assimpCreateInfo,
				nullptr, &renderData.rdLineDescriptorLayout);
			if (result != VK_SUCCESS) {
				Logger::log(1, "%s error: could not create Assimp line drawing descriptor set layout (error: %i)\n", __FUNCTION__, result);
				return false;
			}
		}
	
		return true;
	}

	bool Editor::createDescriptorSets()
	{

		std::vector<VkDescriptorPoolSize> poolSizes =
		{
		  { VK_DESCRIPTOR_TYPE_SAMPLER, 500 },
		  { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 500 },
		  { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 500 },
		  { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 500 },
		  { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 500 },
		  { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 500 },
		};

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.maxSets = 500;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();

		VkResult result = vkCreateDescriptorPool(engineDevice.device(), &poolInfo, nullptr, &renderData.editorDescriptorPool);
		if (result != VK_SUCCESS) {
			Logger::log(1, "%s error: could not init descriptor pool (error: %i)\n", __FUNCTION__, result);
			return false;
		}

		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
			{
				/* line-drawing */
				VkDescriptorSetAllocateInfo lineAllocateInfo{};
				lineAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				lineAllocateInfo.descriptorPool = renderData.editorDescriptorPool;
				lineAllocateInfo.descriptorSetCount = 1;
				lineAllocateInfo.pSetLayouts = &renderData.rdLineDescriptorLayout;

				VkResult result = vkAllocateDescriptorSets(engineDevice.device(), &lineAllocateInfo,
					&renderData.rdLineDescriptorSets[i]);
				if (result != VK_SUCCESS) {
					Logger::log(1, "%s error: could not allocate Assimp line-drawing descriptor set (error: %i)\n", __FUNCTION__, result);
					return false;
				}
			}
			{
				/* selection, non-animated models */
				VkDescriptorSetAllocateInfo selectionDescriptorAllocateInfo{};
				selectionDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				selectionDescriptorAllocateInfo.descriptorPool = renderData.editorDescriptorPool;
				selectionDescriptorAllocateInfo.descriptorSetCount = 1;
				selectionDescriptorAllocateInfo.pSetLayouts = &renderData.rdAvengSelectionDescriptorLayout;

				VkResult result = vkAllocateDescriptorSets(engineDevice.device(), &selectionDescriptorAllocateInfo,
					&renderData.rdAvengSelectionDescriptorSets[i]);
				if (result != VK_SUCCESS) {
					Logger::log(1, "%s error: could not allocate Assimp selection descriptor set (error: %i)\n", __FUNCTION__, result);
					return false;
				}
			}
			{
				/* selection, animated models */
				VkDescriptorSetAllocateInfo skinningSelectionDescriptorAllocateInfo{};
				skinningSelectionDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				skinningSelectionDescriptorAllocateInfo.descriptorPool = renderData.editorDescriptorPool;
				skinningSelectionDescriptorAllocateInfo.descriptorSetCount = 1;
				skinningSelectionDescriptorAllocateInfo.pSetLayouts = &renderData.rdAvengAnimationSelectionDescriptorLayout;

				VkResult result = vkAllocateDescriptorSets(engineDevice.device(), &skinningSelectionDescriptorAllocateInfo,
					&renderData.rdAvengAnimationSelectionDescriptorSets[i]);
				if (result != VK_SUCCESS) {
					Logger::log(1, "%s error: could not allocate Assimp skinning selection descriptor set (error: %i)\n", __FUNCTION__, result);
					return false;
				}
			}
		}

		return true;

	}

	void Editor::updateDescriptorSets(int iters)
	{
		if (iters > SwapChain::MAX_FRAMES_IN_FLIGHT) {
			throw std::runtime_error("editor is attempting to write too many descriptor sets.");
		}
		Logger::log(1, "%s: updating descriptor sets\n", __FUNCTION__);
		for (int i = 0; i < iters; i++)
		{
			{
				/* selection shader, non-animated  */
				VkDescriptorBufferInfo matrixInfo{};
				matrixInfo.buffer = renderData.matrixBuffersView.viewProjUBOs[i].buffer; // mPerspectiveViewMatrixUBO[i].buffer
				matrixInfo.offset = 0;
				matrixInfo.range = VK_WHOLE_SIZE;

				VkDescriptorBufferInfo worldPosInfo{};
				worldPosInfo.buffer = renderData.matrixBuffersView.modelRootSSBOs[i].buffer; // mShaderModelRootMatrixBuffers[i].buffer;
				worldPosInfo.offset = 0;
				worldPosInfo.range = VK_WHOLE_SIZE;

				VkDescriptorBufferInfo selectionInfo{};
				selectionInfo.buffer = editorData.eSelectedInstanceBuffers[i].buffer;
				selectionInfo.offset = 0;
				selectionInfo.range = VK_WHOLE_SIZE;

				VkWriteDescriptorSet matrixWriteDescriptorSet{};
				matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				matrixWriteDescriptorSet.dstSet = renderData.rdAvengSelectionDescriptorSets[i];
				matrixWriteDescriptorSet.dstBinding = 0;
				matrixWriteDescriptorSet.descriptorCount = 1;
				matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

				VkWriteDescriptorSet posWriteDescriptorSet{};
				posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				posWriteDescriptorSet.dstSet = renderData.rdAvengSelectionDescriptorSets[i];
				posWriteDescriptorSet.dstBinding = 1;
				posWriteDescriptorSet.descriptorCount = 1;
				posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

				VkWriteDescriptorSet selectionWriteDescriptorSet{};
				selectionWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				selectionWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				selectionWriteDescriptorSet.dstSet = renderData.rdAvengSelectionDescriptorSets[i];
				selectionWriteDescriptorSet.dstBinding = 2;
				selectionWriteDescriptorSet.descriptorCount = 1;
				selectionWriteDescriptorSet.pBufferInfo = &selectionInfo;

				std::vector<VkWriteDescriptorSet> selectionWriteDescriptorSets =
				{ matrixWriteDescriptorSet, posWriteDescriptorSet, selectionWriteDescriptorSet };

				vkUpdateDescriptorSets(engineDevice.device(), static_cast<uint32_t>(selectionWriteDescriptorSets.size()),
					selectionWriteDescriptorSets.data(), 0, nullptr);
			}

			{
				/* selection shader, animated  */
				VkDescriptorBufferInfo matrixInfo{};
				matrixInfo.buffer = renderData.matrixBuffersView.viewProjUBOs[i].buffer;
				matrixInfo.offset = 0;
				matrixInfo.range = VK_WHOLE_SIZE;

				VkDescriptorBufferInfo boneMatrixInfo{};
				boneMatrixInfo.buffer = renderData.matrixBuffersView.boneMatSSBOs[i].buffer; // mShaderBoneMatrixBuffer.buffer;
				boneMatrixInfo.offset = 0;
				boneMatrixInfo.range = VK_WHOLE_SIZE;

				VkDescriptorBufferInfo worldPosInfo{};
				worldPosInfo.buffer = renderData.matrixBuffersView.modelRootSSBOs[i].buffer;
				worldPosInfo.offset = 0;
				worldPosInfo.range = VK_WHOLE_SIZE;

				VkDescriptorBufferInfo selectionInfo{};
				selectionInfo.buffer = editorData.eSelectedInstanceBuffers[i].buffer;
				selectionInfo.offset = 0;
				selectionInfo.range = VK_WHOLE_SIZE;

				VkWriteDescriptorSet matrixWriteDescriptorSet{};
				matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				matrixWriteDescriptorSet.dstSet = renderData.rdAvengAnimationSelectionDescriptorSets[i];
				matrixWriteDescriptorSet.dstBinding = 0;
				matrixWriteDescriptorSet.descriptorCount = 1;
				matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

				VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
				boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				boneMatrixWriteDescriptorSet.dstSet = renderData.rdAvengAnimationSelectionDescriptorSets[i];
				boneMatrixWriteDescriptorSet.dstBinding = 1;
				boneMatrixWriteDescriptorSet.descriptorCount = 1;
				boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

				VkWriteDescriptorSet posWriteDescriptorSet{};
				posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				posWriteDescriptorSet.dstSet = renderData.rdAvengAnimationSelectionDescriptorSets[i];
				posWriteDescriptorSet.dstBinding = 2;
				posWriteDescriptorSet.descriptorCount = 1;
				posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

				VkWriteDescriptorSet selectionWriteDescriptorSet{};
				selectionWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				selectionWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				selectionWriteDescriptorSet.dstSet = renderData.rdAvengAnimationSelectionDescriptorSets[i];
				selectionWriteDescriptorSet.dstBinding = 3;
				selectionWriteDescriptorSet.descriptorCount = 1;
				selectionWriteDescriptorSet.pBufferInfo = &selectionInfo;

				std::vector<VkWriteDescriptorSet> skinningSelectionWriteDescriptorSets =
				{ matrixWriteDescriptorSet, boneMatrixWriteDescriptorSet,
				  posWriteDescriptorSet, selectionWriteDescriptorSet };

				vkUpdateDescriptorSets(engineDevice.device(), static_cast<uint32_t>(skinningSelectionWriteDescriptorSets.size()),
					skinningSelectionWriteDescriptorSets.data(), 0, nullptr);
			}

			{
				/* line-drawing shader */
				VkDescriptorBufferInfo matrixInfo{};
				matrixInfo.buffer = renderData.matrixBuffersView.viewProjUBOs[i].buffer;
				matrixInfo.offset = 0;
				matrixInfo.range = VK_WHOLE_SIZE;

				VkWriteDescriptorSet matrixWriteDescriptorSet{};
				matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				matrixWriteDescriptorSet.dstSet = renderData.rdLineDescriptorSets[i];
				matrixWriteDescriptorSet.dstBinding = 0;
				matrixWriteDescriptorSet.descriptorCount = 1;
				matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

				std::vector<VkWriteDescriptorSet> writeDescriptorSets =
				{ matrixWriteDescriptorSet };

				vkUpdateDescriptorSets(engineDevice.device(), static_cast<uint32_t>(writeDescriptorSets.size()),
					writeDescriptorSets.data(), 0, nullptr);
			}
		}
	}

	void Editor::cleanup()
	{

		// Free command buffers
		vkFreeCommandBuffers(
			engineDevice.device(),
			engineDevice.commandPoolGraphics(),
			static_cast<uint32_t>(renderData.rdLineCommandBuffers.size()),
			renderData.rdLineCommandBuffers.data()
		);
		renderData.rdLineCommandBuffers.clear();

		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {

			ShaderStorageBuffer::cleanup(engineDevice, editorData.eSelectedInstanceBuffers[i]);
			vkFreeDescriptorSets(engineDevice.device(), renderData.editorDescriptorPool, 1, &renderData.rdAvengSelectionDescriptorSets[i]);
			vkFreeDescriptorSets(engineDevice.device(), renderData.editorDescriptorPool, 1, &renderData.rdAvengAnimationSelectionDescriptorSets[i]);
			vkFreeDescriptorSets(engineDevice.device(), renderData.editorDescriptorPool, 1, &renderData.rdLineDescriptorSets[i]);
		}

		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengSelectionDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengAnimationSelectionDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdLineDescriptorLayout, nullptr);

		vkDestroyRenderPass(engineDevice.device(), renderData.rdLineRenderpass, nullptr);
	}
	
}