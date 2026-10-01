//
//  ModuleInitSpecs.hpp
//
//  Created by Eric Kampman on 5/31/25.
//

#ifndef ModuleInitSpecs_hpp
#define ModuleInitSpecs_hpp

#include <vector>

#include "Module.hpp" // ModuleInitProc / ModuleProcessProc / ... typedefs
#include "pm_params.h"

struct ModuleInitSpec {
    AUParameterAddress id;
    const ModuleParamConstantsGroup* constantsGroup;
    ModuleInitProc initProc = nullptr;
    ModuleDeinitProc deinitProc = nullptr;
    ModuleProcessProc processProc = nullptr;
    ModuleMIDIProc midiProc = nullptr;
    ModuleParamProc paramProc = nullptr;
    ModuleResetProc resetProc = nullptr;
    ModuleSampleRateProc sampleRateProc = nullptr;
    // Optional: if set, the module gets one cross-voice shared object per placement (see
    // ModuleSharedState.hpp). Added last so existing positional initializers keep compiling.
    ModuleSharedStateCreateProc sharedStateCreateProc = nullptr;
};

const std::vector<ModuleInitSpec>& getModuleInitSpecs();

#endif /* ModuleInitSpecs_hpp */
