#include "ObjectRenderSystem.h"
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
		for (int i = 0; i <= 4; i++) {
			for (int j = 0; j < 5; j++) {

				renderer.addLight(
					glm::vec3(i * 4.f, -50.f, j * -4.0f),  // position
					glm::vec3(1.f, 0.0f, 0.0f),    // red color
					0.75f,						   // intensity
					i * 0.15f + 0.3f                 // radius
				);


				renderer.addLight(
					glm::vec3(30 + (i * 4.f), -50.f, 30 + (j * -4.0f)),  // position
					glm::vec3(0.8f, 0.9f, 0.8f),    // turquoise color
					0.75f,						   // intensity
					i * 0.15f + 0.3f                 // radius
				);

				renderer.addLight(
					glm::vec3(-30 + (i * 4.f), -50.f, 30 + (j * -4.0f)),  // position
					glm::vec3(1.f, 0.0f, 1.0f),    // Purple color
					0.75f,						   // intensity
					i * 0.15f + 0.3f                 // radius
				);


			}

			renderer.addLight(
				glm::vec3(-(i * 25.f), -25.f, -5.f),  // position
				glm::vec3(.75f, 0.9f, 1.0f),    // turquoise color
				0.75f,							// intensity
				0.5f                            // radius
			);

		}
		
	}

	void ObjectRenderSystem::render(float frameTime)
	{
		
		updateCamera(frameTime);

		// This needs to be updated to accommodate game data
		updateData(frameTime);

		renderer.beginFrame();

		// Update frame data in renderer
		renderer.updateFrameData(aveng_camera.getProjection(), aveng_camera.getView());

		//// Prepare object data for rendering - TODO (Too many declarations!!!)
		//std::vector<std::tuple<ObjectUniformData, glm::mat4, glm::mat4, AvengModel*>> objectData;
		//for (const auto& obj : renderer.getAppObjects()) {
		//	ObjectUniformData objUniform{ obj.get_texture() };		// Model's texture index
		//	glm::mat4 modelMatrix = obj.transform._mat4();			// Local model matrix
		//	glm::mat4 normalMatrix = obj.transform.normalMatrix();	// Local model normal matrix
		//	AvengModel* model = obj.model.get();						// TODO: This is a shared ptr
		//	//std::cout << "Object to render: " << model->path << std::endl;
		//	objectData.emplace_back(objUniform, modelMatrix, normalMatrix, model);
		//}

		//if (firstFrame) {
		//	std::cout << "First Frame!" << std::endl;
		//	std::cout << "Objects this frame: " << objectData.size() << std::endl;
		//	firstFrame = false;
		//}
		renderer.draw(frameTime);
		/*
			NOTE: renderer.renderObjectsInstanced uses the pipelineManager to select/bind the gfxpipeline
				  Whereas renderer.renderLights punts to the pointLightSystem which handles gfxpipeline binding on its own
		*/
		// Render regular objects (standard pipeline) -- BINDS DESCRIPTORS -- BINDS A DIFFERENT PIPELINE
		//renderer.renderObjectsInstanced(objectData);
		// renderer.renderObjects(objectData);

		// STEP 1F: Enable animation rendering pipeline  
		//if (!animatedInstances.empty()) {
		//	renderer.renderAnimatedModels(animatedInstances);
		//}

		// Render lights -- BINDS DESCRIPTORS -- BINDS A DIFFERENT PIPELINE
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

	void ObjectRenderSystem::loadGame(const std::string& scenePath)
	{
		std::cout << "Loading scene from: " << scenePath << std::endl;
		
		renderer.loadScenes(scenePath.c_str());
	}

} //