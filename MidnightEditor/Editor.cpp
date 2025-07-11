#include "Core/Renderer/Renderer.h"
#include "Editor.h"

namespace aveng {

	Editor::Editor(SystemContext& context) : aveng_context(context)
	{
		std::cout << context.game_data->modPI << std::endl;
		init();
	}
	 
	Editor::~Editor() 
	{}

	void Editor::init() 
	{
		aveng_imgui.init(
			*aveng_context.window,
			aveng_context.renderer->getSwapChainRenderPass(),
			aveng_context.renderer->getImageCount()
		);
	}

	void Editor::render(VkCommandBuffer& commandBuffer)
	{
		aveng_imgui.newFrame();
		aveng_imgui.runGUI();
		aveng_imgui.render(commandBuffer);
	}
	
}