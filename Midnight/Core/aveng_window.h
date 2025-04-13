#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>

namespace aveng {

	class AvengWindow {
		std::string windowName;
		GLFWwindow* window;

		bool framebufferResized = false;
		int width;
		int height;

	public:

		AvengWindow(int w, int h, std::string name);
		~AvengWindow();

		// Removal of copy construction
		AvengWindow(const AvengWindow&) = delete;
		AvengWindow& operator=(const AvengWindow&) = delete;

		bool shouldClose();

		VkExtent2D getExtent() { return { static_cast<uint32_t>(width), static_cast<uint32_t>(height) }; }

		bool wasWindowResized() { return framebufferResized; }

		void resetWindowResizedFlag() { framebufferResized = false; }

		void createWindowSurface(VkInstance instance, VkSurfaceKHR* surface);

		GLFWwindow* getGLFWwindow() const { return window; }

	private:
		static void framebufferResizedCallback(GLFWwindow* window, int width, int height);
		void initWindow();

	};

} // NS