// OpenExternal.hpp — hand a URL or file path to the OS so it opens in the user's default app (the
// default browser for http(s) URLs and .html files). DPF has no built-in cross-platform helper.

#ifndef STRUCKKIT_OPEN_EXTERNAL_HPP
#define STRUCKKIT_OPEN_EXTERNAL_HPP

#include "DistrhoUtils.hpp" // DISTRHO_OS_* macros

#include <cstdlib>
#include <string>

namespace struckkit {

// Best-effort; nothing sensible to do if the launch fails. The target is quoted for the shell, so
// paths with spaces (or a stray quote) are safe.
inline void openExternal(const std::string& target)
{
#if defined(DISTRHO_OS_WINDOWS)
    std::string quoted = "\"";
    for (char c : target) if (c != '"') quoted += c;
    quoted += "\"";
    const std::string cmd = "start \"\" " + quoted;
#else
    std::string quoted = "'";
    for (char c : target) { if (c == '\'') quoted += "'\\''"; else quoted += c; }
    quoted += "'";
  #if defined(DISTRHO_OS_MAC)
    const std::string cmd = "open " + quoted;
  #else
    const std::string cmd = "xdg-open " + quoted + " >/dev/null 2>&1 &"; // xdg-open can block
  #endif
#endif
    const int rc = std::system(cmd.c_str());
    (void)rc;
}

} // namespace struckkit

#endif // STRUCKKIT_OPEN_EXTERNAL_HPP
