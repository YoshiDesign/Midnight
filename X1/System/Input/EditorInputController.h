#pragma once
#include "Editor.h"
#include "IInputController.h"
namespace aveng {

	class EditorInputController : public IInputController {
	public:
		EditorInputController();
		explicit EditorInputController(Editor* editor) : mEditor(editor) {}

		void onMouseMove(const MouseMoveEvent& e);
		void onMouseButton(const MouseButtonEvent& e);
		void onKey(const KeyEvent& e);

		void update(float dt);  // use the dx/dy accumulated

	private:
		Editor* mEditor;
	};
}