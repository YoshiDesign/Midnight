#include "aveng_imgui.h"
#include "CoreVK/EngineDevice.h"
#include "Core/aveng_window.h"
#include "Core/aveng_model.h"
#include "Core/Modeling/AssimpAnimClip.h"
#include "Core/Modeling/AssimpInstance.h"
#include "Core/Modeling/InstanceSettings.h"
#include "CoreVK/VertexBuffer.h"
#include "Editor/API/IEditorUIAPI.h"

namespace aveng {

    /*
    * Note that editorData is not pointer. If it were, we could hot-swap editor data (weaker invariant)
    * Using a reference is cleaner/stronger invariance, but not without obvious limitations.
    * If AvengImgui were able to exist without an Editor, a pointer could easily make more sense.
    *
        // Good for a hybrid approach. Use as a pointer with guarantees via editorData()
        EditorData& editorData() {
            assert(data_ && "EditorData not set");
            return *data_;
        }
    */

    static void sanitizeSelection(EditorData& ed, const IEditorUIAPI& api) {
        auto alive = [&](AnyInstanceHandle h) {
            InstanceView v{};
            return api.uiTryGetInstance(h, v);
        };

        erase_if(ed.selectedMany, [&](auto h) { return !alive(h); });

        if (!alive(ed.primarySelection)) {
            ed.primarySelection = ed.selectedMany.empty() ? AnyInstanceHandle{} : ed.selectedMany.back();
        }
    }

    AvengImgui::AvengImgui(
        VkRenderData& _renderData, 
        IEditorUIAPI& api,
        EditorData& editorData, 
        AvengWindow& _window, 
        EngineDevice& _engineDevice)
        : 
        renderData{ _renderData }, 
        editorData{ editorData }, 
        engineDevice { _engineDevice}, 
        window{ _window },
        api_{ api }
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

        ImGuiStyle& style = ImGui::GetStyle();

        // --------------------------------------------------
        // Layout & Spacing
        // --------------------------------------------------
        style.WindowPadding = ImVec2(14.0f, 12.0f);
        style.FramePadding = ImVec2(12.0f, 8.0f);
        style.ItemSpacing = ImVec2(16.0f, 12.0f);
        style.ItemInnerSpacing = ImVec2(10.0f, 6.0f);
        style.IndentSpacing = 22.0f;
        style.ScrollbarSize = 14.0f;
        style.GrabMinSize = 12.0f;

        // --------------------------------------------------
        // Rounding (soft but not “mobile UI”)
        // --------------------------------------------------
        style.WindowRounding = 6.0f;
        style.ChildRounding = 5.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 5.0f;
        style.ScrollbarRounding = 6.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 5.0f;

        // --------------------------------------------------
        // Borders & Alignment
        // --------------------------------------------------
        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.TabBorderSize = 0.0f;
        style.WindowTitleAlign = ImVec2(0.5f, 0.5f); // centered titles
        style.ButtonTextAlign = ImVec2(0.5f, 0.5f);

        // --------------------------------------------------
        // Colors — Midnight Palette
        // --------------------------------------------------
        ImVec4* colors = style.Colors;

        // Base
        colors[ImGuiCol_Text] = ImVec4(0.88f, 0.90f, 0.94f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.45f, 0.48f, 0.55f, 1.00f);

        colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.08f, 0.10f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.10f, 0.13f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.09f, 0.12f, 0.98f);

        // Frames
        colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.16f, 0.20f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.22f, 0.28f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.26f, 0.33f, 1.00f);

        // Titles
        colors[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.06f, 0.08f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.10f, 0.14f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.05f, 0.06f, 0.08f, 0.75f);

        // Buttons (signature Midnight blue)
        colors[ImGuiCol_Button] = ImVec4(0.20f, 0.45f, 0.75f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.55f, 0.90f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.38f, 0.65f, 1.00f);

        // Headers (tree nodes, collapsing headers)
        colors[ImGuiCol_Header] = ImVec4(0.18f, 0.22f, 0.30f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.32f, 0.42f, 1.00f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.22f, 0.28f, 0.38f, 1.00f);

        // Tabs
        colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.25f, 0.45f, 0.75f, 1.00f);
        colors[ImGuiCol_TabActive] = ImVec4(0.18f, 0.30f, 0.55f, 1.00f);
        colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.12f, 0.16f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.18f, 0.24f, 1.00f);

        // Separators
        colors[ImGuiCol_Separator] = ImVec4(0.22f, 0.25f, 0.32f, 1.00f);
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.35f, 0.45f, 0.65f, 1.00f);
        colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.55f, 0.85f, 1.00f);

        // Scrollbar
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.06f, 0.07f, 0.09f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.24f, 0.32f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.36f, 0.48f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.35f, 0.42f, 0.58f, 1.00f);

        // Checkmarks & sliders
        colors[ImGuiCol_CheckMark] = ImVec4(0.45f, 0.70f, 1.00f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.55f, 0.90f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.35f, 0.65f, 1.00f, 1.00f);

        // Docking
        //colors[ImGuiCol_DockingPreview] = ImVec4(0.35f, 0.55f, 0.85f, 0.45f);
        //colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.06f, 0.07f, 0.09f, 1.00f);

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
            ImGui::Checkbox("All Cameras", &show_all_cameras);

            ImGui::Text(
                "Last Click At: (%d, %d)", editorData.eMouseLastClickX, editorData.eMouseLastClickY);

            ImGui::Text(
                "Camera Position:\t(%.03lf, %.03lf, %.03lf)", editorData.cameraTransform.translation.x, editorData.cameraTransform.translation.y, editorData.cameraTransform.translation.z);
            ImGui::Text(
                "Camera Rotation:\t(%.03lf, %.03lf, %.03lf)", editorData.cameraTransform.rotation.x, editorData.cameraTransform.rotation.y, editorData.cameraTransform.rotation.z);

            if (show_all_cameras) {
                int i = 0;
                ImGui::Text("_____________________________");
                for (const auto& cam : editorData.cameraDebugList) {
                    std::string name(cam.name);
                    ImGui::Text("Camera ID[%d]:\t%s", i, name.c_str());
                    ImGui::Text("Active:\t%d", cam.active);
                    ImGui::Text(
                        "Position:\t(%.03lf, %.03lf, %.03lf)",cam.transform.translation.x, cam.transform.translation.y, cam.transform.translation.z);
                    ImGui::Text(
                        "Rotation:\t(%.03lf, %.03lf, %.03lf)", cam.transform.rotation.x, cam.transform.rotation.y, cam.transform.rotation.z);
                    ++i;
                }
                ImGui::Text("_____________________________");
            }

            if (ImGui::Button("Play HolyShip")) {
                editorData.requestPlay("holyship");
            }
            if (ImGui::Button("Play Starfield")) {
                editorData.requestPlay("starfield");
            }

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

            if (ImGui::CollapsingHeader("Models", ImGuiTreeNodeFlags_DefaultOpen)) {

                ImGui::AlignTextToFramePadding();
                ImGui::Text("%d Models in memory", api_.nModels() - 1);

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

                        ModelRef ref = api_.uiGetOrLoadModel(filePathName);

                        /* This is now the "AssetKey" */
                        if (!ref) {
                            std::printf("%s error: unable to load model file '%s', unknown error \n", __FUNCTION__, filePathName.c_str());
                        }
                        else {
                            /* Model is queued but not loaded yet - selection will be updated after processing */
                            std::printf("Model queued for loading: %s\n", filePathName.c_str());
                            std::printf("Model ID: %d\n", ref.id);
                            editorData.selectedModelId = ref.id;
                            editorData.selectedModelKey = filePathName;
                            editorData.implicitSelection = true;
                        }
                    }
                    ImGuiFileDialog::Instance()->Close();
                }

                /* Acquire Model List */
                const std::vector<UiModelRow> models = api_.uiListModels();

                // Build display string for current selection
                std::string selectedLabel = "<none>";
                if (editorData.selectedModelId != 0) {
                    auto it = std::find_if(models.begin(), models.end(),
                        [&](const UiModelRow& r) { return r.id == editorData.selectedModelId; });
                    if (it != models.end()) {
                        selectedLabel = std::string(it->key.c_str());
                        selectedLabel += it->animated ? " [Animated]" : " [Static]";
                    }
                    else if (!editorData.implicitSelection){
                        // Selected model no longer exists
                        editorData.selectedModelId = NullModelId;
                        editorData.selectedModelKey = "[No Selection]";
                    }
                }
                // ImGui::PopItemWidth();

                if (ImGui::BeginCombo("Selected Model", selectedLabel.c_str()))
                {
                    for (const UiModelRow& r : models)
                    {
                        if (r.id == NullModelId) continue;

                        const bool isSelected = (r.id == editorData.selectedModelId);

                        std::string label = std::string(r.key.c_str());
                        label += r.animated ? " [Animated]" : " [Static]";

                        if (ImGui::Selectable(label.c_str(), isSelected))
                        {
                            editorData.selectedModelId = r.id;
                            editorData.selectedModelKey = r.key;

                            // Optional: clear instance selection when switching models (feel free to remove)
                            // editorData.selectedMany.clear();
                            // editorData.primarySelection = AnyInstanceHandle{};
                        }

                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::BeginDisabled(api_.nModels() == 1);
                
                /// Destroy Models
                if (ImGui::Button("Delete Model")) {
                    ImGui::OpenPopup("Delete Model?");
                }

                if (ImGui::BeginPopupModal("Delete Model?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Delete Model '%s'?", editorData.selectedModelKey.c_str());

                    /* cheating a bit to get buttons more to the center */
                    ImGui::Indent();
                    ImGui::Indent();
                    if (ImGui::Button("OK") || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                        api_.uiUnloadModel(editorData.selectedModelKey);

                        editorData.selectedModelKey = "";
                        editorData.selectedModelId = NullModelId;
                        editorData.implicitSelection = false;
     
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                
                ImGui::EndDisabled();
                

                //ImGui::SameLine();
                //if (ImGui::Button("Create Instance")) {
                //    ModelRef currentModelRef = api_.uiListModelRefs().at(editorData.selectedModelIndex);

                //    if (currentModelRef.isAnimated) {
                //        api_.uiSpawn(currentModelRef, AnimatedCreateSettings{});
                //    }
                //    else {
                //        api_.uiSpawn(currentModelRef, TransformSettings{});
                //    }

                //    /* select new instance */
                //    /// TODO editorData.selectedInstanceHandle = modInstData.miAssimpInstances.size() - 1;
                //}

      /*          if (ImGui::Button("Create Multiple Instances")) {
                    ModelRef currentModelRef = api_.uiListModelRefs().at(editorData.selectedModelIndex);
                    if (currentModelRef.isAnimated) {
                        std::vector<AnimatedCreateSettings> settings(editorData.eManyInstanceCreateNum);
                        api_.uiSpawnMany(currentModelRef, settings, settings.size());
                    }
                    else {
                        std::vector<TransformSettings> settings(editorData.eManyInstanceCreateNum);
                        api_.uiSpawnMany(currentModelRef, settings, settings.size());
                    }
                    /// TODO editorData.selectedInstanceHandle = modInstData.miAssimpInstances.size() - 1;
                }
                ImGui::SameLine();
                ImGui::SliderInt("##MassInstanceCreation", &editorData.eManyInstanceCreateNum, 1, 100, "%d", flags);
                */

                /// END MODELS
            }

            if (ImGui::CollapsingHeader("Instances")) {
                                
                ImGui::BeginDisabled(editorData.selectedModelId == NullModelId);
                

                ModelMeta modelMeta{};
                bool model_ready = api_.uiIsModelLoaded(editorData.selectedModelId, modelMeta);

                if (ImGui::Button("Create Instance")) {
                    if (model_ready && editorData.selectedModelId != NullModelId) {
                        if (modelMeta.animated) { /// Create Animated Instance
                            api_.uiSpawn(
                                api_.uiGetOrLoadModel(editorData.selectedModelKey),
                                AnimatedCreateSettings{});
                        }
                        else {
                            api_.uiSpawn( /// Create Static Instance
                                api_.uiGetOrLoadModel(editorData.selectedModelKey), 
                                TransformSettings{});
                        }
                    }
                }

                ImGui::SameLine();
                if (ImGui::Button("Delete Instance")) {

                }

                ImGui::EndDisabled();
                
                const std::vector<UiInstanceRow> rows = api_.uiListInstances();

                // Multi-select rules
                const bool ctrl = ImGui::GetIO().KeyCtrl;
                const bool shift = ImGui::GetIO().KeyShift; // (range selection later)

                // Optional: filter to selected model
                static bool filterToSelectedModel = false;
                ImGui::Checkbox("Filter to selected model", &filterToSelectedModel);

                ImGui::Separator();

                // Header line
                ImGui::TextUnformatted("Type");
                ImGui::SameLine(60.0f);
                ImGui::TextUnformatted("ModelId");
                ImGui::SameLine(140.0f);
                ImGui::TextUnformatted("Position");
                ImGui::Separator();

                // Scrollable list region
                ImGui::BeginChild("##InstanceList", ImVec2(0, 220), true, ImGuiWindowFlags_HorizontalScrollbar);

                int visibleIndex = 0;
                for (const UiInstanceRow& r : rows)
                {
                    if (filterToSelectedModel && editorData.selectedModelId != 0 && r.modelId != editorData.selectedModelId)
                        continue;

                    const bool isSelected = containsHandle(editorData.selectedMany, r.handle);

                    // Build a readable label (must be unique per row for ImGui)
                    // NOTE: we include "##" with a unique suffix so display text can be pretty.
                    std::string display =
                        std::string(r.animated ? "[A] " : "[S] ")
                        + "Model " + std::to_string((uint32_t)r.modelId)
                        + "  pos(" + std::to_string(r.position.x) + ", "
                        + std::to_string(r.position.y) + ", "
                        + std::to_string(r.position.z) + ")";

                    // Make ID unique using the handle (index/generation)
                    // We can’t easily stringify variant cleanly without a helper, so we use a stable-ish suffix:
                    // You can replace uiHandleDebugId(...) with your own helper later.
                    display += "##";
                    display += std::to_string(visibleIndex++);

                    if (ImGui::Selectable(display.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns))
                    {
                        // Selection semantics
                        if (!ctrl && !shift)
                        {
                            editorData.selectedMany.clear();
                            addUnique(editorData.selectedMany, r.handle);
                            editorData.primarySelection = r.handle;
                        }
                        else if (ctrl)
                        {
                            if (isSelected) {
                                eraseHandle(editorData.selectedMany, r.handle);

                                // Keep primarySelection valid-ish
                                if (editorData.primarySelection == r.handle) {
                                    editorData.primarySelection = editorData.selectedMany.empty()
                                        ? AnyInstanceHandle{} : editorData.selectedMany.back();
                                }
                            }
                            else {
                                addUnique(editorData.selectedMany, r.handle);
                                editorData.primarySelection = r.handle;
                            }
                        }
                        else if (shift)
                        {
                            // Range selection later once you commit to a stable visible ordering + anchor index
                            // For now, behave like single-select:
                            editorData.selectedMany.clear();
                            addUnique(editorData.selectedMany, r.handle);
                            editorData.primarySelection = r.handle;
                        }
                    }

                    // Right click context menu (optional but useful)
                    if (ImGui::BeginPopupContextItem())
                    {
                        if (ImGui::MenuItem("Select Only"))
                        {
                            editorData.selectedMany.clear();
                            addUnique(editorData.selectedMany, r.handle);
                            editorData.primarySelection = r.handle;
                        }
                        if (ImGui::MenuItem("Toggle Selection"))
                        {
                            if (containsHandle(editorData.selectedMany, r.handle)) {
                                eraseHandle(editorData.selectedMany, r.handle);
                            }
                            else {
                                addUnique(editorData.selectedMany, r.handle);
                                editorData.primarySelection = r.handle;
                            }
                        }
                        if (ImGui::MenuItem("Destroy"))
                        {
                            api_.uiDestroy(r.handle);
                            // Optionally call sanitizeSelection(editorData, api_) after you implement it
                        }
                        ImGui::EndPopup();
                    }
                }

                ImGui::EndChild();

                ImGui::Separator();

                // Action buttons on current selection
                const int selCount = (int)editorData.selectedMany.size();
                ImGui::Text("Selected: %d", selCount);

                ImGui::BeginDisabled(selCount == 0);
                if (ImGui::Button("Destroy Selected"))
                {
                    api_.uiDestroyMany(editorData.selectedMany);
                    // sanitizeSelection(editorData, api_);
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Selection"))
                {
                    editorData.selectedMany.clear();
                    editorData.primarySelection = AnyInstanceHandle{};
                }
                ImGui::EndDisabled();

                // Optional: show primary selection details (queries happen via API, not direct pool access)
                if (editorData.primarySelection.index() != 0) // not monostate
                {
                    InstanceView view{};
                    if (api_.uiTryGetInstance(editorData.primarySelection, view))
                    {
                        ImGui::Separator();
                        ImGui::TextUnformatted("Primary Selection");
                        ImGui::Text("ModelId: %u", (uint32_t)view.modelId);
                        ImGui::Text("Animated: %s", view.animated ? "true" : "false");
                        // Add transform fields here once you wire set/get transform through api_
                    }
                }

            }

            if (ImGui::CollapsingHeader("Animations")) {
                //size_t numberOfInstances = modInstData.miAssimpInstances.size() - 1;

                //InstanceSettings settings;
                //size_t numberOfClips = 0;
                //if (numberOfInstances > 0) {
                //    settings = modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance)->getInstanceSettings();
                //    numberOfClips = modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance)->getModel()->getAnimClips().size();
                //}

                //if (numberOfInstances > 0 && numberOfClips > 0) {
                //    std::vector<std::shared_ptr<AssimpAnimClip>> animClips = modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance)->getModel()->getAnimClips();

                //    ImGui::AlignTextToFramePadding();
                //    ImGui::Text("Animation Clip:");
                //    ImGui::SameLine();
                //    if (ImGui::BeginCombo("##ClipCombo",
                //        animClips.at(settings.isAnimClipNr)->getClipName().c_str())) {
                //        for (int i = 0; i < animClips.size(); ++i) {
                //            const bool isSelected = (settings.isAnimClipNr == i);
                //            if (ImGui::Selectable(animClips.at(i)->getClipName().c_str(), isSelected)) {
                //                settings.isAnimClipNr = i;
                //            }

                //            if (isSelected) {
                //                ImGui::SetItemDefaultFocus();
                //            }
                //        }
                //        ImGui::EndCombo();
                //    }

                //    ImGui::AlignTextToFramePadding();
                //    ImGui::Text("Replay Speed:  ");
                //    ImGui::SameLine();
                //    ImGui::SliderFloat("##ClipSpeed", &settings.isAnimSpeedFactor, 0.0f, 2.0f, "%.3f", flags);
                //}
                //else {
                //    /* TODO: better solution if no instances or no clips are found */
                //    ImGui::BeginDisabled();

                //    ImGui::AlignTextToFramePadding();
                //    ImGui::Text("Animation Clip:");
                //    ImGui::SameLine();
                //    ImGui::BeginCombo("##ClipComboDisabled", "None");

                //    float playSpeed = 1.0f;
                //    ImGui::AlignTextToFramePadding();
                //    ImGui::Text("Replay Speed:  ");
                //    ImGui::SameLine();
                //    ImGui::SliderFloat("##ClipSpeedDisabled", &playSpeed, 0.0f, 2.0f, "%.3f", flags);

                //    ImGui::EndDisabled();
                //}

                //if (numberOfInstances > 0) {
                //    modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance)->setInstanceSettings(settings);
                //}
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
            //if (modInstData.miSelectedEditorInstance != 0) {
            //    std::cout << "miSelectedEditorInstance != 0" << std::endl;
            //    InstanceSettings settings = modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance)->getInstanceSettings();

            //    float mouseXScaled = mouseMoveRelX / 20.0f; // This divosor makes the movement more subtle
            //    float mouseYScaled = mouseMoveRelY / 20.0f;
            //    float sinAzimuth = std::sin(glm::radians(renderData.rdViewAzimuth));
            //    float cosAzimuth = std::cos(glm::radians(renderData.rdViewAzimuth));

            //    float modelDistance = glm::length(renderData.rdCameraWorldPosition - settings.isWorldPosition) / 50.0f;

            //    if (editorData.eMouseMoveVertical) {
            //        switch (renderData.rdInstanceEditMode) {
            //        case instanceEditMode::move:
            //            settings.isWorldPosition.y -= mouseYScaled * modelDistance;
            //            break;
            //        case instanceEditMode::rotate:
            //            settings.isWorldRotation.y -= mouseXScaled * 5.0f;
            //            /* keep between -180 and 180 degree */
            //            if (settings.isWorldRotation.y < -180.0f) {
            //                settings.isWorldRotation.y += 360.0f;
            //            }
            //            if (settings.isWorldRotation.y >= 180.0f) {
            //                settings.isWorldRotation.y -= 360.0f;
            //            }
            //            break;
            //        case instanceEditMode::scale:
            //            /* uniform scale, do nothing here  */
            //            break;
            //        }
            //    }
            //    else {
            //        switch (renderData.rdInstanceEditMode) {
            //        case instanceEditMode::move:
            //            settings.isWorldPosition.x += mouseXScaled * modelDistance * cosAzimuth - mouseYScaled * modelDistance * sinAzimuth;
            //            settings.isWorldPosition.z += mouseXScaled * modelDistance * sinAzimuth + mouseYScaled * modelDistance * cosAzimuth;
            //            break;
            //        case instanceEditMode::rotate:
            //            settings.isWorldRotation.z -= (mouseXScaled * cosAzimuth - mouseYScaled * sinAzimuth) * 5.0f;
            //            settings.isWorldRotation.x += (mouseXScaled * sinAzimuth + mouseYScaled * cosAzimuth) * 5.0f;

            //            /* keep between -180 and 180 degree */
            //            if (settings.isWorldRotation.z < -180.0f) {
            //                settings.isWorldRotation.z += 360.0f;
            //            }
            //            if (settings.isWorldRotation.z >= 180.0f) {
            //                settings.isWorldRotation.z -= 360.0f;
            //            }

            //            if (settings.isWorldRotation.x < -180.0f) {
            //                settings.isWorldRotation.x += 360.0f;
            //            }
            //            if (settings.isWorldRotation.x >= 180.0f) {
            //                settings.isWorldRotation.x -= 360.0f;
            //            }
            //            break;
            //        case instanceEditMode::scale:
            //            settings.isScale -= mouseYScaled / 2.0f;
            //            settings.isScale = std::max(0.001f, settings.isScale);
            //            break;
            //        }
            //    }

            //    modInstData.miAssimpInstances.at(modInstData.miSelectedEditorInstance)->setInstanceSettings(settings);
            //}
        }

        /* save old values */
        editorData.eMouseXPos = static_cast<int>(xPos);
        editorData.eMouseYPos = static_cast<int>(yPos);
        //std::cout << "Mouse: (" << editorData.eMouseXPos << ", " << editorData.eMouseYPos << ")" << std::endl;
    }

}