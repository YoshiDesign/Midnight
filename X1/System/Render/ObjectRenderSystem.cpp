#include "ObjectRenderSystem.h"
#include "Utils/window_callbacks.h"

namespace aveng {

	ObjectRenderSystem::ObjectRenderSystem(EngineDevice& device, AvengWindow& window)
		: engineDevice{ device }, aveng_window{ window }
	{
		// Initial camera position
		viewerObject.transform.translation.z = -5.5f;
		viewerObject.transform.translation.y = -2.5f;
	}

	ObjectRenderSystem::~ObjectRenderSystem() = default;

	void ObjectRenderSystem::initialize() 
	{
		// Note: ImageSystem should be initialized first in loadGame() before calling this
		
		// Delegate to renderer for all engine-specific setup
		int numObjects = static_cast<int>(sceneLoader.getObjectCount());
		if (numObjects == 0) numObjects = 1; // Prevent crash with empty scenes
		
		renderer.setupDescriptors(numObjects);
		
		// Initialize PointLightSystem now that descriptor layouts are created
		renderer.initializePointLightSystem();
		
		std::cout << "ObjectRenderSystem initialized with " << numObjects << " objects" << std::endl;
	}



	void ObjectRenderSystem::render(float frameTime, FrameContent& frame_content)
	{
		// Clear Color for now
		frame_content.rgb = glm::vec3(0.001f, 0.008f, 0.06f); // Cool, dark midnight blue
		
		updateCamera(frameTime, viewerObject, keyboardController);
		updateData(frameTime);
		// updatePostProcessing(frameTime);

		// Prepare frame data
		u_GlobalData.projection = aveng_camera.getProjection();
		u_GlobalData.view = aveng_camera.getView();
		u_GlobalData.renderMode = static_cast<int>(renderer.getObjectRenderMode());
		// u_GlobalData.time = toxicTimer;  // Pass time for animated effects

		// Start frame rendering
		VkCommandBuffer commandBuffer = renderer.beginFrame();
		if (!commandBuffer) {
			return; // Skip this frame if we can't get a command buffer
		}

		renderer.beginSwapChainRenderPass(commandBuffer, frame_content.rgb);

		// Update frame data in renderer
		renderer.updateFrameData(u_GlobalData, u_LightsData);

		// Prepare object data for rendering
		std::vector<std::tuple<ObjectUniformData, glm::mat4, glm::mat4, AvengModel*>> objectData;
		for (auto& kv : sceneLoader.getAppObjects()) {
			ObjectUniformData objUniform{ kv.second.get_texture() };
			glm::mat4 modelMatrix = kv.second.transform._mat4();
			glm::mat4 normalMatrix = kv.second.transform.normalMatrix();
			AvengModel* model = kv.second.model.get();
			objectData.emplace_back(objUniform, modelMatrix, normalMatrix, model);
		}

		// Use instanced rendering for better performance
		static bool firstFrame = true;
		if (firstFrame) {
			std::cout << "Instanced Rendering Enabled!" << std::endl;
			std::cout << "   Objects this frame: " << objectData.size() << std::endl;
			firstFrame = false;
		}
		
		renderer.renderObjectsInstanced(objectData);

		// Render lights
		renderer.renderLights(u_LightsData.numLights);

		// Render editor
		editor.render(commandBuffer);

		renderer.endSwapChainRenderPass(commandBuffer);

		renderer.endFrame();
	}

	void ObjectRenderSystem::updateCamera(float frameTime, AvengAppObject& viewerObject, KeyboardController& keyboardController)
	{
		aspect = getAspectRatio();
		// Updates the viewer object transform component based on key input, proportional to the time elapsed since the last frame
		keyboardController.moveCameraXZ(aveng_window.getGLFWwindow(), frameTime);
		aveng_camera.setViewYXZ(viewerObject.transform.translation + glm::vec3(0.f, 0.f, -.80f), viewerObject.transform.rotation + glm::vec3());
		aveng_camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 1000.f);
	}

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

	void ObjectRenderSystem::addLight(const glm::vec3& position, const glm::vec3& color, float intensity, float radius)
	{
		if (u_LightsData.numLights >= LightsUbo::MAX_LIGHTS) {
			std::cout << "Warning: Maximum number of lights (" << LightsUbo::MAX_LIGHTS << ") reached. Cannot add more lights." << std::endl;
			return;
		}

		u_LightsData.lightPositions[u_LightsData.numLights] = glm::vec4(position, radius);
		u_LightsData.lightColors[u_LightsData.numLights] = glm::vec4(color, intensity);
		u_LightsData.numLights++;
	}

	void ObjectRenderSystem::clearLights()
	{
		u_LightsData.numLights = 0;
		// Zero out the light arrays for clean state
		memset(u_LightsData.lightPositions, 0, sizeof(u_LightsData.lightPositions));
		memset(u_LightsData.lightColors, 0, sizeof(u_LightsData.lightColors));
	}

	void ObjectRenderSystem::loadGame(const std::string& scenePath)
	{
		std::cout << "Loading scene from: " << scenePath << std::endl;
		
		sceneLoader.load(scenePath.c_str(), engineDevice);
		
		// Initialize ImageSystem with scene textures
		const auto& sceneTextures = sceneLoader.getSceneTextures();
		std::cout << "Scene has " << sceneTextures.size() << " textures defined" << std::endl;
		renderer.initializeImageSystem(sceneTextures);
		
		std::cout << "Loaded scene with " << sceneLoader.getObjectCount() << " objects" << std::endl;
	}

} //