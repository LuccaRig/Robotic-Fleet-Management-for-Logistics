
#ifndef ROBOT_EVENT_HPP
#define ROBOT_EVENT_HPP

#include "domain.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

enum class EventType {
    ROBOT_CREATED,
    ROBOT_MOVING,
    ROBOT_ARRIVED,
    LOW_BATTERY,
    ROBOT_CHARGING,
    ROBOT_FAILURE,
    CARGO_PICKUP,
    CARGO_DELIVERED
};


class RobotEvent {
private:
    int robot_id = -1;
    int team_id = -1;
    int bay_id = -1;

    Pos position{0, 0};

    EventType type = EventType::ROBOT_CREATED;

    std::string description;
    std::string timestamp;
    std::string destination;

    // Gera a data e hora atuais no formato DD-MM-AAAA HH:MM:SS
    static std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();

        std::time_t now_time =
            std::chrono::system_clock::to_time_t(now);

        std::tm time_info{};

        localtime_r(&now_time, &time_info);

        std::ostringstream timestamp_stream;

        timestamp_stream << std::put_time(
            &time_info, "%d-%m-%Y %H:%M:%S"
        );

        return timestamp_stream.str();
    }

public:
    // Construtor padrao
    RobotEvent()
        : timestamp(getCurrentTimestamp()) {}

    // Construtor parametrizado
    RobotEvent(
        int robot_id,
        int team_id,
        int bay_id,
        Pos position,
        EventType type,
        const std::string& description,
        const std::string& destination
    )
        : robot_id(robot_id),
          team_id(team_id),
          bay_id(bay_id),
          position(position),
          type(type),
          description(description),
          timestamp(getCurrentTimestamp()),
          destination(destination) {}

    // Getters
    int getRobotId() const {
        return robot_id;
    }

    int getTeamId() const {
        return team_id;
    }

    int getBayId() const {
        return bay_id;
    }

    Pos getPosition() const {
        return position;
    }

    EventType getType() const {
        return type;
    }

    const std::string& getDescription() const {
        return description;
    }

    const std::string& getTimestamp() const {
        return timestamp;
    }

    const std::string& getDestination() const {
        return destination;
    }
};

#endif
