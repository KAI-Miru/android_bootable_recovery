// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace aera {

// Limit the recovery-only decision to this thread's metadata mount call.
class MetadataCheckpointScope {
 public:
  MetadataCheckpointScope();
  ~MetadataCheckpointScope();
  MetadataCheckpointScope(const MetadataCheckpointScope&) = delete;
  MetadataCheckpointScope& operator=(const MetadataCheckpointScope&) = delete;

 private:
  bool previous_;
};

}  // namespace aera
