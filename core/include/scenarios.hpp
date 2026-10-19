#ifndef SCENARIOS_HPP
#define SCENARIOS_HPP

#include "runtime.hpp"

Cenario criarCenario(const std::string& nome);
std::string nomesCenarios();
class RoteiroCenario {
public:
    explicit RoteiroCenario(std::string nome) : name_(std::move(nome)) {}
    void avancarCiclo(AplicacaoCentral& runtime);
    bool concluido(const AplicacaoCentral& runtime) const;
private:
    std::string name_;
    int stage_ = 0;
    std::uint64_t generation_ = 0;
    bool queue_observed_ = false;
    std::set<int> passages_seen_;
};

#endif
