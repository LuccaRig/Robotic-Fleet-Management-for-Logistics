import { $, escape, icon, robotName, robotTone } from './ui.js';

const NS = 'http://www.w3.org/2000/svg';
const CELL = 36;
const PAD = 42;
const key = point => `${point.x},${point.y}`;
const center = point => ({ x: PAD + (point.x + .5) * CELL, y: PAD + (point.y + .5) * CELL });

function element(tag, attrs = {}, content = '') {
  const node = document.createElementNS(NS, tag);
  for (const [name, value] of Object.entries(attrs)) node.setAttribute(name, value);
  node.innerHTML = content;
  return node;
}

export function parseMap(text) {
  const lines = text.trim().split(/\r?\n/).filter(line => line.trim()).map(line => line.trim().split(/\s+/));
  const [width, height] = lines.shift().map(Number);
  if (!Number.isInteger(width) || !Number.isInteger(height) || width <= 0 || height <= 0 || width * height > 1000000) throw new Error('O arquivo de mapa tem dimensões inválidas');
  const map = { width, height, walls: new Set(), bays: [], destinations: [], chargers: [], passages: [] };
  for (const [type, ...tokens] of lines) {
    const values = tokens.map(Number);
    if (values.some(value => !Number.isInteger(value))) throw new Error('O arquivo de mapa tem coordenadas inválidas');
    const [a, b, c, d] = values;
    if (type === 'parede') {
      if (a < 0 || b < 0 || c >= width || d >= height || a > c || b > d) throw new Error('Parede fora dos limites do mapa');
      for (let y = b; y <= d; y++) for (let x = a; x <= c; x++) map.walls.add(`${x},${y}`);
    } else if (type === 'baia' || type === 'destino') {
      map[type === 'baia' ? 'bays' : 'destinations'].push({ id: a, x: b, y: c });
    } else if (type === 'carregador') {
      map.chargers.push({ x: a, y: b });
    } else if (type === 'passagem') {
      const cells = [];
      for (let i = 0; i < c; i++) {
        const point = { x: values[3 + i * 2], y: values[4 + i * 2] };
        map.walls.delete(key(point));
        cells.push(point);
      }
      map.passages.push({ id: a, capacity: b, cells });
    } else throw new Error('O arquivo contém um elemento de mapa desconhecido');
  }
  return map;
}

export class Warehouse {
  constructor(svg, map, onRobot, onCargo, onTarget) {
    this.svg = svg;
    this.map = map;
    this.onRobot = onRobot;
    this.onCargo = onCargo;
    this.onTarget = onTarget;
    this.robots = new Map();
    this.cargo = new Map();
    this.gates = new Map();
    this.full = [0, 0, map.width * CELL + PAD * 2, map.height * CELL + PAD * 2];
    this.view = [...this.full];
    svg.innerHTML = `<defs><pattern id="floor-grid" width="${CELL}" height="${CELL}" patternUnits="userSpaceOnUse" x="${PAD}" y="${PAD}"><path d="M ${CELL} 0 H 0 V ${CELL}" fill="none" stroke="#dfe6d7" stroke-width=".7"/><circle cx="${CELL / 2}" cy="${CELL / 2}" r=".7" fill="#d5dfcd"/></pattern></defs><rect width="100%" height="100%" fill="#f4f6ef"/>`;
    svg.append(element('rect', { x: PAD, y: PAD, width: map.width * CELL, height: map.height * CELL, rx: 6, fill: 'url(#floor-grid)', stroke: '#d7e0cf' }));
    for (const wall of map.walls) {
      const [x, y] = wall.split(',').map(Number);
      svg.append(element('g', {}, `<rect class="map-wall" x="${PAD + x * CELL + 2}" y="${PAD + y * CELL + 2}" width="${CELL - 4}" height="${CELL - 4}" rx="3"/><path class="wall-top" d="M${PAD + x * CELL + 6} ${PAD + y * CELL + 6}h${CELL - 12}"/>`));
    }
    const zones = new Map();
    const addZone = (point, kind, label, glyph) => {
      const previous = zones.get(key(point));
      zones.set(key(point), { point, kind, label: previous ? `${previous.label} / ${label}` : label, glyph });
    };
    for (const bay of map.bays) addZone(bay, 'bay', `BAIA ${String(bay.id).padStart(2, '0')}`, 'box');
    for (const destination of map.destinations) addZone(destination, 'delivery', `DESTINO ${String(destination.id).padStart(2, '0')}`, 'check');
    map.chargers.forEach((charger, index) => addZone(charger, 'charger', `RECARGA ${index + 1}`, 'auto'));
    for (const zone of zones.values()) this.drawZone(zone.point, zone.kind, zone.label, zone.glyph);
    for (const passage of map.passages) {
      for (const point of passage.cells) {
        const { x, y } = center(point);
        const gate = element('g', {}, `<title>Passagem ${passage.id}: capacidade para ${passage.capacity} robôs</title><rect x="${x - 16}" y="${y - 16}" width="32" height="32" rx="4" fill="#ede8d4" stroke="#c9b67a" stroke-dasharray="3 2"/><path d="M${x - 12} ${y - 10}h24M${x - 12} ${y + 10}h24" stroke="#c8b67f" stroke-width="2"/><text x="${x}" y="${y + 3}" text-anchor="middle" fill="#9a8452" font-size="9" font-weight="600">0/${passage.capacity}</text>`);
        svg.append(gate);
        if (!this.gates.has(passage.id)) this.gates.set(passage.id, []);
        this.gates.get(passage.id).push(gate);
      }
    }
    for (let x = 0; x < map.width; x++) svg.append(element('text', { x: PAD + (x + .5) * CELL, y: PAD - 13, class: 'map-axis' }, String(x)));
    for (let y = 0; y < map.height; y++) svg.append(element('text', { x: PAD - 18, y: PAD + (y + .5) * CELL + 3, class: 'map-axis' }, String(y)));
    this.cargoLayer = element('g');
    this.destination = element('g', { class: 'map-destination', visibility: 'hidden' }, '<rect x="-15" y="-15" width="30" height="30" rx="5" fill="#15765b10" stroke="#15765b" stroke-width="1.5" stroke-dasharray="4 3"/><path d="M-7 0H7M0-7V7" stroke="#15765b" stroke-width="1.3"/><text class="map-label" y="-22">DESTINO</text>');
    this.robotLayer = element('g');
    svg.append(this.destination, this.cargoLayer, this.robotLayer);
    this.fit();
    let drag;
    svg.addEventListener('pointerdown', event => {
      if (event.button !== 0 || (!this.targeting && event.target.closest('[role=button]'))) return;
      const matrix = svg.getScreenCTM()?.inverse();
      if (!matrix) return;
      drag = { start: new DOMPoint(event.clientX, event.clientY).matrixTransform(matrix), matrix, view: [...this.view], x: event.clientX, y: event.clientY, moved: false };
      svg.setPointerCapture(event.pointerId);
    });
    svg.addEventListener('pointermove', event => {
      if (!drag || this.targeting) return;
      const point = new DOMPoint(event.clientX, event.clientY).matrixTransform(drag.matrix);
      if (Math.hypot(event.clientX - drag.x, event.clientY - drag.y) > 4) drag.moved = true;
      this.view = [drag.view[0] - (point.x - drag.start.x), drag.view[1] - (point.y - drag.start.y), ...drag.view.slice(2)];
      this.applyView();
    });
    svg.addEventListener('pointerup', event => {
      if (!drag) return;
      if (this.targeting && !drag.moved) {
        this.ignoreClickUntil = performance.now() + 150;
        const point = new DOMPoint(event.clientX, event.clientY).matrixTransform(svg.getScreenCTM().inverse());
        const cell = { x: Math.floor((point.x - PAD) / CELL), y: Math.floor((point.y - PAD) / CELL) };
        if (cell.x >= 0 && cell.y >= 0 && cell.x < map.width && cell.y < map.height) this.onTarget(cell);
      }
      drag = null;
    });
    svg.addEventListener('pointercancel', () => { drag = null; });
  }

  drawZone(point, kind, label, glyph) {
    const { x, y } = center(point);
    const color = kind === 'delivery' ? '#70a587' : kind === 'charger' ? '#b4a06d' : '#b6a37c';
    this.svg.append(element('g', {}, `<rect x="${x - 20}" y="${y - 20}" width="40" height="40" rx="7" fill="${color}18" stroke="${color}70"/><svg x="${x - 9}" y="${y - 9}" width="18" height="18" viewBox="0 0 24 24" color="${color}">${icon(glyph)}</svg><text class="map-label zone-${kind}" x="${x}" y="${y - 28}">${escape(label)}</text>`));
  }

  fit() { this.view = [...this.full]; this.applyView(); }

  zoom(factor) {
    const next = this.full[2] / (this.view[2] * factor);
    if (next < .8 || next > 3.5) return;
    this.view = [this.view[0] + this.view[2] * (1 - factor) / 2, this.view[1] + this.view[3] * (1 - factor) / 2, this.view[2] * factor, this.view[3] * factor];
    this.applyView();
  }

  applyView() {
    this.svg.setAttribute('viewBox', this.view.join(' '));
    $('#zoom-label').textContent = `${Math.round(this.full[2] / this.view[2] * 100)}%`;
  }

  setTargeting(value) {
    this.targeting = value;
    this.svg.classList.toggle('targeting', value);
    $('#target-hint').hidden = !value;
  }

  interactive(node, callback) {
    node.addEventListener('click', () => { if (!this.targeting && !(performance.now() < this.ignoreClickUntil)) callback(); });
    node.addEventListener('keydown', event => {
      if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); callback(); }
    });
  }

  render(data, selectedRobot, selectedCargo) {
    const groups = new Map();
    for (const robot of data.robos) {
      const group = groups.get(key(robot.posicao)) || [];
      group.push(robot.id);
      groups.set(key(robot.posicao), group);
    }
    for (const robot of data.robos) {
      let node = this.robots.get(robot.id);
      if (!node) {
        node = element('g', { role: 'button', tabindex: '0' }, '<title></title><circle class="robot-selection" r="20"/><ellipse cy="9" rx="12" ry="5" fill="#33533316"/><rect x="-12" y="-7" width="4" height="7" rx="1.5" fill="#647467"/><rect x="8" y="-7" width="4" height="7" rx="1.5" fill="#647467"/><rect x="-12" y="3" width="4" height="7" rx="1.5" fill="#647467"/><rect x="8" y="3" width="4" height="7" rx="1.5" fill="#647467"/><rect class="robot-body" x="-10" y="-10" width="20" height="22" rx="6"/><path d="M-4-6H4" stroke="#c3ecd7" stroke-width="1.5"/><text class="robot-number" y="3"></text>');
        this.interactive(node, () => this.onRobot(robot.id));
        this.robots.set(robot.id, node);
        this.robotLayer.append(node);
      }
      const group = groups.get(key(robot.posicao));
      const index = group.indexOf(robot.id);
      const angle = index * Math.PI * 2 / group.length - Math.PI / 4;
      const offset = group.length > 1 ? Math.min(16, 9 + group.length) : 0;
      const point = center(robot.posicao);
      node.style.transform = `translate(${point.x + Math.cos(angle) * offset}px, ${point.y + Math.sin(angle) * offset}px)`;
      node.setAttribute('class', `map-robot ${robotTone(robot)}${robot.id === selectedRobot ? ' selected' : ''}${robot.conectado ? '' : ' disconnected'}`);
      node.setAttribute('aria-label', `${robotName(robot.id)}, ${robot.estado}, bateria ${robot.bateria}%`);
      node.setAttribute('aria-pressed', String(robot.id === selectedRobot));
      $('title', node).textContent = `${robotName(robot.id)} · ${robot.estado} · ${robot.bateria}%`;
      $('.robot-number', node).textContent = robot.id;
    }
    for (const [id, node] of this.robots) if (!data.robos.some(robot => robot.id === id)) { node.remove(); this.robots.delete(id); }
    const cargoGroups = new Map();
    for (const cargo of data.cargas || []) {
      const bay = this.map.bays.find(point => point.id === cargo.id_baia);
      const destination = this.map.destinations.find(point => point.id === cargo.id_destino);
      const carrier = data.robos.find(robot => robot.id_carga === cargo.id && robot.acoplado);
      const position = cargo.estado === 'Entregue' ? destination : carrier?.posicao || bay;
      if (!position) continue;
      let node = this.cargo.get(cargo.id);
      if (!node) {
        node = element('g', { role: 'button', tabindex: '0' }, '<title></title><rect class="cargo-shape" x="-12" y="-10" width="24" height="23" rx="4" stroke-width="1.2"/><path d="M-12-4 0 2 12-4M0 2v11" fill="none" stroke="#ffffffa0"/><text class="cargo-id" y="-17" text-anchor="middle" font-size="9" font-weight="600" fill="#829374"></text><text class="cargo-size" y="-2" text-anchor="middle" font-size="8" fill="#fff"></text>');
        this.interactive(node, () => this.onCargo(cargo.id));
        this.cargo.set(cargo.id, node);
        this.cargoLayer.append(node);
      }
      const count = cargoGroups.get(key(position)) || 0;
      cargoGroups.set(key(position), count + 1);
      const point = center(position);
      node.setAttribute('transform', `translate(${point.x + (count % 3) * 7 - 4} ${point.y - Math.floor(count / 3) * 5})`);
      node.setAttribute('class', `cargo-marker${selectedCargo === cargo.id ? ' selected' : ''}`);
      node.setAttribute('aria-pressed', String(selectedCargo === cargo.id));
      node.setAttribute('aria-label', `Carga ${cargo.id}, tamanho ${cargo.tamanho}, ${cargo.estado}`);
      $('.cargo-shape', node).setAttribute('fill', cargo.estado === 'Entregue' ? '#86b49a' : cargo.estado === 'Suspensa' ? '#c98070' : '#c4b184');
      $('.cargo-shape', node).setAttribute('stroke', '#a38e6260');
      $('.cargo-id', node).textContent = `C${cargo.id}`;
      $('.cargo-size', node).textContent = '•'.repeat(cargo.tamanho);
      $('title', node).textContent = `Carga ${cargo.id} · ${cargo.tamanho} robô(s) · ${cargo.estado}`;
    }
    for (const [id, node] of this.cargo) if (!(data.cargas || []).some(cargo => cargo.id === id)) { node.remove(); this.cargo.delete(id); }
    for (const passage of data.passagens || []) for (const node of this.gates.get(passage.id) || []) {
      const capacity = this.map.passages.find(item => item.id === passage.id).capacity;
      $('text', node).textContent = `${passage.ocupado}/${capacity}`;
      $('rect', node).setAttribute('fill', passage.ocupado ? '#e8d9ac' : '#ede8d4');
      $('rect', node).setAttribute('stroke', passage.aguardando ? '#b1812c' : '#c9b67a');
      $('title', node).textContent = `Passagem ${passage.id} · ${passage.ocupado}/${capacity} vagas ocupadas · ${passage.aguardando} aguardando`;
    }
    const robot = data.robos.find(item => item.id === selectedRobot);
    const showDestination = robot && key(robot.posicao) !== key(robot.destino);
    this.destination.setAttribute('visibility', showDestination ? 'visible' : 'hidden');
    if (showDestination) {
      const point = center(robot.destino);
      this.destination.setAttribute('transform', `translate(${point.x} ${point.y})`);
    }
  }
}
