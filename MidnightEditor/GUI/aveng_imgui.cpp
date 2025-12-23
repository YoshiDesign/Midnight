#include "aveng_imgui.h"

#include <string>
#include <limits>
#include <filesystem>

#include "CoreVK/EngineDevice.h"
#include "Core/aveng_window.h"
#include "Core/aveng_model.h"
#include "Core/Modeling/AssimpAnimClip.h"
#include "Core/Modeling/AssimpInstance.h"
#include "Core/Modeling/InstanceSettings.h"
#include "CoreVK/VertexBuffer.h"
#include "avpch.h"

namespace aveng {

    AvengImgui::AvengImgui(VkRenderData& _renderData, GameData& _gameData, EditorData& editorData, AvengWindow& _window, EngineDevice& _engineDevice, ModelAndInstanceData& _modInstData)
        : renderData{ _renderData }, gameData{ _gameData }, 
        editorData{ editorData }, engineDevice { _engineDevice}, 
        modInstData{ _modInstData }, window{ _window }
    {
        // Initialize all the timing vectors with their proper sizes
        mFPSValues.resize(mNumFPSValues, 0.0f);
        mFrameTimeValues.resize(mNumFrameTimeValues, 0.0f);
        mModelUploadValues.resize(mNumModelUploadValues, 0.0f);
        mMatrixGenerationValues.resize(mNumMatrixGenerationValues, 0.0f);
        mMatrixUploadValues.resize(mNumMatrixUploadValues, 0.0f);
        mUiGenValues.resize(mNumUiGenValues, 0.0f);
        mUiDrawValues.resize(mNumUiDrawValues, 0.0f);
    }

    // Initialize the vulkan and glfw imgui implementations
    void AvengImgui::init(VkRenderPass renderPass, uint32_t imageCount)
    {
        // set up a descriptor pool stored on this instance, see header for more comments on this.
        VkDescriptorPoolSize pool_sizes[] = {
            {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000} };

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
        pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;

        if (vkCreateDescriptorPool(engineDevice.device(), &pool_info, nullptr, &descriptorPool) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to set up descriptor pool for imgui");
        }

        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        (void)io;
        // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

        // Setup Dear ImGui style
        ImGui::StyleColorsLight();
        // ImGui::StyleColorsClassic();

        // Setup Platform/Renderer backends
        // Initialize imgui for vulkan
        ImGui_ImplGlfw_InitForVulkan(window.getGLFWwindow(), true);
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.ApiVersion = VK_API_VERSION_1_0;  // Required in ImGui 1.92+
        init_info.Instance = engineDevice.instance();
        init_info.PhysicalDevice = engineDevice.physicalDevice();
        init_info.Device = engineDevice.device();
        init_info.QueueFamily = engineDevice.getGraphicsQueueFamily();
        init_info.Queue = engineDevice.graphicsQueue();

        // pipeline cache is a potential future optimization, ignoring for now
        init_info.PipelineCache = VK_NULL_HANDLE;
        init_info.DescriptorPool = descriptorPool;
        // todo, Implement a memory allocator library (VMA) sooner than later
        init_info.Allocator = VK_NULL_HANDLE;
        init_info.MinImageCount = 2;
        init_info.ImageCount = imageCount;
        init_info.CheckVkResultFn = check_vk_result;
        
        // Set pipeline info - RenderPass is now part of PipelineInfoMain (API change in ImGui 1.92+)
        init_info.PipelineInfoMain.RenderPass = renderPass;
        init_info.PipelineInfoMain.Subpass = 0;
        
        ImGui_ImplVulkan_Init(&init_info);

        // Note: Font upload is now handled automatically by NewFrame() on first call
        // (ImGui_ImplVulkan_CreateFontsTexture and ImGui_ImplVulkan_DestroyFontUploadObjects were removed)
    }

    AvengImgui::~AvengImgui() {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        vkDestroyDescriptorPool(engineDevice.device(), descriptorPool, nullptr);
        ImGui::DestroyContext();
    }

    void AvengImgui::newFrame() {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    // this tells imgui that we're done setting up the current frame,
    // then gets the draw data from imgui and uses it to record to the provided
    // command buffer the necessary draw commands
    void AvengImgui::render(int frameIndex) {
        ImGui::Render();
        ImDrawData* drawdata = ImGui::GetDrawData();

        // NOTE: This could be using its own command buffer (and its own renderpass), which could help if we ever want to run the editor on a different thread
        ImGui_ImplVulkan_RenderDrawData(drawdata, renderData.rdGUICommandBuffers.at(frameIndex));
    }

    void AvengImgui::updateInputState(const InputState& state) {
        inputState = state;
    }

    void AvengImgui::runGUI() {

        //static float slider = 0.0f;
        static float value = 3.1415926f;
        static bool hasChanged = false;
        static int counter = 0;

        {
            ImGui::Begin("Engine Info");
            ImGui::Text("Video Device:\t %s", engineDevice.properties.deviceName);
            ImGui::End();
        }

        {
            ImGui::Begin("Debug");

            ImGui::Checkbox("Input Panel", &show_input_panel);

            ImGui::Text(
                "Last Click At: (%d, %d)", editorData.eMouseLastClickX, editorData.eMouseLastClickY);
            ImGui::Text(
                "App Mode: %d", gameData.currentAppMode);
            ImGui::Text(
                "Camera View:\t\t(%.03lf, %.03lf, %.03lf)", gameData.cameraView.x, gameData.cameraView.y, gameData.cameraView.z);
            ImGui::Text(
                "Camera Rotation:\t(%.03lf, %.03lf, %.03lf)", gameData.cameraRot.x, gameData.cameraRot.y, gameData.cameraRot.z);
            ImGui::Text(
                "Camera Position:\t(%.03lf, %.03lf, %.03lf)", gameData.cameraPos.x, gameData.cameraPos.y, gameData.cameraPos.z);
            ImGui::Text(
                "Player Rotation:\t(%.03lf, %.03lf, %.03lf)", gameData.playerRot.x,gameData.playerRot.y, gameData.playerRot.z);
            ImGui::Text(
                "Player Position:\t(%.03lf, %.03lf, %.03lf)", gameData.playerPos.x, gameData.playerPos.y, gameData.playerPos.z);
            ImGui::Text(
                "Mod Rotation:\t(%.03lf, %.03lf, %.03lf)", gameData.modRot.x, gameData.modRot.y, gameData.modRot.z);
            ImGui::Text(
                "Mod Position:\t(%.03lf, %.03lf, %.03lf)", gameData.modPos.x, gameData.modPos.y, gameData.modPos.z);
            ImGui::Text(
                "Forward Direction:\t(%.03lf, %.03lf, %.03lf)", gameData.forwardDir.x, gameData.forwardDir.y, gameData.forwardDir.z);

            ImGui::Text("GFX-Pipe:\t%d", gameData.cur_pipe);

            ImGui::End(); // End Debug window
        }

        if (show_input_panel) {
            ImGui::Begin("Input Triggers");
            ImGui::Text("W: \t%d", inputState.keyDown[GLFW_KEY_W]);
            ImGui::Text("A: \t%d", inputState.keyDown[GLFW_KEY_A]);
            ImGui::Text("S: \t%d", inputState.keyDown[GLFW_KEY_S]);
            ImGui::Text("D: \t%d", inputState.keyDown[GLFW_KEY_D]);
            ImGui::End();
        }

        {
            ImGuiIO& io = ImGui::GetIO();
            ImGuiWindowFlags imguiWindowFlags = 0;

            ImGui::SetNextWindowBgAlpha(0.8f);

            /* dim background for modal dialogs */
            ImGuiStyle& style = ImGui::GetStyle();
            style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.75f);

            /* avoid inf values (division by zero) */
            if (renderData.rdFrameTime > 0.0) {
                mNewFps = 1.0f / renderData.rdFrameTime * 1000.f;
            }
            /* make an averge value to avoid jumps */
            mFramesPerSecond = (mAveragingAlpha * mFramesPerSecond) + (1.0f - mAveragingAlpha) * mNewFps;

            /* clamp manual input on all sliders to min/max */
            ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp;

            /* avoid literal double compares */
            if (mUpdateTime < 0.000001) {
                mUpdateTime = ImGui::GetTime();
            }

            while (mUpdateTime < ImGui::GetTime()) {
                mFPSValues.at(mFpsOffset) = mFramesPerSecond;
                mFpsOffset = ++mFpsOffset % mNumFPSValues;

                mFrameTimeValues.at(mFrameTimeOffset) = renderData.rdFrameTime;
                mFrameTimeOffset = ++mFrameTimeOffset % mNumFrameTimeValues;

                mModelUploadValues.at(mModelUploadOffset) = renderData.rdUploadToVBOTime;
                mModelUploadOffset = ++mModelUploadOffset % mNumModelUploadValues;

                mMatrixGenerationValues.at(mMatrixGenOffset) = renderData.rdMatrixGenerateTime;
                mMatrixGenOffset = ++mMatrixGenOffset % mNumMatrixGenerationValues;

                mMatrixUploadValues.at(mMatrixUploadOffset) = renderData.rdUploadToUBOTime;
                mMatrixUploadOffset = ++mMatrixUploadOffset % mNumMatrixUploadValues;

                mUiGenValues.at(mUiGenOffset) = renderData.rdUIGenerateTime;
                mUiGenOffset = ++mUiGenOffset % mNumUiGenValues;

                mUiDrawValues.at(mUiDrawOffset) = renderData.rdUIDrawTime;
                mUiDrawOffset = ++mUiDrawOffset % mNumUiDrawValues;

                mUpdateTime += 1.0 / 30.0;
            }

            if (!ImGui::Begin("Control", nullptr, imguiWindowFlags)) {
                /* window collapsed */
                ImGui::End();
                return;
            }

            ImGui::Text("FPS: %10.4f", mFramesPerSecond);

            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                float averageFPS = 0.0f;
                for (const auto value : mFPSValues) {
                    averageFPS += value;
                }
                averageFPS /= static_cast<float>(mNumFPSValues);
                std::string fpsOverlay = "now:     " + std::to_string(mFramesPerSecond) + "\n30s avg: " + std::to_string(averageFPS);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("FPS");
                ImGui::SameLine();
                ImGui::PlotLines("##FrameTimes", mFPSValues.data(), mFPSValues.size(), mFpsOffset, fpsOverlay.c_str(), 0.0f,
                    std::numeric_limits<float>::max(), ImVec2(0, 80));
                ImGui::EndTooltip();
            }

            if (ImGui::CollapsingHeader("Info")) {
                ImGui::Text("Triangles:              %10i", renderData.rdTriangleCount);

                std::string unit = "B";
                float memoryUsage = renderData.rdMatricesSize;

                if (memoryUsage > 1024.0f * 1024.0f) {
                    memoryUsage /= 1024.0f * 1024.0f;
                    unit = "MB";
                }
                else  if (memoryUsage > 1024.0f) {
                    memoryUsage /= 1024.0f;
                    unit = "KB";
                }

                ImGui::Text("Instance Matrix Size:  %8.2f %2s", memoryUsage, unit.c_str());

                std::string windowDims = std::to_string(renderData.rdWidth) + "x" + std::to_string(renderData.rdHeight);
                ImGui::Text("Window Dimensions:      %10s", windowDims.c_str());

                std::string imgWindowPos = std::to_string(static_cast<int>(ImGui::GetWindowPos().x)) + "/" + std::to_string(static_cast<int>(ImGui::GetWindowPos().y));
                ImGui::Text("ImGui Window Position:  %10s", imgWindowPos.c_str());
            }

            if (ImGui::CollapsingHeader("Timers")) {
                ImGui::Text("Frame Time:             %10.4f ms", renderData.rdFrameTime);

                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    float averageFrameTime = 0.0f;
                    for (const auto value : mFrameTimeValues) {
                        averageFrameTime += value;
                    }
                    averageFrameTime /= static_cast<float>(mNumMatrixGenerationValues);
                    std::string frameTimeOverlay = "now:     " + std::to_string(renderData.rdFrameTime) +
                        " ms\n30s avg: " + std::to_string(averageFrameTime) + " ms";
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Frame Time       ");
                    ImGui::SameLine();
                    ImGui::PlotLines("##FrameTime", mFrameTimeValues.data(), mFrameTimeValues.size(), mFrameTimeOffset,
                        frameTimeOverlay.c_str(), 0.0f, std::numeric_limits<float>::max(), ImVec2(0, 80));
                    ImGui::EndTooltip();
                }

                ImGui::Text("Model Upload Time:      %10.4f ms", renderData.rdUploadToVBOTime);

                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    float averageModelUpload = 0.0f;
                    for (const auto value : mModelUploadValues) {
                        averageModelUpload += value;
                    }
                    averageModelUpload /= static_cast<float>(mNumModelUploadValues);
                    std::string modelUploadOverlay = "now:     " + std::to_string(renderData.rdUploadToVBOTime) +
                        " ms\n30s avg: " + std::to_string(averageModelUpload) + " ms";
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("VBO Upload");
                    ImGui::SameLine();
                    ImGui::PlotLines("##ModelUploadTimes", mModelUploadValues.data(), mModelUploadValues.size(), mModelUploadOffset,
                        modelUploadOverlay.c_str(), 0.0f, std::numeric_limits<float>::max(), ImVec2(0, 80));
                    ImGui::EndTooltip();
                }

                ImGui::Text("Matrix Generation Time: %10.4f ms", renderData.rdMatrixGenerateTime);

                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    float averageMatGen = 0.0f;
                    for (const auto value : mMatrixGenerationValues) {
                        averageMatGen += value;
                    }
                    averageMatGen /= static_cast<float>(mNumMatrixGenerationValues);
                    std::string matrixGenOverlay = "now:     " + std::to_string(renderData.rdMatrixGenerateTime) +
                        " ms\n30s avg: " + std::to_string(averageMatGen) + " ms";
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Matrix Generation");
                    ImGui::SameLine();
                    ImGui::PlotLines("##MatrixGenTimes", mMatrixGenerationValues.data(), mMatrixGenerationValues.size(), mMatrixGenOffset,
                        matrixGenOverlay.c_str(), 0.0f, std::numeric_limits<float>::max(), ImVec2(0, 80));
                    ImGui::EndTooltip();
                }

                ImGui::Text("Matrix Upload Time:     %10.4f ms", renderData.rdUploadToUBOTime);

                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    float averageMatrixUpload = 0.0f;
                    for (const auto value : mMatrixUploadValues) {
                        averageMatrixUpload += value;
                    }
                    averageMatrixUpload /= static_cast<float>(mNumMatrixUploadValues);
                    std::string matrixUploadOverlay = "now:     " + std::to_string(renderData.rdUploadToUBOTime) +
                        " ms\n30s avg: " + std::to_string(averageMatrixUpload) + " ms";
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("UBO Upload");
                    ImGui::SameLine();
                    ImGui::PlotLines("##MatrixUploadTimes", mMatrixUploadValues.data(), mMatrixUploadValues.size(), mMatrixUploadOffset,
                        matrixUploadOverlay.c_str(), 0.0f, std::numeric_limits<float>::max(), ImVec2(0, 80));
                    ImGui::EndTooltip();
                }

                ImGui::Text("UI Generation Time:     %10.4f ms", renderData.rdUIGenerateTime);

                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    float averageUiGen = 0.0f;
                    for (const auto value : mUiGenValues) {
                        averageUiGen += value;
                    }
                    averageUiGen /= static_cast<float>(mNumUiGenValues);
                    std::string uiGenOverlay = "now:     " + std::to_string(renderData.rdUIGenerateTime) +
                        " ms\n30s avg: " + std::to_string(averageUiGen) + " ms";
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("UI Generation");
                    ImGui::SameLine();
                    ImGui::PlotLines("##UIGenTimes", mUiGenValues.data(), mUiGenValues.size(), mUiGenOffset,
                        uiGenOverlay.c_str(), 0.0f, std::numeric_limits<float>::max(), ImVec2(0, 80));
                    ImGui::EndTooltip();
                }

                ImGui::Text("UI Draw Time:           %10.4f ms", renderData.rdUIDrawTime);

                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    float averageUiDraw = 0.0f;
                    for (const auto value : mUiDrawValues) {
                        averageUiDraw += value;
                    }
                    averageUiDraw /= static_cast<float>(mNumUiDrawValues);
                    std::string uiDrawOverlay = "now:     " + std::to_string(renderData.rdUIDrawTime) +
                        " ms\n30s avg: " + std::to_string(averageUiDraw) + " ms";
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("UI Draw");
                    ImGui::SameLine();
                    ImGui::PlotLines("##UIDrawTimes", mUiDrawValues.data(), mUiDrawValues.size(), mUiDrawOffset,
                        uiDrawOverlay.c_str(), 0.0f, std::numeric_limits<float>::max(), ImVec2(0, 80));
                    ImGui::EndTooltip();
                }
            }

            if (ImGui::CollapsingHeader("Camera")) {
                ImGui::Text("Camera Position: %s", glm::to_string(renderData.rdCameraWorldPosition).c_str());
                ImGui::Text("View Azimuth:    %6.1f", renderData.rdViewAzimuth);
                ImGui::Text("View Elevation:  %6.1f", renderData.rdViewElevation);

                ImGui::AlignTextToFramePadding();
                ImGui::Text("Field of View");
                ImGui::SameLine();
                ImGui::SliderInt("##FOV", &renderData.rdFieldOfView, 40, 150, "%d", flags);
            }

            if (ImGui::CollapsingHeader("Models")) {
                /* state is changed during model deletion, so save it first */
                bool modelListEmtpy = modInstData.miModelList.size() == 1;
                std::string selectedModelName = "None";

                /* Validate selected model index and reset if invalid */
                if (!modelListEmtpy) {
                    selectedModelName = modInstData.miModelList.at(modInstData.miSelectedEditorModel)->getModelFileName().c_str();
                }

                if (modelListEmtpy) {
                    ImGui::BeginDisabled();
                }

                ImGui::AlignTextToFramePadding();
                ImGui::Text("Models :");
                ImGui::SameLine();
                ImGui::PushItemWidth(200);
                if (ImGui::BeginCombo("##ModelCombo",
                    // avoid access the empty model vector
                    selectedModelName.c_str())) {
                    for (int i = 1; i < modInstData.miModelList.size(); ++i) {
                        const bool isSelected = (modInstData.miSelectedEditorModel == i);
                        if (ImGui::Selectable(modInstData.miModelList.at(i)->getModelFileName().c_str(), isSelected)) {
                            modInstData.miSelectedEditorModel = i;
                            selectedModelName = modInstData.miModelList.at(modInstData.miSelectedEditorModel)->getModelFileName().c_str();
                        }

                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();

                if (modelListEmtpy) {
                    ImGui::EndDisabled();
                }


                if (ImGui::Button("Import Model")) {
                    IGFD::FileDialogConfig config;
                    config.path = ".";
                    config.countSelectionMax = 1;
                    config.flags = ImGuiFileDialogFlags_Modal;
                    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                    ImGuiFileDialog::Instance()->OpenDialog("ChooseModelFile", "Choose Model File",
                        "Supported Model Files{.gltf,.glb,.obj,.fbx,.dae,.mdl,.md3,.pk3}", config);
                }

                if (ImGuiFileDialog::Instance()->Display("ChooseModelFile")) {
                    if (ImGuiFileDialog::Instance()->IsOk()) {
                        std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();

                        /* try to construct a relative path */
                        std::filesystem::path currentPath = std::filesystem::current_path();
                        std::string relativePath = std::filesystem::relative(filePathName, currentPath).generic_string();

                        if (!relativePath.empty()) {
                            filePathName = relativePath;
                        }
                        /* Windows does understand forward slashes, but std::filesystem preferres backslashes... */
                        std::replace(filePathName.begin(), filePathName.end(), '\\', '/');

                        if (!modInstData.miModelAddCallbackFunction(filePathName)) {
                            std::printf("%s error: unable to load model file '%s', unknown error \n", __FUNCTION__, filePathName.c_str());
                        }
                        else {
                            /* Model is queued but not loaded yet - selection will be updated after processing */
                            std::printf("Model queued for loading: %s\n", filePathName.c_str());

                        }
                    }
                    ImGuiFileDialog::Instance()->Close();
                }

                if (modelListEmtpy) {
                    ImGui::BeginDisabled();
                }

                ImGui::SameLine();
                if (ImGui::Button("Delete Model")) {
                    ImGui::OpenPopup("Delete Model?");
                }

                if (ImGui::BeginPopupModal("Delete Model?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Delete Model '%s'?", modInstData.miModelList.at(modInstData.miSelectedEditorModel)->getModelFileName().c_str());

                    /* cheating a bit to get buttons more to the center */
                    ImGui::Indent();
                    ImGui::Indent();
                    if (ImGui::Button("OK") || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                        modInstData.miModelDeleteCallbackFunction(modInstData.miModelList.at(modInstData.miSelectedEditorModel)->getModelFileName().c_str());

                        /* decrement selected model index to point to model that is in list before the deleted one */
                        if (modInstData.miSelectedEditorModel > 1) {
                            modInstData.miSelectedEditorModel -= 1;
                        }

                        /* reset model instance to first instance - if we have instances */
                        if (!modInstData.miAssimpInstances.empty()) {
                            modInstData.miSelectedEditorInstance = 1;
                        }

                        /* if we have only the null instance left, disable selection */
                        if (modInstData.miAssimpInstances.size() == 1) {
                            modInstData.miSelectedEditorInstance = 0;
                            editorData.eHighlightSelectedInstance = false;
                        }

                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                ImGui::SameLine();
                if (ImGui::Button("Create Instance")) {
                    std::shared_ptr<AvengModel> currentModel = modInstData.miModelList[modInstData.miSelectedEditorModel];
                    modInstData.miInstanceAddCallbackFunction(currentModel);
                    /* select new instance */
                    modInstData.miSelectedEditorInstance = modInstData.miAssimpInstances.size() - 1;
                }

                if (ImGui::Button("Create Multiple Instances")) {
                    std::shared_ptr<AvengModel> currentModel = modInstData.miModelList[modInstData.miSelectedEditorModel];
                    modInstData.miInstanceAddManyCallbackFunction(currentModel, editorData.eManyInstanceCreateNum);
                    modInstData.miSelectedEditorInstance = modInstData.miAssimpInstances.size() - 1;
                }
                ImGui::SameLine();
                ImGui::SliderInt("##MassInstanceCreation", &editorData.eManyInstanceCreateNum, 1, 100, "%d", flags);

                if (modelListEmtpy) {
                    ImGui::EndDisabled();
                }
            }

            if (ImGui::CollapsingHeader("Instances")) {
                bool modelListEmtpy = modInstData.miModelList.size() == 1;
                bool nullInstanceSelected = modInstData.miSelectedEditorInstance == 0;
                size_t numberOfInstances = modInstData.miAssimpInstances.size() - 1;

                ImGui::Text("Number of Instances: %ld", numberOfInstances);

                if (modelListEmtpy) {
                    ImGui::BeginDisabled();
                }

                ImGui::AlignTextToFramePadding();
                ImGui::Text("Hightlight Instance:");
                ImGui::SameLine();
                ImGui::Checkbox("##HighlightInstance", &editorData.eHighlightSelectedInstance);

                ImGui::AlignTextToFramePadding();
                ImGui::Text("Selected Instance  :");
                ImGui::SameLine();
                ImGui::PushButtonRepeat(true);
                if (ImGui::ArrowButton("##Left", ImGuiDir_Left) &&
                    modInstData.miSelectedEditorInstance > 1) {
                    modInstData.miSelectedEditorInstance--;
                }
                if (modelListEmtpy || nullInstanceSelected) {
                    ImGui::BeginDisabled();
                }

                ImGui::SameLine();
                ImGui::PushItemWidth(30);
                ImGui::DragInt("##SelInst", &modInstData.miSelectedEditorInstance, 1, 1,
                    modInstData.miAssimpInstances.size() - 1, "%3d", flags);
                ImGui::PopItemWidth();

                if (modelListEmtpy || nullInstanceSelected) 
                {
                    ImGui::EndDisabled();
                }

                ImGui::SameLine();
                if (ImGui::ArrowButton("##Right", ImGuiDir_Right) &&
                    modInstData.miSelectedEditorInstance < (modInstData.miAssimpInstances.size() - 1)) 
                {
                    modInstData.miSelectedEditorInstance++;
                }
                ImGui::PopButtonRepeat();

                if (modelListEmtpy) {
                    ImGui::EndDisabled();
                }

                if (modelListEmtpy || nullInstanceSelected) {
                    ImGui::BeginDisabled();
                }

                // Clamp here for DragInt
                modInstData.miSelectedEditorInstance = std::clamp(modInstData.miSelectedEditorInstance, 0,
                    static_cast<int>(modInstData.miAssimpInstances.size() - 1));

                InstanceSettings settings;
                if (numberOfInstances > 0) {
                    settings = modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance)->getInstanceSettings();
                }

                if (ImGui::Button("Center This Instance")) {
                    std::shared_ptr<AssimpInstance> currentInstance = modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance);
                    // Callback Function - Center Selected Instance
                    modInstData.miInstanceCenterCallbackFunctionEditor(currentInstance);
                }

                ImGui::SameLine();

                /* we MUST retain the last model */
                unsigned int numberOfInstancesPerModel = 0;
                if (modInstData.miAssimpInstances.size() > 1) {
                    std::shared_ptr<AssimpInstance> currentInstance = modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance);
                    std::string currentModelName = currentInstance->getModel()->getModelFileName();
                    numberOfInstancesPerModel = modInstData.miAssimpInstancesPerModel[currentModelName].size();
                }

                if (numberOfInstancesPerModel < 2) {
                    ImGui::BeginDisabled();
                }

                ImGui::SameLine();
                if (ImGui::Button("Delete Instance")) {
                    std::shared_ptr<AssimpInstance> currentInstance = modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance);
                    // Callback Function - Delete Instance
                    modInstData.miInstanceDeleteCallbackFunction(currentInstance);

                    /* hard reset for now */
                    if (modInstData.miSelectedEditorInstance > 1) {
                        modInstData.miSelectedEditorInstance -= 1;
                    }
                    settings = modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance)->getInstanceSettings();
                }

                if (numberOfInstancesPerModel < 2) {
                    ImGui::EndDisabled();
                }

                if (ImGui::Button("Clone Instance")) {
                    std::shared_ptr<AssimpInstance> currentInstance = modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance);
                    // Callback Function - Clone Instance
                    modInstData.miInstanceCloneCallbackFunction(currentInstance);

                    /* reset to last position for now */
                    modInstData.miSelectedEditorInstance = modInstData.miAssimpInstances.size() - 1;

                    /* read back settings for UI */
                    settings = modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance)->getInstanceSettings();
                }

                if (ImGui::Button("Create Multiple Clones")) {
                    std::shared_ptr<AssimpInstance> currentInstance = modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance);
                    // Callback function - Clone Many
                    modInstData.miInstanceCloneManyCallbackFunction(currentInstance, editorData.eManyInstanceCloneNum);

                    /* reset to last position for now */
                    modInstData.miSelectedEditorInstance = modInstData.miAssimpInstances.size() - 1;

                    /* read back settings for UI */
                    settings = modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance)->getInstanceSettings();
                }
                ImGui::SameLine();
                ImGui::SliderInt("##MassInstanceCloning", &editorData.eManyInstanceCloneNum, 1, 100, "%d", flags);

                if (modelListEmtpy || nullInstanceSelected) {
                    ImGui::EndDisabled();
                }

                /* get the new size, in case of a deletion */
                numberOfInstances = modInstData.miAssimpInstances.size() - 1;

                std::string baseModelName = "None";
                if (numberOfInstances > 0 && !nullInstanceSelected) {
                    baseModelName = modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance)->getModel()->getModelFileName();
                }
                ImGui::Text("Base Model: %s", baseModelName.c_str());

                if (numberOfInstances == 0 || nullInstanceSelected) {
                    ImGui::BeginDisabled();
                }

                ImGui::AlignTextToFramePadding();
                ImGui::Text("Swap Y and Z axes:     ");
                ImGui::SameLine();
                ImGui::Checkbox("##ModelAxisSwap", &settings.isSwapYZAxis);

                ImGui::AlignTextToFramePadding();
                ImGui::Text("Model Pos (X/Y/Z):     ");
                ImGui::SameLine();
                ImGui::SliderFloat3("##ModelPos", glm::value_ptr(settings.isWorldPosition),
                    -25.0f, 25.0f, "%.3f", flags);

                ImGui::AlignTextToFramePadding();
                ImGui::Text("Model Rotation (X/Y/Z):");
                ImGui::SameLine();
                ImGui::SliderFloat3("##ModelRot", glm::value_ptr(settings.isWorldRotation),
                    -180.0f, 180.0f, "%.3f", flags);

                ImGui::AlignTextToFramePadding();
                ImGui::Text("Model Scale:           ");
                ImGui::SameLine();
                ImGui::SliderFloat("##ModelScale", &settings.isScale,
                    0.001f, 10.0f, "%.4f", flags);

                if (ImGui::Button("Reset Instance Values")) {
                    InstanceSettings defaultSettings{};

                    /* save and restore index positions */
                    int instanceIndex = settings.isInstanceIndexPosition;
                    settings = defaultSettings;
                    settings.isInstanceIndexPosition = instanceIndex;
                }

                if (numberOfInstances == 0 || nullInstanceSelected) {
                    ImGui::EndDisabled();
                }

                if (numberOfInstances > 0) {
                    modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance)->setInstanceSettings(settings);
                }
            }

            if (ImGui::CollapsingHeader("Animations")) {
                size_t numberOfInstances = modInstData.miAssimpInstances.size() - 1;

                InstanceSettings settings;
                size_t numberOfClips = 0;
                if (numberOfInstances > 0) {
                    settings = modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance)->getInstanceSettings();
                    numberOfClips = modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance)->getModel()->getAnimClips().size();
                }

                if (numberOfInstances > 0 && numberOfClips > 0) {
                    std::vector<std::shared_ptr<AssimpAnimClip>> animClips = modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance)->getModel()->getAnimClips();

                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Animation Clip:");
                    ImGui::SameLine();
                    if (ImGui::BeginCombo("##ClipCombo",
                        animClips.at(settings.isAnimClipNr)->getClipName().c_str())) {
                        for (int i = 0; i < animClips.size(); ++i) {
                            const bool isSelected = (settings.isAnimClipNr == i);
                            if (ImGui::Selectable(animClips.at(i)->getClipName().c_str(), isSelected)) {
                                settings.isAnimClipNr = i;
                            }

                            if (isSelected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Replay Speed:  ");
                    ImGui::SameLine();
                    ImGui::SliderFloat("##ClipSpeed", &settings.isAnimSpeedFactor, 0.0f, 2.0f, "%.3f", flags);
                }
                else {
                    /* TODO: better solution if no instances or no clips are found */
                    ImGui::BeginDisabled();

                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Animation Clip:");
                    ImGui::SameLine();
                    ImGui::BeginCombo("##ClipComboDisabled", "None");

                    float playSpeed = 1.0f;
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Replay Speed:  ");
                    ImGui::SameLine();
                    ImGui::SliderFloat("##ClipSpeedDisabled", &playSpeed, 0.0f, 2.0f, "%.3f", flags);

                    ImGui::EndDisabled();
                }

                if (numberOfInstances > 0) {
                    modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance)->setInstanceSettings(settings);
                }
            }

            ImGui::End();
        }

    }

    void AvengImgui::handleMouseButtonEvents(int button, int action, int mods) {

        editorData.eMouseLastClickX = editorData.eMouseXPos;
        editorData.eMouseLastClickY = editorData.eMouseYPos;

        /* forward to ImGui */
        ImGuiIO& io = ImGui::GetIO();
        if (button >= 0 && button < ImGuiMouseButton_COUNT) {
            io.AddMouseButtonEvent(button, action == GLFW_PRESS);
        }

        /* hide from application if above ImGui window */
        if (io.WantCaptureMouse && io.WantCaptureMouseUnlessPopupClose) {
            return;
        }

        /* trigger selection when left button has been released */
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
            std::cout << "ImGUI: Mouse Click!!" << std::endl;
            editorData.eMousePick = true; // The mouse was clicked
            renderData.rdInstanceEditMode = instanceEditMode::move;
        }

        /* move instance around with middle button pressed */
        if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS) {
            editorData.eMouseMove = true;
            if (glfwGetKey(window.getGLFWwindow(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
                editorData.eMouseMoveVerticalShiftKey = GLFW_KEY_LEFT_SHIFT;
                editorData.eMouseMoveVertical = true;
            }
            if (glfwGetKey(window.getGLFWwindow(), GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
                editorData.eMouseMoveVerticalShiftKey = GLFW_KEY_RIGHT_SHIFT;
                editorData.eMouseMoveVertical = true;
            }
        }

        if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_RELEASE) {
            editorData.eMouseMove = false;
        }

        /* move camera view while right button is hold   */
        if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
            editorData.eMouseLock = true;
        }
        if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE) {
            editorData.eMouseLock = false;
        }

        if (editorData.eMouseLock) {
            glfwSetInputMode(window.getGLFWwindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            /* enable raw mode if possible */
            if (glfwRawMouseMotionSupported()) {
                glfwSetInputMode(window.getGLFWwindow(), GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
            }
        }
        else {
            glfwSetInputMode(window.getGLFWwindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }

    void AvengImgui::hideMouse(bool hide) {
        /* v1.89.8 removed the check for disabled mouse cursor in GLFW
         * we need to ignore the mouse postion if the mouse lock is active */
        ImGuiIO& io = ImGui::GetIO();

        if (hide) {
            io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        }
        else {
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        }
    }

    void AvengImgui::handleMousePositionEvents(double xPos, double yPos, bool rmbDown) {
        //std::cout << "Handling mouse Position Event (Editor) " << std::endl;
        /* forward to ImGui */
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent((float)xPos, (float)yPos);

        /* hide from application if above ImGui window */
        if (io.WantCaptureMouse && io.WantCaptureMouseUnlessPopupClose) {
            return;
        }

        /* calculate relative movement from last position */
        int mouseMoveRelX = static_cast<int>(xPos) - editorData.eMouseXPos;
        int mouseMoveRelY = static_cast<int>(yPos) - editorData.eMouseYPos;

        if (editorData.eMouseLock) {
            renderData.rdViewAzimuth += mouseMoveRelX / 10.0;
            /* keep between 0 and 360 degree */
            if (renderData.rdViewAzimuth < 0.0) {
                renderData.rdViewAzimuth += 360.0;
            }
            if (renderData.rdViewAzimuth >= 360.0) {
                renderData.rdViewAzimuth -= 360.0;
            }

            renderData.rdViewElevation -= mouseMoveRelY / 10.0;
            /* keep between -89 and +89 degree */
            renderData.rdViewElevation = std::clamp(renderData.rdViewElevation, -89.0f, 89.0f);

        }

        if (editorData.eMouseMove) {
            std::cout << "True" << std::endl;
            if (modInstData.miSelectedEditorInstance != 0) {
                std::cout << "miSelectedEditorInstance != 0" << std::endl;
                InstanceSettings settings = modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance)->getInstanceSettings();

                float mouseXScaled = mouseMoveRelX / 20.0f; // This divosor makes the movement more subtle
                float mouseYScaled = mouseMoveRelY / 20.0f;
                float sinAzimuth = std::sin(glm::radians(renderData.rdViewAzimuth));
                float cosAzimuth = std::cos(glm::radians(renderData.rdViewAzimuth));

                float modelDistance = glm::length(renderData.rdCameraWorldPosition - settings.isWorldPosition) / 50.0f;

                if (editorData.eMouseMoveVertical) {
                    switch (renderData.rdInstanceEditMode) {
                    case instanceEditMode::move:
                        settings.isWorldPosition.y -= mouseYScaled * modelDistance;
                        break;
                    case instanceEditMode::rotate:
                        settings.isWorldRotation.y -= mouseXScaled * 5.0f;
                        /* keep between -180 and 180 degree */
                        if (settings.isWorldRotation.y < -180.0f) {
                            settings.isWorldRotation.y += 360.0f;
                        }
                        if (settings.isWorldRotation.y >= 180.0f) {
                            settings.isWorldRotation.y -= 360.0f;
                        }
                        break;
                    case instanceEditMode::scale:
                        /* uniform scale, do nothing here  */
                        break;
                    }
                }
                else {
                    switch (renderData.rdInstanceEditMode) {
                    case instanceEditMode::move:
                        settings.isWorldPosition.x += mouseXScaled * modelDistance * cosAzimuth - mouseYScaled * modelDistance * sinAzimuth;
                        settings.isWorldPosition.z += mouseXScaled * modelDistance * sinAzimuth + mouseYScaled * modelDistance * cosAzimuth;
                        break;
                    case instanceEditMode::rotate:
                        settings.isWorldRotation.z -= (mouseXScaled * cosAzimuth - mouseYScaled * sinAzimuth) * 5.0f;
                        settings.isWorldRotation.x += (mouseXScaled * sinAzimuth + mouseYScaled * cosAzimuth) * 5.0f;

                        /* keep between -180 and 180 degree */
                        if (settings.isWorldRotation.z < -180.0f) {
                            settings.isWorldRotation.z += 360.0f;
                        }
                        if (settings.isWorldRotation.z >= 180.0f) {
                            settings.isWorldRotation.z -= 360.0f;
                        }

                        if (settings.isWorldRotation.x < -180.0f) {
                            settings.isWorldRotation.x += 360.0f;
                        }
                        if (settings.isWorldRotation.x >= 180.0f) {
                            settings.isWorldRotation.x -= 360.0f;
                        }
                        break;
                    case instanceEditMode::scale:
                        settings.isScale -= mouseYScaled / 2.0f;
                        settings.isScale = std::max(0.001f, settings.isScale);
                        break;
                    }
                }

                modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance)->setInstanceSettings(settings);
            }
        }

        /* save old values */
        editorData.eMouseXPos = static_cast<int>(xPos);
        editorData.eMouseYPos = static_cast<int>(yPos);
        //std::cout << "Mouse: (" << editorData.eMouseXPos << ", " << editorData.eMouseYPos << ")" << std::endl;
    }

}