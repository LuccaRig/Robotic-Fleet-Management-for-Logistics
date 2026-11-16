const paths = {
  map: '<path d="m3 6 6-3 6 3 6-3v15l-6 3-6-3-6 3Z"/><path d="M9 3v15M15 6v15"/>',
  box: '<path d="m12 3 9 5v9l-9 5-9-5V8Z"/><path d="m3 8 9 5 9-5M12 13v9M7.5 5.5l9 5"/>',
  history: '<path d="M3 11a9 9 0 1 1 2.8 7M3 4v7h7M12 7v5l3 2"/>',
  robot: '<rect x="5" y="7" width="14" height="13" rx="4"/><path d="M12 3v4M2 11v5M22 11v5M9 16h6"/><path d="M9 11h.01M15 11h.01" stroke-width="3"/>',
  plus: '<path d="M12 5v14M5 12h14"/>',
  minus: '<path d="M5 12h14"/>',
  check: '<path d="m5 12 4 4L19 6"/>',
  route: '<circle cx="5" cy="5" r="2"/><circle cx="19" cy="19" r="2"/><path d="M7 5h8a4 4 0 0 1 0 8H9a3 3 0 0 0 0 6h8"/>',
  arrow: '<path d="M4 12h16m-6-6 6 6-6 6"/>',
  expand: '<path d="M9 3H3v6M15 3h6v6M3 15v6h6M21 15v6h-6"/>',
  layers: '<path d="m12 3 10 6-10 6L2 9Zm-9 11 9 5 9-5M3 18l9 5 9-5"/>',
  tag: '<path d="M3 3h8l10 10-8 8L3 11Z"/><circle cx="7" cy="7" r="1"/>',
  target: '<circle cx="12" cy="12" r="7"/><circle cx="12" cy="12" r="2"/><path d="M12 1v4M12 19v4M1 12h4M19 12h4"/>',
  gate: '<path d="M4 21V4h16v17M8 4v17M16 4v17M4 9h16M4 15h16"/>',
  close: '<path d="m6 6 12 12M6 18 18 6"/>',
  auto: '<path d="m13 2-9 12h7l-1 8 10-13h-8Z"/>',
  hand: '<path d="M8 12V6a2 2 0 0 1 4 0v5-7a2 2 0 0 1 4 0v7-5a2 2 0 0 1 4 0v10a6 6 0 0 1-6 6h-2c-2 0-4-1-5-3l-4-6a2 2 0 0 1 3-2l2 2"/>',
  alert: '<path d="m10.3 4-8 14a2 2 0 0 0 1.7 3h16a2 2 0 0 0 1.7-3l-8-14a2 2 0 0 0-3.4 0Z"/><path d="M12 9v4M12 17h.01"/>',
  play: '<path d="m8 4 12 8-12 8Z"/>',
  'chevron-left': '<path d="m15 5-7 7 7 7"/>',
  'chevron-right': '<path d="m9 5 7 7-7 7"/>',
};

export const $ = (selector, root = document) => root.querySelector(selector);
export const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
export const escape = value => String(value ?? '').replace(/[&<>"']/g, char => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[char]));
export const icon = name => `<svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${paths[name] || paths.box}</svg>`;
export const icons = root => $$('[data-icon]', root).forEach(node => { node.innerHTML = icon(node.dataset.icon); });
export const number = value => Number(value || 0).toLocaleString('pt-BR');
export const robotName = id => `AMR-${String(id).padStart(2, '0')}`;
export const coordinates = value => value ? `(${value.x}, ${value.y})` : 'Sem destino';
export const robotTone = robot => !robot.conectado ? 'muted' : robot.falha ? 'red' : robot.bateria <= 15 || robot.estado.includes('recarga') || robot.estado.includes('Recarregando') ? 'amber' : robot.modo === 'manual' ? 'purple' : 'blue';
export const cargoTone = cargo => cargo.estado === 'Suspensa' ? 'red' : cargo.estado === 'Pendente' ? 'muted' : 'blue';
export const timestamp = value => value ? new Intl.DateTimeFormat('pt-BR', { dateStyle: 'short', timeStyle: 'medium', timeZone: 'America/Sao_Paulo' }).format(new Date(value)) : 'Ainda não registrada';

export async function request(path, options = {}) {
  const response = await fetch(path, { ...options, signal: AbortSignal.timeout(5000), cache: 'no-store' });
  const body = await response.json();
  if (!response.ok) throw new Error(body.erro || 'Não foi possível concluir a solicitação');
  return body;
}

export function toast(message, tone = 'success', retry) {
  const node = document.createElement('div');
  node.className = `toast ${tone}`;
  node.innerHTML = `${icon(tone === 'success' ? 'check' : 'alert')}<div><strong>${tone === 'success' ? 'Confirmação' : 'Aviso'}</strong><p>${escape(message)}</p></div><button class="button secondary icon-button compact" aria-label="Fechar aviso">${icon('close')}</button>`;
  $('button', node).onclick = () => node.remove();
  if (retry) {
    const button = document.createElement('button');
    button.className = 'button secondary compact';
    button.textContent = 'Reenviar';
    button.onclick = () => { node.remove(); retry(); };
    $('div', node).append(button);
  }
  $('#toasts').append(node);
  while ($('#toasts').children.length > 4) $('#toasts').firstElementChild.remove();
  if (!retry) setTimeout(() => node.remove(), tone === 'success' ? 5500 : 10000);
}
