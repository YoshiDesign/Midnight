#include "FramePacketBuilder.h"

/*
Build order guarantees flavor-ordered output:
1. Process all static instances -> append to drawList, create static batches
2. Record staticInstanceCount = drawList.size(), staticBatchCount = batches.size()
3. Process all animated instances -> append to drawList, create animated batches
4. Record animatedInstanceCount = drawList.size() - staticInstanceCount
5. Record animatedBatchCount = batches.size() - staticBatchCount
6. Post-process animated batches for bone info
7. Assign pick IDs if requested
8. Store in framePackets_[frameIndex]
*/

namespace aveng {


    

    // Explicit instantiations can live here if you want to compile in one TU.
    // Otherwise include this .cpp in a compilation unit where instance types are known.
    // template const FramePacket& FramePacketBuilder::build<AvengInstance, AssimpInstance>(...);

}
