#include "aveng_window.h"
#include "System/InputSystem.h"
#include <iostream>
#include <stdexcept>

namespace aveng {

	AvengWindow::AvengWindow(int w, int h, std::string name) : width{ w }, height{ h }, windowName{ name }
	{
		glfwInit();

		// Instruct GLFW to NOT use the OpenGL API since we're using Vulkan
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		// Open in windowed mode - Since we're using Vulkan we need to handle window resizing in a different way
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

		// @type GLFWwindow* window;
		// @4th arg - Using windowed mode
		// @5th arg - Using OpenGL context
		mWindow = glfwCreateWindow(width, height, windowName.c_str(), nullptr, nullptr);

		// The parent AvengWindow object is 'this'
		glfwSetWindowUserPointer(mWindow, this);

		// Whenever our window is resized, this callback is executed with the args in the callback's signature (new width and height)
		glfwSetFramebufferSizeCallback(mWindow, framebufferResizedCallback);

		// Mouse callback - buttons
		glfwSetMouseButtonCallback(mWindow,
			[](GLFWwindow* win, int button, int action, int mods) {
				auto window = static_cast<AvengWindow*>(glfwGetWindowUserPointer(win));

				if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && window)
				{
					// Handle mouse press at (xpos, ypos)
					double xpos, ypos;
					glfwGetCursorPos(win, &xpos, &ypos);
					window->onMouseButton(button, action, mods, xpos, ypos);

				}
				else if (window) {
					window->onMouseButton(button, action, mods);
				}
			}
		);

		// Mouse callback - position
		glfwSetCursorPosCallback(mWindow,
			[](GLFWwindow* win, double xpos, double ypos) {
				auto window = static_cast<AvengWindow*>(glfwGetWindowUserPointer(win));
				if (window) {
					window->onMouseMove(xpos, ypos);
				}
			}
		);

		glfwSetKeyCallback(mWindow,
			[](GLFWwindow* win, int key, int scancode, int action, int mods) {

				auto window = static_cast<AvengWindow*>(glfwGetWindowUserPointer(win));
				if (window) {
					window->onKey(key, scancode, action, mods);
				}

			}
		);
	}

	AvengWindow::~AvengWindow()
	{
		glfwDestroyWindow(mWindow);
		glfwTerminate();
	}

	void AvengWindow::onMouseButton(int button, int action, int mods, double x, double y) {
		if (inputSystem) {
			inputSystem->handleMouseButton(button, action, mods, x, y);
		}
		else { std::cout << "missing input system 1" << std::endl; }
	}

	void AvengWindow::onMouseMove(double x, double y) {
		if (inputSystem) {
			inputSystem->handleMouseMove(x, y);
		}
		else { std::cout << "missing input system 2" << std::endl; }
	}

	void AvengWindow::onKey(int key, int scancode, int action, int mods)
	{
		if (inputSystem) {
			inputSystem->handleKey(key, scancode, action, mods);
		} else { std::cout << "missing input system 3" << std::endl; }
	}

	void AvengWindow::createWindowSurface(VkInstance instance, VkSurfaceKHR* surface) const
	{
		if (glfwCreateWindowSurface(instance, mWindow, nullptr, surface) != VK_SUCCESS)
		{
			// TODO - More concise debugging. For example one might check for VK_ERROR_EXTENSION_NOT_PRESENT or first perform vkDestroySurfaceKHR
			throw std::runtime_error("GLFW failed to create the window surface.");
		}
	}

	bool AvengWindow::shouldClose() { return glfwWindowShouldClose(mWindow); }

	void AvengWindow::framebufferResizedCallback(GLFWwindow* window, int width, int height)
	{
		auto avengWindow = reinterpret_cast<AvengWindow*>(glfwGetWindowUserPointer(window));
		avengWindow->framebufferResized = true;
		avengWindow->width = width;
		avengWindow->height = height;
	}

} 