
#include "aveng_imgui.h"
// #include "Core/aveng_window.h"
#include "avpch.h"
#include "GUI/imgui.h"
#include "GUI/imgui_impl_glfw.h"
#include "GUI/imgui_impl_vulkan.h"

namespace aveng {

    AvengImgui::AvengImgui(EngineDevice& _device) : device{_device} {}

    // ok this just initializes imgui using the provided integration files. So in our case we need to
    // initialize the vulkan and glfw imgui implementations, since that's what our engine is built
    // using.
    void AvengImgui::init(AvengWindow& window, VkRenderPass renderPass, uint32_t imageCount)
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

        if (vkCreateDescriptorPool(device.device(), &pool_info, nullptr, &descriptorPool) != VK_SUCCESS) 
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
        init_info.Instance = device.instance();
        init_info.PhysicalDevice = device.physicalDevice();
        init_info.Device = device.device();
        init_info.QueueFamily = device.getGraphicsQueueFamily();
        init_info.Queue = device.graphicsQueue();

        // pipeline cache is a potential future optimization, ignoring for now
        init_info.PipelineCache = VK_NULL_HANDLE;
        init_info.DescriptorPool = descriptorPool;
        // todo, Implement a memory allocator library (VMA) sooner than later
        init_info.Allocator = VK_NULL_HANDLE;
        init_info.MinImageCount = 2;
        init_info.ImageCount = imageCount;
        init_info.CheckVkResultFn = check_vk_result;
        ImGui_ImplVulkan_Init(&init_info, renderPass);

        // upload fonts, this is done by recording and submitting a one time use command buffer
        // which can be done easily by using some existing helper functions on the EngineDevice object
        auto commandBuffer = device.beginSingleTimeCommands();
        ImGui_ImplVulkan_CreateFontsTexture(commandBuffer);
        device.endSingleTimeCommands(commandBuffer);

        // Cleanup the font object
        ImGui_ImplVulkan_DestroyFontUploadObjects();
    }

    AvengImgui::~AvengImgui() {
        vkDestroyDescriptorPool(device.device(), descriptorPool, nullptr);
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
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
    void AvengImgui::render(VkCommandBuffer commandBuffer) {
        ImGui::Render();
        ImDrawData* drawdata = ImGui::GetDrawData();
        ImGui_ImplVulkan_RenderDrawData(drawdata, commandBuffer);
    }

    void AvengImgui::runGUI() {

        //static float slider = 0.0f;
        static float value = 3.1415926f;
        static bool hasChanged = false;
        static int counter = 0;

        GameData& game_data = *context.game_data;

        {
            ImGui::Begin("Engine Info");
            ImGui::Text("Video Device:\t %s", device.properties.deviceName);
            ImGui::End();
        }

        {
            ImGui::Begin("Debug");

            ImGui::Checkbox("Player Debug", &show_player_controller_window);

            ImGui::Text(
                "Objects: %d", game_data.num_objs);
            ImGui::Text(
                "Flight Mode: %d", game_data.fly_mode);
            ImGui::Text(
                "Camera View:\t\t(%.03lf, %.03lf, %.03lf)", game_data.cameraView.x, game_data.cameraView.y, game_data.cameraView.z);
            ImGui::Text(
                "Camera Rotation:\t(%.03lf, %.03lf, %.03lf)", game_data.cameraRot.x, game_data.cameraRot.y, game_data.cameraRot.z);
            ImGui::Text(
                "Camera Position:\t(%.03lf, %.03lf, %.03lf)", game_data.cameraPos.x, game_data.cameraPos.y, game_data.cameraPos.z);
            ImGui::Text(
                "Player Rotation:\t(%.03lf, %.03lf, %.03lf)", game_data.playerRot.x,game_data.playerRot.y, game_data.playerRot.z);
            ImGui::Text(
                "Player Position:\t(%.03lf, %.03lf, %.03lf)", game_data.playerPos.x, game_data.playerPos.y, game_data.playerPos.z);
            ImGui::Text(
                "Mod Rotation:\t(%.03lf, %.03lf, %.03lf)", game_data.modRot.x, game_data.modRot.y, game_data.modRot.z);
            ImGui::Text(
                "Mod Position:\t(%.03lf, %.03lf, %.03lf)", game_data.modPos.x, game_data.modPos.y, game_data.modPos.z);
            ImGui::Text(
                "Forward Direction:\t(%.03lf, %.03lf, %.03lf)", game_data.forwardDir.x, game_data.forwardDir.y, game_data.forwardDir.z);
            ImGui::SliderFloat("float", &game_data.player_modPI, 0.0f, 2 * PI);
            ImGui::SliderFloat("float", &game_data.camera_modPI, 0.0f, 2 * PI);

            //ImGui::ColorEdit3("clear color",
                //(float*)&clear_color);  // Edit 3 floats representing a color

            if (ImGui::Button("GFX")) {
                WindowCallbacks::updatePipeline();
            }

            ImGui::SameLine();
            ImGui::Text("GFX-Pipe:\t%d", game_data.cur_pipe);

            ImGui::Text(
                "Frame = %.3f ms/frame (%.1f FPS)",
                1000.0f / ImGui::GetIO().Framerate,
                ImGui::GetIO().Framerate);
            //ImGui::Text("c = %d", counter);
            ImGui::End();
        }

        // Show the player info window
        if (show_player_controller_window) {
            ImGui::Begin("Player Debug", &show_player_controller_window);
            ImGui::Text("Speed:\t%f", game_data.speed);
            ImGui::Text("Player center delta:\t%f", game_data.DPI);
            ImGui::Text("Player roll radians:\t%f", game_data.player_z_rot);
            ImGui::Text("Roll Cooldown:\t%f", game_data.DeltaRoll);
            ImGui::Text("Velocity:\t%.02f, %.02f, %.02f", game_data.velocity.x, game_data.velocity.y, game_data.velocity.z);
            ImGui::Text("Torque Direction:\t%d", game_data.pn);
            //if (ImGui::Button("Close")) show_player_controller_window = false;
            ImGui::End();
        }
    }

}