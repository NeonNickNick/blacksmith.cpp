const $ = (selector) => document.querySelector(selector);
const lobby = $('#lobby');
const matchView = $('#match');
let sessionId = null;
let selectedIron = 0;
let currentMode = 'lobby';
let currentState = null;
const API_BASE = String(window.BLACKSMITH_API_BASE || '').replace(/\/$/, '');

async function api(path, body) {
  const response = await fetch(`${API_BASE}${path}`, body === undefined ? {
    headers: { 'ngrok-skip-browser-warning': '1' },
  } : {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', 'ngrok-skip-browser-warning': '1' },
    body: JSON.stringify(body),
  });
  const payload = await response.json();
  if (!response.ok) throw new Error(payload.error || `HTTP ${response.status}`);
  return payload;
}

function showLobby() {
  lobby.classList.remove('hidden');
  matchView.classList.add('hidden');
  currentMode = 'lobby';
  $('#lobby-error').textContent = '';
  $('#action-error').textContent = '';
}

function showMatch() {
  lobby.classList.add('hidden');
  matchView.classList.remove('hidden');
}

function number(value) {
  return Number.isInteger(value) ? String(value) : Number(value).toFixed(1).replace(/\.0$/, '');
}

function renderCombatant(player, target) {
  const resources = [
    ['IRON', player.iron], ['GOLD IRON', player.gold_iron], ['MAGIC', player.magic],
    ['SPACE', player.space], ['TIME', player.time],
  ];
  const other = [['CROSS', player.resources.cross], ['DREAM', player.resources.dream],
    ['SPIRIT', player.resources.spirit], ['FIRE', player.resources.fire], ['WATER', player.resources.water],
    ['WOOD', player.resources.wood], ['EARTH', player.resources.earth], ['BOLT', player.resources.bolt]];
  target.innerHTML = `<div class="combatant-head"><span class="side-name">${player.label}</span><span class="role-tag">${player.role.toUpperCase()}</span></div>
    <div class="hp-row"><span class="hp-number">${number(player.hp)} <span class="hp-caption">HP</span></span><span class="hp-caption">MAX ${number(player.mhp)}</span></div>
    <div class="hp-bar"><div class="hp-fill" style="width:${Math.max(0, Math.min(100, player.mhp ? player.hp / player.mhp * 100 : 0))}%"></div></div>
    <div class="resource-main">${resources.map(([label, value]) => `<div class="resource"><span>${label}</span><strong>${number(value)}</strong></div>`).join('')}</div>
    <div class="resource-sub">${other.map(([label, value]) => `<div class="resource-small"><span>${label}</span><strong>${number(value)}</strong></div>`).join('')}</div>`;
}

function render(state) {
  currentState = state;
  showMatch();
  const isReview = state.phase === 'review';
  const isTrial = state.phase === 'trial';
  const isOver = state.phase === 'finished' || (state.finished && !isTrial);
  const trialOver = isTrial && state.trial_action_complete;
  $('#mode-kicker').textContent = isReview ? 'GAME REVIEW' : isTrial ? 'TRIAL · TEMPORARY GAME' : isOver ? 'GAME COMPLETE' : 'SIMULTANEOUS ROUND';
  $('#match-title').textContent = isReview ? '对局复盘' : isTrial ? '试玩中' : isOver ? '本局结束' : '对局中';
  $('#round-number').textContent = String(state.round).padStart(2, '0');
  renderCombatant(state.players[0], $('#player-card'));
  renderCombatant(state.players[1], $('#master-card'));
  $('#master-score').textContent = state.master_score == null ? '—' : `${(state.master_score * 100).toFixed(1)}%`;
  const searchNote = state.search ? ` · 搜索 ${state.search.depth}/${state.search.requested_depth} · ${state.search.nodes.toLocaleString()} nodes${state.search.budget_exhausted ? ' · 达到预算' : ''}` : '';
  $('#commit-state').innerHTML = `<i></i> ${isReview ? '历史局面' : isOver ? '本局已结束' : 'Master 已提交本回合动作'}${!isReview && !isOver ? searchNote : ''}`;
  const legal = state.legal_actions || [];
  $('#legal-count').textContent = `${legal.length} 个可用动作`;
  $('#legal-actions').innerHTML = legal.map((name) => `<option value="${escapeHtml(name)}"></option>`).join('');
  $('#action-input').disabled = isOver || isReview || trialOver;
  $('#action-form').querySelector('button').disabled = isOver || isReview || trialOver;
  $('#resign-button').disabled = isOver || isReview || trialOver;
  $('#action-help').textContent = isTrial
    ? '试玩中的行动不会写入原对局。认输或结束后返回当前复盘位置。'
    : '输入格式与 play.cmd 相同。自动补全仅显示当前可用技能。';
  $('#action-error').textContent = '';
  $('#play-controls').classList.toggle('hidden', isReview || isOver);
  $('#review-controls').classList.toggle('hidden', !isReview);
  $('#new-game-button').textContent = isTrial ? '返回复盘' : '返回设置';
  $('#last-actions').classList.toggle('hidden', !state.previous_move);
  if (state.previous_move) {
    $('#last-actions').textContent = `上一回合 · 玩家：${state.previous_move.player}　Master：${state.previous_move.master}`;
  }
  $('#result-banner').classList.toggle('hidden', !isOver && !(isTrial && trialOver));
  if (isOver) $('#result-banner').textContent = state.ending || (state.outcome ? resultLabel(state.outcome) : '本局已结束');
  else if (isTrial && state.trial_ending) $('#result-banner').textContent = state.trial_ending;
  else if (isTrial && state.finished) $('#result-banner').textContent = resultLabel(state.outcome);
  else $('#result-banner').textContent = '';
  if (isReview) {
    $('#review-position').textContent = `位置 ${state.position} / ${state.move_count}`;
    $('#prev-button').disabled = state.position <= 0;
    $('#next-button').disabled = state.position >= state.move_count;
    $('#trial-button').disabled = state.finished;
    $('#recommendations').classList.add('hidden');
  }
  $('#recommendations').classList.add('hidden');
  $('#recommendations').innerHTML = '';
  $('#action-input').value = '';
}

function escapeHtml(text) {
  return String(text).replace(/[&<>"']/g, (char) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[char]);
}

function resultLabel(outcome) {
  if (outcome >= 3) return outcome === 4 ? '平局（达到回合上限）' : '平局';
  return outcome === 1 ? '玩家获胜' : 'Master 获胜';
}

async function loadLobby() {
  try {
    const config = await api('/api/config');
    $('#connection-label').textContent = '引擎在线';
    if (config.starting_iron !== null) {
      selectedIron = Number(config.starting_iron);
      document.querySelectorAll('.iron-choice').forEach((button) => button.classList.toggle('selected', Number(button.dataset.iron) === selectedIron));
    }
    if (config.starting_iron !== null) document.querySelectorAll('.iron-choice').forEach((button) => button.disabled = true);
  } catch (error) {
    $('#connection-label').textContent = '无法连接本机引擎';
    $('.connection').classList.add('offline');
    $('#lobby-error').textContent = error.message;
    $('#start-button').disabled = true;
  }
}

document.querySelectorAll('.iron-choice').forEach((button) => button.addEventListener('click', () => {
  selectedIron = Number(button.dataset.iron);
  document.querySelectorAll('.iron-choice').forEach((choice) => choice.classList.toggle('selected', choice === button));
}));

$('#start-button').addEventListener('click', async () => {
  $('#start-button').disabled = true;
  $('#lobby-error').textContent = '正在加载模型并准备 Master…';
  try {
    const response = await api('/api/new', { iron: selectedIron });
    sessionId = response.session;
    currentMode = 'playing';
    $('#result-banner').classList.add('hidden');
    $('#last-actions').classList.add('hidden');
    render(response.state);
  } catch (error) {
    $('#lobby-error').textContent = error.message;
  } finally {
    $('#start-button').disabled = false;
  }
});

$('#action-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  const input = $('#action-input');
  const action = input.value.trim();
  if (!action || !sessionId) return;
  const button = $('#action-form').querySelector('button'); button.disabled = true;
  try {
    const route = currentMode === 'trial' ? `/api/match/${sessionId}/trial/move` : `/api/match/${sessionId}/move`;
    const response = await api(route, { action });
    if (response.trial_left) {
      currentMode = 'review'; render(response.state); return;
    }
    if (response.ending && currentMode !== 'trial') response.state.ending = response.ending;
    if (response.trial_ending) response.state.trial_ending = response.trial_ending;
    render(response.state);
    if (response.player_action) {
      $('#last-actions').classList.remove('hidden');
      $('#last-actions').textContent = `本回合 · 玩家：${response.player_action}　Master：${response.master_action}`;
    }
    if (response.ending || response.trial_ending) {
      $('#result-banner').classList.remove('hidden');
      $('#result-banner').textContent = response.ending || response.trial_ending;
    }
    input.value = '';
  } catch (error) {
    $('#action-error').textContent = error.message;
  } finally {
    button.disabled = false;
    input.focus();
  }
});

$('#resign-button').addEventListener('click', async () => {
  const input = $('#action-input'); input.value = 'resign';
  $('#action-form').requestSubmit();
});

async function goReview(position) {
  try {
    const response = await api(`/api/match/${sessionId}/review`, { position });
    currentMode = 'review'; render(response.state);
  } catch (error) { $('#action-error').textContent = error.message; }
}

$('#prev-button').addEventListener('click', () => goReview(Math.max(0, (currentState.position || 0) - 1)));
$('#next-button').addEventListener('click', () => goReview(Math.min(currentState.move_count, (currentState.position || 0) + 1)));

$('#recommend-button').addEventListener('click', async () => {
  const button = $('#recommend-button'); button.disabled = true; button.textContent = '搜索分析中…';
  try {
    const data = await api(`/api/match/${sessionId}/recommendations`, {});
    const container = $('#recommendations');
    container.innerHTML = `<div class="recommendations-meta">搜索深度 ${data.depth}/${data.requested_depth} · ${data.expanded_nodes.toLocaleString()} nodes · 策略权重不是动作胜率</div><div class="recommendation-columns">${data.recommendations.map((side) => `<div><div class="recommendation-title">${side.label}</div>${side.actions.map((action, index) => `<div class="recommendation-item"><span>${index + 1}. ${escapeHtml(action.name)}</span><span>${(action.weight * 100).toFixed(1)}%</span></div>`).join('') || '<div class="field-note">没有可推荐的动作</div>'}</div>`).join('')}</div>`;
    container.classList.remove('hidden');
  } catch (error) { $('#action-error').textContent = error.message; }
  finally { button.disabled = false; button.textContent = '查看双方推荐'; }
});

$('#trial-button').addEventListener('click', async () => {
  const button = $('#trial-button'); button.disabled = true;
  try {
    const response = await api(`/api/match/${sessionId}/trial`, {});
    currentMode = 'trial'; render(response.state);
  } catch (error) { $('#action-error').textContent = error.message; }
  finally { button.disabled = false; }
});

$('#new-game-button').addEventListener('click', async () => {
  if (currentMode === 'trial') {
    try {
      const response = await api(`/api/match/${sessionId}/trial/leave`, {});
      currentMode = 'review'; render(response.state); return;
    } catch (error) { $('#action-error').textContent = error.message; return; }
  }
  if (sessionId) api(`/api/match/${sessionId}/close`, {}).catch(() => {});
  sessionId = null;
  showLobby();
});

loadLobby();
