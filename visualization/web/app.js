import { $, $$, escape, icon, icons, number, robotName, coordinates, robotTone, cargoTone, timestamp, request, toast } from './ui.js';
import { parseMap, Warehouse } from './map.js';

icons(document);
const state = { robos: [], cargas: [], missoes: [], passagens: [], atual: false };
const pending = new Map();
const robotNodes = new Map();
let map, warehouse, selectedRobot = null, selectedCargo = null, modalKind;
let historyView = 'eventos', historyPage = 0, historyBusy = false, historyEpoch = 0;
let modalEpoch = 0;
let cargoSignature = '', historyRobotSignature = '';

const currentRobot = () => state.robos.find(robot => robot.id === selectedRobot);
const busy = (action, robot) => [...pending.values()].some(item => item.command.acao === action && (robot == null || item.command.id_robo === robot));
const idle = robot => robot && robot.conectado && !robot.falha && !robot.acoplado && !robot.id_missao && robot.estado === 'Disponível';
const missionFor = cargo => state.missoes.find(mission => mission.id === cargo.id_missao);
const teamFor = cargo => missionFor(cargo)?.integrantes.map(robotName).join(', ') || 'Aguardando equipe';
const pill = (label, tone) => `<span class="status-pill ${tone}">${escape(label)}</span>`;
const empty = (title, description, glyph = 'box') => `<div class="empty-state">${icon(glyph)}<strong>${escape(title)}</strong><p>${escape(description)}</p></div>`;

function selectRobot(id) {
  selectedRobot = id;
  selectedCargo = null;
  warehouse?.setTargeting(false);
  render();
}

function renderConnection() {
  const connection = $('#connection');
  connection.className = `connection ${state.atual ? 'online' : 'offline'}`;
  $('span', connection).textContent = state.atual ? 'Central conectada' : state.conectado ? 'Aguardando telemetria' : 'Central desconectada';
  $('#live-badge').className = `live-badge${state.atual ? ' online' : ''}`;
  $('#live-badge').innerHTML = `<i></i>${state.atual ? 'Ao vivo' : 'Sem atualização'}`;
  let notice = '';
  if (state.central_ativa === false) notice = 'A central foi encerrada. Os últimos dados foram preservados. Reinicie a aplicação para continuar a operação.';
  else if (!state.atual) notice = 'Aguardando dados atuais da central. Os comandos ficam indisponíveis até a conexão ser restabelecida.';
  else if (state.versao !== 2) notice = 'Esta central ainda não disponibiliza os dados completos de cargas e equipes. Atualize o executável antes da demonstração.';
  else {
    const suspended = state.cargas.filter(cargo => cargo.estado === 'Suspensa').length;
    if (suspended) notice = `${suspended} ${suspended === 1 ? 'carga com missão suspensa' : 'cargas com missões suspensas'}. Recupere os integrantes da equipe e solicite a retomada pelo controle do robô.`;
  }
  $('#notice').textContent = notice;
  $('#notice').hidden = !notice;
  $$('[data-open]').forEach(button => { button.disabled = !state.atual; });
}

function renderMetrics() {
  const pendingCargo = state.cargas.filter(cargo => cargo.estado === 'Pendente').length;
  const delivered = state.cargas.filter(cargo => cargo.estado === 'Entregue').length;
  const active = state.cargas.filter(cargo => !['Pendente', 'Entregue', 'Suspensa'].includes(cargo.estado)).length;
  const alerts = state.robos.filter(robot => robot.falha || !robot.conectado || robot.bateria <= 15).length;
  $('#metric-active').textContent = number(active);
  $('#metric-pending').textContent = `${number(pendingCargo)} na fila`;
  $('#metric-delivered').textContent = number(delivered);
  $('#metric-total').textContent = `de ${number(state.cargas.length)}`;
  $('#metric-distance').textContent = number(state.robos.reduce((sum, robot) => sum + robot.distancia, 0));
  $('#metric-alerts').textContent = number(alerts);
  $('#alerts-metric').classList.toggle('has-alerts', alerts > 0);
}

function renderRobotList() {
  const list = $('#robot-list');
  $('#robot-count').textContent = number(state.robos.length);
  if (state.robos.length && !robotNodes.size) list.replaceChildren();
  for (const robot of state.robos) {
    let node = robotNodes.get(robot.id);
    if (!node) {
      node = document.createElement('button');
      node.type = 'button';
      node.className = 'button secondary robot-row';
      node.innerHTML = `<span class="robot-row-icon">${icon('robot')}</span><span class="robot-row-info"><strong></strong><span class="robot-row-state"></span></span><span class="robot-row-battery"></span>`;
      node.onclick = () => selectRobot(robot.id);
      robotNodes.set(robot.id, node);
      list.append(node);
    }
    node.setAttribute('aria-pressed', String(robot.id === selectedRobot));
    node.setAttribute('aria-label', `${robotName(robot.id)}, ${robot.conectado ? robot.estado : 'Desconectado'}, bateria ${robot.bateria}%`);
    node.title = node.getAttribute('aria-label');
    $('.robot-row-icon', node).className = `robot-row-icon ${robotTone(robot)}`;
    $('strong', node).textContent = robotName(robot.id);
    $('.robot-row-state', node).textContent = robot.conectado ? robot.estado : 'Desconectado';
    $('.robot-row-state', node).className = `robot-row-state ${robotTone(robot)}`;
    $('.robot-row-battery', node).textContent = `${robot.bateria}%`;
  }
  for (const [id, node] of robotNodes) if (!state.robos.some(robot => robot.id === id)) { node.remove(); robotNodes.delete(id); }
  if (!state.robos.length && !list.children.length) list.innerHTML = '<p class="list-empty">Nenhum robô cadastrado.</p>';
  const signature = state.robos.map(robot => robot.id).join(',');
  if (historyRobotSignature !== signature) {
    const previous = $('#history-robot').value;
    $('#history-robot').innerHTML = `<option value="">Toda a frota</option>${state.robos.map(robot => `<option value="${robot.id}">${robotName(robot.id)}</option>`).join('')}`;
    if (state.robos.some(robot => String(robot.id) === previous)) $('#history-robot').value = previous;
    historyRobotSignature = signature;
  }
}

function renderInspector() {
  const robot = currentRobot();
  $('#inspector-content').hidden = !robot;
  $('#inspector-empty').hidden = Boolean(robot);
  if (!robot) return;
  $('#robot-name').textContent = robotName(robot.id);
  $('#robot-status').textContent = robot.conectado ? robot.estado : 'Desconectado';
  $('#robot-status').className = `status-pill ${robotTone(robot)}`;
  $('#robot-portrait').className = `robot-portrait ${robotTone(robot)}`;
  $('#robot-battery-label').textContent = `${robot.bateria}%`;
  $('#robot-battery-bar').style.width = `${robot.bateria}%`;
  $('#robot-battery-bar').classList.toggle('low', robot.bateria <= 15);
  $('#robot-position').textContent = coordinates(robot.posicao);
  $('#robot-destination').textContent = coordinates(robot.destino);
  $('#robot-distance').textContent = `${number(robot.distancia)} células`;
  $('#robot-coupled').textContent = robot.acoplado ? 'Acoplado à carga' : 'Desacoplado';
  $('#robot-mission').textContent = robot.id_missao ? `M${robot.id_missao} / C${robot.id_carga}` : 'Sem missão';
  const mission = state.missoes.find(item => item.id === robot.id_missao);
  $('#robot-team').textContent = mission ? mission.integrantes.map(id => String(id).padStart(2, '0')).join(' + ') : 'Individual';
  const waiting = busy('definir_modo', robot.id) || busy('definir_destino', robot.id);
  const canMove = state.atual && idle(robot) && !waiting;
  $$('[data-mode]').forEach(button => {
    button.classList.toggle('active', button.dataset.mode === robot.modo);
    button.setAttribute('aria-pressed', String(button.dataset.mode === robot.modo));
    button.disabled = !canMove || button.dataset.mode === robot.modo;
  });
  $('#mode-caption').textContent = waiting ? 'Aguardando confirmação' : robot.modo === 'manual' ? 'Controle do operador' : 'Controle da central';
  $('#destination-fields').disabled = !canMove || robot.modo !== 'manual';
  $('#pick-target').disabled = !canMove || robot.modo !== 'manual';
  $('#destination-hint').textContent = waiting ? 'Aguarde a confirmação da central.' : !idle(robot) ? 'Aguarde o robô ficar livre para definir um destino.' : robot.modo !== 'manual' ? 'Ative o modo manual para escolher um destino.' : 'Escolha um local, informe coordenadas ou selecione no mapa.';
  if (!canMove || robot.modo !== 'manual') warehouse?.setTargeting(false);
  $('#toggle-fault').disabled = !state.atual || !robot.conectado || busy('definir_falha', robot.id);
  $('#toggle-fault').classList.toggle('recovery', robot.falha);
  $('#toggle-fault').innerHTML = `${icon(robot.falha ? 'check' : 'alert')}<span>${robot.falha ? 'Remover defeito' : 'Gerar defeito'}</span>`;
  $('#battery-level').disabled = !state.atual || !robot.conectado;
  $('button', $('#battery-form')).disabled = !state.atual || !robot.conectado || busy('definir_bateria', robot.id);
  const cargo = state.cargas.find(item => item.id === robot.id_carga);
  $('#resume-mission').hidden = cargo?.estado !== 'Suspensa';
  $('#resume-mission').disabled = !state.atual || busy('retomar_missao');
}

function renderCargo() {
  const selected = state.cargas.find(cargo => cargo.id === selectedCargo);
  $('#map-cargo-details').hidden = !selected;
  if (selected) {
    $('#map-cargo-name').textContent = `Carga ${String(selected.id).padStart(2, '0')}`;
    $('#map-cargo-route').textContent = `Baia ${selected.id_baia} → Destino ${selected.id_destino}`;
    $('#map-cargo-status').textContent = selected.estado;
    $('#map-cargo-status').className = `status-pill ${cargoTone(selected)}`;
    $('#map-cargo-size').textContent = `${selected.tamanho} ${selected.tamanho === 1 ? 'robô' : 'robôs'}`;
    $('#map-cargo-team').textContent = selected.motivo || teamFor(selected);
  }
  const signature = JSON.stringify([state.cargas, state.missoes, $('#cargo-filter').value, selectedCargo]);
  if (signature === cargoSignature) return;
  cargoSignature = signature;
  const filter = $('#cargo-filter').value;
  const cargos = state.cargas.filter(cargo => filter === 'todas' || (filter === 'pendentes' && cargo.estado === 'Pendente') || (filter === 'entregues' && cargo.estado === 'Entregue') || (filter === 'suspensas' && cargo.estado === 'Suspensa') || (filter === 'ativas' && !['Pendente', 'Entregue', 'Suspensa'].includes(cargo.estado)));
  $('#cargo-table').innerHTML = cargos.length ? `<table><thead><tr><th>Carga</th><th>Origem e destino</th><th>Equipe</th><th>Situação</th><th>Operação</th></tr></thead><tbody>${cargos.map(cargo => `<tr${selectedCargo === cargo.id ? ' class="selected-cargo-row"' : ''}><td><span class="table-glyph">${icon('box')}<span><strong>Carga ${cargo.id}</strong><small>${cargo.tamanho} ${cargo.tamanho === 1 ? 'robô necessário' : 'robôs necessários'}</small></span></span></td><td>Baia ${cargo.id_baia} → Destino ${cargo.id_destino}</td><td>${escape(teamFor(cargo))}<small>${cargo.id_missao ? `Missão ${cargo.id_missao}` : 'Sem missão atribuída'}</small></td><td>${pill(cargo.estado, cargoTone(cargo))}${cargo.motivo ? `<small>${escape(cargo.motivo)}</small>` : ''}</td><td><button class="button secondary compact" data-locate="${cargo.id}">${icon('target')}Localizar</button></td></tr>`).join('')}</tbody></table>` : empty('Nenhuma carga', state.cargas.length ? 'Nenhuma carga corresponde ao filtro selecionado.' : 'Nenhuma carga cadastrada na execução atual.');
  $$('[data-locate]').forEach(button => {
    button.onclick = () => {
      selectedCargo = Number(button.dataset.locate);
      const cargo = state.cargas.find(item => item.id === selectedCargo);
      const team = missionFor(cargo)?.integrantes;
      if (team?.length) selectedRobot = team[0];
      $('#cargo-dialog').close();
      warehouse.setTargeting(false);
      warehouse.fit();
      render();
      $('#warehouse').scrollIntoView({ block: 'center' });
    };
  });
}

function render() {
  renderConnection();
  renderMetrics();
  renderRobotList();
  renderInspector();
  renderCargo();
  warehouse?.render(state, selectedRobot, selectedCargo);
}

async function send(command, label, originalId) {
  const id = originalId || crypto.randomUUID();
  if (pending.has(id)) return;
  const payload = { ...command, id };
  const promise = new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      pending.delete(id);
      renderInspector();
      const error = new Error('Sem confirmação da central. Você pode reenviar o mesmo pedido com segurança.');
      error.retry = () => send(command, label, id).catch(() => {});
      reject(error);
    }, 12000);
    pending.set(id, { command: payload, resolve, reject, timer });
  });
  promise.catch(() => {});
  renderInspector();
  try {
    await request('/api/commands', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) });
  } catch (error) {
    error.retry = () => send(command, label, id).catch(() => {});
    const item = pending.get(id);
    if (item) { clearTimeout(item.timer); pending.delete(id); item.reject(error); }
  }
  try {
    const result = await promise;
    toast(`${label}. ${result.mensagem}`);
    return result;
  } catch (error) {
    toast(error.message, 'warning', error.retry);
    throw error;
  } finally { renderInspector(); }
}

function operate(action, details, label) {
  const robot = currentRobot();
  if (!robot) return;
  return send({ acao: action, id_robo: robot.id, geracao: robot.geracao, ...details }, label).catch(() => {});
}

async function poll() {
  try {
    const data = await request('/api/state');
    Object.assign(state, { cargas: [], missoes: [], passagens: [] }, data);
    state.atual = Boolean(data.atual && data.central_ativa);
    if (selectedRobot == null || !state.robos.some(robot => robot.id === selectedRobot)) selectedRobot = state.robos.find(robot => robot.conectado)?.id ?? state.robos[0]?.id ?? null;
    for (const result of data.respostas || []) {
      const item = pending.get(result.id);
      if (!item) continue;
      clearTimeout(item.timer);
      pending.delete(result.id);
      if (result.aceito) item.resolve(result); else item.reject(new Error(result.mensagem));
    }
    render();
  } catch {
    state.atual = false;
    state.conectado = false;
    renderConnection();
    renderInspector();
  } finally { setTimeout(poll, document.hidden ? 1200 : 400); }
}

async function loadHistory() {
  if (!$('#history-dialog').open || historyBusy) return;
  historyBusy = true;
  const epoch = historyEpoch;
  try {
    const data = await request(`/api/history?visao=${historyView}&pagina=${historyPage}&robo=${encodeURIComponent($('#history-robot').value)}`);
    if (epoch !== historyEpoch || !$('#history-dialog').open) return;
    $('#history-note').textContent = 'Registros da execução atual, com data e hora de Brasília.';
    if (!data.itens.length) $('#history-table').innerHTML = empty('Nenhum registro', historyView === 'eventos' ? 'Os eventos da operação aparecerão aqui conforme forem registrados.' : 'Os transportes aparecerão quando uma carga for coletada.', 'history');
    else if (historyView === 'eventos') $('#history-table').innerHTML = `<table><thead><tr><th>Data e hora</th><th>Robô</th><th>Evento</th><th>Missão / carga</th></tr></thead><tbody>${data.itens.map(item => `<tr><td>${timestamp(item.occurred_at_ms)}</td><td>${item.robot_id >= 0 ? robotName(item.robot_id) : 'Central'}</td><td>${escape(item.description)}</td><td>${item.mission_id ? `M${item.mission_id} / C${item.cargo_id}` : 'Sem missão'}</td></tr>`).join('')}</tbody></table>`;
    else $('#history-table').innerHTML = `<table><thead><tr><th>Carga</th><th>Rota</th><th>Robôs participantes</th><th>Coleta</th><th>Entrega</th><th>Situação</th></tr></thead><tbody>${data.itens.map(item => `<tr><td><strong>Carga ${item.cargo_id}</strong><small>Missão ${item.mission_id}</small></td><td>Baia ${item.bay_id}<small>Destino ${escape(item.destination)}</small></td><td>${escape(item.robos || 'Equipe não registrada')}</td><td>${timestamp(item.pickup_at_ms)}</td><td>${timestamp(item.delivered_at_ms)}</td><td>${pill(item.status === 'entregue' ? 'Entregue' : 'Em transporte', 'blue')}</td></tr>`).join('')}</tbody></table>`;
    $('#history-page').textContent = `Página ${historyPage + 1}`;
    $('#history-prev').disabled = historyPage === 0;
    $('#history-next').disabled = !data.tem_mais;
  } catch (error) {
    $('#history-note').textContent = error.message;
  } finally {
    historyBusy = false;
    if (epoch !== historyEpoch) loadHistory();
  }
}

function resetHistory() { historyPage = 0; historyEpoch++; loadHistory(); }

function openModal(kind) {
  modalEpoch++;
  modalKind = kind;
  $('#modal-title').textContent = kind === 'carga' ? 'Adicionar carga' : 'Adicionar robô';
  $('#modal-description').textContent = kind === 'carga' ? 'Informe a baia, o destino e o número de robôs necessários. A carga será adicionada ao fim da fila.' : 'Informe a identificação e a posição inicial do robô, fora das passagens.';
  const nextId = Math.max(0, ...(kind === 'carga' ? state.cargas : state.robos).map(item => item.id)) + 1;
  const idField = `<label>Identificação<input name="id_entidade" type="number" min="${kind === 'carga' ? 1 : 0}" max="${kind === 'carga' ? Number.MAX_SAFE_INTEGER : 2147483647}" value="${nextId}" required></label>`;
  if (kind === 'carga') $('#modal-fields').innerHTML = `${idField}<label>Robôs necessários<select name="tamanho">${[1, 2, 3, 4].map(size => `<option value="${size}">${size} ${size === 1 ? 'robô' : 'robôs'}</option>`).join('')}</select></label><label>Baia de origem<select name="id_baia">${map.bays.map(point => `<option value="${point.id}">Baia ${point.id}</option>`).join('')}</select></label><label>Destino de entrega<select name="id_destino">${map.destinations.map(point => `<option value="${point.id}">Destino ${point.id}</option>`).join('')}</select></label>`;
  else {
    let initial;
    for (let y = 0; y < map.height && !initial; y++) for (let x = 0; x < map.width && !initial; x++) if (!map.walls.has(`${x},${y}`) && !map.passages.some(passage => passage.cells.some(point => point.x === x && point.y === y))) initial = { x, y };
    $('#modal-fields').innerHTML = `${idField}<div></div><label>Posição X<input name="x" type="number" min="0" max="${map.width - 1}" value="${initial?.x ?? 0}" required></label><label>Posição Y<input name="y" type="number" min="0" max="${map.height - 1}" value="${initial?.y ?? 0}" required></label>`;
  }
  $('#modal-submit').disabled = false;
  $('#modal-submit').innerHTML = `Adicionar${icon('arrow')}`;
  $('#modal-error').hidden = true;
  $('#modal').showModal();
}

$$('[data-open]').forEach(button => { button.onclick = () => openModal(button.dataset.open); button.disabled = true; });
$$('[data-mode]').forEach(button => { button.onclick = () => operate('definir_modo', { modo: button.dataset.mode }, 'Modo de operação atualizado'); });
$$('[data-layer]').forEach(button => {
  button.onclick = () => {
    const active = button.classList.toggle('active');
    button.setAttribute('aria-pressed', String(active));
    $('#map-stage').classList.toggle(button.dataset.layer === 'cargas' ? 'no-cargo' : 'no-labels', !active);
  };
});
$('#open-cargo').onclick = () => { renderCargo(); $('#cargo-dialog').showModal(); };
$('#close-cargo').onclick = () => $('#cargo-dialog').close();
$('#close-cargo-details').onclick = () => { selectedCargo = null; render(); };
$('#cargo-filter').onchange = renderCargo;
$('#fit-map').onclick = () => warehouse?.fit();
$('#zoom-in').onclick = () => warehouse?.zoom(.8);
$('#zoom-out').onclick = () => warehouse?.zoom(1.25);
$('#cancel-target').onclick = () => warehouse?.setTargeting(false);
$('#pick-target').onclick = () => warehouse?.setTargeting(!warehouse.targeting);
$('#destination-place').onchange = event => {
  if (!event.target.value) return;
  const [x, y] = event.target.value.split(',');
  $('#destination-x').value = x;
  $('#destination-y').value = y;
};
for (const name of ['x', 'y']) $(`#destination-${name}`).oninput = () => { $('#destination-place').value = ''; };
$('#destination-form').onsubmit = event => {
  event.preventDefault();
  warehouse?.setTargeting(false);
  operate('definir_destino', { destino: { x: Number($('#destination-x').value), y: Number($('#destination-y').value) } }, 'Destino recebido');
};
$('#toggle-fault').onclick = () => operate('definir_falha', { falha: !currentRobot().falha }, currentRobot().falha ? 'Defeito removido' : 'Defeito gerado');
$('#battery-level').oninput = event => { $('#battery-output').textContent = `${event.target.value}%`; };
$('#battery-form').onsubmit = event => { event.preventDefault(); operate('definir_bateria', { bateria: Number($('#battery-level').value) }, 'Bateria atualizada'); };
$('#resume-mission').onclick = () => send({ acao: 'retomar_missao', id_missao: currentRobot().id_missao }, 'Retomada solicitada').catch(() => {});
$('#open-history').onclick = () => { $('#history-dialog').showModal(); resetHistory(); };
$('#close-history').onclick = () => $('#history-dialog').close();
$('#history-dialog').addEventListener('close', () => { historyEpoch++; });
$('#modal-close').onclick = $('#modal-cancel').onclick = () => $('#modal').close();
for (const dialog of [$('#modal'), $('#history-dialog'), $('#cargo-dialog')]) {
  dialog.addEventListener('click', event => {
    if (event.target !== dialog) return;
    const rect = dialog.getBoundingClientRect();
    if (event.clientX < rect.left || event.clientX > rect.right || event.clientY < rect.top || event.clientY > rect.bottom) dialog.close();
  });
}
$('#modal-form').onsubmit = async event => {
  event.preventDefault();
  if ($('#modal-submit').disabled) return;
  const epoch = modalEpoch;
  const values = Object.fromEntries([...new FormData(event.target)].map(([key, value]) => [key, Number(value)]));
  const command = modalKind === 'carga' ? { acao: 'adicionar_carga', carga: { id: values.id_entidade, tamanho: values.tamanho, id_baia: values.id_baia, id_destino: values.id_destino } } : { acao: 'adicionar_robo', id_robo: values.id_entidade, posicao: { x: values.x, y: values.y } };
  $('#modal-submit').disabled = true;
  $('#modal-submit').textContent = 'Aguardando confirmação';
  $('#modal-error').hidden = true;
  try {
    await send(command, modalKind === 'carga' ? 'Carga adicionada' : 'Novo robô solicitado');
    if (epoch === modalEpoch) $('#modal').close();
  } catch (error) {
    if (epoch === modalEpoch) {
      $('#modal-error').textContent = error.message;
      $('#modal-error').hidden = false;
    }
  } finally {
    if (epoch === modalEpoch) { $('#modal-submit').disabled = false; $('#modal-submit').innerHTML = `Adicionar${icon('arrow')}`; }
  }
};
$$('[data-history]').forEach(button => { button.onclick = () => { historyView = button.dataset.history; $$('[data-history]').forEach(item => item.classList.toggle('active', item === button)); resetHistory(); }; });
$('#history-robot').onchange = resetHistory;
$('#history-prev').onclick = () => { historyPage = Math.max(0, historyPage - 1); historyEpoch++; loadHistory(); };
$('#history-next').onclick = () => { historyPage++; historyEpoch++; loadHistory(); };
setInterval(() => { if (historyPage === 0 && !document.hidden) loadHistory(); }, 3000);

try {
  const config = await request('/api/config');
  map = parseMap(config.mapa);
  $('#scenario-label').textContent = `Cenário ${config.cenario}`;
  $('#map-size').textContent = `Galpão · ${map.width} × ${map.height} células`;
  warehouse = new Warehouse($('#warehouse'), map, selectRobot, id => { selectedCargo = id; render(); }, point => {
    $('#destination-x').value = point.x;
    $('#destination-y').value = point.y;
    $('#destination-place').value = '';
    warehouse.setTargeting(false);
    $('#destination-x').focus({ preventScroll: true });
  });
  $('#map-empty').hidden = true;
  $('#destination-x').max = map.width - 1;
  $('#destination-y').max = map.height - 1;
  $('#destination-x').value = Math.min(3, map.width - 1);
  $('#destination-y').value = Math.min(5, map.height - 1);
  const places = [...map.bays.map(point => ({ ...point, label: `Baia ${point.id}` })), ...map.destinations.map(point => ({ ...point, label: `Destino ${point.id}` })), ...map.chargers.map((point, index) => ({ ...point, label: `Recarga ${index + 1}` }))];
  $('#destination-place').innerHTML += places.map(point => `<option value="${point.x},${point.y}">${escape(point.label)} · ${coordinates(point)}</option>`).join('');
  poll();
} catch (error) {
  $('#map-empty').innerHTML = `<strong>Não foi possível preparar o galpão</strong><span>${escape(error.message)}</span>`;
  toast(error.message, 'warning');
}
