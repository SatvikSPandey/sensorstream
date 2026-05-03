#pragma once

#include "data_store.h"
#include "sensor_simulator.h"
#include "processing_engine.h"

#include <string>
#include <thread>
#include <atomic>
#include <cstdint>

// Forward-declare httplib::Server to avoid pulling the entire httplib.h
// into every translation unit that includes api_server.h
namespace httplib { class Server; }

namespace sensorstream {

// ApiServer — HTTP layer wrapping all engine components
//
// Owns the httplib::Server via unique_ptr to keep httplib.h out of
// the header — only api_server.cpp includes it.
// Runs the HTTP listener on a dedicated std::thread (not jthread —
// httplib has its own stop mechanism via svr_.stop())
//
class ApiServer {
public:
    ApiServer(DataStore&          store,
              SensorSimulator&    simulator,
              ProcessingEngine&   engine,
              uint16_t            port = 8080);

    ~ApiServer();

    ApiServer(const ApiServer&)            = delete;
    ApiServer& operator=(const ApiServer&) = delete;

    // Registers all routes and starts listening — blocks until stop() is called
    void start();
    void stop();

    [[nodiscard]] bool is_running() const noexcept;

private:
    void setup_routes();

    // JSON builders — each returns a std::string of serialised JSON
    std::string build_sensors_json();
    std::string build_sensor_latest_json(const std::string& sensor_id);
    std::string build_sensor_history_json(const std::string& sensor_id,
                                          std::size_t limit);
    std::string build_anomalies_json(std::size_t limit);
    std::string build_metrics_json();

    DataStore&        store_;
    SensorSimulator&  simulator_;
    ProcessingEngine& engine_;
    uint16_t          port_;

    // unique_ptr<httplib::Server> — forward-declared above, defined in .cpp
    // This is the Pimpl-lite pattern: hides the httplib dependency from
    // all headers that include api_server.h
    std::unique_ptr<httplib::Server> svr_;
    std::thread                      listener_thread_;
    std::atomic<bool>                running_{ false };
};

} // namespace sensorstream
