#ifndef PC_MONITOR_H
#define PC_MONITOR_H

#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <esp_timer.h>
#include <cJSON.h>
#include <mqtt.h>

class PcMonitor {
public:
    PcMonitor();
    ~PcMonitor();

    // Initialize MQTT client and start subscribing
    void Start(const std::string& broker_host, int broker_port,
               const std::string& topic,
               const std::string& username = "", const std::string& password = "");

    // Callback when new PC stats arrive
    using StatsCallback = std::function<void(const cJSON* root)>;
    void OnStatsReceived(StatsCallback callback) { on_stats_ = std::move(callback); }

    // Get latest cached stats (for display when MQTT hasn't updated yet)
    const cJSON* GetLastStats() const { return last_stats_.get(); }

    // Get latest stats as a raw JSON string (safe to use after function return)
    std::string GetLastStatsJson() const;

    // Manually trigger reconnect
    void Reconnect();

    // Stop MQTT and release resources (call before restart)
    void Stop();

private:
    std::unique_ptr<Mqtt> mqtt_;
    std::string broker_host_;
    int broker_port_;
    std::string topic_;
    std::string username_;
    std::string password_;
    StatsCallback on_stats_;
    std::unique_ptr<cJSON, void(*)(cJSON*)> last_stats_{nullptr, cJSON_Delete};

    // Connection state
    bool connected_ = false;
    esp_timer_handle_t reconnect_timer_ = nullptr;
    std::shared_ptr<std::atomic<bool>> alive_;

    void OnMqttMessage(const std::string& topic, const std::string& payload);
    void OnMqttConnected();
    void OnMqttDisconnected();
    void ScheduleReconnect();
    bool Connect();
};

#endif  // PC_MONITOR_H