#pragma once
#include "GUI/aveng_imgui.h"
#include "Core/data.h"
#include "CoreVK/swapchain.h"
#include "Core/aveng_window.h"
#include "CoreVK/swapchain.h"

namespace aveng {

	class Editor {
	public:
		Editor(RenderData& _renderData, GameData& _gameData);
		~Editor();
		void init(AvengWindow& window, SwapChain* swapchain);
		void render(VkCommandBuffer& commandBuffer);

	private:
		RenderData& renderData;
		GameData& gameData;
		AvengImgui aveng_imgui{ renderData, gameData };
	};

}