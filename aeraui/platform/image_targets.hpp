/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <aeraui/backend.hpp>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace aeraui::images {
struct Block {
  std::string path;
  uint64_t bytes = 0;
};
inline bool ValidName(const std::string &name) {
  return !name.empty() && name.size() <= 128 &&
      name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos;
}
inline std::string BaseName(const std::string &name) {
  if (name.size() > 2 && (name.compare(name.size()-2, 2, "_a") == 0 ||
                         name.compare(name.size()-2, 2, "_b") == 0)) return name.substr(0, name.size()-2);
  return name;
}
inline std::string SuggestedTarget(std::string filename, const std::vector<Volume> &targets) {
  for (char &c : filename) c = std::tolower(static_cast<unsigned char>(c));
  if (filename.size() >= 4 && filename.compare(filename.size() - 4, 4, ".img") == 0)
    filename.resize(filename.size() - 4);
  struct Match { size_t begin, end; std::string path; };
  std::vector<Match> matches;
  for (const auto &target : targets) {
    std::string name = target.path.substr(target.path.find_last_of('/') + 1);
    // Filesystem image entries may use recovery mount-point aliases.
    if (target.path == "/system_root") name = "system";
    else if (target.path == "/system_image") name = "system";
    else if (target.path == "/vendor_image") name = "vendor";
    if (target.slot_select) name = BaseName(name);
    if (!ValidName(name)) continue;
    for (char &c : name) c = std::tolower(static_cast<unsigned char>(c));
    for (size_t at = filename.find(name); at != std::string::npos;
         at = filename.find(name, at + 1)) {
      const auto end = at + name.size();
      if ((at == 0 || !std::isalnum(static_cast<unsigned char>(filename[at - 1]))) &&
          (end == filename.size() || !std::isalnum(static_cast<unsigned char>(filename[end]))))
        matches.push_back({at, end, target.path});
    }
  }
  std::string result;
  for (const auto &match : matches) {
    // Prefer vendor_boot over the nested boot match, but do not guess when
    // separate partition names occur in the same filename.
    if (std::any_of(matches.begin(), matches.end(), [&](const Match &other) {
          return other.begin <= match.begin && other.end >= match.end &&
                 other.end - other.begin > match.end - match.begin;
        })) continue;
    if (!result.empty() && result != match.path) return {};
    result = match.path;
  }
  return result;
}
inline std::vector<Volume> PhysicalTargets(const std::map<std::string, Block> &blocks,
                                         const std::set<std::string> &represented) {
  std::vector<Volume> result;
  std::set<std::string> included;
  const std::set<std::string> configuration = {"userdata", "data", "metadata", "cache", "persist",
      "misc", "frp", "modemst1", "modemst2", "fsc", "fsg", "keystore", "devinfo", "config"};
  for (const auto &[name, block] : blocks) {
    const auto base = BaseName(name);
    // Super needs the dynamic-partition unmap/resize workflow, never a raw
    // fallback. Snapshot COW devices and mapper nodes are not physical targets.
    if (!ValidName(name) || base == "super" || base == "super_empty" ||
        name.find("-cow") != std::string::npos || block.path.find("/dm-") != std::string::npos ||
        configuration.count(base) || base.rfind("oplusreserve", 0) == 0 ||
        represented.count(base) || !block.bytes) continue;
    const bool slotted = blocks.count(base+"_a") && blocks.count(base+"_b");
    const auto target = slotted ? base : name;
    if (!included.insert(target).second) continue;
    const auto bytes = slotted ? std::min(blocks.at(base+"_a").bytes, blocks.at(base+"_b").bytes) : block.bytes;
    result.push_back({target, "/block/"+target, bytes, false, slotted, false});
  }
  return result;
}
}
