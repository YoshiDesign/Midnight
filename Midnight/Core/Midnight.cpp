#include "Midnight.h"
#include "Core/Modeling/ModelAndInstanceData.h"
namespace aveng {

	Midnight::Midnight(GameData& gd)
		// Just remember that initialization order is the member declaration order, not your initializer list order
		: game_data(gd)
		, aveng_window(WIDTH, HEIGHT, "MIDNIGHT ENGINE")
		, engineDevice(aveng_window)
		, modelLib_(engineDevice, renderData)
		, sceneFacade_(modelLib_, modelLib_.query()) // intentionally injecting registry via getter
		, renderer(engineDevice, aveng_window, renderData, cameraManager, modelLib_.query())
#ifdef ENABLE_EDITOR
		, editor_{ std::make_unique<Editor>(renderData,
			renderer,
			game_data,
			engineDevice,
			aveng_window,
			cameraManager, 
			sceneFacade_
		)
		}
		, editorInput_( std::make_unique<EditorInput>(editor_.get()) )
		, inputRouter_( std::make_unique<EditorGameRouter>(game_data.currentAppMode, *editorInput_, *gameInput_) )
		, inputSystem_( std::make_unique<InputSystem>(*inputRouter_, game_data))
		, frame_(std::make_unique<AvengFrame>(
			renderer,
			sceneFacade_, /// Interface
			modelLib_, /// Interface
			renderData,
			game_data,
			engineDevice,
			editor_.get()
		))
#else
		, gameInput_(std::make_unique<GameInput>())
		, inputSystem_(std::make_unique<InputSystem>(*gameInput_, game_data))
		, frame_(std::make_unique<AvengFrame>(
			renderer,
			sceneFacade_, /// Interface
			modelLib_, /// Interface
			renderData,
			game_data,
			engineDevice,
			nullptr
		))
#endif
	{
		initializeDependencies();
		initialize();
	}

	void Midnight::render(float frameTime) {

		// Load new models if any are pending. Side-Effect: Creates descriptor sets for this model
#ifdef ENABLE_EDITOR

		modelLib_.processPendingModelLoads();
		modelLib_.processPendingUnloads();

		// Give ImGUI the latest input state
		updateGUI(inputState()); 
		if (game_data.modeSwitchRequested)
		{
			game_data.currentAppMode = game_data.requestedMode;
			game_data.modeSwitchRequested = false;

			// Good to know - In case anything needs dealing with
			// editor.onModeSwitched(frame.currentFrameIndex(), game_data.currentAppMode);
		}
#endif
		frame_->render(frameTime); 
	}

	/* */
	void Midnight::updateCamera(float frameTime)
	{
		// Fetched all the way from downtown (the swapchain)
		aspect = renderer.getAspectRatio();

		// Track key press to transform viewer object
		// keyboardController.moveCameraXZ(window.getGLFWwindow(), frameTime);
		cameraManager.update(frameTime, inputState());

		// Apply new viewer obj values to the camera
		cameraManager.active().camera.setViewYXZ(
			cameraManager.active().camera.transform().translation + glm::vec3(0.f, 0.f, -.80f),
			cameraManager.active().camera.transform().rotation);

		// Recalculate perspective
		cameraManager.active().camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 1000.f);

		// Punt to the renderer
		renderData.cameraProxy.projection = cameraManager.active().camera.getProjection();
		renderData.cameraProxy.view = cameraManager.active().camera.getView();

	}

	/* */
	int Midnight::registerCamera(std::string name, std::unique_ptr<ICameraDriver> cameraDriver) {
		return cameraManager.createCamera(std::move(name), std::move(cameraDriver));
	}

#ifdef ENABLE_EDITOR
	/* */
	void Midnight::updateGUI(const InputState& state) {
		editor_->updateInputState(state);
	}
#endif

	void Midnight::registerCallbacks()
	{

		modelLib_.setDestroyInstancesForModelCallback(
			[this](ModelId id) {
				return sceneFacade_.destroyAllInstancesForModel(id);
			}
		);

	}

	void Midnight::initializeDependencies() {
//#ifdef ENABLE_EDITOR - Moved these to the constructor
//		editor_ = std::make_unique<Editor>(renderData, renderer, game_data, engineDevice, aveng_window, cameraManager);
//		editorInput_ = std::make_unique<EditorInput>(editor_.get());
//		inputRouter_ = std::make_unique<EditorGameRouter>(game_data.currentAppMode, *editorInput_, gameInput);
//
//		inputSystem_ = std::make_unique<InputSystem>(*inputRouter_, game_data);
//		frame_ = std::make_unique<AvengFrame>(renderer, renderData, game_data, engineDevice, editor_.get());
//#else
//		inputSystem_ = std::make_unique<InputSystem>(gameInput, game_data);
//		frame_ = std::make_unique<AvengFrame>(renderer, renderData, game_data, engineDevice, nullptr);
//#endif

		aveng_window.setInputSystem(inputSystem_.get());

		registerCallbacks();

#ifdef M_DEBUG
		assert(frame_ && "Frame not initialized");
#endif

	}

	void Midnight::initialize() {
		renderer.initialize();

#ifdef ENABLE_EDITOR
		if (editor_) {
			editor_->initialize(renderer.pGetSwapChain());
		}
#endif
	}

	void Midnight::shutdown() {
	
		VkResult result = vkDeviceWaitIdle(engineDevice.device());
		if (result != VK_SUCCESS) {
			std::printf("%s fatal error: could not wait for device idle (error: %i)\n", __FUNCTION__, result);
			return;
		}

		modelLib_.cleanup();
		renderer.cleanup();

	}

}