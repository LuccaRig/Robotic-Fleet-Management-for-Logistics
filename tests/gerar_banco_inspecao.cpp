// Programa independente para gerar um banco SQLite de inspecao persistente.
// Execute a partir da raiz do repositorio. Nao apaga o banco entre execucoes.
#include "buffer.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        const std::filesystem::path caminho = "logs/test_events_inspecao.db";
        std::filesystem::create_directories(caminho.parent_path());

        ColetorDados collector(caminho.string());
        collector.iniciar();
        BufferEventos buffer(collector, 3);

        buffer.adicionarEvento(EventoRobo(1, -1, -1, {0, 0}, TipoEvento::ROBO_CRIADO,
                                   "Robo 1 inicializado", ""));
        buffer.adicionarEvento(EventoRobo(1, 7, 2, {3, 4}, TipoEvento::ROBO_EM_MOVIMENTO,
                                   "Robo 1 em deslocamento", "Baia 2"));
        buffer.adicionarEvento(EventoRobo(2, 7, 2, {8, 4}, TipoEvento::CARGA_COLETADA,
                                   "Carga coletada", "Estoque"));
        buffer.adicionarEvento(EventoRobo(1, 7, 2, {10, 5}, TipoEvento::CARGA_ENTREGUE,
                                   "Carga entregue", "Expedicao"));

        // Garante que todos os eventos aceitos foram processados antes de sair.
        collector.parar();

        std::cout << "Banco preservado: " << std::filesystem::absolute(caminho) << '\n';
        std::cout << "4 eventos enviados; buffer conserva " << buffer.quantidade()
                  << " eventos recentes (capacidade " << buffer.capacidade() << ").\n";
        std::cout << "Novas execucoes acrescentam registros ao mesmo banco.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Falha ao gerar banco de inspecao: " << e.what() << '\n';
        return 1;
    }
}
