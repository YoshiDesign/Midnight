#pragma once
#include "GUI/aveng_imgui.h"
#include "CoreVK/swapchain.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/SkinningPipeline.h"
#include "CoreVK/LinePipeline.h"
#include "Core/aveng_window.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "Core/data.h"

namespace aveng {

	class Editor {
	public:
		Editor(VkRenderData& _renderData, GameData& _gameData, EngineDevice& _engineDevice, AvengWindow& window, ModelAndInstanceData& modelInstanceData);
		~Editor();
		void init(SwapChain* swapchain);
		void setup(float dt);
		void render(int frameIndex);
		void cleanup();

	private:
		VkRenderData& renderData;
		GameData& gameData;
		ModelAndInstanceData& mModelInstanceData;
		EngineDevice& engineDevice;
		AvengWindow& window;
		AvengImgui aveng_imgui{ renderData, gameData, window, engineDevice, mModelInstanceData };
	};

}