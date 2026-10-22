#ifndef PC_MONITOR_PAGE_H
#define PC_MONITOR_PAGE_H

#include <lvgl.h>
#include <cJSON.h>
#include <functional>
#include <string>
#include <vector>

class PcMonitorPage {
public:
    PcMonitorPage();
    ~PcMonitorPage();

    void Show();
    void Hide();
    bool IsVisible() const { return visible_; }
    void ApplyStats(const cJSON* root);
    void SetPendingJson(const std::string& json);

private:
    void DoShow();
    void FlushDeferredOps();

    static constexpr int kRows = 4;  // CPU, MEM, GPU, DISK

    bool created_ = false;
    bool visible_ = false;
    lv_obj_t* page_ = nullptr;
    lv_obj_t* bar_[kRows] = {};
    lv_obj_t* value_label_[kRows] = {};
    lv_obj_t* bottom_label_ = nullptr;

    // JSON string received before page existed. Parsed in DoShow().
    std::string pending_json_;

    // Operations queued via lv_async_call to run outside the current
    // lv_timer_handler invocation, preventing recursive re-entry.
    std::vector<std::function<void()>> deferred_ops_;
};

#endif  // PC_MONITOR_PAGE_H