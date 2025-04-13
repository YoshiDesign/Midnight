#pragma once

#include "../Scene/app_object.h"
#include "../aveng_window.h"
#include "../Player/Action.h"
#include "../data.h"


namespace aveng {
	class KeyboardController {

	public:
        struct KeyMappings {
            int w = GLFW_KEY_W;
            int a = GLFW_KEY_A;
            int s = GLFW_KEY_S;
            int d = GLFW_KEY_D;
            int e = GLFW_KEY_E;
            int q = GLFW_KEY_Q;
            int left = GLFW_KEY_LEFT;
            int lookUp = GLFW_KEY_UP;
            int right = GLFW_KEY_RIGHT;
            int lookDown = GLFW_KEY_DOWN;
        };

        KeyboardController(AvengAppObject& _viewerObject, Data& data);

        void moveCameraXZ(GLFWwindow* window, float dt);

        KeyMappings keys{};
        glm::vec3 velocity{ 0.0f, 0.0f, 8.0f };
        float climbSpeed{ 10.0f };
        float lookSpeed{ 2.0f };
        float rollSpeed{ 8.0f };
        glm::vec3 forwardDir;
        glm::vec3 c_affine{ 1.f };
        glm::vec3 c_rotate{ 0.f, 0.f, 0.f };
        float camera_deltaX{ 0.f };
        float modPI = 0.f;

        int barrelCooldown{ 100 };
        bool barrelRolling = false;
        Data& data;

    private:
        AvengAppObject& viewerObject;

	};
}