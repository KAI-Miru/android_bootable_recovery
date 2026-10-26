/* SPDX-License-Identifier: Apache-2.0 */
#include "../platform/image_targets.hpp"
#include <cassert>
#include <algorithm>
using namespace aeraui::images;
int main() {
  std::map<std::string, Block> blocks;
  for (const char *name : {"abl", "aop", "aop_config", "bluetooth", "boot", "cpucp", "cpucp_dtb",
      "devcfg", "dsp", "dtbo", "engineering_cdt", "featenabler", "hyp", "hyp_ac_config", "imagefv",
      "init_boot", "keymaster", "multiimgoem", "multiimgqti", "modem", "oplus_sec", "pdp", "pdp_cdb",
      "pvmfw", "qupfw", "recovery", "secretkeeper", "shrm", "soccp", "soccp_dcd", "soccp_debug",
      "splash", "spuservice", "tme_config", "tme_fw", "tme_seq_patch", "tz", "tz_ac_config", "tz_qti_config",
      "uefi", "uefisecapp", "vbmeta", "vbmeta_system", "vbmeta_vendor", "vendor_boot", "xbl",
      "xbl_ac_config", "xbl_config", "xbl_ramdump"}) {
    blocks[std::string(name)+"_a"] = {"/dev/block/"+std::string(name)+"_a", 4096};
    blocks[std::string(name)+"_b"] = {"/dev/block/"+std::string(name)+"_b", 8192};
  }
  const auto firmware = PhysicalTargets(blocks, {});
  assert(firmware.size() == 49);
  for (const auto &volume : firmware) { assert(volume.slot_select); assert(volume.bytes == 4096); }
  for (const auto &volume : firmware)
    assert(SuggestedTarget(volume.name + ".img", firmware) == volume.path);
  assert(SuggestedTarget("DTBO_A.IMG", firmware) == "/block/dtbo");
  assert(SuggestedTarget("vendor_boot-signed.img", firmware) == "/block/vendor_boot");
  assert(SuggestedTarget("AERA-guacamole-ALPHA-boot.img", firmware) == "/block/boot");
  assert(SuggestedTarget("bootloader.img", firmware).empty());
  assert(SuggestedTarget("boot-modem.img", firmware).empty());
  assert(SuggestedTarget("aera.img", firmware).empty());
  assert(SuggestedTarget("unknown.img", {{"img", "/block/img"}}).empty());
  assert(PhysicalTargets(blocks, {"boot", "init_boot", "vendor_boot", "recovery", "dtbo", "abl"}).size() == 43);
  blocks["splash"] = {"/dev/block/splash_a", 4096};
  assert(PhysicalTargets(blocks, {}).size() == firmware.size());
  for (const char *name : {"super", "userdata", "metadata", "persist", "modemst1", "misc", "oplusreserve1"})
    blocks[name] = {"/dev/block/"+std::string(name), 4096};
  blocks["system_a"] = {"/dev/block/dm-1", 4096};
  blocks["bad/name"] = {"/dev/block/bad", 4096};
  assert(PhysicalTargets(blocks, {}).size() == firmware.size());
  std::map<std::string, Block> older{{"modem", {"/dev/block/modem", 8192}}};
  const auto single = PhysicalTargets(older, {});
  assert(single.size() == 1 && !single[0].slot_select && single[0].path == "/block/modem");
  assert(!ValidName("../modem") && !ValidName("modem;reboot"));
  const std::vector<aeraui::Volume> available = {
      {"Boot", "/boot"}, {"System", "/system_root"}, {"Vendor", "/vendor", 0, false, true, true},
      {"Device-specific image", "/block/new_partition"}};
  assert(SuggestedTarget("boot.img", available) == "/boot");
  assert(SuggestedTarget("system_a.img", available) == "/system_root");
  assert(SuggestedTarget("vendor.img", available) == "/vendor");
  assert(SuggestedTarget("new_partition-v2.img", available) == "/block/new_partition");
  assert(SuggestedTarget("modem.img", available).empty());
  assert(SuggestedTarget("persist.img", PhysicalTargets(blocks, {})).empty());
  assert(SuggestedTarget("system.img", {{"System Image", "/system_image"}}) == "/system_image");
  assert(SuggestedTarget("vendor.img", {{"Vendor Image", "/vendor_image"}}) == "/vendor_image");
}
