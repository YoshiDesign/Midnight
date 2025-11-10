#pragma once
#include "GUI/aveng_imgui.h"
#include "Core/data.h"
#include "CoreVK/swapchain.h"
#include "Core/aveng_window.h"
#include "CoreVK/EngineDevice.h"
#include "Core/Modeling/ModelAndInstanceData.h"

namespace aveng {

	class Editor {
	public:
		Editor(VkRenderData& _renderData, GameData& _gameData, EngineDevice& _engineDevice, ModelAndInstanceData& modelInstanceData);
		~Editor();
		void init(AvengWindow& window, SwapChain* swapchain);
		void render(int frameIndex);

	private:
		VkRenderData& renderData;
		GameData& gameData;
		ModelAndInstanceData& mModelInstanceData;
		EngineDevice& engineDevice;
		AvengImgui aveng_imgui{ renderData, gameData, engineDevice, mModelInstanceData };
	};

}