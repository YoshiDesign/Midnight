#include "ICameraDriver.h"
namespace aveng {

	void PlayerCamera::update(float dt, const InputState& input, CameraTransform& t) {

		glm::vec3 rotate{ 0 };

		// TODO - Move these to a Game Parameters class
		float climbSpeed{ 120.0f };
		float lookSpeed{ 2.0f };
		float rollSpeed{ 8.0f };

		// Note: glfwGetKey polls cached state, and won't tell you about key repeat status. Don't use it for player controls.
		// but that's not relevant here anymore. What's for dinner?

		if (input.keyDown[GLFW_KEY_UP]) // look up
		{
			rotate.x += 1.f;
		}
		if (input.keyDown[GLFW_KEY_DOWN]) // look down
		{
			rotate.x -= 1.f;
		}

		if (input.keyDown[GLFW_KEY_LEFT]) // look left
		{
			rotate.y -= 1.f;
		}
		if (input.keyDown[GLFW_KEY_RIGHT]) // look right
		{
			rotate.y += 1.f;
		}

		// This if statement effectively makes sure that rotate (matrix) is non-zero
		if (glm::dot(rotate, rotate) > std::numeric_limits<float>::epsilon()) {
			// Update according to Delta Time. Normalize keeps multiple rotations in sync so one direction doesn't rotate faster than another
			t.rotation += lookSpeed * dt * glm::normalize(rotate);
		}
		//
		// Prevent things from going upside down
		t.rotation.x = glm::clamp(t.rotation.x, -1.5f, 1.5f);
		// 360 degree max rotation then repeat
		t.rotation.y = glm::mod(t.rotation.y, glm::two_pi<float>());

		float yaw = t.rotation.y;
		
		const glm::vec3 forwardDir{ sin(yaw), 0.f, cos(yaw) };
		const glm::vec3 rightDir{ forwardDir.z, 0.f, -forwardDir.x };
		const glm::vec3 upDir{ 0.f, -1.f, 0.f };

		glm::vec3 moveDir{ 0.f };
		if (input.keyDown[GLFW_KEY_W]) // Key W - Camera moves forward
		{
			moveDir += forwardDir;
		}
		if (input.keyDown[GLFW_KEY_S]) // Key S - Move Backward
		{
			moveDir -= forwardDir;
		}
		if (input.keyDown[GLFW_KEY_A]) // Key A - Strafe left
		{
			moveDir -= rightDir;
		}
		if (input.keyDown[GLFW_KEY_D]) // Key D - Strafe right
		{
			moveDir += rightDir;
		}
		if (input.keyDown[GLFW_KEY_Q]) // Key Q - Move Up (negative y)
		{
			moveDir -= upDir;
		}
		if (input.keyDown[GLFW_KEY_E]) // Key E - Move Down
		{
			moveDir += upDir;
		}

		// This if statement effectively makes sure that rotate (matrix) is non-zero
		if (glm::dot(moveDir, moveDir) > std::numeric_limits<float>::epsilon()) {
			// Update according to Delta Time. Normalize keeps multiple rotations in sync so one direction doesn't rotate faster than another
			t.translation += climbSpeed * dt * glm::normalize(moveDir);
		}

	}

}