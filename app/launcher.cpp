#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#ifndef FLEET_PROJECT_ROOT
#error FLEET_PROJECT_ROOT must be provided by CMake
#endif

#ifndef FLEET_SIMULATOR_EXECUTABLE
#error FLEET_SIMULATOR_EXECUTABLE must be provided by CMake
#endif

namespace {
volatile std::sig_atomic_t processo_filho = 0;
volatile std::sig_atomic_t sinal_pendente = 0;

void encaminharSinal(int sinal) {
    if (processo_filho > 0) {
        kill(static_cast<pid_t>(processo_filho), sinal);
    } else {
        sinal_pendente = sinal;
    }
}

int aguardarProcesso(pid_t pid) {
    int status = 0;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno == EINTR) {
            continue;
        }
        throw std::runtime_error("Não foi possível aguardar o servidor: " +
                                 std::string(std::strerror(errno)));
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}
}

int main() {
    try {
        const std::filesystem::path raiz(FLEET_PROJECT_ROOT);
        const std::filesystem::path servidor = raiz / "visualization" / "server.py";
        const std::filesystem::path simulador(FLEET_SIMULATOR_EXECUTABLE);
        const std::filesystem::path python_venv = raiz / "visualization" / ".venv" / "bin" / "python";

        if (!std::filesystem::is_regular_file(servidor)) {
            throw std::runtime_error("Servidor Python não encontrado: " + servidor.string());
        }
        if (!std::filesystem::is_regular_file(simulador) ||
            access(simulador.c_str(), X_OK) != 0) {
            throw std::runtime_error("Executável C++ não encontrado ou sem permissão: " +
                                     simulador.string() + ". Compile o projeto com MQTT ativado.");
        }

        const std::string python =
            std::filesystem::is_regular_file(python_venv) &&
                    access(python_venv.c_str(), X_OK) == 0
                ? python_venv.string()
                : "python3";
        std::vector<std::string> argumentos{
            python, servidor.string(), "--executavel", simulador.string()};
        std::vector<char*> argv;
        argv.reserve(argumentos.size() + 1);
        for (auto& argumento : argumentos) {
            argv.push_back(argumento.data());
        }
        argv.push_back(nullptr);

        struct sigaction acao {};
        acao.sa_handler = encaminharSinal;
        sigemptyset(&acao.sa_mask);
        if (sigaction(SIGINT, &acao, nullptr) == -1 ||
            sigaction(SIGTERM, &acao, nullptr) == -1) {
            throw std::runtime_error("Não foi possível configurar o encerramento do servidor.");
        }

        const pid_t filho = fork();
        if (filho == -1) {
            throw std::runtime_error("Não foi possível iniciar o servidor Python: " +
                                     std::string(std::strerror(errno)));
        }
        if (filho == 0) {
            struct sigaction padrao {};
            padrao.sa_handler = SIG_DFL;
            sigemptyset(&padrao.sa_mask);
            sigaction(SIGINT, &padrao, nullptr);
            sigaction(SIGTERM, &padrao, nullptr);
            if (setpgid(0, 0) == -1) {
                std::fprintf(stderr, "Erro ao preparar o processo do servidor: %s\n",
                             std::strerror(errno));
                _exit(1);
            }
            execvp(python.c_str(), argv.data());
            std::fprintf(stderr, "Erro ao iniciar Python (%s): %s\n", python.c_str(),
                         std::strerror(errno));
            _exit(127);
        }

        processo_filho = static_cast<std::sig_atomic_t>(filho);
        if (sinal_pendente != 0) {
            kill(filho, sinal_pendente);
        }

        return aguardarProcesso(filho);
    } catch (const std::exception& erro) {
        std::cerr << "Erro: " << erro.what() << '\n';
        return 1;
    }
}
