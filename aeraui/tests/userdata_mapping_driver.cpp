// SPDX-License-Identifier: Apache-2.0
#include "../../aera_userdata_mapping.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>

using aera::userdata_mapping::State;

struct Operations {
  State initial = State::kPresent;
  unsigned queries = 0, commands = 0, sleeps = 0, logs = 0;
  unsigned disappear_after = 0, error_query = 0;
  int command_result = 0;
  bool removed = false;
  State GetState() {
    ++queries;
    if (queries == error_query) return State::kError;
    if (initial != State::kPresent) return initial;
    return removed && sleeps >= disappear_after ? State::kAbsent : State::kPresent;
  }
  int Remove(const char* command) {
    const char* expected = aera::userdata_mapping::kCommand ? aera::userdata_mapping::kCommand : "disabled";
    assert(!strcmp(command, expected));
    ++commands;
    removed = true;
    return command_result;
  }
  void LogResult(const char*, int result) { assert(result == command_result); ++logs; }
  void Sleep(unsigned usec) {
    assert(usec == 50000);
    ++sleeps;
    assert(sleeps <= 40);
  }
};

static void ProbeFilesystem() {
  char directory[] = "/tmp/aera-mapping-probe-XXXXXX";
  assert(mkdtemp(directory));
  const std::string node = std::string(directory) + "/userdata";
  assert(aera::userdata_mapping::Probe(node.c_str()) == State::kAbsent);
  int fd = open(node.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  assert(fd >= 0 && close(fd) == 0);
  assert(aera::userdata_mapping::Probe(node.c_str()) == State::kPresent);
  assert(aera::userdata_mapping::Probe((node + "/child").c_str()) == State::kError);
  assert(unlink(node.c_str()) == 0);
  assert(symlink("missing-dm-device", node.c_str()) == 0);
  assert(aera::userdata_mapping::Probe(node.c_str()) == State::kPresent);
  assert(unlink(node.c_str()) == 0);
  assert(aera::userdata_mapping::Probe(node.c_str()) == State::kAbsent);
  assert(rmdir(directory) == 0);
}

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string scenario = argv[1];
  if (scenario == "probe") { ProbeFilesystem(); return 0; }
  if (scenario == "command") {
    puts(aera::userdata_mapping::kCommand ? aera::userdata_mapping::kCommand : "none");
    return 0;
  }
  Operations operations;
  bool expected = true;
  unsigned expected_sleeps = 0;
  if (scenario == "absent") operations.initial = State::kAbsent;
  else if (scenario == "immediate") {}
  else if (scenario == "delayed") operations.disappear_after = expected_sleeps = 3;
  else if (scenario == "deadline") operations.disappear_after = expected_sleeps = 40;
  else if (scenario == "too-late" || scenario == "survives") {
    operations.disappear_after = scenario == "too-late" ? 41 : 1000;
    expected = false; expected_sleeps = 40;
  } else if (scenario == "command-failed" || scenario == "tool-missing") {
    operations.command_result = scenario == "tool-missing" ? 127 : -1;
    expected = false;
  } else if (scenario == "initial-error") {
    operations.initial = State::kError; expected = false;
  } else if (scenario == "poll-error") {
    operations.error_query = 2; expected = false;
  } else assert(false);
  const bool result = aera::userdata_mapping::Release(operations);
  if (!aera::userdata_mapping::kCommand) {
    assert(result && operations.queries == 0 && operations.commands == 0);
    assert(operations.sleeps == 0 && operations.logs == 0);
  } else {
    assert(result == expected && operations.sleeps == expected_sleeps);
    const unsigned commands = operations.initial == State::kPresent ? 1 : 0;
    assert(operations.commands == commands && operations.logs == commands);
  }
}
