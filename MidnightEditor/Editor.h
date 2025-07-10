#pragma once
#include "GUI/aveng_imgui.h"
#include "Utils/SystemContext.h"

namespace aveng {

	class Editor {
	public:
		Editor(SystemContext& context);
		~Editor();
		void init();

	private:
		SystemContext& game_context;
	};

}