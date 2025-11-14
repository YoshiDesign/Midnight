#pragma once
#include "GUI/aveng_imgui.h"
#include "CoreVK/swapchain.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/SkinningPipeline.h"
#include "CoreVK/LinePipeline.h"
#include "Core/aveng_window.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "Core/data.h"
#include "EditorData.h"
#include "Core/Renderer/Renderer.h"

namespace aveng {

	class Editor {
	public:
		Editor(VkRenderData& _renderData, Renderer& _renderer, GameData& _gameData, EngineDevice& _engineDevice, AvengWindow& window, ModelAndInstanceData& modelInstanceData);
		~Editor();
		void init(SwapChain* swapchain);
		void setupFrame(float dt);
		void render(int frameIndex);
		void cleanup();

		void setSelectedInstance(const std::shared_ptr<AssimpInstance>& instance, size_t instanceToStore, unsigned int i);
		bool drawSelectedInstanceGizmo(int frameIndex);

		bool createDescriptorLayouts();
		bool createDescriptorSets();
		bool createCommandBuffers();
		bool createSSBOs();
		void updateDescriptorSets(int iters = 1);

		void updateStorageBuffers(int frameIndex);

	private:

		Timer mUploadToVBOTimer{};

		/* color hightlight for selection etc */
		std::vector<glm::vec2> mSelectedInstance{};
		std::vector<VkShaderStorageBufferData> mSelectedInstanceBuffers{};
		VkVertexBufferData mLineVertexBuffer{};

		bool mHighlightSelectedInstance = false;
		float mSelectedInstanceHighlightValue = 1.0f;

		CoordArrowsModel mCoordArrowsModel{};
		RotationArrowsModel mRotationArrowsModel{};
		ScaleArrowsModel mScaleArrowsModel{};
		VkLineMesh mCoordArrowsMesh{};

		unsigned int mCoordArrowsLineIndexCount = 0;

		instanceEditMode rdInstanceEditMode = instanceEditMode::move;

		std::shared_ptr<VkLineMesh> mLineMesh = nullptr;

		VkResult result;
		VkRenderData& renderData;
		GameData& gameData;
		ModelAndInstanceData& mModelInstanceData;
		EngineDevice& engineDevice;
		AvengWindow& window;
		Renderer& renderer;
		EditorData editorData;
		AvengImgui aveng_imgui{ renderData, gameData, editorData, window, engineDevice, mModelInstanceData };
	};

}