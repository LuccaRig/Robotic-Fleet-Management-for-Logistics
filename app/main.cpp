#include "runtime.hpp"
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace {
volatile std::sig_atomic_t interrupted = 0;
void signalHandler(int) { interrupted = 1; }
std::string executablePath(const char* argv0) {
#ifdef __APPLE__
    std::uint32_t length = 0; _NSGetExecutablePath(nullptr, &length); std::vector<char> path(length);
    if (_NSGetExecutablePath(path.data(), &length) == 0) return std::filesystem::canonical(path.data()).string();
#else
    std::error_code error; auto path = std::filesystem::canonical("/proc/self/exe", error); if (!error) return path.string();
#endif
    if (std::filesystem::exists(argv0)) return std::filesystem::canonical(argv0).string();
    throw std::runtime_error("Não foi possível localizar o executável");
}
Scenario baseScenario() {
    WarehouseGrid map(12, 8); map.addBay({1, {2, 2}, {}}); map.addDestination({1, {9, 5}}); map.setCell(1, 1, CellState::CHARGING_STATION);
    Cargo cargo; cargo.id = 1; cargo.bay_id = 1; cargo.destination_id = 1;
    return {"base", std::move(map), {cargo}, {{1, {1, 2}}}};
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 7 && std::string(argv[1]) == "--robot-worker") return runRobotWorker(std::stoi(argv[2]), std::stoi(argv[3]), std::stoull(argv[4]), {std::stoi(argv[5]), std::stoi(argv[6])});
        std::string scenario = "base", database = "logs/etapa1.db"; bool interactive = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--interativo") interactive = true;
            else if ((arg == "--cenario" || arg == "--banco") && i + 1 < argc) { const auto value = argv[++i]; if (arg == "--cenario") scenario = value; else database = value; }
            else if (arg == "--ajuda") { std::cout << "Uso: simulador_exec [--cenario base] [--banco logs/etapa1.db] [--interativo]\n"; return 0; }
            else throw std::invalid_argument("Argumento inválido: " + arg);
        }
        if (scenario != "base") throw std::invalid_argument("Cenário desconhecido");
        std::signal(SIGINT, signalHandler); std::signal(SIGTERM, signalHandler);
        ControlRuntime runtime(executablePath(argv[0]), database, baseScenario()); runtime.start();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        std::string pending;
        while (!interrupted && !runtime.exitRequested()) {
            runtime.tick();
            if (!interactive && runtime.complete()) break;
            if (!interactive && std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Prazo do cenário excedido; consulte cargas e eventos");
            if (interactive) {
                pollfd input{STDIN_FILENO, POLLIN, 0};
                if (poll(&input, 1, 20) > 0 && (input.revents & (POLLIN | POLLHUP))) {
                    char buffer[4096]; const auto n = read(STDIN_FILENO, buffer, sizeof(buffer));
                    if (n == 0) break;
                    if (n > 0) pending.append(buffer, static_cast<std::size_t>(n));
                    std::size_t end;
                    while ((end = pending.find('\n')) != std::string::npos) {
                        const auto line = pending.substr(0, end); pending.erase(0, end + 1);
                        try { runtime.command(line); } catch (const std::exception& e) { runtime.print("Comando recusado: " + std::string(e.what())); }
                    }
                }
            } else std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        runtime.stop(); std::cout << "Sistema encerrado.\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "Erro: " << e.what() << '\n'; return 1; }
}
