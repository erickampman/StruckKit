// pm_factory_test — builds the whole v1 module set through ModuleFactory + the trimmed
// getModuleInitSpecs() registry, places them in a ModuleMap (as a Voice will), and confirms the
// SinOsc among them still produces sound. Proves the factory/spec glue off Apple.

#include "Module.hpp"
#include "ModuleFactory.hpp"
#include "ModuleInitSpecs.hpp"
#include "ModuleMap.hpp"
#include "PitchConverter.hpp"
#include "pm_addresses.h"

#include <cmath>
#include <cstdio>

static int g_fails = 0;
#define CHECK(cond, msg)                                                                           \
   do {                                                                                            \
      if (!(cond)) {                                                                               \
         std::printf("FAIL: %s\n", (msg));                                                         \
         ++g_fails;                                                                                \
      }                                                                                            \
   } while (0)

int main() {
   const auto& specs = getModuleInitSpecs();
   CHECK(specs.size() == 19, "19 module specs (v1 + DrumKit)");

   ModuleFactory factory(48000.0, 512);
   ModuleMap map;
   map.reserveModules(specs.size());
   PitchConverter pc(440.0);

   for (const auto& s : specs) {
      auto mod = factory.createModule(s.id, s.constantsGroup, s.initProc, s.deinitProc,
                                      s.processProc, s.midiProc, s.paramProc, s.resetProc,
                                      s.sampleRateProc);
      mod->setPitchConverter(&pc);
      map.add(s.id, std::move(mod));
   }
   CHECK(map.allModules().size() == 19, "19 modules built into the map");

   // Every named v1 module is present.
   CHECK(map.get(pitchConverter1Address) != nullptr, "pitchConverter present");
   CHECK(map.get(slew1Address) != nullptr, "slew present");
   CHECK(map.get(sinOsc1Address) != nullptr, "sinOsc present");
   CHECK(map.get(ifsOsc1Address) != nullptr, "ifsOsc present");
   CHECK(map.get(adsr1Address) != nullptr, "adsr present");
   CHECK(map.get(ifsEnv1Address) != nullptr, "ifsEnv present");
   CHECK(map.get(ifsLFO1Address) != nullptr, "ifsLFO present");
   CHECK(map.get(ifsEnv2Address) != nullptr, "ifsEnv2 present");
   CHECK(map.get(ifsLFO2Address) != nullptr, "ifsLFO2 present");
   CHECK(map.get(cvMixer1Address) != nullptr, "cvMixer1 present");
   CHECK(map.get(cvMixer2Address) != nullptr, "cvMixer2 present");
   CHECK(map.get(combine1Address) != nullptr, "combine present");
   CHECK(map.get(finalAddress) != nullptr, "final present");
   CHECK(map.get(strike1Address) != nullptr, "strike present");
   CHECK(map.get(modalBank1Address) != nullptr, "modalBank present");

   // The SinOsc built by the factory still runs and oscillates.
   Module* osc = map.get(sinOsc1Address);
   CHECK(osc && osc->processProc, "sinOsc has a process proc");
   osc->processProc(1, 512, osc);
   int crossings = 0;
   for (int i = 1; i < 512; ++i)
      if ((osc->output.first->data[i - 1] <= 0.0f) != (osc->output.first->data[i] <= 0.0f))
         ++crossings;
   CHECK(crossings >= 6, "factory-built sinOsc oscillates");

   if (g_fails == 0)
      std::printf("ALL PASS\n");
   return g_fails ? 1 : 0;
}
