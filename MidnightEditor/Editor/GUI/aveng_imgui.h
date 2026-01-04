#pragma once

#include "avpch.h"
#include "CoreVK/VkRenderData.h"
#include "Editor/EditorData.h"
#include "Core/Input/InputState.h"
#include "Editor/GUI/models/CoordArrowsModel.h"
#include "Editor/GUI/models/RotationArrowsModel.h"
#include "Editor/GUI/models/ScaleArrowsModel.h"
#include "Utils/Timer.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "Editor/Utils/selection_utils.h"

#include "Editor/GUI/imgui.h"
#include "Editor/GUI/imgui_impl_glfw.h"
#include "Editor/GUI/imgui_impl_vulkan.h"
#include "Editor/GUI/imgui_internal.h"
#include "Editor/GUI/ImGuiFileDialog.h"
/*
    UI state should only store stable identifiers (ModelId, AnyInstanceHandle),
    and UI presentation should be built from query snapshots (IModelQuery, IInstanceQuery)
*/

namespace aveng {

    class EngineDevice;
    class AvengWindow;
    struct IEditorUIAPI;
    struct InputState;

	static void check_vk_result(VkResult err) {
		if (err == 0) return;
		fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
		if (err < 0) abort();
	}

	class AvengImgui {

	public:

		AvengImgui(
            VkRenderData& _renderData, 
            IEditorUIAPI& api_,
            EditorData& editorData, 
            AvengWindow& _window, 
            EngineDevice& _engineDevice);

		~AvengImgui();

		void init(VkRenderPass renderPass, uint32_t imageCount);

		void newFrame();
		void render(int frameIndex);
		void runGUI();

        void handleMouseButtonEvents(int button, int action, int mods);
        void handleMousePositionEvents(double xPos, double yPos, bool rmbDown);
		void hideMouse(bool hide);
        void updateInputState(const InputState& updateInputState);

		bool show_input_panel = false;
        bool show_all_cameras = false;

	private:
		// EngineDevice& device;
		VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        InputState inputState;
		VkRenderData& renderData;
		EngineDevice& engineDevice;
        AvengWindow& window;
        EditorData& editorData;
        IEditorUIAPI& api_;

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