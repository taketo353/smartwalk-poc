const isAdmin = location.pathname.replace(/\/$/, '') === '/admin';
const get = (id) => document.getElementById(id);
const risks = new Map([
  ['low', {name: '低い', icon: '○'}],
  ['caution', {name: '注意', icon: '!'}],
  ['high', {name: '高い', icon: '!'}],
  ['unknown', {name: '判定不明', icon: '?'}],
]);
const labels = new Map([
  ['dry', '乾燥'], ['wet', '湿潤'], ['snow', '積雪'], ['ice', '凍結'], ['unknown', '不明'],
]);
const temperatureSources = new Map([['sensor', 'センサー'], ['demo', 'デモ用の温度']]);
const visionSources = new Map([['model', '画像認識モデル'], ['manual', '手動ラベル']]);
const imageSources = new Map([['saved', '保存画像'], ['live', 'カメラ画像']]);
const dateFormat = new Intl.DateTimeFormat('ja-JP', {
  year: 'numeric', month: '2-digit', day: '2-digit',
  hour: '2-digit', minute: '2-digit', second: '2-digit', hourCycle: 'h23', timeZone: 'Asia/Tokyo',
});
const missing = '—';
const connectionError = '観測データを取得できません。';
let connectionGeneration = 0;
let lastHistory;

function setText(id, value) {
  const element = get(id);
  if (element.textContent !== value) element.textContent = value;
}

function formatTime(isoTime) {
  const time = isoTime ? new Date(isoTime) : null;
  return time && Number.isFinite(time.getTime()) ? `${dateFormat.format(time)} JST` : '';
}

function formatTemperature(value) {
  return Number.isFinite(value) ? `${value} °C` : missing;
}

function showStatus(status, error) {
  const risk = risks.has(status?.risk) ? status.risk : 'unknown';
  const {name, icon} = risks.get(risk);
  get('risk-panel').dataset.risk = risk;
  setText('risk-icon', icon);
  setText('risk-level', name);
  setText('risk-reason', error ?? status?.reason ?? '観測データがありません。');

  const formatted = formatTime(status?.observed_at);
  const observationTime = get('observation-time');
  if (formatted) {
    observationTime.dateTime = status.observed_at;
  } else {
    observationTime.removeAttribute('datetime');
  }
  setText('observation-time', formatted || '未取得');

  const sampleInputs = [];
  if (status?.temperature_source === 'demo') sampleInputs.push('デモ用の温度');
  if (status?.image_source === 'saved') sampleInputs.push('保存画像');
  if (status?.vision_source === 'manual') sampleInputs.push('手動ラベル');
  get('sample-notice').hidden = !sampleInputs.length;
  setText('sample-notice', sampleInputs.length ? `判定に使用した入力　${sampleInputs.join('・')}` : '');

  if (isAdmin) {
    setText('location-id', status?.location_id ?? missing);
    setText('surface-temperature', formatTemperature(status?.surface_temperature_c));
    setText('temperature-source', temperatureSources.get(status?.temperature_source) ?? missing);
    setText('surface-label', labels.get(status?.label) ?? missing);
    setText('vision-source', visionSources.get(status?.vision_source) ?? missing);
    setText('image-source', imageSources.get(status?.image_source) ?? missing);
    setText('image-id', status?.image_id ?? missing);
  }
}

function showHistory(items, error) {
  const snapshot = JSON.stringify({items, error});
  if (snapshot === lastHistory) return;
  lastHistory = snapshot;

  const rows = items.map((item) => {
    const row = document.createElement('tr');
    const values = [
      formatTime(item.observed_at) || missing,
      (risks.get(item.risk) ?? risks.get('unknown')).name,
      [formatTemperature(item.surface_temperature_c), temperatureSources.get(item.temperature_source)].filter(Boolean).join('・'),
      [labels.get(item.label) ?? missing, visionSources.get(item.vision_source)].filter(Boolean).join('・'),
      imageSources.get(item.image_source) ?? missing,
      item.reason ?? missing,
    ];
    for (const value of values) {
      const cell = document.createElement('td');
      cell.textContent = value;
      row.append(cell);
    }
    return row;
  });

  get('history-table').hidden = !rows.length;
  get('history-state').hidden = Boolean(rows.length);
  setText('history-state', error ?? '観測履歴はまだありません。');
  get('history-rows').replaceChildren(...rows);
}

async function fetchData(path) {
  const response = await fetch(path, {cache: 'no-store', signal: AbortSignal.timeout(4000)});
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  return response.json();
}

function refresh() {
  setTimeout(refresh, 5000);
  const generation = connectionGeneration;
  fetchData('/status').then((status) => {
    if (!status || !risks.has(status.risk)) throw new Error('Invalid status');
    if (generation === connectionGeneration) showStatus(status);
  }).catch(() => {
    if (generation === connectionGeneration) showStatus(null, connectionError);
  });

  if (isAdmin) {
    fetchData('/history').then(({items}) => {
      if (!Array.isArray(items)) throw new Error('Invalid history');
      if (generation === connectionGeneration) showHistory(items);
    }).catch(() => {
      if (generation === connectionGeneration) showHistory([], '観測履歴を取得できません。');
    });
  }
}

if (isAdmin) {
  document.title = '路面の観測データ | SmartWalk';
  setText('page-title', '路面の観測データ');
  get('student-link').removeAttribute('aria-current');
  get('admin-link').setAttribute('aria-current', 'page');
}
get('admin-view').hidden = !isAdmin;

window.addEventListener('offline', () => {
  connectionGeneration += 1;
  showStatus(null, connectionError);
  if (isAdmin) showHistory([], '観測履歴を取得できません。');
});
refresh();
