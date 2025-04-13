#include "../CoreVK/EngineDevice.h"
#include "../Core/aveng_window.h"
#include "aveng_imgui.h"
#include "../avpch.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

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
        ImGui::StyleColorsDark();
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

    void AvengImgui::runGUI(Data& data) {
        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can
        // browse its code to learn more about Dear ImGui!).
        if (show_demo_window) ImGui::ShowDemoWindow(&show_demo_window);

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named
        // window.
        {
            //static float slider = 0.0f;
            static float value = 3.1415926f;
            static bool hasChanged = false;
            static int counter = 0;

            ImGui::Begin("Debug you fool!"); 

            ImGui::Checkbox("Player Debug", &show_player_controller_window);

            ImGui::Text(
                "Objects: %d", data.num_objs); 
            ImGui::Text(
                "Flight Mode: %d", data.fly_mode);
            ImGui::Text(
                "Camera View:\t\t(%.03lf, %.03lf, %.03lf)", data.cameraView.x, data.cameraView.y, data.cameraView.z);
            ImGui::Text(
                "Camera Rotation:\t(%.03lf, %.03lf, %.03lf)", data.cameraRot.x, data.cameraRot.y, data.cameraRot.z);
            ImGui::Text(
                "Camera Position:\t(%.03lf, %.03lf, %.03lf)", data.cameraPos.x, data.cameraPos.y, data.cameraPos.z); 
            ImGui::Text(
                "Player Rotation:\t(%.03lf, %.03lf, %.03lf)", data.playerRot.x, data.playerRot.y, data.playerRot.z);
            ImGui::Text(
                "Player Position:\t(%.03lf, %.03lf, %.03lf)", data.playerPos.x, data.playerPos.y, data.playerPos.z);
            ImGui::Text(
                "Mod Rotation:\t(%.03lf, %.03lf, %.03lf)", data.modRot.x, data.modRot.y, data.modRot.z);
            ImGui::Text(
                "Mod Position:\t(%.03lf, %.03lf, %.03lf)", data.modPos.x, data.modPos.y, data.modPos.z);
            ImGui::Text(
                "Forward Direction:\t(%.03lf, %.03lf, %.03lf)", data.forwardDir.x, data.forwardDir.y, data.forwardDir.z);
            ImGui::SliderFloat("float", &data.player_modPI, 0.0f, 2*PI);
            ImGui::SliderFloat("float", &data.camera_modPI, 0.0f, 2*PI);

            //ImGui::ColorEdit3("clear color",
                //(float*)&clear_color);  // Edit 3 floats representing a color
            
            if (ImGui::Button("GFX"))
                WindowCallbacks::updatePipeline();

            ImGui::SameLine();
            ImGui::Text("GFX-Pipe:\t%d", data.cur_pipe);
           
            ImGui::Text(
                "Frame = %.3f ms/frame (%.1f FPS)",
                1000.0f / ImGui::GetIO().Framerate,
            ImGui::GetIO().Framerate);
            //ImGui::Text("c = %d", counter);
            ImGui::End();
        }

        // 3. Show another simple window.
        if (show_player_controller_window) {
            ImGui::Begin("Player Debug", &show_player_controller_window);
            ImGui::Text("Speed:\t%f", data.speed);
            ImGui::Text("Player center delta:\t%f", data.DPI);
            ImGui::Text("Player roll radians:\t%f", data.player_z_rot);
            ImGui::Text("Roll Cooldown:\t%f", data.DeltaRoll);
            ImGui::Text("Velocity:\t%.02f, %.02f, %.02f", data.velocity.x, data.velocity.y, data.velocity.z);
            ImGui::Text("Torque Direction:\t%d", data.pn);
            //if (ImGui::Button("Close")) show_player_controller_window = false;
            ImGui::End();
        }
    }

}  // namespace lve