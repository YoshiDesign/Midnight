#include "Midnight.h"
#include "Core/Modeling/ModelAndInstanceData.h"
namespace aveng {

	Midnight::Midnight(GameData& gd)
		: game_data(gd)
		, aveng_window(WIDTH, HEIGHT, "MIDNIGHT ENGINE")
		, engineDevice(aveng_window)
		, modelSource_(createModelSource())
		, renderer(engineDevice, aveng_window, std::move(modelSource_), renderData, cameraManager)
		, staticMgr(renderData, engineDevice)
		, animMgr(renderData, engineDevice)
	{
		initializeDependencies();
		initialize();
	}

	std::unique_ptr<IModelSource> Midnight::createModelSource() {
#ifdef ENABLE_EDITOR
		return std::make_unique<FilesystemModelSource>();
#else
		return std::make_unique<PackModelSource>("assets.pak");
#endif
	}

	void Midnight::render(float frameTime) {

#ifdef ENABLE_EDITOR
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

	// This is the game's camera updates, not the editor's. It's more convenient to do this here at the moment
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

	int Midnight::registerCamera(std::string name, std::unique_ptr<ICameraDriver> cameraDriver) {
		return cameraManager.createCamera(std::move(name), std::move(cameraDriver));
	}

	void Midnight::updateGUI(const InputState& state) {
		editor_->updateInputState(state);
	}

	void Midnight::registerInstanceManagerCallbacks()
	{

		/*
		    // Clean Example or whatever you prefer...

			static_cb.onInstanceSelected = [&](InstanceHandle<StaticTag> h) {
				// do selection stuff
			};

			staticMgr.setCallbacks(static_cb);

			// Example: animated callbacks
			anim_cb.onInstanceSelected = [&](InstanceHandle<AnimatedTag> h) {
				// do selection stuff
			};

			animMgr.setCallbacks(anim_cb);
		*/

		// Note the cool C++20 designated initialization syntax (fun fact: concept was borrowed from C99). Works for simple aggregate types
		staticMgr.setCallbacks({
			.onDelete = [&](const StaticHandle& h) { staticMgr.deleteInstance(h); },
			.onDeleteMany = [&](std::vector<const StaticHandle&> h) { staticMgr.deleteInstances(h); },
			.onClone = [&](const StaticHandle& h) { staticMgr.cloneInstance(h);  },
			.onCloneMany = [&](const StaticHandle& h, int n) { staticMgr.cloneInstances(h, n); },
			// .onCenter = [&](const StaticHandle& h) { editor_->centerOn(h); },
			.onInstanceAdd = [&](const ModelId& h) { staticMgr.addInstance(h); },
			.onInstanceAddMany = [&](std::vector<const ModelId&> h, unsigned int n) { staticMgr.addInstances(h, n);  }
		});

		animMgr.setCallbacks({
			.onDelete = [&](const AnimatedHandle& h) { animMgr.deleteInstance(h); },
			.onDeleteMany = [&](std::vector < const AnimatedHandle&> h) { animMgr.deleteInstances(h); },
			.onClone = [&](const AnimatedHandle& h) { animMgr.cloneInstance(h);  },
			.onCloneMany = [&](const AnimatedHandle& h, int n) { animMgr.cloneInstances(h, n); },
			// .onCenter = [&](const AnimatedHandle& h) { editor_->centerOn(h); }
			.onInstanceAdd = [&](const ModelId& h) { animMgr.addInstance(h); },
			.onInstanceAddMany = [&](std::vector<const ModelId&> h, unsigned int n) { animMgr.addInstances(h, n);  }
		});

	}

	void Midnight::initializeDependencies() {
#ifdef ENABLE_EDITOR
		editor_ = std::make_unique<Editor>(renderData, renderer, game_data, engineDevice, aveng_window, cameraManager);
		editorInput_ = std::make_unique<EditorInput>(editor_.get());
		inputRouter_ = std::make_unique<EditorGameRouter>(game_data.currentAppMode, *editorInput_, gameInput);

		inputSystem_ = std::make_unique<InputSystem>(*inputRouter_, game_data);
		frame_ = std::make_unique<AvengFrame>(renderer, renderData, game_data, engineDevice, editor_.get());
#else
		inputSystem_ = std::make_unique<InputSystem>(gameInput, game_data);
		frame_ = std::make_unique<AvengFrame>(renderer, renderData, game_data, engineDevice, nullptr);
#endif

		aveng_window.setInputSystem(inputSystem_.get());
		registerInstanceManagerCallbacks();

		assert(frame_ && "Frame not initialized");

	}

	void Midnight::initialize() {
		renderer.initialize();

#ifdef ENABLE_EDITOR
		if (editor_) {
			editor_->initialize(renderer.pGetSwapChain());
		}
#endif
	}

}