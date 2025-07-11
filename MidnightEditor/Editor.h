#pragma once
#include "GUI/aveng_imgui.h"
#include "Utils/SystemContext.h"

namespace aveng {

	class Editor {
	public:
		Editor(SystemContext& context);
		~Editor();
		void init();
		void render(VkCommandBuffer& commandBuffer);

	private:
		SystemContext& aveng_context;
		AvengImgui aveng_imgui{ *aveng_context.device, aveng_context };
	};

}