#pragma once
#include "CoreVK/VkRenderData.h"
#include "Utils/window_callbacks.h"
#include "Utils/Timer.h"
#include "Game/data.h"

// libs
#include "Utils/glm_includes.h"
#include "GUI/imgui.h"
#include "GUI/imgui_impl_glfw.h"
#include "GUI/imgui_impl_vulkan.h"
#include "GUI/ImGuiFileDialog.h"
#include "GUI/models/CoordArrowsModel.h"
#include "GUI/models/RotationArrowsModel.h"
#include "GUI/models/ScaleArrowsModel.h"
#include "EditorData.h"

#include "Core/Modeling/ModelAndInstanceData.h"

// std
#include <stdexcept>

class EngineDevice;
class AvengWindow;

namespace aveng {

	static void check_vk_result(VkResult err) {
		if (err == 0) return;
		fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
		if (err < 0) abort();
	}

	class AvengImgui {
	public:

		AvengImgui(VkRenderData& _renderData, GameData& _gameData, EditorData& editorData, AvengWindow& _window, EngineDevice& _engineDevice, ModelAndInstanceData& _modInstData);
		void init(VkRenderPass renderPass, uint32_t imageCount);
		~AvengImgui();

		void newFrame();
		void render(int frameIndex);
		void runGUI();

        void handleMouseButtonEvents(int button, int action, int mods);
        void handleMousePositionEvents(double xPos, double yPos, bool rmbDown);
		void hideMouse(bool hide);

		bool show_player_controller_window = false;
		ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	private:
		// EngineDevice& device;
		VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
		VkRenderData& renderData;
        ModelAndInstanceData& modInstData;
		GameData& gameData;
		EngineDevice& engineDevice;
        AvengWindow& window;
        EditorData& editorData;

        VkRenderPass mSelectionRenderpass = VK_NULL_HANDLE;
        VkRenderPass mLineRenderpass = VK_NULL_HANDLE;

        float mFramesPerSecond = 0.0f;
        /* averaging speed */
        float mAveragingAlpha = 0.96f;

        std::vector<float> mFPSValues{};
        int mNumFPSValues = 90;

        std::vector<float> mFrameTimeValues{};
        int mNumFrameTimeValues = 90;

        std::vector<float> mModelUploadValues{};
        int mNumModelUploadValues = 90;

        std::vector<float> mMatrixGenerationValues{};
        int mNumMatrixGenerationValues = 90;

        std::vector<float> mMatrixUploadValues{};
        int mNumMatrixUploadValues = 90;

        std::vector<float> mUiGenValues{};
        int mNumUiGenValues = 90;

        std::vector<float> mUiDrawValues{};
        int mNumUiDrawValues = 90;

        float mNewFps = 0.0f;
        double mUpdateTime = 0.0;

        int mFpsOffset = 0;
        int mFrameTimeOffset = 0;
        int mModelUploadOffset = 0;
        int mMatrixGenOffset = 0;
        int mMatrixUploadOffset = 0;
        int mUiGenOffset = 0;
        int mUiDrawOffset = 0;

	};
} 