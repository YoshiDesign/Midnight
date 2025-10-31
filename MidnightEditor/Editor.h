#pragma once
#include "GUI/aveng_imgui.h"
#include "Core/data.h"
#include "CoreVK/swapchain.h"
#include "Core/aveng_window.h"
#include "CoreVK/EngineDevice.h"

namespace aveng {

	class Editor {
	public:
		Editor(VkRenderData& _renderData, GameData& _gameData, EngineDevice& _engineDevice);
		~Editor();
		void init(AvengWindow& window, SwapChain* swapchain);
		void render(VkCommandBuffer& commandBuffer);

	private:
		VkRenderData& renderData;
		GameData& gameData;
		EngineDevice& engineDevice;
		AvengImgui aveng_imgui{ renderData, gameData, engineDevice };
	};

}