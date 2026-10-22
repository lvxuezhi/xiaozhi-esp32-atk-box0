#include "pc_monitor.h"
#include <esp_log.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <memory>
#include "board.h"
#include "application.h"

#define TAG "PcMonitor"

PcMonitor::PcMonitor() : alive_(std::make_shared<std::atomic<bool>>(true)) {}

PcMonitor::~PcMonitor() {
    if (alive_) {
        *alive_ = false;
    }
    if (reconnect_timer_) {
        esp_timer_stop(reconnect_timer_);
        esp_timer_delete(reconnect_timer_);
    }
    if (mqtt_) {
        mqtt_.reset();
    }
}

void PcMonitor::Start(const std::string& broker_host, int broker_port,
                      const std::string& topic,
                      const std::string& username, const std::string& password) {
    alive_ = std::make_shared<std::atomic<bool>>(true);
    broker_host_ = broker_host;
    broker_port_ = broker_port;
    topic_ = topic;
    username_ = username;
    password_ = password;

    if (reconnect_timer_) {
        esp_timer_stop(reconnect_timer_);
        esp_timer_delete(reconnect_timer_);
        reconnect_timer_ = nullptr;
    }

    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            PcMonitor* self = static_cast<PcMonitor*>(arg);
            if (self->alive_ && *self->alive_) {
                self->Reconnect();
            }
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "pc_monitor_reconnect",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &reconnect_timer_));

    Connect();
}

bool PcMonitor::Connect() {
    auto& board = Board::GetInstance();
    auto network = board.GetNetwork();
    if (!network) {
        ESP_LOGE(TAG, "No network interface available");
        return false;
    }

    mqtt_ = network->CreateMqtt(0);
    if (!mqtt_) {
        ESP_LOGE(TAG, "Failed to create MQTT client");
        return false;
    }

    std::string client_id = "pc_monitor_" + board.GetUuid();

    mqtt_->OnMessage([this](const std::string& topic, const std::string& payload) {
        if (alive_ && *alive_) {
            Application::GetInstance().Schedule([this, topic = std::string(topic), payload = std::string(payload)]() {
                if (alive_ && *alive_ && mqtt_) {
                    OnMqttMessage(topic, payload);
                }
            });
        }
    });

    mqtt_->OnConnected([this]() {
        if (alive_ && *alive_) {
            // Defer subscription to avoid race in MQTT task
            Application::GetInstance().Schedule([this]() {
                if (alive_ && *alive_ && mqtt_) {
                    OnMqttConnected();
                }
            });
        }
    });

    mqtt_->OnDisconnected([this]() {
        if (alive_ && *alive_) {
            Application::GetInstance().Schedule([this]() {
                if (alive_ && *alive_) {
                    OnMqttDisconnected();
                }
            });
        }
    });

    if (auto connected = mqtt_->Connect(broker_host_, broker_port_, client_id,
                             username_.empty() ? "" : username_,
                             password_.empty() ? "" : password_); !connected) {
        ESP_LOGE(TAG, "Failed to connect to MQTT broker %s:%d", broker_host_.c_str(), broker_port_);
        ScheduleReconnect();
        return false;
    }

    ESP_LOGI(TAG, "Connected to MQTT broker %s:%d", broker_host_.c_str(), broker_port_);
    connected_ = true;
    return true;
}

void PcMonitor::Stop() {
    auto prev = std::move(alive_);
    if (prev) { *prev = false; }
    if (reconnect_timer_) {
        esp_timer_stop(reconnect_timer_);
        esp_timer_delete(reconnect_timer_);
        reconnect_timer_ = nullptr;
    }
    if (mqtt_) {
        mqtt_.reset();
    }
    connected_ = false;
    ESP_LOGI(TAG, "PC Monitor stopped");
}

void PcMonitor::Reconnect() {
    if (connected_) return;
    ESP_LOGI(TAG, "Reconnecting to MQTT broker...");
    Connect();
}

void PcMonitor::ScheduleReconnect() {
    if (reconnect_timer_ && alive_ && *alive_) {
        esp_timer_stop(reconnect_timer_);
        esp_timer_start_once(reconnect_timer_, 10000000);  // 10 seconds
    }
}

void PcMonitor::OnMqttConnected() {
    ESP_LOGI(TAG, "MQTT connected");
    connected_ = true;
    if (!topic_.empty()) {
        mqtt_->Subscribe(topic_.c_str());
    }
}

void PcMonitor::OnMqttDisconnected() {
    ESP_LOGW(TAG, "MQTT disconnected");
    connected_ = false;
    ScheduleReconnect();
}

void PcMonitor::OnMqttMessage(const std::string& topic, const std::string& payload) {
    if (topic != topic_) return;

    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse JSON: %s", payload.c_str());
        return;
    }

    last_stats_.reset(cJSON_Duplicate(root, true));
    ESP_LOGI(TAG, "stats: cpu=%s mem=%s gpu=%s disk=%s",
             cJSON_GetObjectItemCaseSensitive(root, "cpu") ? "y" : "n",
             cJSON_GetObjectItemCaseSensitive(root, "mem") ? "y" : "n",
             cJSON_GetObjectItemCaseSensitive(root, "gpu") ? "y" : "n",
             cJSON_GetObjectItemCaseSensitive(root, "disk") ? "y" : "n");

    if (on_stats_) {
        on_stats_(root);
    }
}

std::string PcMonitor::GetLastStatsJson() const {
    if (!last_stats_) return "";
    char* raw = cJSON_PrintUnformatted(last_stats_.get());
    if (!raw) return "";
    std::string result(raw);
    cJSON_free(raw);
    return result;
}
