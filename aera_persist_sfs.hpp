// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace aera {

// Qualcomm SFS needs writable persist when RPMB has no authentication key.
// Never provisions RPMB; unknown/error status leaves the mount unchanged.
void PreparePersistForSfs();

}  // namespace aera
