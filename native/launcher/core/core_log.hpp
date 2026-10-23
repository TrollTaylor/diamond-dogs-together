// Diamond Dogs Together launcher core: mgs-coop\launcher.log. One line per
// step: "<UTC time> pid=<process> <text>". Only states, times, process ids and
// counts go in: every line is passed through RedactForLog, so a join code,
// a SteamID or a path never reaches the file even by mistake. Over 1 MB the
// log moves to launcher.old.log first. Never throws, never blocks for long
// (the window and the background helper write to the same file).
#pragma once
#include <string>

#include "core_env.hpp"
#include "core_paths.hpp"

namespace ddt::core {

inline constexpr std::uint64_t kLauncherLogMaxBytes = 1u << 20;
void LauncherLog(Env& env, const InstancePaths& paths, const std::string& text);

}  // namespace ddt::core
