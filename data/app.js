/**
 * TinyRadio - Frontend Application Logic
 * Supports dual playback: Web Browser Audio & ESP32 I2S Hardware
 * Includes auto-detection for Captive Portal / SoftAP mode
 */

// Estado global de la aplicación
const AppState = {
  stations: [],
  filteredStations: [],
  currentStation: null,
  isPlaying: false,
  playbackTarget: 'web', // 'web' o 'esp32'
  activeGenre: 'all',
  volume: 70,
  searchQuery: '',
  isApMode: false
};

const GenreIcons = {
  'Rock': '🎸',
  'Lo-Fi': '🎧',
  'Electronic': '⚡',
  'Synthwave': '🕹️',
  'Jazz': '🎷',
  'Classical': '🎻',
  'Chillout': '☕',
  'Indie': '🌵',
  'Latin': '💃',
  'Pop': '🎤',
  'News': '📰',
  'Ambient': '🌌',
  'Acoustic': '🪕',
  'default': '📻'
};

const elements = {
  setupBanner: document.getElementById('setupBanner'),
  bannerWifiBtn: document.getElementById('bannerWifiBtn'),

  targetWebBtn: document.getElementById('targetWebBtn'),
  targetEspBtn: document.getElementById('targetEspBtn'),

  heroArtwork: document.getElementById('stationArtwork'),
  heroGenreIcon: document.getElementById('heroGenreIcon'),
  liveBadge: document.getElementById('liveBadge'),
  liveText: document.getElementById('liveText'),
  heroGenre: document.getElementById('heroGenre'),
  heroCountry: document.getElementById('heroCountry'),
  heroBitrate: document.getElementById('heroBitrate'),
  heroTitle: document.getElementById('heroTitle'),
  heroSubtitle: document.getElementById('heroSubtitle'),
  heroFavBtn: document.getElementById('heroFavBtn'),
  playPauseBtn: document.getElementById('playPauseBtn'),
  playIcon: document.getElementById('playIcon'),
  stopIcon: document.getElementById('stopIcon'),
  prevStationBtn: document.getElementById('prevStationBtn'),
  nextStationBtn: document.getElementById('nextStationBtn'),
  volumeSlider: document.getElementById('volumeSlider'),
  volumeLabel: document.getElementById('volumeLabel'),

  searchInput: document.getElementById('searchInput'),
  clearSearchBtn: document.getElementById('clearSearchBtn'),
  genreChips: document.getElementById('genreChips'),

  stationsGrid: document.getElementById('stationsGrid'),
  stationsCount: document.getElementById('stationsCount'),
  emptyState: document.getElementById('emptyState'),

  audioPlayer: document.getElementById('browserAudioPlayer'),

  addModal: document.getElementById('addModal'),
  openAddModalBtn: document.getElementById('openAddModalBtn'),
  closeAddModalBtn: document.getElementById('closeAddModalBtn'),
  cancelAddBtn: document.getElementById('cancelAddBtn'),
  addStationForm: document.getElementById('addStationForm'),

  wifiModal: document.getElementById('wifiModal'),
  openWifiModalBtn: document.getElementById('openWifiModalBtn'),
  closeWifiModalBtn: document.getElementById('closeWifiModalBtn'),
  cancelWifiBtn: document.getElementById('cancelWifiBtn'),
  wifiForm: document.getElementById('wifiForm'),
  scanWifiBtn: document.getElementById('scanWifiBtn'),
  wifiList: document.getElementById('wifiList'),
  wifiSSID: document.getElementById('wifiSSID'),

  infoModal: document.getElementById('infoModal'),
  openInfoModalBtn: document.getElementById('openInfoModalBtn'),
  closeInfoModalBtn: document.getElementById('closeInfoModalBtn'),
  infoIP: document.getElementById('infoIP'),
  infoRSSI: document.getElementById('infoRSSI'),
  infoHeap: document.getElementById('infoHeap'),
  infoAudioMode: document.getElementById('infoAudioMode'),
  infoUptime: document.getElementById('infoUptime'),

  toastContainer: document.getElementById('toastContainer')
};

// ---------------- INICIALIZACIÓN ----------------
document.addEventListener('DOMContentLoaded', () => {
  initEventListeners();
  loadSavedSettings();
  loadStations();
  setupAudioEvents();
  checkSystemStatus();
  startStatusPolling();
});

function loadSavedSettings() {
  const savedTarget = localStorage.getItem('tinyradio_target');
  if (savedTarget === 'esp32' || savedTarget === 'web') {
    AppState.playbackTarget = savedTarget;
    updateTargetUI();
  }

  const savedVol = localStorage.getItem('tinyradio_vol');
  if (savedVol !== null) {
    AppState.volume = parseInt(savedVol, 10);
    elements.volumeSlider.value = AppState.volume;
    elements.volumeLabel.textContent = `${AppState.volume}%`;
    elements.audioPlayer.volume = AppState.volume / 100;
  }
}

async function loadStations() {
  try {
    const response = await fetch('/api/stations');
    if (response.ok) {
      AppState.stations = await response.json();
    } else {
      throw new Error('Fallback stations.json');
    }
  } catch (err) {
    try {
      const fallback = await fetch('stations.json');
      AppState.stations = await fallback.json();
    } catch (e) {
      AppState.stations = getDemoStations();
    }
  }

  syncFavorites();
  filterAndRenderStations();

  if (AppState.stations.length > 0 && !AppState.currentStation) {
    selectStation(AppState.stations[0], false);
  }
}

function syncFavorites() {
  const localFavs = JSON.parse(localStorage.getItem('tinyradio_favs') || '[]');
  if (localFavs.length > 0) {
    AppState.stations.forEach(st => {
      if (localFavs.includes(st.id)) {
        st.favorite = true;
      }
    });
  }
}

function saveFavoritesLocally() {
  const favIds = AppState.stations.filter(s => s.favorite).map(s => s.id);
  localStorage.setItem('tinyradio_favs', JSON.stringify(favIds));
}

// ---------------- EVENT LISTENERS ----------------
function initEventListeners() {
  // Banner de portal cautivo
  elements.bannerWifiBtn.addEventListener('click', () => {
    elements.wifiModal.classList.remove('hidden');
    scanWifiNetworks();
  });

  // Selector de destino
  elements.targetWebBtn.addEventListener('click', () => setPlaybackTarget('web'));
  elements.targetEspBtn.addEventListener('click', () => setPlaybackTarget('esp32'));

  // Controles Hero
  elements.playPauseBtn.addEventListener('click', togglePlayPause);
  elements.prevStationBtn.addEventListener('click', playPreviousStation);
  elements.nextStationBtn.addEventListener('click', playNextStation);
  elements.heroFavBtn.addEventListener('click', toggleHeroFavorite);

  // Slider de volumen
  elements.volumeSlider.addEventListener('input', (e) => {
    const val = parseInt(e.target.value, 10);
    setVolume(val);
  });

  // Búsqueda
  elements.searchInput.addEventListener('input', (e) => {
    AppState.searchQuery = e.target.value.toLowerCase().trim();
    elements.clearSearchBtn.classList.toggle('hidden', AppState.searchQuery === '');
    filterAndRenderStations();
  });

  elements.clearSearchBtn.addEventListener('click', () => {
    elements.searchInput.value = '';
    AppState.searchQuery = '';
    elements.clearSearchBtn.classList.add('hidden');
    filterAndRenderStations();
  });

  // Chips de géneros
  elements.genreChips.addEventListener('click', (e) => {
    const btn = e.target.closest('.genre-chip');
    if (!btn) return;
    elements.genreChips.querySelectorAll('.genre-chip').forEach(c => c.classList.remove('active'));
    btn.classList.add('active');
    AppState.activeGenre = btn.dataset.genre;
    filterAndRenderStations();
  });

  // Modales
  setupModal(elements.openAddModalBtn, elements.closeAddModalBtn, elements.cancelAddBtn, elements.addModal);
  setupModal(elements.openWifiModalBtn, elements.closeWifiModalBtn, elements.cancelWifiBtn, elements.wifiModal, () => {
    scanWifiNetworks();
  });
  setupModal(elements.openInfoModalBtn, elements.closeInfoModalBtn, null, elements.infoModal, onOpenInfoModal);

  // Formularios
  elements.addStationForm.addEventListener('submit', handleAddStation);
  elements.scanWifiBtn.addEventListener('click', scanWifiNetworks);
  elements.wifiForm.addEventListener('submit', handleWifiConnect);
}

function setupModal(openBtn, closeBtn, cancelBtn, modalEl, onOpen) {
  if (openBtn) {
    openBtn.addEventListener('click', () => {
      modalEl.classList.remove('hidden');
      if (onOpen) onOpen();
    });
  }
  if (closeBtn) closeBtn.addEventListener('click', () => modalEl.classList.add('hidden'));
  if (cancelBtn) cancelBtn.addEventListener('click', () => modalEl.classList.add('hidden'));
  modalEl.addEventListener('click', (e) => {
    if (e.target === modalEl) modalEl.classList.add('hidden');
  });
}

// ---------------- GESTIÓN DE AUDIO ----------------
function setPlaybackTarget(target) {
  if (AppState.playbackTarget === target) return;
  const wasPlaying = AppState.isPlaying;

  if (AppState.isPlaying) {
    stopCurrentPlayback();
  }

  AppState.playbackTarget = target;
  localStorage.setItem('tinyradio_target', target);
  updateTargetUI();

  if (target === 'esp32') {
    showToast('📻 Modo ESP32: El audio saldrá por el módulo DAC I2S');
  } else {
    showToast('🔊 Modo Web: El audio sonará en tu móvil / PC');
  }

  if (wasPlaying && AppState.currentStation) {
    startPlayback(AppState.currentStation);
  }
}

function updateTargetUI() {
  const isWeb = AppState.playbackTarget === 'web';
  elements.targetWebBtn.classList.toggle('active', isWeb);
  elements.targetEspBtn.classList.toggle('active', !isWeb);
  elements.infoAudioMode.textContent = isWeb ? 'Audio Web (Navegador)' : 'Hardware ESP32 (I2S DAC)';
}

function setupAudioEvents() {
  const audio = elements.audioPlayer;

  audio.addEventListener('playing', () => {
    if (AppState.playbackTarget === 'web') {
      setPlayState(true);
      setLiveBadge('live', 'EN VIVO');
    }
  });

  audio.addEventListener('waiting', () => {
    if (AppState.playbackTarget === 'web') {
      setLiveBadge('buffering', 'CONECTANDO...');
    }
  });

  audio.addEventListener('error', () => {
    if (AppState.playbackTarget === 'web') {
      setPlayState(false);
      setLiveBadge('offline', 'ERROR STREAM');
      showToast('⚠️ No se pudo reproducir este stream en el navegador');
    }
  });

  audio.addEventListener('pause', () => {
    if (AppState.playbackTarget === 'web') {
      setPlayState(false);
      setLiveBadge('ready', 'PAUSADO');
    }
  });
}

function togglePlayPause() {
  if (!AppState.currentStation) {
    if (AppState.stations.length > 0) {
      selectStation(AppState.stations[0], true);
    }
    return;
  }

  if (AppState.isPlaying) {
    stopCurrentPlayback();
  } else {
    startPlayback(AppState.currentStation);
  }
}

function selectStation(station, autoPlay = true) {
  AppState.currentStation = station;
  updateHeroStation(station);
  highlightActiveCard(station.id);

  if (autoPlay) {
    startPlayback(station);
  }
}

async function startPlayback(station) {
  if (!station || !station.url) return;
  setLiveBadge('buffering', 'CONECTANDO...');

  if (AppState.playbackTarget === 'web') {
    try {
      elements.audioPlayer.src = station.url;
      elements.audioPlayer.volume = AppState.volume / 100;
      await elements.audioPlayer.play();
    } catch (err) {
      setLiveBadge('offline', 'STREAM ERROR');
      showToast('⚠️ Error al cargar stream web');
      setPlayState(false);
    }
  } else {
    try {
      const res = await fetch('/api/play', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ id: station.id, url: station.url })
      });
      if (res.ok) {
        setPlayState(true);
        setLiveBadge('live', 'ESP32 EN VIVO');
      }
    } catch (err) {
      setPlayState(true);
      setLiveBadge('live', 'ESP32 (SIMULADO)');
      showToast('Enviado comando a ESP32');
    }
  }
}

async function stopCurrentPlayback() {
  if (AppState.playbackTarget === 'web') {
    elements.audioPlayer.pause();
    elements.audioPlayer.src = '';
  } else {
    try {
      await fetch('/api/stop', { method: 'POST' });
    } catch (err) {}
  }
  setPlayState(false);
  setLiveBadge('ready', 'EN ESPERA');
}

function setPlayState(playing) {
  AppState.isPlaying = playing;
  document.body.classList.toggle('playing', playing);
  elements.playIcon.classList.toggle('hidden', playing);
  elements.stopIcon.classList.toggle('hidden', !playing);
}

function setLiveBadge(state, text) {
  elements.liveBadge.className = 'status-indicator-badge ' + state;
  elements.liveText.textContent = text;
}

function setVolume(val) {
  AppState.volume = val;
  elements.volumeLabel.textContent = `${val}%`;
  localStorage.setItem('tinyradio_vol', val);

  if (AppState.playbackTarget === 'web') {
    elements.audioPlayer.volume = val / 100;
  } else {
    fetch('/api/volume', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ volume: val })
    }).catch(() => {});
  }
}

function playNextStation() {
  if (AppState.filteredStations.length === 0) return;
  const currentIndex = AppState.filteredStations.findIndex(s => s.id === AppState.currentStation?.id);
  const nextIndex = (currentIndex + 1) % AppState.filteredStations.length;
  selectStation(AppState.filteredStations[nextIndex], true);
}

function playPreviousStation() {
  if (AppState.filteredStations.length === 0) return;
  const currentIndex = AppState.filteredStations.findIndex(s => s.id === AppState.currentStation?.id);
  const prevIndex = (currentIndex - 1 + AppState.filteredStations.length) % AppState.filteredStations.length;
  selectStation(AppState.filteredStations[prevIndex], true);
}

// ---------------- HERO UI ----------------
function updateHeroStation(station) {
  elements.heroTitle.textContent = station.name;
  elements.heroSubtitle.textContent = `Sintonizando: ${station.genre} • ${station.country || 'Global'}`;
  elements.heroGenre.textContent = station.genre || 'Música';
  elements.heroCountry.textContent = station.country || 'Global';
  elements.heroBitrate.textContent = `${station.bitrate || 128} kbps`;
  elements.heroGenreIcon.textContent = GenreIcons[station.genre] || GenreIcons['default'];
  elements.heroFavBtn.classList.toggle('is-fav', Boolean(station.favorite));
}

function toggleHeroFavorite() {
  if (!AppState.currentStation) return;
  toggleFavoriteStation(AppState.currentStation.id);
}

async function toggleFavoriteStation(stationId) {
  const station = AppState.stations.find(s => s.id === stationId);
  if (!station) return;

  station.favorite = !station.favorite;
  saveFavoritesLocally();

  if (AppState.currentStation && AppState.currentStation.id === stationId) {
    elements.heroFavBtn.classList.toggle('is-fav', station.favorite);
  }

  fetch('/api/favorites/toggle', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id: stationId, favorite: station.favorite })
  }).catch(() => {});

  filterAndRenderStations();
  showToast(station.favorite ? `⭐ Añadida a favoritos: ${station.name}` : `Quitada de favoritos`);
}

// ---------------- FILTRADO Y RENDERIZADO ----------------
function filterAndRenderStations() {
  let list = [...AppState.stations];

  if (AppState.activeGenre === 'fav') {
    list = list.filter(s => s.favorite);
  } else if (AppState.activeGenre !== 'all') {
    list = list.filter(s => (s.genre || '').toLowerCase() === AppState.activeGenre.toLowerCase());
  }

  if (AppState.searchQuery) {
    const q = AppState.searchQuery;
    list = list.filter(s =>
      s.name.toLowerCase().includes(q) ||
      (s.genre && s.genre.toLowerCase().includes(q)) ||
      (s.country && s.country.toLowerCase().includes(q))
    );
  }

  AppState.filteredStations = list;
  elements.stationsCount.textContent = `Estaciones (${list.length})`;

  renderStationsGrid(list);
}

function renderStationsGrid(stations) {
  elements.stationsGrid.innerHTML = '';

  if (stations.length === 0) {
    elements.emptyState.classList.remove('hidden');
    return;
  }
  elements.emptyState.classList.add('hidden');

  const fragment = document.createDocumentFragment();

  stations.forEach(station => {
    const card = document.createElement('div');
    card.className = 'station-card';
    if (AppState.currentStation && AppState.currentStation.id === station.id) {
      card.classList.add('active-card');
    }
    card.dataset.id = station.id;

    const icon = GenreIcons[station.genre] || GenreIcons['default'];

    card.innerHTML = `
      <div class="card-icon-box">${icon}</div>
      <div class="card-content">
        <h4 class="card-title" title="${escapeHtml(station.name)}">${escapeHtml(station.name)}</h4>
        <div class="card-meta">
          <span class="card-genre">${escapeHtml(station.genre || 'Varios')}</span>
          <span>•</span>
          <span>${escapeHtml(station.country || 'Global')}</span>
        </div>
      </div>
      <div class="card-actions">
        <button class="card-fav-btn ${station.favorite ? 'is-fav' : ''}" data-id="${station.id}" title="Favorito">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <path d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z"></path>
          </svg>
        </button>
      </div>
    `;

    card.addEventListener('click', (e) => {
      if (e.target.closest('.card-fav-btn')) return;
      selectStation(station, true);
    });

    const favBtn = card.querySelector('.card-fav-btn');
    favBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      toggleFavoriteStation(station.id);
    });

    fragment.appendChild(card);
  });

  elements.stationsGrid.appendChild(fragment);
}

function highlightActiveCard(stationId) {
  document.querySelectorAll('.station-card').forEach(card => {
    card.classList.toggle('active-card', card.dataset.id === stationId);
  });
}

// ---------------- AÑADIR NUEVA ESTACIÓN ----------------
async function handleAddStation(e) {
  e.preventDefault();
  const name = document.getElementById('newStationName').value.trim();
  const url = document.getElementById('newStationUrl').value.trim();
  const genre = document.getElementById('newStationGenre').value;
  const country = document.getElementById('newStationCountry').value.trim() || 'Global';

  if (!name || !url) return;

  const newStation = {
    id: 'custom_' + Date.now(),
    name,
    url,
    genre,
    country,
    bitrate: 128,
    favorite: true
  };

  AppState.stations.unshift(newStation);
  saveFavoritesLocally();
  filterAndRenderStations();
  selectStation(newStation, true);

  elements.addModal.classList.add('hidden');
  elements.addStationForm.reset();

  showToast(`✅ Estación añadida: ${name}`);

  fetch('/api/stations', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(newStation)
  }).catch(() => {});
}

// ---------------- WIFI MANAGER & PORTAL CAUTIVO ----------------
async function checkSystemStatus() {
  try {
    const res = await fetch('/api/status');
    if (res.ok) {
      const status = await res.json();
      AppState.isApMode = Boolean(status.isApMode);

      // Si está en modo SoftAP / Portal Cautivo, mostrar banner y sugerir configuración
      if (AppState.isApMode) {
        elements.setupBanner.classList.remove('hidden');
        // Si se abrió con ?setup=1 o en primer arranque, abrir el modal automáticamente
        if (window.location.search.includes('setup') || window.location.hostname.includes('192.168.4.1')) {
          elements.wifiModal.classList.remove('hidden');
          scanWifiNetworks();
        }
      }
    }
  } catch (e) {
    // Si no responde el endpoint de estado, comprobar si estamos en 192.168.4.1
    if (window.location.hostname === '192.168.4.1') {
      elements.setupBanner.classList.remove('hidden');
    }
  }
}

async function scanWifiNetworks() {
  elements.wifiList.innerHTML = '<div class="wifi-status-msg">🔍 Escaneando redes WiFi...</div>';

  try {
    const res = await fetch('/api/scan-wifi');
    if (!res.ok) throw new Error('Error escaneo');
    const networks = await res.json();

    if (!networks || networks.length === 0) {
      elements.wifiList.innerHTML = '<div class="wifi-status-msg">No se detectaron redes</div>';
      return;
    }

    elements.wifiList.innerHTML = '';
    networks.forEach(net => {
      const item = document.createElement('div');
      item.className = 'wifi-item';
      item.innerHTML = `
        <strong>${escapeHtml(net.ssid)}</strong>
        <span style="opacity: 0.7;">${net.rssi} dBm</span>
      `;
      item.addEventListener('click', () => {
        elements.wifiSSID.value = net.ssid;
      });
      elements.wifiList.appendChild(item);
    });
  } catch (err) {
    elements.wifiList.innerHTML = `
      <div class="wifi-status-msg">
        Introduce el nombre (SSID) de tu router debajo:
      </div>
    `;
  }
}

async function handleWifiConnect(e) {
  e.preventDefault();
  const ssid = elements.wifiSSID.value.trim();
  const password = document.getElementById('wifiPass').value;

  if (!ssid) return;

  showToast(`Conectando ESP32 a "${ssid}"...`);

  try {
    const res = await fetch('/api/wifi', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid, password })
    });
    if (res.ok) {
      showToast('Configuración enviada. El ESP32 se reiniciará en la nueva red.');
      elements.wifiModal.classList.add('hidden');
    }
  } catch (err) {
    showToast('⚠️ No se pudo enviar configuración WiFi');
  }
}

// ---------------- INFORMACIÓN Y DIAGNÓSTICO ----------------
async function onOpenInfoModal() {
  try {
    const res = await fetch('/api/hardware-info');
    if (res.ok) {
      const data = await res.json();
      elements.infoIP.textContent = data.ip || '192.168.4.1';
      elements.infoRSSI.textContent = `${data.rssi || 0} dBm (${data.ssid || 'AP'})`;
      elements.infoHeap.textContent = `${Math.round((data.freeHeap || 0) / 1024)} KB`;
      elements.infoUptime.textContent = formatUptime(data.uptime || 0);
    }
  } catch (e) {
    elements.infoIP.textContent = window.location.hostname || 'Modo Local';
    elements.infoRSSI.textContent = 'N/A';
    elements.infoHeap.textContent = '~260 KB';
    elements.infoUptime.textContent = 'En ejecución';
  }
}

function startStatusPolling() {
  setInterval(async () => {
    if (AppState.playbackTarget !== 'esp32') return;
    try {
      const res = await fetch('/api/status');
      if (res.ok) {
        const status = await res.json();
        if (status.isPlaying !== AppState.isPlaying) {
          setPlayState(status.isPlaying);
        }
      }
    } catch (e) {}
  }, 4000);
}

// ---------------- UTILIDADES ----------------
function showToast(msg) {
  const toast = document.createElement('div');
  toast.className = 'toast';
  toast.textContent = msg;
  elements.toastContainer.appendChild(toast);
  setTimeout(() => {
    toast.style.opacity = '0';
    setTimeout(() => toast.remove(), 300);
  }, 3200);
}

function escapeHtml(text) {
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}

function formatUptime(seconds) {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  return `${h}h ${m}m ${s}s`;
}

function getDemoStations() {
  return [
    { id: "1", name: "SomaFM: Groove Salad", genre: "Chillout", url: "http://ice1.somafm.com/groovesalad-128-mp3", country: "USA", bitrate: 128, favorite: true },
    { id: "2", name: "Radio Paradise (Rock)", genre: "Rock", url: "http://stream.radioparadise.com/rock-128", country: "USA", bitrate: 128, favorite: true },
    { id: "3", name: "Nightwave Plaza", genre: "Synthwave", url: "http://radio.plaza.one/mp3", country: "Global", bitrate: 128, favorite: true }
  ];
}
