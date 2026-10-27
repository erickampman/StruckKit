// PresetFile.hpp — a patch as a preset file: the same module-nested JSON as Presets/*.json, written
// by the UI's Save and read back by its Load (and by the dev-only dumper, PresetDump.hpp). Uses the
// kPresetKeys mapping (module, item pair per param) from PresetKeys.hpp, so a saved patch can also be
// dropped into Presets/ as a factory preset unchanged.
//
// The reader is a small JSON parser of its own, not a library: it keeps only what the loader needs
// (schema, meta.name, and the state.parameterState leaves flattened to "module.item" -> float, just
// as tools/gen_presets.py flattens the factory files) and skips everything else.

#ifndef STRUCKKIT_PRESETFILE_HPP
#define STRUCKKIT_PRESETFILE_HPP

#include "StruckKitParams.h"
#include "PresetKeys.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace struckkit {

static constexpr const char* kPresetSchema = "struckkit.preset";

// Emit modules in this order; every module in kPresetKeys must appear here.
static constexpr const char* kPresetModuleOrder[] = {
   "Master", "Crash", "Snare", "Toms", "Hats", "HandDrums", "Percussion",
};

inline std::string jsonEscape(const std::string& s)
{
   std::string out;
   for (const char c : s)
   {
      if (c == '"' || c == '\\') { out += '\\'; out += c; }
      else if (static_cast<unsigned char>(c) < 0x20) out += ' ';
      else out += c;
   }
   return out;
}

// Build the module-nested JSON for `values` (kParamCount entries). Values are stringified like PM
// (ints as "0"/"1", floats to 6 dp).
inline std::string buildPresetJson(const float* values, const std::string& name = "Untitled")
{
   char iso[32] = "";
   const std::time_t t = std::time(nullptr);
   std::strftime(iso, sizeof iso, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));

   std::string j;
   j += "{\n";
   j += "  \"app\": { \"name\": \"StruckKit\", \"version\": \"1.0.0\" },\n";
   j += "  \"plugin\": { \"format\": \"clap\", \"id\": \"com.unlikelyware.struckkit\" },\n";
   j += "  \"meta\": { \"author\": \"\", \"createdAt\": \"" + std::string(iso)
        + "\", \"name\": \"" + jsonEscape(name) + "\" },\n";
   j += "  \"schema\": \"" + std::string(kPresetSchema) + "\",\n";
   j += "  \"schemaVersion\": 1,\n";
   j += "  \"state\": {\n";
   j += "    \"parameterState\": {\n";

   const int nMods = static_cast<int>(sizeof(kPresetModuleOrder) / sizeof(kPresetModuleOrder[0]));
   for (int m = 0; m < nMods; ++m)
   {
      const char* mod = kPresetModuleOrder[m];
      j += "      \"" + std::string(mod) + "\": {\n";
      bool first = true;
      for (uint32_t i = 0; i < kParamCount; ++i)
      {
         if (std::strcmp(kPresetKeys[i].module, mod) != 0)
            continue;
         char val[32];
         if (kParams[i].integer)
            std::snprintf(val, sizeof val, "%d", static_cast<int>(std::lround(values[i])));
         else
            std::snprintf(val, sizeof val, "%.6f", values[i]);
         if (!first) j += ",\n";
         first = false;
         j += "        \"" + std::string(kPresetKeys[i].item) + "\": \"" + val + "\"";
      }
      j += "\n      }";
      j += (m + 1 < nMods) ? ",\n" : "\n";
   }

   j += "    }\n";
   j += "  }\n";
   j += "}\n";
   return j;
}

inline bool writePresetFile(const std::string& path, const float* values, const std::string& name)
{
   const std::string json = buildPresetJson(values, name);
   std::ofstream out(path, std::ios::binary | std::ios::trunc);
   if (!out) return false;
   out.write(json.data(), static_cast<std::streamsize>(json.size()));
   return static_cast<bool>(out);
}

// What the loader keeps from a preset file.
struct PresetFileData
{
   std::string schema;
   std::string name;                                   // meta.name, "" if absent
   std::vector<std::pair<std::string, float>> values;  // "module.item" -> value
};

namespace detail {

class PresetJsonReader
{
public:
   PresetJsonReader(const std::string& text, PresetFileData& out) : s(text), out(out) {}

   bool parse()
   {
      skipWs();
      if (!value()) return false;
      skipWs();
      return pos == s.size();
   }

private:
   void skipWs()
   {
      while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
         ++pos;
   }

   bool string(std::string& v)
   {
      if (pos >= s.size() || s[pos] != '"') return false;
      ++pos;
      v.clear();
      while (pos < s.size())
      {
         const char c = s[pos++];
         if (c == '"') return true;
         if (c != '\\') { v += c; continue; }
         if (pos >= s.size()) return false;
         const char e = s[pos++];
         switch (e)
         {
            case '"': case '\\': case '/': v += e; break;
            case 'n': case 't': case 'r': case 'b': case 'f': v += ' '; break;
            case 'u':   // not needed for any key or number; keep the name readable
               if (pos + 4 > s.size()) return false;
               pos += 4; v += '?'; break;
            default: return false;
         }
      }
      return false;
   }

   // A leaf: record it if it sits where the loader looks.
   void leaf(const std::string& text)
   {
      if (path.size() == 1 && path[0] == "schema") out.schema = text;
      else if (path.size() == 2 && path[0] == "meta" && path[1] == "name") out.name = text;
      else if (path.size() == 4 && path[0] == "state" && path[1] == "parameterState")
      {
         char* end = nullptr;
         const float v = std::strtof(text.c_str(), &end);
         if (end != text.c_str() && std::isfinite(v))
            out.values.emplace_back(path[2] + "." + path[3], v);
      }
   }

   bool value()
   {
      if (++depth > 32) return false;
      bool ok = false;
      if (pos >= s.size()) ok = false;
      else if (s[pos] == '{') ok = object();
      else if (s[pos] == '[') ok = array();
      else if (s[pos] == '"') { std::string v; ok = string(v); if (ok) leaf(v); }
      else
      {
         // number, true, false, null
         const size_t start = pos;
         while (pos < s.size() && std::strchr(",}] \t\r\n", s[pos]) == nullptr) ++pos;
         const std::string tok = s.substr(start, pos - start);
         ok = !tok.empty();
         if (ok && tok != "null") leaf(tok == "true" ? "1" : tok == "false" ? "0" : tok);
      }
      --depth;
      return ok;
   }

   bool object()
   {
      ++pos; skipWs();
      if (pos < s.size() && s[pos] == '}') { ++pos; return true; }
      while (true)
      {
         std::string key;
         skipWs();
         if (!string(key)) return false;
         skipWs();
         if (pos >= s.size() || s[pos] != ':') return false;
         ++pos; skipWs();
         path.push_back(key);
         const bool ok = value();
         path.pop_back();
         if (!ok) return false;
         skipWs();
         if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
         if (pos < s.size() && s[pos] == '}') { ++pos; return true; }
         return false;
      }
   }

   bool array()
   {
      ++pos; skipWs();
      if (pos < s.size() && s[pos] == ']') { ++pos; return true; }
      path.push_back("[]");   // nothing the loader wants lives in an array
      while (true)
      {
         skipWs();
         if (!value()) { path.pop_back(); return false; }
         skipWs();
         if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
         path.pop_back();
         if (pos < s.size() && s[pos] == ']') { ++pos; return true; }
         return false;
      }
   }

   const std::string&       s;
   PresetFileData&          out;
   size_t                   pos = 0;
   int                      depth = 0;
   std::vector<std::string> path;
};

} // namespace detail

// Parse preset JSON. False for malformed JSON or a file that is not a StruckKit preset.
inline bool parsePresetJson(const std::string& text, PresetFileData& out)
{
   out = PresetFileData();
   detail::PresetJsonReader reader(text, out);
   return reader.parse() && out.schema == kPresetSchema;
}

inline bool readPresetFile(const std::string& path, PresetFileData& out)
{
   std::ifstream in(path, std::ios::binary);
   if (!in) return false;
   const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
   if (text.size() > (1u << 20)) return false;   // a preset is ~1 KB; this is not one
   return parsePresetJson(text, out);
}

} // namespace struckkit

#endif // STRUCKKIT_PRESETFILE_HPP
