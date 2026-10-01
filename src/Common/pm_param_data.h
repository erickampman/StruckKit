// pm_param_data.h — per-module parameter constant groups (converted from the Objective-C
// ParamGroupData.m). Each module's group is added as the module is ported. NOTE: for the DSP
// port these carry the correct item enums + default values (what addParams / the process
// functions read); full metadata fidelity (units, value strings, exact ranges for the CLAP
// params UI) is finished in L2 when the params extension is built.

#ifndef PM_PARAM_DATA_H
#define PM_PARAM_DATA_H

#include "pm_params.h"

extern const ModuleParamConstantsGroup sinOscConstantsGroup;
extern const ModuleParamConstantsGroup ifsOscConstantsGroup;
extern const ModuleParamConstantsGroup ifsEnvConstantsGroup;
extern const ModuleParamConstantsGroup ifsLFOConstantsGroup;
extern const ModuleParamConstantsGroup cvMixerConstantsGroup;
extern const ModuleParamConstantsGroup lpfConstantsGroup;
extern const ModuleParamConstantsGroup levelConstantsGroup;
extern const ModuleParamConstantsGroup adsrConstantsGroup;
extern const ModuleParamConstantsGroup combineConstantsGroup;
extern const ModuleParamConstantsGroup finalConstantsGroup;
extern const ModuleParamConstantsGroup slewModuleConstantsGroup;
extern const ModuleParamConstantsGroup pitchConverterModuleConstantsGroup;
extern const ModuleParamConstantsGroup strikeConstantsGroup;
extern const ModuleParamConstantsGroup modalBankConstantsGroup;
extern const ModuleParamConstantsGroup drumKitConstantsGroup;

#endif // PM_PARAM_DATA_H
