#include "mqtt_client.hpp"

#if defined(ENABLE_MQTT) && ENABLE_MQTT

#include <mosquitto.h>
#include <iostream>
#include <memory>
#include <mutex>

struct MqttPublisher::Impl {
    mosquitto* mosq = nullptr;
    bool connected = false;
    std::mutex mutex_;

    ~Impl() {
        if (mosq) {
            mosquitto_disconnect(mosq);
            mosquitto_destroy(mosq);
            mosq = nullptr;
        }
        mosquitto_lib_cleanup();
    }
};

MqttPublisher::MqttPublisher() : impl_(new Impl()) {
    mosquitto_lib_init();
}

MqttPublisher::~MqttPublisher() { delete impl_; }

void MqttPublisher::connect(const std::string& host, int port) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    if (impl_->mosq) return;
    impl_->mosq = mosquitto_new("orchestrator", true, nullptr);
    if (!impl_->mosq) {
        std::cerr << "MQTT: falha ao criar cliente mosquitto\n";
        return;
    }
    int rc = mosquitto_connect(impl_->mosq, host.c_str(), port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "MQTT: falha ao conectar a " << host << ":" << port
                  << " (" << rc << ")\n";
        mosquitto_destroy(impl_->mosq);
        impl_->mosq = nullptr;
        return;
    }
    mosquitto_loop_start(impl_->mosq);
    impl_->connected = true;
}

void MqttPublisher::publish(const std::string& topic, const std::string& payload, bool retain) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    if (!impl_->mosq || !impl_->connected) return;
    int rc = mosquitto_publish(impl_->mosq, nullptr, topic.c_str(),
                               static_cast<int>(payload.size()),
                               payload.data(), 1, retain ? 1 : 0, false);
    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "MQTT: falha ao publicar em " << topic << " (" << rc << ")\n";
    }
}

bool MqttPublisher::isConnected() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->connected;
}

void MqttPublisher::disconnect() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    if (impl_->mosq) {
        mosquitto_disconnect(impl_->mosq);
        mosquitto_loop_stop(impl_->mosq, true);
        mosquitto_destroy(impl_->mosq);
        impl_->mosq = nullptr;
        impl_->connected = false;
    }
}

#else // ENABLE_MQTT OFF -> stub no-op

MqttPublisher::MqttPublisher() = default;
MqttPublisher::~MqttPublisher() = default;

void MqttPublisher::connect(const std::string&, int) {}
void MqttPublisher::publish(const std::string&, const std::string&, bool) {}
bool MqttPublisher::isConnected() const { return false; }
void MqttPublisher::disconnect() {}

#endif // ENABLE_MQTT
