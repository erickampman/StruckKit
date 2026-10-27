// sk_presetfile_test — the Save / Load file format (src/UI/PresetFile.hpp), headless: a patch written
// and read back keeps every value, every factory preset in Presets/ parses with all of its keys, and
// files that are not presets are refused.
//
// Usage: sk-presetfile-test <Presets dir> <scratch dir>

#include "PresetFile.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace struckkit;

namespace {

int g_fails = 0;
void check(bool ok, const std::string& what) {
   std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
   if (!ok) ++g_fails;
}

bool find(const PresetFileData& d, const std::string& key, float& v) {
   for (const auto& kv : d.values)
      if (kv.first == key) { v = kv.second; return true; }
   return false;
}

} // namespace

int main(int argc, char** argv) {
   if (argc < 3) { std::fprintf(stderr, "usage: %s <Presets dir> <scratch dir>\n", argv[0]); return 2; }
   namespace fs = std::filesystem;
   const fs::path presets(argv[1]), scratch(argv[2]);
   fs::create_directories(scratch);

   std::printf("round trip\n");
   {
      float values[kParamCount];
      for (uint32_t i = 0; i < kParamCount; ++i) {
         // somewhere inside each range, not the default, so a dropped key shows
         const float t = 0.25f + 0.5f * float(i) / float(kParamCount);
         values[i] = kParams[i].min + t * (kParams[i].max - kParams[i].min);
         if (kParams[i].integer) values[i] = std::round(values[i]);
      }
      const fs::path file = scratch / "round \"trip\".json";
      check(writePresetFile(file.string(), values, "round \"trip\""), "writes");
      PresetFileData d;
      check(readPresetFile(file.string(), d), "reads back as a preset");
      check(d.name == "round \"trip\"", "keeps the name, quotes and all");
      bool all = true;
      for (uint32_t i = 0; i < kParamCount; ++i) {
         float v = 0.0f;
         const std::string key = std::string(kPresetKeys[i].module) + "." + kPresetKeys[i].item;
         const bool has = find(d, key, v);
         if (!has || std::fabs(v - values[i]) > 1e-5f) {
            std::printf("    %s: %s\n", key.c_str(), has ? "differs" : "missing");
            all = false;
         }
      }
      check(all, "every param comes back");
   }

   std::printf("factory presets\n");
   int count = 0;
   for (const auto& e : fs::directory_iterator(presets)) {
      if (e.path().extension() != ".json") continue;
      ++count;
      PresetFileData d;
      const bool ok = readPresetFile(e.path().string(), d);
      bool all = ok;
      for (uint32_t i = 0; ok && i < kParamCount; ++i) {
         if (i == kParamVoices) continue;   // came after the factory presets, which leave it alone
         float v;
         if (!find(d, std::string(kPresetKeys[i].module) + "." + kPresetKeys[i].item, v)) all = false;
      }
      check(all, e.path().filename().string() + " parses with every sound param");
   }
   check(count > 0, "found factory presets");

   std::printf("not presets\n");
   {
      PresetFileData d;
      check(!parsePresetJson("", d), "empty");
      check(!parsePresetJson("{\"state\": {\"parameterState\": {}}}", d), "no schema");
      check(!parsePresetJson("{\"schema\": \"other.preset\"}", d), "another schema");
      check(!parsePresetJson("{\"schema\": \"struckkit.preset\"", d), "truncated");
      check(parsePresetJson("{\"schema\": \"struckkit.preset\", \"x\": [1, {\"y\": true}, null]}", d),
            "arrays, bools and null are skipped, not refused");
   }

   std::printf(g_fails == 0 ? "ALL PASS\n" : "%d FAILED\n", g_fails);
   return g_fails == 0 ? 0 : 1;
}
