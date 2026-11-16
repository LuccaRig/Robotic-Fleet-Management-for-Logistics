#ifndef MQTT_CLIENT_HPP
#define MQTT_CLIENT_HPP

#include <memory>
#include <functional>
#include <string>

class ClienteMqtt {
public:
    using Receptor = std::function<void(std::string, bool)>;

    ClienteMqtt();
    ~ClienteMqtt();
    ClienteMqtt(const ClienteMqtt&) = delete;
    ClienteMqtt& operator=(const ClienteMqtt&) = delete;

    void connect(const std::string& host, int port, const std::string& topico = {}, Receptor receptor = {});
    void connect(const std::string& topico, Receptor receptor);

    void publish(const std::string& topic, const std::string& payload, bool retain = true);

    bool isConnected() const;

    void disconnect();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
