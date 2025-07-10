#include "Core/Renderer/Renderer.h"
#include "Editor.h"

namespace aveng {

	Editor::Editor(SystemContext& context) : game_context(context)
	{
	}
	 
	Editor::~Editor() 
	{}

	void Editor::init() 
	{
		AvengImgui aveng_imgui(*game_context.device);
		aveng_imgui.init(
			*game_context.window,
			game_context.renderer->getSwapChainRenderPass(),
			game_context.renderer->getImageCount()
		);

	}
	
}