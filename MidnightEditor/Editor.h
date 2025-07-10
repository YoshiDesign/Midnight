#pragma once
#include "GUI/aveng_imgui.h"
#include "aveng/SystemContext.h"

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