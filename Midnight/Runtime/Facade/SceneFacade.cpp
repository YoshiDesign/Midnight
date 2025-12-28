#include "SceneFacade.h"

namespace aveng {

    SceneFacade::SceneFacade(
        IModelLibrary& modelLib,
        const IModelQuery& modelQuery
    )
        : modelLib_(modelLib)
        , modelQuery_(modelQuery)
    {
    }

}