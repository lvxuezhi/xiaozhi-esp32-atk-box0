#include "pc_monitor_page.h"
#include <lvgl.h>
#include <esp_lvgl_port.h>
#include <esp_log.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "application.h"

#define TAG "PcMonitorPage"

// Declare the Noto Sans font with CJK support
LV_FONT_DECLARE(font_noto_sans_basic_20_4);

PcMonitorPage::PcMonitorPage() = default;

static const lv_color_t kBarColor = LV_COLOR_MAKE(0x00, 0xFF, 0xFF);
static const lv_color_t kBarWarn  = LV_COLOR_MAKE(0xFF, 0x66, 0x00);
static const lv_color_t kBarDanger= LV_COLOR_MAKE(0xFF, 0x00, 0x00);

struct RowDef {
    const char* key;
    float warn_pct;
    float danger_pct;
};

static constexpr RowDef kRowDefs[] = {
    {"cpu",  75.0f, 95.0f},
    {"mem",  80.0f, 95.0f},
    {"gpu",  70.0f, 95.0f},
    {"disk", 85.0f, 95.0f},
};

PcMonitorPage::~PcMonitorPage() = default;

void PcMonitorPage::Show() {
    ESP_LOGI("PcMonitorPage", "Show() called, visible_=%d created_=%d", (int)visible_, (int)created_);
    if (visible_) return;
    lv_async_call([](void* arg) {
        ESP_LOGI("PcMonitorPage", "async callback firing");
        static_cast<PcMonitorPage*>(arg)->DoShow();
    }, this);
}

void PcMonitorPage::DoShow() {
    if (!created_) {
        auto* screen = lv_screen_active();
        if (!screen) {
            ESP_LOGE("PcMonitorPage", "ERROR: lv_screen_active() returned NULL!");
            return;
        }
        ESP_LOGI("PcMonitorPage", "screen OK, creating fullscreen overlay");

        lv_coord_t w = lv_display_get_horizontal_resolution(nullptr);
        lv_coord_t h = lv_display_get_vertical_resolution(nullptr);
        if (!w || !h) { w = 320; h = 240; }
        ESP_LOGI("PcMonitorPage", "display resolution %dx%d", (int)w, (int)h);

        // FULLSCREEN overlay — direct children of screen, no container.
        // Absolute coordinates: no flex, no wrap, no auto-resize.
        // DIAGNOSTIC: magenta background so we can tell at a glance whether
        // the overlay is actually rendered on top of the main UI.
        page_ = lv_obj_create(screen);
        lv_obj_set_size(page_, w, h);
        lv_obj_set_pos(page_, 0, 0);
        lv_obj_clear_flag(page_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(page_, 0, 0);
        lv_obj_set_style_bg_color(page_, lv_color_make(0x10, 0x14, 0x1A), 0);
        lv_obj_set_style_bg_opa(page_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(page_, 0, 0);

        const lv_font_t* text_font = &font_noto_sans_basic_20_4;

        const int kFontH = (int)text_font->line_height;
        const int kXName = 6;
        const int kNameW = 64;   // widest label is "DISK" at ~52px
        const int kValW  = 56;   // "100%" fits comfortably
        const int kBarW  = w - kXName - kNameW - 12 - kValW;
        const int kRowH  = kFontH + 8;
        ESP_LOGI("PcMonitorPage", "layout: kXName=%d kNameW=%d kValW=%d kBarW=%d kRowH=%d",
                 kXName, kNameW, kValW, kBarW, kRowH);

        const int kTitleY = 6;
        const int kRowsY = kTitleY + kFontH + 6;
        const int kBottomY = kRowsY + 4 * kRowH + 2;

        // Title
        lv_obj_t* title = lv_label_create(page_);
        lv_label_set_text(title, "PC MONITOR");
        lv_obj_set_pos(title, 8, kTitleY);
        lv_obj_set_size(title, w - 16, kFontH);
        lv_obj_set_style_text_font(title, text_font, 0);
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);

        const char* names[] = {"CPU", "MEM", "GPU", "DISK"};
        for (int i = 0; i < 4; i++) {
            int y = kRowsY + i * kRowH;

            lv_obj_t* name = lv_label_create(page_);
            lv_label_set_text(name, names[i]);
            lv_obj_set_pos(name, kXName, y);
            lv_obj_set_size(name, kNameW, kFontH);
            lv_obj_set_style_text_font(name, text_font, 0);
            lv_obj_set_style_text_color(name, lv_color_white(), 0);
            lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_CLIP);
            lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);

            // Bar — sits between the name and the value, no overlap
            lv_obj_t* bar = lv_bar_create(page_);
            lv_bar_set_range(bar, 0, 100);
            lv_bar_set_value(bar, 0, LV_ANIM_OFF);
            lv_obj_set_pos(bar, kXName + kNameW + 6, y + 2);
            lv_obj_set_size(bar, kBarW, kFontH - 8);
            lv_obj_set_style_bg_color(bar, lv_color_make(0x33, 0x33, 0x33), 0);
            lv_obj_set_style_radius(bar, 3, 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
            lv_obj_set_style_pad_all(bar, 0, 0);
            lv_obj_set_style_pad_all(bar, 0, LV_PART_INDICATOR);
            lv_obj_t* indic = lv_obj_get_child(bar, 0);
            if (indic) {
                lv_obj_set_style_bg_color(indic, kBarColor, 0);
                lv_obj_set_style_radius(indic, 3, 0);
            }
            bar_[i] = bar;

            // Value: right edge flush with the screen's right margin
            lv_obj_t* val = lv_label_create(page_);
            lv_label_set_text(val, "--");
            lv_obj_set_pos(val, w - kValW, y);
            lv_obj_set_size(val, kValW - 4, kFontH);
            lv_obj_set_style_text_font(val, text_font, 0);
            lv_obj_set_style_text_color(val, lv_color_white(), 0);
            lv_label_set_long_mode(val, LV_LABEL_LONG_MODE_CLIP);
            lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
            value_label_[i] = val;
        }

        // Bottom status
        bottom_label_ = lv_label_create(page_);
        lv_label_set_text(bottom_label_, "Waiting for PC data...");
        lv_obj_set_pos(bottom_label_, kXName, kBottomY);
        lv_obj_set_size(bottom_label_, w - kXName - 8, kFontH);
        lv_obj_set_style_text_font(bottom_label_, text_font, 0);
        lv_obj_set_style_text_color(bottom_label_, lv_color_make(0xAA, 0xAA, 0xAA), 0);
        lv_label_set_long_mode(bottom_label_, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_align(bottom_label_, LV_TEXT_ALIGN_LEFT, 0);

        // Move to top so it covers the main UI
        lv_obj_move_foreground(page_);
        ESP_LOGI("PcMonitorPage", "page created and moved to foreground, page_=%p", page_);

        lv_obj_add_event_cb(page_, [](lv_event_t* e) {
            auto* self = (PcMonitorPage*)lv_event_get_user_data(e);
            if (self && self->visible_) {
                self->Hide();
            }
        }, LV_EVENT_CLICKED, this);

        created_ = true;
    }

    visible_ = true;
    ESP_LOGI("PcMonitorPage", "page created fully, now running deferred ops");
    // Run deferred ops immediately (not via async call) to ensure visibility
    for (auto& op : deferred_ops_) {
        op();
    }
    deferred_ops_.clear();
    lv_refr_now(lv_display_get_default());
    ESP_LOGI("PcMonitorPage", "forced refresh done, page_=%p", page_);

    // Apply any JSON string received before the page existed
    if (!pending_json_.empty()) {
        cJSON* root = cJSON_Parse(pending_json_.c_str());
        if (root) {
            deferred_ops_.emplace_back([this, root]() {
                ApplyStats(root);
                cJSON_Delete(root);
            });
            pending_json_.clear();
        }
    }

    deferred_ops_.emplace_back([this]() {
        lv_obj_set_style_opa(page_, LV_OPA_COVER, 0);
    });
    lv_async_call([](void* arg) {
        static_cast<PcMonitorPage*>(arg)->FlushDeferredOps();
    }, this);
}

void PcMonitorPage::SetPendingJson(const std::string& json) {
    pending_json_ = json;
}

void PcMonitorPage::Hide() {
    if (!created_ || !visible_) return;
    visible_ = false;
    deferred_ops_.emplace_back([this]() {
        lv_obj_set_style_opa(page_, LV_OPA_0, 0);
    });
    lv_async_call([](void* arg) {
        static_cast<PcMonitorPage*>(arg)->FlushDeferredOps();
    }, this);
}

void PcMonitorPage::ApplyStats(const cJSON* root) {
    if (!root || !page_) return;
    ESP_LOGI("PcMonitorPage", "ApplyStats called");
    deferred_ops_.emplace_back([this, root]() {
        for (int i = 0; i < 4; i++) {
            cJSON* val = cJSON_GetObjectItemCaseSensitive(root, kRowDefs[i].key);
            float pct = 0.0f;
            if (cJSON_IsNumber(val)) {
                pct = (float)val->valuedouble;
                if (pct > 100.0f) pct = 100.0f;
            }

            lv_color_t color = kBarColor;
            if (pct >= kRowDefs[i].danger_pct) color = kBarDanger;
            else if (pct >= kRowDefs[i].warn_pct) color = kBarWarn;

            if (bar_[i]) {
                lv_bar_set_value(bar_[i], (int)lroundf(pct), LV_ANIM_OFF);
                lv_obj_t* indic = lv_obj_get_child(bar_[i], 0);
                if (indic) {
                    lv_obj_set_style_bg_color(indic, color, 0);
                }
            }

            if (value_label_[i]) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d%%", (int)lroundf(pct));
                lv_label_set_text(value_label_[i], buf);
            }
        }
    });
    lv_async_call([](void* arg) {
        static_cast<PcMonitorPage*>(arg)->FlushDeferredOps();
    }, this);
}

void PcMonitorPage::FlushDeferredOps() {
    ESP_LOGI("PcMonitorPage", "FlushDeferredOps called, ops=%d", (int)deferred_ops_.size());
    for (auto& op : deferred_ops_) {
        op();
    }
    deferred_ops_.clear();
    lv_refr_now(lv_display_get_default());
}