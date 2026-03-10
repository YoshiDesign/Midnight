#pragma once
namespace aveng {

#ifndef WTF_BOOM
#define WTF_BOOM -1
#endif
#define ArraySize(array)        ( sizeof(array)/sizeof((array)[0]) )

	static constexpr uint32_t k_invalid_index = 0xffffffff;
	const uint32_t MAX_BINDLESS_TEXTURES = 500;
	const uint32_t MAX_BINDLESS_BUFFERS = 1000;
	const uint32_t MAX_BINDLESS_TEXEL_BUFFERS = 1000;

}