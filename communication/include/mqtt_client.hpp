#ifndef MQTT_CLIENT_HPP
#define MQTT_CLIENT_HPP

#include <memory>
#include <string>

class MqttPublisher {
public:
    MqttPublisher();
    ~MqttPublisher();
    MqttPublisher(const MqttPublisher&) = delete;
    MqttPublisher& operator=(const MqttPublisher&) = delete;

    void connect(const std::string& host, int port);

    void publish(const std::string& topic, const std::string& payload, bool retain = true);

    bool isConnected() const;

    void disconnect();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
