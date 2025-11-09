#include "Core/Renderer/Renderer.h"
#include "Editor.h"


namespace aveng {

	Editor::Editor(VkRenderData& _renderData, GameData& _gameData, EngineDevice& _engineDevice, ModelAndInstanceData& modelInstanceData)
		: renderData{ _renderData }, gameData{ _gameData }, engineDevice{ _engineDevice }, mModelInstanceData{ modelInstanceData }
	{}

	Editor::~Editor() 
	{
		
	}

	void Editor::init(AvengWindow& window, SwapChain* swapchain) 
	{
		aveng_imgui.init(
			window,
			swapchain->getRenderPass(),
			swapchain->imageCount()
		);
	}

	void Editor::render(VkCommandBuffer& commandBuffer)
	{
		aveng_imgui.newFrame();
		aveng_imgui.runGUI();
		aveng_imgui.render(commandBuffer);
	}
	
}