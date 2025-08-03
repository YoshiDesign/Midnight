#include "ObjectRenderSystem.h"
#include "Core/Animation/AssimpInstance.h"
#include "Utils/window_callbacks.h"

namespace aveng {

	ObjectRenderSystem::ObjectRenderSystem(AvengWindow& window)
		: window{ window }
	{

		firstFrame = true;

		// Initial camera position
		viewerObject.transform.translation.z = -5.5f;
		viewerObject.transform.translation.y = -2.5f;
	}

	ObjectRenderSystem::~ObjectRenderSystem() = default;

	void ObjectRenderSystem::initialize() 
	{
		// Note: ImageSystem should be initialized first in loadGame() before calling this

		// Lights, temporary placement
		for (int i = 1; i <= 4; i++) {
			for (int j = 1; j < 5; j++) {

				renderer.addLight(
					glm::vec3(-100 + i * -10.f, -2.f, 33 + j * 4.0f),  // position
					glm::vec3(1.f, 0.0f, 0.0f),    // red color
					0.75f,
					i * j * 0.1f                           // slightly larger radius
				);

			}

			renderer.addLight(
				glm::vec3((i * 25.f), -25.f, -5.f),  // position
				glm::vec3(.75f, 0.9f, 1.0f),    // turquoise color
				0.75f,							// intensity
				0.5f                            // radius
			);

		}
		
	}

	void ObjectRenderSystem::render(float frameTime)
	{
		
		updateCamera(frameTime);

		// Deprecate this in favor of the new renderData object
		updateData(frameTime);

		renderer.beginFrame();

		// Update frame data in renderer
		renderer.updateFrameData(aveng_camera.getProjection(), aveng_camera.getView());

		// Prepare object data for rendering - TODO - This seems highly inefficient
		std::vector<std::tuple<ObjectUniformData, glm::mat4, glm::mat4, AvengModel*>> objectData;
		for (const auto& obj : renderer.getAppObjects()) {
			ObjectUniformData objUniform{ obj.get_texture() };		// Model's texture index
			glm::mat4 modelMatrix = obj.transform._mat4();			// Local model matrix
			glm::mat4 normalMatrix = obj.transform.normalMatrix();	// Local model normal matrix
			AvengModel* model = obj.model.get();						// TODO: This is a shared ptr
			objectData.emplace_back(objUniform, modelMatrix, normalMatrix, model);
		}

		if (firstFrame) {
			std::cout << "Instanced Rendering Enabled!" << std::endl;
			std::cout << "Objects this frame: " << objectData.size() << std::endl;
			firstFrame = false;
		}
		
		// Render regular objects (standard pipeline) -- BINDS DESCRIPTORS
		// renderer.renderObjectsInstanced(objectData);
		renderer.renderObjects(objectData);

		// STEP 1F: Enable animation rendering pipeline  
		//if (!animatedInstances.empty()) {
		//	renderer.renderAnimatedModels(animatedInstances);
		//}

		// Render lights -- BINDS DESCRIPTORS
		renderer.renderLights();
	
#ifdef ENABLE_EDITOR
		renderer.renderEditor();
#endif
		renderer.endFrame();
	}

	void ObjectRenderSystem::updateCamera(float frameTime)
	{
		aspect = getAspectRatio();
		// Updates the viewer object transform component based on key input, proportional to the time elapsed since the last frame
		keyboardController.moveCameraXZ(window.getGLFWwindow(), frameTime);
		aveng_camera.setViewYXZ(viewerObject.transform.translation + glm::vec3(0.f, 0.f, -.80f), viewerObject.transform.rotation + glm::vec3());
		aveng_camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 1000.f);
	}

	/**
	* Deprecated
	*/
	void ObjectRenderSystem::updateData(float frameTime)
	{

		game_data.num_objs = 2;
		game_data.cur_pipe = WindowCallbacks::getCurPipeline();
		game_data.dt = frameTime;
		game_data.camera_modPI = viewerObject.transform.modPI;

		game_data.cameraView = aveng_camera.getCameraView();
		game_data.cameraPos = viewerObject.transform.translation;
		game_data.cameraRot = viewerObject.transform.rotation;
		game_data.fly_mode = WindowCallbacks::flightMode;

	}

	void ObjectRenderSystem::updatePostProcessing(float frameTime)
	{
		// Example: Dynamic shader switching based on game state
		// This is where you'd implement your toxic cloud detection logic
		static bool inToxicCloud = false;
		static float toxicTimer = 0.0f;
		toxicTimer += frameTime;

		// Toggle toxic cloud effect every 5 seconds for demo
		if (toxicTimer > 5.0f) {
			inToxicCloud = !inToxicCloud;
			toxicTimer = 0.0f;

			if (inToxicCloud) {
				std::cout << "Entering toxic cloud - switching to distorted rendering!" << std::endl;
				renderer.setObjectRenderMode(ObjectRenderMode::DISTORTED);
				// Temporarily disable post-processing until system is complete
				renderer.setPostProcessMode(PostProcessMode::TOXIC_CLOUD);

				// Example: Print available pipelines for debugging
				auto pipelines = renderer.getAvailablePipelines();
				std::cout << "Available pipelines: ";
				for (const auto& name : pipelines) {
					std::cout << name << " ";
				}
				std::cout << std::endl;
			}
			else {
				std::cout << "Exiting toxic cloud - returning to normal rendering" << std::endl;
				renderer.setObjectRenderMode(ObjectRenderMode::STANDARD);
				// renderer.setPostProcessMode(PostProcessMode::NONE);
			}
		}
	}

	void ObjectRenderSystem::loadGame(const std::string& scenePath)
	{
		std::cout << "Loading scene from: " << scenePath << std::endl;
		
		renderer.loadScenes(scenePath.c_str());
	}

} //