// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "ui_components.hpp"
#include "phone_keyboard.hpp"

namespace aeraui::widgets {
inline void ClearPassword(std::string& text) {
  volatile char* bytes = text.empty() ? nullptr : &text[0];
  for (size_t i = 0; i < text.size(); ++i) bytes[i] = 0;
  text.clear();
}

struct ScreenPasswordChange {
  std::string current, replacement;
  ~ScreenPasswordChange() { ClearPassword(current); ClearPassword(replacement); }
};

// Return true to close. Never send passwords through DataManager, logs or RPC.
using PasswordSubmit = std::function<bool(const std::string&, std::string&)>;
inline void ScreenPasswordPrompt(lv_obj_t* parent, const char* title,
                                 const char* description, PasswordSubmit submit) {
  auto* overlay = lv_obj_create(parent);
  Clear(overlay);
  lv_obj_set_user_data(overlay, &kModalMarker);
  lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(overlay, kMainCanvas, 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
  const bool landscape = Landscape(parent);
  auto* form = lv_obj_create(overlay);
  Clear(form);
  lv_obj_set_size(form, landscape ? LV_PCT(48) : LV_PCT(92),
                       landscape ? LV_PCT(92) : LV_SIZE_CONTENT);
  lv_obj_align(form, landscape ? LV_ALIGN_LEFT_MID : LV_ALIGN_TOP_MID, 24, 24);
  lv_obj_set_flex_flow(form, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(form, 24, 0);
  auto* heading = Label(form, title, &lv_font_montserrat_48, kText);
  lv_obj_set_width(heading, LV_PCT(100));
  auto* hint = Label(form, description, &lv_font_montserrat_28, kMuted);
  lv_obj_set_width(hint, LV_PCT(100)); lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  auto* input = TextArea(form);
  lv_obj_set_size(input, LV_PCT(100), 124);
  lv_textarea_set_one_line(input, true);
  lv_textarea_set_password_mode(input, true);
  lv_textarea_set_password_show_time(input, 0);
  lv_textarea_set_max_length(input, 64);
  lv_textarea_set_placeholder_text(input, i18n::Translate("Password"));
  lv_obj_set_style_text_font(input, UiFont(&lv_font_montserrat_32), 0);
  lv_obj_set_style_text_color(input, kText, 0);
  lv_obj_set_style_text_color(input, kMuted, LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_bg_color(input, kMainPanel, 0);
  lv_obj_set_style_bg_opa(input, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(input, kMainLine, 0);
  lv_obj_set_style_border_width(input, 2, 0);
  lv_obj_set_style_border_color(input, kAccent, LV_STATE_FOCUSED);
  lv_obj_set_style_radius(input, 24, 0);
  lv_obj_set_style_pad_all(input, 28, 0);
  auto* error = Label(form, "", &lv_font_montserrat_28, kText);
  lv_obj_set_width(error, LV_PCT(100)); lv_label_set_long_mode(error, LV_LABEL_LONG_WRAP);
  auto* actions = lv_obj_create(form);
  Clear(actions); lv_obj_set_size(actions, LV_PCT(100), 124);
  lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(actions, 24, 0);
  auto* cancel = Button(actions, "Cancel", [overlay, input] {
    lv_textarea_set_text(input, ""); lv_obj_delete_async(overlay);
  });
  auto* confirm = Button(actions, "Continue", [overlay, input, error, submit] {
    std::string password = lv_textarea_get_text(input), message;
    lv_textarea_set_text(input, "");
    const bool okay = submit(password, message);
    ClearPassword(password);
    if (lv_obj_is_valid(overlay)) {
      if (okay) lv_obj_delete_async(overlay);
      else i18n::BindLabel(error, message.c_str());
    }
  }, true);
  for (auto* button : {cancel, confirm}) {
    lv_obj_set_width(button, LV_PCT(46)); lv_obj_set_height(button, 116);
  }
  auto* keyboard = lv_keyboard_create(overlay);
  phone_keyboard::Apply(keyboard);
  lv_obj_set_size(keyboard, landscape ? LV_PCT(48) : LV_PCT(94),
                           landscape ? LV_PCT(90) : LV_PCT(48));
  lv_obj_align(keyboard, landscape ? LV_ALIGN_RIGHT_MID : LV_ALIGN_BOTTOM_MID, -24, -24);
  lv_keyboard_set_textarea(keyboard, input);
  lv_obj_add_event_cb(keyboard, [](lv_event_t* event) {
    lv_obj_send_event(static_cast<lv_obj_t*>(lv_event_get_user_data(event)), LV_EVENT_CLICKED, nullptr);
  }, LV_EVENT_READY, confirm);
  lv_obj_add_event_cb(input, [](lv_event_t* event) {
    lv_obj_send_event(static_cast<lv_obj_t*>(lv_event_get_user_data(event)), LV_EVENT_CLICKED, nullptr);
  }, LV_EVENT_READY, confirm);
  lv_obj_add_state(input, LV_STATE_FOCUSED);
  lv_obj_send_event(input, LV_EVENT_FOCUSED, nullptr);
}
}  // namespace aeraui::widgets
