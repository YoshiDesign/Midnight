#include "XOne.h"
#include "avpch.h"

#define LOG(a) std::cout << a << std::endl

int main(void)
{

	xone::XOne app{};

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