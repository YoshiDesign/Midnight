#include "KeyboardController.h"
#include "../Player/GameplayFunctions.h"
#include "../Math/aveng_math.h"
#include "../../avpch.h"

namespace aveng{

	KeyboardController::KeyboardController(AvengAppObject& _viewerObject, Data& _data) 
		: viewerObject{ _viewerObject }, data{_data}
	{};

	void KeyboardController::moveCameraXZ(GLFWwindow* window, float dt) {
		
		glm::vec3 rotate{ 0 };

		if (glfwGetKey(window, keys.lookUp) == GLFW_PRESS)
		{
			rotate.x += 1.f;
		}
		if (glfwGetKey(window, keys.lookDown) == GLFW_PRESS)
		{
			rotate.x -= 1.f;
		}		
		
		if (glfwGetKey(window, keys.left) == GLFW_PRESS)
		{
			rotate.y -= 1.f;
		}
		if (glfwGetKey(window, keys.right) == GLFW_PRESS)
		{
			rotate.y += 1.f;
		}

		// This if statement effectively makes sure that rotate (matrix) is non-zero
		if (glm::dot(rotate, rotate) > std::numeric_limits<float>::epsilon()) {
			// Update according to Delta Time. Normalize keeps multiple rotations in sync so one direction doesn't rotate faster than another
			viewerObject.transform.rotation += lookSpeed * dt * glm::normalize(rotate);
		}
		//
		// Prevent things from going upside down
		viewerObject.transform.rotation.x = glm::clamp(viewerObject.transform.rotation.x, -1.5f, 1.5f);
		// 360 degree max rotation then repeat
		viewerObject.transform.rotation.y = glm::mod(viewerObject.transform.rotation.y, glm::two_pi<float>());
		
		
		float yaw = viewerObject.transform.rotation.y;
		const glm::vec3 forwardDir{ sin(yaw), 0.f, cos(yaw) };
		const glm::vec3 rightDir{ forwardDir.z, 0.f, -forwardDir.x };
		const glm::vec3 upDir{ 0.f, -1.f, 0.f };

		glm::vec3 moveDir{ 0.f };
		if (glfwGetKey(window, keys.w) == GLFW_PRESS)
		{
			moveDir += forwardDir;
		}
		if (glfwGetKey(window, keys.s) == GLFW_PRESS)
		{
			moveDir -= forwardDir;
		}
		if (glfwGetKey(window, keys.a) == GLFW_PRESS)
		{
			moveDir -= rightDir;
		}
		if (glfwGetKey(window, keys.d) == GLFW_PRESS)
		{
			moveDir += rightDir;
		}
		if (glfwGetKey(window, keys.q) == GLFW_PRESS)
		{
			moveDir -= upDir;
		}
		if (glfwGetKey(window, keys.e) == GLFW_PRESS)
		{
			moveDir += upDir;
		}


		// This if statement effectively makes sure that rotate (matrix) is non-zero
		if (glm::dot(moveDir, moveDir) > std::numeric_limits<float>::epsilon()) {
			// Update according to Delta Time. Normalize keeps multiple rotations in sync so one direction doesn't rotate faster than another
			viewerObject.transform.translation += climbSpeed * dt * glm::normalize(moveDir);
		}

	}

} 