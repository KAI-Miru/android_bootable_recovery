// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "design.hpp"

namespace aeraui::widgets {
// The native decryption illustration, shared with screen lock and Preferences.
inline lv_obj_t* LockIcon(lv_obj_t* parent, int size = 208) {
  using namespace design;
  const auto scale = [size](int value) { return value * size / 208; };
  auto* icon = lv_obj_create(parent);
  Clear(icon);
  lv_obj_set_size(icon, size, size);
  lv_obj_set_style_pad_all(icon, 0, 0);
  lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
  auto* shackle = lv_obj_create(icon);
  Clear(shackle);
  lv_obj_set_size(shackle, scale(74), scale(86));
  lv_obj_set_pos(shackle, scale(67), scale(44));
  lv_obj_set_style_radius(shackle, scale(38), 0);
  lv_obj_set_style_border_width(shackle, std::max(1, scale(9)), 0);
  lv_obj_set_style_border_color(shackle, kAccent, 0);
  auto* body = lv_obj_create(icon);
  Panel(body, scale(16), kAccent);
  lv_obj_set_size(body, scale(104), scale(80));
  lv_obj_set_pos(body, scale(52), scale(94));
  lv_obj_set_style_pad_all(body, 0, 0);
  // Hide the lower shackle arc even when the selected surface is translucent.
  lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
  lv_obj_set_style_blur_backdrop(body, false, 0);
  lv_obj_set_style_blur_radius(body, 0, 0);
  auto* dot = lv_obj_create(body);
  Clear(dot);
  lv_obj_set_size(dot, scale(18), scale(18));
  lv_obj_align(dot, LV_ALIGN_CENTER, 0, -scale(7));
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot, kCanvas, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  auto* stem = lv_obj_create(body);
  Clear(stem);
  lv_obj_set_size(stem, scale(8), scale(24));
  lv_obj_align(stem, LV_ALIGN_CENTER, 0, scale(10));
  lv_obj_set_style_radius(stem, scale(4), 0);
  lv_obj_set_style_bg_color(stem, kCanvas, 0);
  lv_obj_set_style_bg_opa(stem, LV_OPA_COVER, 0);
  for (auto* part : {shackle, body, dot, stem})
    lv_obj_remove_flag(part, LV_OBJ_FLAG_CLICKABLE);
  return icon;
}
}  // namespace aeraui::widgets
