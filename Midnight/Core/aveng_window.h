#pragma once

#include "System/Input/EventPayloads.h"
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>

namespace aveng {
	class InputSystem;

	class AvengWindow {
		std::string windowName;
		GLFWwindow* mWindow;

		bool framebufferResized = false;
		int width;
		int height;

	public: 

		AvengWindow(int w, int h, std::string name);
		~AvengWindow();

		// Removal of copy construction
		AvengWindow(const AvengWindow&) = delete;
		AvengWindow& operator=(const AvengWindow&) = delete;

		GLFWwindow* getGLFWwindow() const { return mWindow; }

		bool shouldClose();

		VkExtent2D getExtent() { return { static_cast<uint32_t>(width), static_cast<uint32_t>(height) }; }

		bool wasWindowResized() { return framebufferResized; }

		void resetWindowResizedFlag() { framebufferResized = false; }

		void createWindowSurface(VkInstance instance, VkSurfaceKHR* surface) const;

		void onMouseMove(double x, double y);

		void onMouseButton(int button, int action, int mods, double x = 0, double y = 0);

		void onKey(int key, int scancode, int action, int mods);

		void setInputSystem(InputSystem* input) { inputSystem = input; }

	private:
		static void framebufferResizedCallback(GLFWwindow* window, int width, int height);
		void initWindow();

		InputSystem* inputSystem = nullptr;

	};

}