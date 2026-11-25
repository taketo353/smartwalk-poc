const isAdmin = location.pathname.replace(/\/$/, '') === '/admin';
const get = (id) => document.getElementById(id);
const risks = new Map([
  ['low', {name: '低い'}],
  ['caution', {name: '注意'}],
  ['high', {name: '高い'}],
  ['unknown', {name: '判定不明'}],
]);
const moods = new Map([
  ['low', '今日は安心して歩けそうです'],
  ['caution', '少し気をつけて歩きましょう'],
  ['high', '凍結のおそれがあります。十分注意してください。'],
  ['unknown', 'いまは状況がわかりません'],
]);
const locations = [
  {id: 'kakuma-slope-01', name: '角間坂', x: 148, y: 126},
];
const markers = new Map();
const svgNamespace = 'http://www.w3.org/2000/svg';
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
let previousRisk = null;

const flashyMode = !isAdmin;
const motionOK = flashyMode && !matchMedia('(prefers-reduced-motion: reduce)').matches;
let snow = null;

function setText(id, value) {
  const element = get(id);
  if (element.textContent !== value) element.textContent = value;
}

function setTextSoft(id, value) {
  const element = get(id);
  if (element.textContent === value) return;
  if (!motionOK) { element.textContent = value; return; }
  element.getAnimations().forEach((animation) => animation.cancel());
  const fadeOut = element.animate([{opacity: 1}, {opacity: 0}], {duration: 110, easing: 'ease-in'});
  fadeOut.onfinish = () => {
    element.textContent = value;
    element.animate([{opacity: 0}, {opacity: 1}], {duration: 220, easing: 'ease-out'});
  };
}

function formatTime(isoTime) {
  const time = isoTime ? new Date(isoTime) : null;
  return time && Number.isFinite(time.getTime()) ? `${dateFormat.format(time)} JST` : '';
}

function formatTemperature(value) {
  return Number.isFinite(value) ? `${value} °C` : missing;
}

function createShape(name, attributes) {
  const element = document.createElementNS(svgNamespace, name);
  for (const [key, value] of Object.entries(attributes)) element.setAttribute(key, value);
  return element;
}

function buildMap() {
  const layer = get('map-markers');
  for (const spot of locations) {
    const marker = createShape('g', {class: 'map-marker', 'data-risk': 'unknown', transform: `translate(${spot.x} ${spot.y})`});
    const mark = createShape('svg', {class: 'marker-mark', x: -7, y: -7, width: 14, height: 14, viewBox: '0 0 24 24'});
    const markUse = createShape('use', {href: '#icon-unknown'});
    const name = createShape('text', {class: 'marker-name', y: 30, 'text-anchor': 'middle'});
    const level = createShape('text', {class: 'marker-level', y: 44, 'text-anchor': 'middle'});
    name.textContent = spot.name;
    mark.append(markUse);
    marker.append(
      createShape('circle', {class: 'marker-ring marker-ring-a', r: 10}),
      createShape('circle', {class: 'marker-ring marker-ring-b', r: 10}),
      createShape('circle', {class: 'marker-halo', r: 15}),
      createShape('circle', {class: 'marker-round', r: 10}),
      createShape('path', {class: 'marker-wedge', d: 'M0 -12L11 7H-11Z'}),
      mark, name, level,
    );
    layer.append(marker);
    markers.set(spot.id, {marker, markUse, level, name: spot.name});
  }
}

function showMap(status) {
  const risk = risks.has(status?.risk) ? status.risk : 'unknown';
  const summary = [];
  for (const [id, entry] of markers) {
    const level = id === status?.location_id ? risk : 'unknown';
    entry.marker.setAttribute('data-risk', level);
    entry.markUse.setAttribute('href', `#icon-${level}`);
    entry.level.textContent = risks.get(level).name;
    summary.push(`${entry.name}は凍結リスク ${risks.get(level).name}`);
  }
  get('slope-map').setAttribute('aria-label', `角間坂の簡易マップ。${summary.join('、')}。`);
}

const snowThemes = new Map([
  ['low', {tint: [255, 223, 240], wind: .12, fall: .45, count: 40, confetti: true}],
  ['caution', {tint: [255, 214, 180], wind: .35, fall: .9, count: 90}],
  ['high', {tint: [255, 190, 200], wind: .08, fall: .3, count: 18}],
  ['unknown', {tint: [230, 210, 245], wind: .18, fall: .5, count: 55}],
]);
const confettiPalette = ['#FF6B9D', '#FFC2E2', '#FFD166', '#9FF5D2', '#C77DFF', '#FF9F4A'];

function initSnowCanvas() {
  const canvas = get('snow-canvas');
  const ctx = canvas.getContext('2d');
  let width = 0;
  let height = 0;
  let particles = [];
  let theme = snowThemes.get('unknown');

  function resize() {
    width = canvas.width = window.innerWidth;
    height = canvas.height = window.innerHeight;
  }

  function spawn() {
    const confetti = Boolean(theme.confetti) && Math.random() < .4;
    return {
      x: Math.random() * width,
      y: Math.random() * height,
      r: Math.random() * 2.4 + .6,
      vy: theme.fall * (Math.random() * .6 + .7),
      vx: theme.wind * (Math.random() * .6 + .7),
      alpha: Math.random() * .5 + .35,
      rotation: Math.random() * Math.PI,
      spin: (Math.random() - .5) * .08,
      confetti,
      sparkle: !confetti && Math.random() < .25,
      color: confetti ? confettiPalette[Math.floor(Math.random() * confettiPalette.length)] : null,
    };
  }

  function step() {
    ctx.clearRect(0, 0, width, height);
    while (particles.length < theme.count) particles.push(spawn());
    particles.length = theme.count;
    const [r, g, b] = theme.tint;
    const color = `rgb(${r} ${g} ${b})`;
    ctx.fillStyle = color;
    ctx.strokeStyle = color;
    for (const particle of particles) {
      particle.x += particle.vx;
      particle.y += particle.vy;
      particle.rotation += particle.confetti ? particle.spin : .01;
      if (particle.y > height + 4) { particle.y = -4; particle.x = Math.random() * width; }
      if (particle.x > width + 4) particle.x = -4;
      if (particle.x < -4) particle.x = width + 4;
      ctx.globalAlpha = particle.alpha;
      if (particle.confetti) {
        const w = particle.r * 2.6;
        ctx.save();
        ctx.translate(particle.x, particle.y);
        ctx.rotate(particle.rotation);
        ctx.fillStyle = particle.color;
        ctx.fillRect(-w / 2, -w / 4, w, w / 2);
        ctx.restore();
      } else if (particle.sparkle) {
        const s = particle.r * 1.9;
        ctx.save();
        ctx.translate(particle.x, particle.y);
        ctx.rotate(particle.rotation);
        ctx.lineWidth = Math.max(.6, particle.r * .4);
        ctx.beginPath();
        ctx.moveTo(-s, 0);
        ctx.lineTo(s, 0);
        ctx.moveTo(0, -s);
        ctx.lineTo(0, s);
        ctx.stroke();
        ctx.restore();
      } else {
        ctx.beginPath();
        ctx.arc(particle.x, particle.y, particle.r, 0, Math.PI * 2);
        ctx.fill();
      }
    }
    ctx.globalAlpha = 1;
    requestAnimationFrame(step);
  }

  resize();
  window.addEventListener('resize', resize);
  requestAnimationFrame(step);

  return {
    setRisk(risk) {
      theme = snowThemes.get(risk) ?? snowThemes.get('unknown');
      for (const particle of particles) {
        particle.vy = theme.fall * (Math.random() * .6 + .7);
        particle.vx = theme.wind * (Math.random() * .6 + .7);
        particle.confetti = Boolean(theme.confetti) && Math.random() < .4;
        particle.sparkle = !particle.confetti && Math.random() < .25;
        particle.color = particle.confetti
          ? confettiPalette[Math.floor(Math.random() * confettiPalette.length)]
          : null;
      }
    },
  };
}

function enableScrollReveal() {
  const cards = document.querySelectorAll('.card');
  cards.forEach((card, index) => {
    card.style.transitionDelay = `${index * 70}ms`;
    setTimeout(() => card.classList.add('revealed'), 60);
  });
  if (!('IntersectionObserver' in window)) return;
  const observer = new IntersectionObserver((entries) => {
    for (const entry of entries) {
      if (entry.isIntersecting) {
        entry.target.classList.add('revealed');
        observer.unobserve(entry.target);
      }
    }
  }, {threshold: .15});
  cards.forEach((card) => observer.observe(card));
}

function enableHeaderScroll() {
  let ticking = false;
  window.addEventListener('scroll', () => {
    if (ticking) return;
    ticking = true;
    requestAnimationFrame(() => {
      document.body.classList.toggle('scrolled', window.scrollY > 8);
      ticking = false;
    });
  }, {passive: true});
}

function enableMapTilt() {
  const card = get('location-panel');
  const figure = card.querySelector('.locator');
  card.addEventListener('pointermove', (event) => {
    if (event.pointerType !== 'mouse') return;
    const rect = card.getBoundingClientRect();
    const px = (event.clientX - rect.left) / rect.width - .5;
    const py = (event.clientY - rect.top) / rect.height - .5;
    figure.style.transform = `rotateX(${(-py * 8).toFixed(2)}deg) rotateY(${(px * 10).toFixed(2)}deg)`;
  });
  card.addEventListener('pointerleave', () => { figure.style.transform = ''; });
}

function triggerRiskChangeEffects(risk, riskColor) {
  const panel = get('risk-panel');

  if (risk === 'high') {
    // 高リスクへの切り替えは「伝わること」優先。派手な動きは出さない。
    get('risk-level').animate([{opacity: .4}, {opacity: 1}], {duration: 280, easing: 'ease-out'});
    return;
  }

  get('risk-flash').style.setProperty('--flash-color', riskColor);
  get('risk-flash').animate(
    [{opacity: 0}, {opacity: risk === 'low' ? .6 : .32, offset: .18}, {opacity: 0}],
    {duration: 650, easing: 'ease-out'},
  );

  if (risk === 'low') {
    panel.animate(
      [
        {transform: 'translateX(0)'},
        {transform: 'translateX(-5px)'},
        {transform: 'translateX(5px)'},
        {transform: 'translateX(-3px)'},
        {transform: 'translateX(0)'},
      ],
      {duration: 360, easing: 'ease-in-out'},
    );
  }

  const peak = risk === 'low' ? 1.1 : 1.03;
  const start = risk === 'low' ? .55 : .85;
  get('risk-level').animate(
    [
      {transform: `scale(${start})`, opacity: 0},
      {transform: `scale(${peak})`, opacity: 1, offset: .7},
      {transform: 'scale(1)', opacity: 1},
    ],
    {duration: risk === 'low' ? 480 : 320, easing: 'cubic-bezier(.34, 1.56, .64, 1)'},
  );
}

function showStatus(status, error) {
  const risk = risks.has(status?.risk) ? status.risk : 'unknown';
  const {name} = risks.get(risk);
  get('risk-panel').dataset.risk = risk;
  get('risk-icon-use').setAttribute('href', `#icon-${risk}`);
  setText('risk-level', name);
  if (flashyMode) setTextSoft('risk-mood', moods.get(risk));
  setTextSoft('risk-reason', error ?? status?.reason ?? '観測データがありません。');

  const formatted = formatTime(status?.observed_at);
  const observationTime = get('observation-time');
  if (formatted) {
    observationTime.dateTime = status.observed_at;
  } else {
    observationTime.removeAttribute('datetime');
  }
  setTextSoft('observation-time', formatted || '未取得');

  const sampleInputs = [];
  if (status?.temperature_source === 'demo') sampleInputs.push('デモ用の温度');
  if (status?.image_source === 'saved') sampleInputs.push('保存画像');
  if (status?.vision_source === 'manual') sampleInputs.push('手動ラベル');
  get('sample-notice').hidden = !sampleInputs.length;
  setText('sample-notice', sampleInputs.length ? `判定に使用した入力　${sampleInputs.join('・')}` : '');

  showMap(status);

  if (snow) snow.setRisk(risk);
  if (motionOK && previousRisk !== null && previousRisk !== risk) {
    triggerRiskChangeEffects(risk, getComputedStyle(get('risk-panel')).getPropertyValue('--grad-a').trim());
  }
  previousRisk = risk;

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
  document.body.classList.add('admin');
  document.querySelector('.page-intro p').textContent = '観測値・判定結果・入力元・履歴を確認する運用担当者向けの画面です。判定は参考値です。';
}
get('admin-view').hidden = !isAdmin;
buildMap();
enableScrollReveal();
enableHeaderScroll();

if (flashyMode) document.body.classList.add('flashy');
if (motionOK) {
  snow = initSnowCanvas();
  enableMapTilt();
} else {
  get('snow-canvas').hidden = true;
}

window.addEventListener('offline', () => {
  connectionGeneration += 1;
  showStatus(null, connectionError);
  if (isAdmin) showHistory([], '観測履歴を取得できません。');
});
refresh();
