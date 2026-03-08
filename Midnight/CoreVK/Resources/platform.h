#pragma once
namespace aveng {

#ifndef WTF_BOOM
#define WTF_BOOM -1
#endif

	#define ArraySize(array)        ( sizeof(array)/sizeof((array)[0]) )
	const size_t k_invalid_index = 0xfffffffe;

}