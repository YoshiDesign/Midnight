#include "Core/Renderer/Renderer.h"
#include "Editor.h"


namespace aveng {

	Editor::Editor(VkRenderData& _renderData, GameData& _gameData, EngineDevice& _engineDevice, AvengWindow& window, ModelAndInstanceData& modelInstanceData)
		: renderData{ _renderData }, gameData{ _gameData }, engineDevice{ _engineDevice }, mModelInstanceData{ modelInstanceData }, window{ window }
	{}

	Editor::~Editor() 
	{
		
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
		if (!SkinningPipeline::init(engineDevice, renderData.rdSelectionRenderpass, renderData.rdAvengSkinningSelectionPipelineLayout,
			renderData.rdAvengSkinningSelectionPipeline, vertexShaderFile, fragmentShaderFile)) {
			Logger::log(1, "%s error: could not init Assimp Skinning Selection shader pipeline\n", __FUNCTION__);
			throw std::runtime_error("noob");
		}

		aveng_imgui.init(
			swapchain->getRenderPass(),
			swapchain->imageCount()
		);
	}

	void Editor::setup(float dt)
	{
		aveng_imgui.setup(dt);
	}

	void Editor::render(int frameIndex)
	{
		aveng_imgui.newFrame();
		aveng_imgui.runGUI();
		aveng_imgui.drawSelectedInstanceGizmo(frameIndex);
		aveng_imgui.render(frameIndex);
	}

	void Editor::cleanup()
	{
		vkDestroyRenderPass(engineDevice.device(), renderData.rdLineRenderpass, nullptr);
	}
	
}