const form = document.querySelector('#scenario-form');
const select = document.querySelector('#scenario');
const button = document.querySelector('#start-button');
const status = document.querySelector('#status');
const delay = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));

function showStatus(message, isError = false) {
  status.textContent = message;
  status.dataset.error = String(isError);
}

async function loadScenarios() {
  const response = await fetch('/api/scenarios', { cache: 'no-store' });
  if (!response.ok) throw new Error('Não foi possível carregar os cenários disponíveis.');
  const scenarios = await response.json();
  if (!Array.isArray(scenarios) || scenarios.length === 0) {
    throw new Error('Nenhum cenário está disponível.');
  }
  select.replaceChildren(new Option('Selecione um cenário', ''));
  for (const scenario of scenarios) {
    select.add(new Option(scenario.nome, scenario.id));
  }
  select.disabled = false;
  button.disabled = false;
  showStatus('A simulação ainda não foi iniciada.');
}

async function waitForOperation() {
  while (true) {
    await delay(400);
    try {
      const config = await fetch('/api/config', { cache: 'no-store' });
      if (config.ok) {
        window.location.replace('/');
        return;
      }
    } catch {
      // The server may be transitioning from the picker to the operation interface.
    }

    try {
      const response = await fetch('/api/start-status', { cache: 'no-store' });
      if (!response.ok) continue;
      const startup = await response.json();
      if (startup.estado === 'error') {
        throw new Error(startup.erro || 'Não foi possível iniciar o cenário.');
      }
      if (startup.estado === 'starting') {
        showStatus('Iniciando broker, simulador e central...');
      }
    } catch (error) {
      if (error instanceof TypeError) continue;
      throw error;
    }
  }
}

form.addEventListener('submit', async event => {
  event.preventDefault();
  if (!select.value) {
    showStatus('Selecione um cenário para continuar.', true);
    select.focus();
    return;
  }

  select.disabled = true;
  button.disabled = true;
  showStatus('Iniciando broker, simulador e central...');
  try {
    const response = await fetch('/api/start', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ cenario: select.value })
    });
    const result = await response.json();
    if (!response.ok) throw new Error(result.erro || 'Não foi possível iniciar o cenário.');
    await waitForOperation();
  } catch (error) {
    select.disabled = false;
    button.disabled = false;
    showStatus(error.message, true);
  }
});

loadScenarios().catch(error => {
  select.replaceChildren(new Option('Cenários indisponíveis', ''));
  showStatus(error.message, true);
});
