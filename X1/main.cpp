#include "XOne.h"
#include "avpch.h"
// #include "Apps/Gravity.h"

#define LOG(a) std::cout << a << std::endl

int main(void)
{
	std::vector<int> int_vec; // Wat?
	aveng::XOne app{};

	try {
		app.run();
	}
	catch (const std::exception& e)
	{
		LOG(e.what());
		return -1;
	}

	return EXIT_SUCCESS;

}