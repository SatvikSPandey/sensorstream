#include "api_server.h"
#include "dashboard.h"
#include "statistics.h"

// httplib.h is ONLY included in this .cpp — nowhere else
// This keeps compile times fast: httplib.h is ~3000 lines
#define CPPHTTPLIB_NO_EXCEPTIONS
#include "../third_party/httplib.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

namespace sensorstream {

ApiServer::ApiServer(DataStore&        store,
                     SensorSimulator&  simulator,
                     ProcessingEngine& engine,
                     uint16_t          port)
    : store_(store)
    , simulator_(simulator)
    , engine_(engine)
    , port_(port)
    , svr_(std::make_unique<httplib::Server>())
{}

ApiServer::~ApiServer() {
    stop();
}

void ApiServer::start() {
    setup_routes();
    running_.store(true, std::memory_order_relaxed);

    // httplib::Server::listen() blocks — run it on a dedicated thread
    listener_thread_ = std::thread([this]() {
        svr_->listen("0.0.0.0", port_);
        running_.store(false, std::memory_order_relaxed);
    });
}

void ApiServer::stop() {
    if (svr_) svr_->stop();
    if (listener_thread_.joinable()) listener_thread_.join();
}

bool ApiServer::is_running() const noexcept {
    return running_.load(std::memory_order_relaxed);
}

void ApiServer::setup_routes() {

    // CORS header on every response — needed for the dashboard JS
    // to call the API when the page is opened directly in a browser
    svr_->set_default_headers({
        {"Access-Control-Allow-Origin",  "*"},
        {"Access-Control-Allow-Methods", "GET, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type"}
    });

    // GET / — serve the embedded HTML dashboard
    svr_->Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(DASHBOARD_HTML, "text/html; charset=utf-8");
    });

    // GET /api/sensors — all sensor latest readings + stats
    svr_->Get("/api/sensors", [this](const httplib::Request&,
                                      httplib::Response& res) {
        res.set_content(build_sensors_json(), "application/json");
    });

    // GET /api/sensors/:id/latest — single sensor latest reading
    svr_->Get(R"(/api/sensors/([^/]+)/latest)",
        [this](const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];
            res.set_content(build_sensor_latest_json(id), "application/json");
        });

    // GET /api/sensors/:id/history?limit=N
    svr_->Get(R"(/api/sensors/([^/]+)/history)",
        [this](const httplib::Request& req, httplib::Response& res) {
            std::string id    = req.matches[1];
            std::size_t limit = 100;
            if (req.has_param("limit")) {
                try { limit = std::stoul(req.get_param_value("limit")); }
                catch (...) { limit = 100; }
            }
            res.set_content(build_sensor_history_json(id, limit),
                            "application/json");
        });

    // GET /api/anomalies?limit=N
    svr_->Get("/api/anomalies", [this](const httplib::Request& req,
                                        httplib::Response& res) {
        std::size_t limit = 50;
        if (req.has_param("limit")) {
            try { limit = std::stoul(req.get_param_value("limit")); }
            catch (...) { limit = 50; }
        }
        res.set_content(build_anomalies_json(limit), "application/json");
    });

    // GET /api/metrics — throughput, drops, buffer utilisation
    svr_->Get("/api/metrics", [this](const httplib::Request&,
                                      httplib::Response& res) {
        res.set_content(build_metrics_json(), "application/json");
    });
}

// ── JSON builders ──────────────────────────────────────────────────────────

std::string ApiServer::build_sensors_json() {
    auto ids = store_.sensor_ids();
    json arr  = json::array();

    for (const auto& id : ids) {
        auto reading = store_.latest(id);
        auto stats   = store_.latest_stats(id);
        if (!reading.has_value()) continue;

        double mean   = stats.has_value() ? stats->first  : 0.0;
        double stddev = stats.has_value() ? stats->second : 0.0;
        double zscore = z_score(reading->value, mean, stddev);

        arr.push_back({
            {"sensor_id",  id},
            {"type",       sensor_type_to_string(reading->type)},
            {"unit",       sensor_type_to_unit(reading->type)},
            {"value",      reading->value},
            {"timestamp_ns", reading->timestamp_ns},
            {"mean",       mean},
            {"stddev",     stddev},
            {"z_score",    zscore},
            {"is_anomaly", is_anomaly(reading->value, mean, stddev)}
        });
    }
    return arr.dump();
}

std::string ApiServer::build_sensor_latest_json(const std::string& id) {
    auto reading = store_.latest(id);
    if (!reading.has_value()) {
        return json{{"error", "sensor not found"}}.dump();
    }
    auto stats  = store_.latest_stats(id);
    double mean   = stats.has_value() ? stats->first  : 0.0;
    double stddev = stats.has_value() ? stats->second : 0.0;

    return json{
        {"sensor_id",    id},
        {"type",         sensor_type_to_string(reading->type)},
        {"unit",         sensor_type_to_unit(reading->type)},
        {"value",        reading->value},
        {"timestamp_ns", reading->timestamp_ns},
        {"mean",         mean},
        {"stddev",       stddev},
        {"z_score",      z_score(reading->value, mean, stddev)},
        {"is_anomaly",   is_anomaly(reading->value, mean, stddev)}
    }.dump(2); // pretty-print with 2-space indent for /latest endpoint
}

std::string ApiServer::build_sensor_history_json(const std::string& id,
                                                  std::size_t limit) {
    auto hist = store_.history(id, limit);
    json arr  = json::array();
    for (const auto& r : hist) {
        arr.push_back({
            {"value",        r.value},
            {"timestamp_ns", r.timestamp_ns}
        });
    }
    return arr.dump();
}

std::string ApiServer::build_anomalies_json(std::size_t limit) {
    auto events = store_.recent_anomalies(limit);
    json arr    = json::array();
    for (const auto& e : events) {
        arr.push_back({
            {"sensor_id",          e.reading.sensor_id},
            {"type",               sensor_type_to_string(e.reading.type)},
            {"value",              e.reading.value},
            {"timestamp_ns",       e.reading.timestamp_ns},
            {"z_score",            e.z_score},
            {"mean_at_detection",  e.mean_at_detection},
            {"stddev_at_detection",e.stddev_at_detection}
        });
    }
    return arr.dump();
}

std::string ApiServer::build_metrics_json() {
    std::size_t buf_used = simulator_.readings_produced() > 0
        ? store_.total_readings() % 4096
        : 0;

    return json{
        {"readings_processed",    engine_.readings_processed()},
        {"readings_produced",     simulator_.readings_produced()},
        {"readings_dropped",      simulator_.readings_dropped()},
        {"anomalies_detected",    engine_.anomalies_detected()},
        {"total_stored",          store_.total_readings()},
        {"buffer_capacity",       SensorBuffer::capacity()},
        {"buffer_utilization_pct",
            100.0 * static_cast<double>(buf_used) /
            static_cast<double>(SensorBuffer::capacity())}
    }.dump();
}

} // namespace sensorstream
