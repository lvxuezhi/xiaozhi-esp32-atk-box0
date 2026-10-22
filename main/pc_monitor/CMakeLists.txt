idf_component_register(
    SRCS "pc_monitor.cc" "pc_monitor_page.cc"
    INCLUDE_DIRS "."
    REQUIRES json lvgl
)