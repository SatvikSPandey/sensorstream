#include "sensor_simulator.h"
#include "processing_engine.h"
#include "data_store.h"
#include "api_server.h"

#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdlib>

namespace {
    // Atomic flag set by SIGINT/SIGTERM handler — checked by main loop
    // volatile sig_atomic_t is the C standard type for signal-safe flags
    // std::atomic<bool> is used here for the C++ main loop side
    std::atomic<bool> g_shutdown{ false };

    void signal_handler(int /*sig*/) {
        g_shutdown.store(true, std::memory_order_relaxed);
    }
}

int main() {
    // ── Signal handling ────────────────────────────────────────────────────
    // Catch Ctrl+C (SIGINT) and Docker stop (SIGTERM)
    // Both trigger a clean shutdown instead of abrupt process termination
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // ── Read port from environment — Docker/Render inject $PORT ───────────
    // Render's free tier assigns a dynamic port via the PORT env variable
    // Locally defaults to 8080 if PORT is not set
    uint16_t port = 8080;
    if (const char* env_port = std::getenv("PORT")) {
        try {
            int p = std::stoi(env_port);
            if (p > 0 && p < 65536) port = static_cast<uint16_t>(p);
        } catch (...) {
            // invalid PORT value — keep default 8080
        }
    }

    std::cout << "=================================================\n";
    std::cout << "  SensorStream — Industrial Sensor Processing    \n";
    std::cout << "  C++20 | Lock-Free Pipeline | REST API          \n";
    std::cout << "  Author: Satvik Pandey                          \n";
    std::cout << "  github.com/SatvikSPandey                       \n";
    std::cout << "=================================================\n";
    std::cout << "  Starting on port " << port << "...\n\n";

    // ── Construct components ───────────────────────────────────────────────
    // Order matters: buffer and store have no dependencies,
    // simulator and engine depend on buffer, api depends on all three
    sensorstream::SensorBuffer   buffer;
    sensorstream::DataStore      store;
    sensorstream::SensorSimulator simulator(buffer);
    sensorstream::ProcessingEngine engine(buffer, store);
    sensorstream::ApiServer        api(store, simulator, engine, port);

    // ── Start all threads ──────────────────────────────────────────────────
    // Start processing engine before simulator — ensures no readings
    // are produced before the consumer is ready to drain them
    engine.start();
    simulator.start();
    api.start();     // spawns HTTP listener thread, returns immediately

    std::cout << "[OK] Processing engine started\n";
    std::cout << "[OK] Sensor simulator started  (5 sensors @ 10 Hz)\n";
    std::cout << "[OK] HTTP API listening on port " << port << "\n";
    std::cout << "[OK] Dashboard: http://localhost:" << port << "/\n\n";
    std::cout << "Press Ctrl+C to stop.\n";

    // ── Main loop — prints stats every 5 seconds ───────────────────────────
    while (!g_shutdown.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        if (g_shutdown.load(std::memory_order_relaxed)) break;

        std::cout << "[STATS]"
                  << "  produced="   << simulator.readings_produced()
                  << "  processed="  << engine.readings_processed()
                  << "  anomalies="  << engine.anomalies_detected()
                  << "  dropped="    << simulator.readings_dropped()
                  << "\n";
    }

    // ── Graceful shutdown ──────────────────────────────────────────────────
    // Order is the reverse of startup:
    // Stop simulator first — no new readings enter the buffer
    // Stop engine after — drains any remaining readings from buffer
    // Stop API last    — no requests served after data stops updating
    std::cout << "\n[SHUTDOWN] Signal received — stopping...\n";

    simulator.stop();
    std::cout << "[SHUTDOWN] Simulator stopped\n";

    engine.stop();
    std::cout << "[SHUTDOWN] Engine stopped  ("
              << engine.readings_processed() << " total processed)\n";

    api.stop();
    std::cout << "[SHUTDOWN] API server stopped\n";
    std::cout << "[SHUTDOWN] Clean exit.\n";

    return 0;
}
