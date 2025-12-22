#include "Midnight.h"

namespace aveng {

	Midnight::Midnight(Editor& editor, AvengWindow& window ) :
#ifdef ENABLE_EDITOR
		editorInput{ &editor }
		, inputRouter{ mode_, editorInput, gameInput }
		, inputSystem{ inputRouter }
#else
		inputSystem{ gameInput }
#endif
	{
		window.setInputSystem(&inputSystem);
	}
}