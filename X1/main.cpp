#include "XOne.h"
#ifdef ENABLE_EDITOR
#include "Editor.h"
#endif
#include "avpch.h"
// #include "Apps/Gravity.h"

#define LOG(a) std::cout << a << std::endl

int main(void)
{
	std::vector<int> int_vec; // Wat?
	aveng::XOne app{};

	try {
#ifdef ENABLE_EDITOR
		aveng::Editor editor(app.context());
#endif
		app.run();
	}
	catch (const std::exception& e)
	{
		LOG(e.what());
		return -1;
	}

	return EXIT_SUCCESS;

}