#include "runtime.hpp"
#include "scenarios.hpp"

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <thread>
#include <vector>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace {
volatile std::sig_atomic_t sinal_recebido = 0;

void tratarSinal(int) {
    sinal_recebido = 1;
}

std::string localizarExecutavel(const char* caminho_informado) {
#ifdef __APPLE__
    std::uint32_t tamanho = 0;
    _NSGetExecutablePath(nullptr, &tamanho);
    std::vector<char> caminho(tamanho);
    if (_NSGetExecutablePath(caminho.data(), &tamanho) == 0) {
        return std::filesystem::canonical(caminho.data()).string();
    }
#else
    std::error_code erro;
    const auto caminho = std::filesystem::canonical("/proc/self/exe", erro);
    if (!erro) {
        return caminho.string();
    }
#endif

    if (std::filesystem::exists(caminho_informado)) {
        return std::filesystem::canonical(caminho_informado).string();
    }
    throw std::runtime_error("Não foi possível localizar o executável");
}

int executarProcessoRobo(int argc, char** argv) {
    if (argc != 7 || std::string(argv[1]) != "--robot-worker") {
        return -1;
    }

    return executarAgenteRobo(
        std::stoi(argv[2]), std::stoi(argv[3]), std::stoull(argv[4]),
        {std::stoi(argv[5]), std::stoi(argv[6])});
}

}

int main(int argc, char** argv) {
    try {
        const int resultado_processo_robo = executarProcessoRobo(argc, argv);
        if (resultado_processo_robo >= 0) {
            return resultado_processo_robo;
        }

        std::string nome_cenario = "base";
        std::string caminho_banco = "logs/etapa1.db";
        bool modo_interativo = false;

        for (int indice = 1; indice < argc; ++indice) {
            const std::string argumento = argv[indice];
            if (argumento == "--interativo") {
                modo_interativo = true;
            } else if ((argumento == "--cenario" || argumento == "--banco") && indice + 1 < argc) {
                const std::string valor = argv[++indice];
                if (argumento == "--cenario") {
                    nome_cenario = valor;
                } else {
                    caminho_banco = valor;
                }
            } else if (argumento == "--ajuda") {
                std::cout << "Uso: simulador_exec [--cenario NOME] [--banco logs/etapa1.db] [--interativo]\n"
                          << "Cenários: " << nomesCenarios() << "\n";
                return 0;
            } else {
                throw std::invalid_argument("Argumento inválido: " + argumento);
            }
        }

        std::signal(SIGINT, tratarSinal);
        std::signal(SIGTERM, tratarSinal);

        AplicacaoCentral aplicacao(
            localizarExecutavel(argv[0]), caminho_banco, criarCenario(nome_cenario));
        RoteiroCenario roteiro(nome_cenario);
        aplicacao.iniciar();

        const auto prazo = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        std::string entrada_pendente;
        while (!sinal_recebido && !aplicacao.encerramentoSolicitado()) {
            aplicacao.avancarCiclo();
            if (!modo_interativo) {
                roteiro.avancarCiclo(aplicacao);
                if (roteiro.concluido(aplicacao)) {
                    aplicacao.imprimir("Cenário concluído: " + nome_cenario);
                    break;
                }
                if (std::chrono::steady_clock::now() >= prazo) {
                    throw std::runtime_error("Prazo do cenário excedido; consulte cargas e eventos");
                }
            } else {
                pollfd entrada{STDIN_FILENO, POLLIN, 0};
                if (poll(&entrada, 1, 20) > 0 && (entrada.revents & (POLLIN | POLLHUP))) {
                    char bloco[4096];
                    const auto lidos = read(STDIN_FILENO, bloco, sizeof(bloco));
                    if (lidos == 0) {
                        break;
                    }
                    if (lidos > 0) {
                        entrada_pendente.append(bloco, static_cast<std::size_t>(lidos));
                    }

                    std::size_t fim_linha;
                    while ((fim_linha = entrada_pendente.find('\n')) != std::string::npos) {
                        const auto linha = entrada_pendente.substr(0, fim_linha);
                        entrada_pendente.erase(0, fim_linha + 1);
                        try {
                            aplicacao.executarComando(linha);
                        } catch (const std::exception& erro) {
                            aplicacao.imprimir("Comando recusado: " + std::string(erro.what()));
                        }
                    }
                }
            }

            if (!modo_interativo) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }

        aplicacao.parar();
        std::cout << "Sistema encerrado.\n";
        return 0;
    } catch (const std::exception& erro) {
        std::cerr << "Erro: " << erro.what() << '\n';
        return 1;
    }
}
