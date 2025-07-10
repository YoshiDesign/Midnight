#pragma once

#include "CoreVK/EngineDevice.h"
#include "Core/data.h"
#include "Core/aveng_window.h"
#include "Utils/window_callbacks.h"

// libs
#include <glm/glm.hpp>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "Utils/SystemContext.h"

// std
#include <stdexcept>

namespace aveng {

	static void check_vk_result(VkResult err) {
		if (err == 0) return;
		fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
		if (err < 0) abort();
	}

	class AvengImgui {
	public:

		AvengImgui(EngineDevice& device);
		void init(AvengWindow& window, VkRenderPass renderPass, uint32_t imageCount);
		~AvengImgui();

		void newFrame();
		void render(VkCommandBuffer commandBuffer);
		void runGUI();

		bool show_player_controller_window = false;
		ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	private:
		EngineDevice& device;
		VkDescriptorPool descriptorPool;
		SystemContext context;

	};
}  // namespace lve