#pragma once
#include "../aveng_window.h"
#include "../data.h"
#include "../../avpch.h"

#define MAX_PIPELINES 2

namespace aveng {
	
	class WindowCallbacks {

	public:

		static void testKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
		{
			switch (key)
			{
			case GLFW_KEY_SPACE			: if (action == GLFW_PRESS) updatePipeline(); 
				break;
			case GLFW_KEY_PERIOD		: if (action == GLFW_PRESS) updateFlightMode();
				break;
			
			default:
				break;
			}

		}
		//
		static void updatePipeline() 
		{
			current_pipeline = (current_pipeline + 1) % MAX_PIPELINES;
		}

		//
		static int getCurPipeline()
		{
			return current_pipeline;
		}

		static void updateFlightMode()
		{
			flightMode = !flightMode;
		}

		static int current_pipeline;
		static glm::vec3 modRot;
		static glm::vec3 modTrans;
		static int posNeg;
		static float modPI;
		static bool flightMode;

	private:

	};

}