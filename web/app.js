/**
 * ==============================================================================
 * SMART GROCERY STORE 4.0 - IOT WEB APPLICATION LOGIC (PART 3)
 * MQTT HiveMQ WebSocket • 10 Shelves Grid • Chart.js • Grocery Analytics • Control
 * ==============================================================================
 */

// 1. MQTT BROKER CONFIGURATION
// Địa chỉ broker (wss://), tài khoản MQTT và khóa ký lấy từ hộp thoại Bảo mật (security.js).
const MQTT_CONFIG = {
  clientId: 'TapHoaWeb_' + Math.random().toString(16).substring(2, 10),
  topicTelemetry: 'nhom22/smartshelf/telemetry',
  topicCmd: 'nhom22/smartshelf/cmd'
};

// 2. 10 SHELVES GROCERY STATE
let shelfState = {
  K01: { id: 'K01', name: 'Coca-Cola 330ml', weight: 990, unit: 330, qty: 3, min: 3, low_stock: false, category: 'Đồ uống' },
  K02: { id: 'K02', name: 'Mì Hảo Hảo', weight: 375, unit: 75, qty: 5, min: 5, low_stock: false, category: 'Ăn liền' },
  K03: { id: 'K03', name: 'Bánh Oishi Snack', weight: 200, unit: 50, qty: 4, min: 4, low_stock: false, category: 'Bánh kẹo' },
  K04: { id: 'K04', name: 'Sữa Vinamilk 180ml', weight: 760, unit: 190, qty: 4, min: 4, low_stock: false, category: 'Sữa tươi' },
  K05: { id: 'K05', name: 'Bột giặt OMO 800g', weight: 2400, unit: 800, qty: 3, min: 2, low_stock: false, category: 'Hóa phẩm' },
  K06: { id: 'K06', name: 'Dầu ăn Simply 1L', weight: 3680, unit: 920, qty: 4, min: 3, low_stock: false, category: 'Gia vị' },
  K07: { id: 'K07', name: 'Nước mắm Nam Ngư', weight: 2200, unit: 550, qty: 4, min: 3, low_stock: false, category: 'Gia vị' },
  K08: { id: 'K08', name: 'Bánh Chocopie Hộp', weight: 594, unit: 198, qty: 3, min: 2, low_stock: false, category: 'Bánh kẹo' },
  K09: { id: 'K09', name: 'Trứng gà Ba Huân 10q', weight: 1800, unit: 600, qty: 3, min: 2, low_stock: false, category: 'Thực phẩm' },
  K10: { id: 'K10', name: 'Tương ớt Chinsu 250g', weight: 1120, unit: 280, qty: 4, min: 3, low_stock: false, category: 'Gia vị' }
};

let mqttClient = null;
let isDemoMode = false;
let demoInterval = null;
let lastHardwareTelemetryAt = 0;
let currentChartMode = 'weight'; // 'weight' or 'qty'
let currentCategoryFilter = 'ALL';
let historicalLogs = [];
const MAX_LOGS = 150;

// Chart.js object
let realtimeChart = null;
const MAX_CHART_POINTS = 16;

// 10 Vibrant Line Colors for Chart
const CHART_COLORS = [
  '#38bdf8', // K01 Sky Blue
  '#f59e0b', // K02 Amber
  '#10b981', // K03 Emerald
  '#a855f7', // K04 Purple
  '#ec4899', // K05 Pink
  '#f43f5e', // K06 Rose
  '#06b6d4', // K07 Cyan
  '#f97316', // K08 Orange
  '#84cc16', // K09 Lime
  '#6366f1'  // K10 Indigo
];

// Helper: Chuẩn hóa ID kệ (K1 -> K01)
function normalizeShelfId(id) {
  if (!id) return 'K01';
  if (id === 'K1') return 'K01';
  if (id === 'K2') return 'K02';
  if (id === 'K3') return 'K03';
  if (id === 'K4') return 'K04';
  if (id === 'K5') return 'K05';
  if (id === 'K6') return 'K06';
  if (id === 'K7') return 'K07';
  if (id === 'K8') return 'K08';
  if (id === 'K9') return 'K09';
  return id;
}

// Telemetry được xác minh chữ ký HMAC khi đã nhập khóa; dù vậy mọi chuỗi nhận từ MQTT
// vẫn được escape trước khi chèn bằng innerHTML (phòng thủ nhiều lớp).
const VALID_SHELF_ID = /^K(0[1-9]|10)$/;
function escapeHtml(value) {
  return String(value ?? '').replace(/[&<>"']/g, ch => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
  })[ch]);
}

// ==============================================================================
// 3. INITIALIZATION & DOM READY
// ==============================================================================
document.addEventListener('DOMContentLoaded', () => {
  const clientIdEl = document.getElementById('val-client-id');
  if (clientIdEl) clientIdEl.textContent = MQTT_CONFIG.clientId;

  // Load saved logs from localStorage
  loadLogsFromStorage();

  // Render initial shelf cards
  renderShelfCards();

  // Render control action buttons
  renderControlButtons();

  // Populate modal shelf options
  populateModalShelfOptions();

  // Setup UI tabs
  initNavTabs();

  // Setup Chart for 10 shelves
  initChart();

  // Setup Event Listeners
  setupEventListeners();

  // Nạp khóa ký của phiên (nếu đã nhập), rồi kết nối MQTT Broker
  document.getElementById('btn-security')?.addEventListener('click', openSecurityModal);
  initSigningKey().catch(err => console.error('[SECURITY]', err)).finally(() => {
    updateSecurityBadge();
    connectMQTT();
  });

  setInterval(() => {
    const sourceNote = document.getElementById('telemetry-source-note');
    if (!isDemoMode && lastHardwareTelemetryAt && Date.now() - lastHardwareTelemetryAt > 8000 && sourceNote) {
      sourceNote.textContent = 'ESP32 chưa gửi telemetry trong hơn 8 giây; dữ liệu đang hiển thị có thể đã cũ.';
      sourceNote.style.color = '#ef4444';
    }
  }, 2000);

  // Initial render with default values
  updateUIWithTelemetry({
    device: 'SmartGrocery_Hub10',
    uptime: 0,
    shelves: Object.values(shelfState),
    total_alerts: 0
  }, false);
});

// ==============================================================================
// 4. MQTT CLIENT CONNECTION & HANDLERS
// ==============================================================================
function connectMQTT() {
  const statusBadge = document.getElementById('mqtt-status-badge');
  const statusText = document.getElementById('mqtt-status-text');

  if (statusBadge) statusBadge.className = 'status-badge connecting';
  if (statusText) statusText.textContent = 'Đang kết nối Broker...';

  const connectUrl = getBrokerUrl();
  console.log('[MQTT] Connecting to:', connectUrl);

  try {
    mqttClient = mqtt.connect(connectUrl, {
      clientId: MQTT_CONFIG.clientId,
      clean: true,
      connectTimeout: 7000,
      reconnectPeriod: 5000,
      ...getMqttCredentials()
    });

    mqttClient.on('connect', () => {
      console.log('[MQTT] Connected to', connectUrl);
      if (statusBadge) statusBadge.className = 'status-badge connected';
      if (statusText) statusText.textContent = 'MQTT Đã kết nối';
      showToast('Đã kết nối MQTT Broker qua TLS (wss)', 'success');

      // Subscribe to telemetry topic
      mqttClient.subscribe(MQTT_CONFIG.topicTelemetry, (err) => {
        if (!err) {
          console.log('[MQTT] Subscribed to:', MQTT_CONFIG.topicTelemetry);
        } else {
          console.error('[MQTT] Subscription error:', err);
        }
      });
    });

    mqttClient.on('message', async (topic, message) => {
      if (topic !== MQTT_CONFIG.topicTelemetry || isDemoMode) return;
      const raw = message.toString();
      try {
        if (hasSigningKey()) {
          // Có khóa: chỉ nhận bản tin có chữ ký đúng và còn mới.
          const result = await verifyTelemetry(raw);
          if (!result.ok) {
            reportRejectedTelemetry(result.reason);
            return;
          }
          result.data.verified = true;
          updateUIWithTelemetry(result.data, true);
        } else {
          // Chưa có khóa: vẫn hiển thị để xem nhanh, nhưng đánh dấu chưa xác minh.
          const payload = JSON.parse(raw);
          payload.verified = false;
          updateUIWithTelemetry(payload, true);
        }
      } catch (e) {
        console.warn('[MQTT PARSE ERROR]:', e);
      }
    });

    mqttClient.on('reconnect', () => {
      if (statusBadge) statusBadge.className = 'status-badge connecting';
      if (statusText) statusText.textContent = 'Đang kết nối lại...';
    });

    mqttClient.on('offline', () => {
      if (statusBadge) statusBadge.className = 'status-badge disconnected';
      if (statusText) statusText.textContent = 'Mất kết nối MQTT';
    });

    mqttClient.on('error', (err) => {
      console.error('[MQTT ERROR]:', err);
      if (statusBadge) statusBadge.className = 'status-badge disconnected';
      if (statusText) statusText.textContent = 'Lỗi kết nối MQTT';
    });
  } catch (err) {
    console.error('[MQTT INIT FAILED]:', err);
  }
}

function reconnectMQTT() {
  if (mqttClient) mqttClient.end(true);
  connectMQTT();
}

// Send Command via MQTT: lệnh luôn được ký HMAC; ESP32 từ chối lệnh không có chữ ký hợp lệ.
async function publishCommand(cmdObj) {
  if (!hasSigningKey()) {
    showToast('Cần nhập khóa bảo mật (nút Bảo mật) trước khi gửi lệnh.', 'error');
    openSecurityModal();
    return;
  }
  if (!mqttClient || !mqttClient.connected) {
    showToast('Chưa gửi được lệnh: dashboard chưa kết nối MQTT.', 'error');
    return;
  }
  const signed = await signCommand(cmdObj);
  mqttClient.publish(MQTT_CONFIG.topicCmd, signed);
  console.log('[MQTT PUB CMD] ->', MQTT_CONFIG.topicCmd, cmdObj.cmd, cmdObj.id || '');
  const target = cmdObj.id ? ` xuống Kệ ${cmdObj.id}` : '';
  showToast(`Đã gửi lệnh [${cmdObj.cmd.toUpperCase()}]${target} (đã ký)`, 'success');
}

// ==============================================================================
// 5. RENDER DYNAMIC 10 SHELVES CARDS
// ==============================================================================
function renderShelfCards() {
  const container = document.getElementById('shelves-container');
  if (!container) return;

  const shelves = Object.values(shelfState);
  container.innerHTML = '';

  shelves.forEach(shelf => {
    // Check category filter
    let isVisible = true;
    if (currentCategoryFilter === 'ALERT') {
      isVisible = shelf.low_stock;
    } else if (currentCategoryFilter !== 'ALL') {
      isVisible = (shelf.category === currentCategoryFilter);
    }

    const card = document.createElement('div');
    card.className = `shelf-card ${shelf.low_stock ? 'low-stock-alert' : ''}`;
    card.id = `shelf-card-${shelf.id}`;
    if (!isVisible) card.style.display = 'none';

    // Calculate capacity percent
    const maxCap = Math.max(shelf.min * 2, 8);
    const percent = Math.min(Math.round((shelf.qty / maxCap) * 100), 100);

    card.innerHTML = `
      <div class="shelf-header">
        <div class="shelf-title-wrap">
          <span class="shelf-badge">${shelf.id}</span>
          <span class="shelf-cat-badge">${escapeHtml(shelf.category || 'Tạp hóa')}</span>
          <h3 class="shelf-name" id="name-${shelf.id}">${escapeHtml(shelf.name)}</h3>
        </div>
        <span class="stock-pill ${shelf.low_stock ? 'low-stock' : 'in-stock'}" id="pill-${shelf.id}">
          ${shelf.low_stock ? 'THIẾU HÀNG!' : 'ĐỦ HÀNG'}
        </span>
      </div>

      <div class="shelf-stats">
        <div class="stat-box">
          <span class="stat-label">Khối lượng</span>
          <span class="stat-value" id="weight-${shelf.id}">${shelf.weight} <small>g</small></span>
        </div>
        <div class="stat-box">
          <span class="stat-label">Số lượng</span>
          <span class="stat-value" id="qty-${shelf.id}">${shelf.qty} <small>món</small></span>
        </div>
        <div class="stat-box">
          <span class="stat-label">Ngưỡng báo</span>
          <span class="stat-value" id="min-${shelf.id}">${shelf.min} <small>món</small></span>
        </div>
      </div>

      <div class="progress-wrap">
        <div class="progress-info">
          <span>Mức lấp đầy kệ</span>
          <span id="percent-${shelf.id}">${percent}%</span>
        </div>
        <div class="progress-bar-bg">
          <div class="progress-bar-fill ${shelf.low_stock ? 'fill-alert' : ''}" id="fill-${shelf.id}" style="width: ${percent}%;"></div>
        </div>
      </div>

      <div class="shelf-footer">
        <div class="shelf-unit-info">
          <i class="fa-solid fa-scale-balanced"></i> 1 món: <strong id="unit-${shelf.id}">${shelf.unit}</strong>g
        </div>
        <div class="quick-sim-buttons">
          <button class="btn-mini-sim buy" onclick="quickAdjustQty('${shelf.id}', -1)" title="Mô phỏng khách mua 1 món">
            <i class="fa-solid fa-minus"></i> Mua
          </button>
          <button class="btn-mini-sim restock" onclick="quickAdjustQty('${shelf.id}', 1)" title="Bổ sung thêm 1 món vào kệ">
            <i class="fa-solid fa-plus"></i> Nhập
          </button>
        </div>
        <div class="shelf-btn-group">
          <button class="btn-card-action btn-tare" onclick="sendTareCmd('${shelf.id}')" title="Hiệu chuẩn 0g cho kệ ${shelf.id}">
            <i class="fa-solid fa-bullseye"></i> Tare
          </button>
          <button class="btn-card-action btn-config" onclick="openConfigModal('${shelf.id}')" title="Sửa thông số kệ ${shelf.id}">
            <i class="fa-solid fa-gear"></i> Sửa
          </button>
        </div>
      </div>
    `;

    container.appendChild(card);
  });
}

// Category filter handler
function filterCategory(cat) {
  currentCategoryFilter = cat;
  document.querySelectorAll('.cat-pill').forEach(btn => {
    if (btn.getAttribute('onclick')?.includes(`'${cat}'`)) {
      btn.classList.add('active');
    } else {
      btn.classList.remove('active');
    }
  });
  renderShelfCards();
}

// Quick adjust qty (+1 or -1) for live demonstration
function quickAdjustQty(shelfId, delta) {
  const normId = normalizeShelfId(shelfId);
  const s = shelfState[normId];
  if (!s) return;

  s.qty = Math.max(0, s.qty + delta);
  s.weight = Math.round(s.qty * s.unit);
  s.low_stock = (s.qty <= s.min);

  updateUIWithTelemetry({
    device: 'TapHoa_InteractiveDemo',
    uptime: Math.floor(performance.now() / 1000),
    shelves: Object.values(shelfState),
    total_alerts: Object.values(shelfState).filter(x => x.low_stock).length
  }, true);

  const actionText = delta > 0 ? `Đã bổ sung +1 ${s.name}` : `Khách đã mua 1 ${s.name}`;
  showToast(`${actionText} (Còn ${s.qty} món)`, delta > 0 ? 'success' : 'info');
}

// ==============================================================================
// 6. PROCESS TELEMETRY & UPDATE UI
// ==============================================================================
function updateUIWithTelemetry(data, saveToHistory = true) {
  if (!data || !data.shelves) return;

  const sourceNote = document.getElementById('telemetry-source-note');
  if (sourceNote) {
    const source = data.source || (String(data.device || '').includes('Giả lập') ? 'browser-demo' : 'unknown');
    if (source === 'hardware') lastHardwareTelemetryAt = Date.now();
    const verifyLabel = data.verified ? ' • đã xác minh chữ ký' : ' • CHƯA XÁC MINH (chưa nhập khóa)';
    sourceNote.textContent = source === 'hardware'
      ? `Dữ liệu cảm biến ESP32 • nhận lúc ${new Date().toLocaleTimeString('vi-VN')}${verifyLabel}`
      : source === 'browser-demo'
        ? 'ĐANG MÔ PHỎNG TRÊN TRÌNH DUYỆT — số liệu không đến từ cảm biến.'
        : source === 'browser-preview'
          ? 'Bản xem trước giao diện — chưa xác nhận số liệu từ ESP32.'
        : 'Dữ liệu mẫu/đang chờ ESP32 gửi telemetry; chưa xác nhận đây là số đo thật.';
    sourceNote.style.color = (source === 'hardware' && data.verified) ? '#10b981' : '#f59e0b';
  }

  let totalQty = 0;
  let totalWeight = 0;
  let alertShelves = 0;

  data.shelves.forEach(rawShelf => {
    const id = normalizeShelfId(String(rawShelf.id || ''));
    // Bỏ qua mã kệ lạ: mã kệ được dùng làm id phần tử và trong thuộc tính onclick
    if (!VALID_SHELF_ID.test(id)) return;
    const existing = shelfState[id] || {};

    const qty = rawShelf.qty !== undefined ? Number(rawShelf.qty) : (existing.qty || 0);
    const weight = rawShelf.weight !== undefined ? Number(rawShelf.weight) : (existing.weight || 0);
    const unit = rawShelf.unit ? Number(rawShelf.unit) : (existing.unit || 1);
    const min = rawShelf.min !== undefined ? Number(rawShelf.min) : (existing.min || 0);
    const low_stock = rawShelf.low_stock !== undefined ? Boolean(rawShelf.low_stock) : (qty <= min);
    const name = rawShelf.name || existing.name || `Sản phẩm ${id}`;
    const category = rawShelf.category || existing.category || 'Tạp hóa';

    shelfState[id] = {
      id: id,
      name: name,
      weight: weight,
      unit: unit,
      qty: qty,
      min: min,
      low_stock: low_stock,
      category: category
    };

    totalQty += qty;
    totalWeight += weight;
    if (low_stock) alertShelves++;

    // Update Card DOM Elements if card exists
    const cardEl = document.getElementById(`shelf-card-${id}`);
    const nameEl = document.getElementById(`name-${id}`);
    const weightEl = document.getElementById(`weight-${id}`);
    const qtyEl = document.getElementById(`qty-${id}`);
    const minEl = document.getElementById(`min-${id}`);
    const unitEl = document.getElementById(`unit-${id}`);
    const pillEl = document.getElementById(`pill-${id}`);
    const percentEl = document.getElementById(`percent-${id}`);
    const fillEl = document.getElementById(`fill-${id}`);

    if (nameEl) nameEl.textContent = name;
    if (weightEl) weightEl.innerHTML = `${weight} <small>g</small>`;
    if (qtyEl) qtyEl.innerHTML = `${qty} <small>món</small>`;
    if (minEl) minEl.innerHTML = `${min} <small>món</small>`;
    if (unitEl) unitEl.textContent = unit;

    if (pillEl) {
      if (low_stock) {
        pillEl.className = 'stock-pill low-stock';
        pillEl.textContent = 'THIẾU HÀNG!';
      } else {
        pillEl.className = 'stock-pill in-stock';
        pillEl.textContent = 'ĐỦ HÀNG';
      }
    }

    if (cardEl) {
      if (low_stock) cardEl.classList.add('low-stock-alert');
      else cardEl.classList.remove('low-stock-alert');
    }

    const maxCap = Math.max(min * 2, 8);
    const percent = Math.min(Math.round((qty / maxCap) * 100), 100);
    if (percentEl) percentEl.textContent = `${percent}%`;
    if (fillEl) {
      fillEl.style.width = `${percent}%`;
      if (low_stock) fillEl.classList.add('fill-alert');
      else fillEl.classList.remove('fill-alert');
    }

    if (saveToHistory) {
      addHistoryRecord(shelfState[id]);
    }
  });

  // Update KPI Metrics
  const metricQty = document.getElementById('metric-total-qty');
  const metricWeight = document.getElementById('metric-total-weight');
  const metricAlerts = document.getElementById('metric-total-alerts');
  const metricAlertStatus = document.getElementById('metric-alert-status');
  const metricUptime = document.getElementById('metric-uptime');
  const metricDevice = document.getElementById('metric-device-name');

  if (metricQty) metricQty.textContent = totalQty;
  if (metricWeight) metricWeight.textContent = totalWeight.toLocaleString();
  if (metricAlerts) metricAlerts.textContent = alertShelves;
  if (metricAlertStatus) {
    metricAlertStatus.textContent = alertShelves > 0 ? `Cần nạp thêm ${alertShelves} kệ!` : 'Kho hàng đầy đủ';
    metricAlertStatus.style.color = alertShelves > 0 ? '#ef4444' : '#10b981';
  }
  if (metricUptime) metricUptime.textContent = data.uptime || Math.floor(performance.now() / 1000);
  if (metricDevice) metricDevice.textContent = data.store ? `${data.store} (${data.device || 'Hub10'})` : (data.device || 'ESP32_SmartShelf');

  // Update Chart
  updateChartData(Object.values(shelfState));

  // Update Analytics
  generateAnalyticsInsights(Object.values(shelfState), alertShelves);

  // Update History Table
  if (saveToHistory) {
    renderHistoryTable();
  }
}

// ==============================================================================
// 7. DATA ANALYSIS & RECOMMENDATION
// ==============================================================================
function generateAnalyticsInsights(shelves, alertCount) {
  const analysisEl = document.getElementById('analysis-text');
  if (!analysisEl) return;

  if (alertCount === 0) {
    analysisEl.innerHTML = `
      <p style="color: #34d399;"><i class="fa-solid fa-circle-check"></i> <strong>Toàn bộ 10 kệ hàng tạp hóa đều đang ở mức an toàn.</strong></p>
      <p>Hệ thống hoạt động ổn định. Tải trọng phân bổ tốt. Không phát hiện mặt hàng nào dưới ngưỡng báo động tối thiểu.</p>
    `;
    return;
  }

  const lowList = shelves.filter(s => s.low_stock);
  const adviceItems = lowList.map(s => {
    const deficit = Math.max(s.min - s.qty, 1);
    return `<li><strong>Kệ ${s.id} [${escapeHtml(s.category)}] - ${escapeHtml(s.name)}</strong>: Hiện còn <strong>${s.qty} món</strong> (ngưỡng báo ${s.min}, cảnh báo khi không vượt quá ngưỡng). 👉 Cần nhập thêm tối thiểu <strong>+${deficit} món</strong>.</li>`;
  }).join('');

  analysisEl.innerHTML = `
    <p style="color: #f87171;"><i class="fa-solid fa-triangle-exclamation"></i> <strong>PHÁT HIỆN ${alertCount}/10 KỆ HÀNG CẦN NHẬP BỔ SUNG GẤP:</strong></p>
    <ul style="margin-left: 20px; margin-top: 6px; display: flex; flex-direction: column; gap: 4px;">
      ${adviceItems}
    </ul>
    <p style="margin-top: 8px; font-size: 13px; color: #94a3b8;">* Hệ thống tự động kích hoạt đèn LED tại chỗ và gửi thông báo khẩn qua Telegram Bot tới người quản lý kho.</p>
  `;
}

// ==============================================================================
// 8. REAL-TIME CHART.JS INITIALIZATION (10 SHELVES)
// ==============================================================================
function initChart() {
  const ctx = document.getElementById('realtimeChart');
  if (!ctx) return;

  const initialLabels = Array.from({ length: 8 }, (_, i) => `${(7 - i) * 3}s trước`);
  const shelves = Object.values(shelfState);

  const datasets = shelves.map((s, idx) => ({
    label: `${s.id}: ${s.name}`,
    data: Array(8).fill(currentChartMode === 'weight' ? s.weight : s.qty),
    borderColor: CHART_COLORS[idx % CHART_COLORS.length],
    backgroundColor: 'transparent',
    borderWidth: 2,
    tension: 0.35,
    fill: false,
    pointRadius: 2
  }));

  realtimeChart = new Chart(ctx, {
    type: 'line',
    data: {
      labels: initialLabels,
      datasets: datasets
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      interaction: {
        mode: 'index',
        intersect: false
      },
      plugins: {
        legend: {
          position: 'top',
          labels: {
            color: '#cbd5e1',
            font: { family: 'Outfit', size: 12 },
            boxWidth: 14
          }
        },
        tooltip: {
          backgroundColor: 'rgba(15, 23, 42, 0.9)',
          titleFont: { family: 'Outfit', weight: 'bold' },
          bodyFont: { family: 'JetBrains Mono' }
        }
      },
      scales: {
        x: {
          grid: { color: 'rgba(255, 255, 255, 0.05)' },
          ticks: { color: '#64748b', font: { family: 'JetBrains Mono', size: 11 } }
        },
        y: {
          grid: { color: 'rgba(255, 255, 255, 0.05)' },
          ticks: { color: '#64748b', font: { family: 'JetBrains Mono', size: 11 } }
        }
      }
    }
  });
}

function updateChartData(shelves) {
  if (!realtimeChart) return;

  const now = new Date();
  const timeLabel = now.toTimeString().split(' ')[0];

  realtimeChart.data.labels.push(timeLabel);
  if (realtimeChart.data.labels.length > MAX_CHART_POINTS) {
    realtimeChart.data.labels.shift();
  }

  shelves.forEach((shelf, idx) => {
    if (realtimeChart.data.datasets[idx]) {
      const val = currentChartMode === 'weight' ? Number(shelf.weight) : Number(shelf.qty);
      realtimeChart.data.datasets[idx].data.push(val);
      realtimeChart.data.datasets[idx].label = `${shelf.id}: ${shelf.name}`;

      if (realtimeChart.data.datasets[idx].data.length > MAX_CHART_POINTS) {
        realtimeChart.data.datasets[idx].data.shift();
      }
    }
  });

  realtimeChart.update('none');
}

// ==============================================================================
// 9. CONTROL BUTTONS & MODAL CONFIGURATION
// ==============================================================================
function renderControlButtons() {
  const tareContainer = document.getElementById('ctrl-tare-container');
  const configContainer = document.getElementById('ctrl-config-container');

  const shelves = Object.values(shelfState);

  if (tareContainer) {
    tareContainer.innerHTML = shelves.map(s => `
      <button class="btn-action" onclick="sendTareCmd('${s.id}')" style="font-size: 13px; padding: 7px 12px;">
        <i class="fa-solid fa-rotate-left"></i> Tare ${s.id} (${escapeHtml(s.name)})
      </button>
    `).join('');
  }

  if (configContainer) {
    configContainer.innerHTML = shelves.map(s => `
      <button class="btn-action" onclick="openConfigModal('${s.id}')" style="font-size: 13px; padding: 7px 12px;">
        <i class="fa-solid fa-pen-to-square"></i> Cấu hình ${s.id}
      </button>
    `).join('');
  }
}

function populateModalShelfOptions() {
  const select = document.getElementById('modal-shelf-select');
  if (!select) return;

  const shelves = Object.values(shelfState);
  select.innerHTML = shelves.map(s => `
    <option value="${s.id}">${s.id}: ${escapeHtml(s.name)} (${escapeHtml(s.unit)}g, min=${escapeHtml(s.min)})</option>
  `).join('');
}

function onModalShelfSelectChange(shelfId) {
  const s = shelfState[shelfId];
  if (!s) return;

  document.getElementById('modal-shelf-id').value = s.id;
  document.getElementById('modal-title').textContent = `Cài đặt thông số Kệ ${s.id}`;
  document.getElementById('modal-shelf-name').value = s.name;
  document.getElementById('modal-unit-weight').value = s.unit;
  document.getElementById('modal-min-qty').value = s.min;
}

function openConfigModal(shelfId) {
  const normId = normalizeShelfId(shelfId);
  const s = shelfState[normId];
  if (!s) return;

  const select = document.getElementById('modal-shelf-select');
  if (select) select.value = normId;

  document.getElementById('modal-shelf-id').value = normId;
  document.getElementById('modal-title').textContent = `Cài đặt thông số Kệ ${normId}`;
  document.getElementById('modal-shelf-name').value = s.name;
  document.getElementById('modal-unit-weight').value = s.unit;
  document.getElementById('modal-min-qty').value = s.min;

  document.getElementById('config-modal')?.classList.add('open');
}

function closeConfigModal() {
  document.getElementById('config-modal')?.classList.remove('open');
}

function handleSaveConfig(e) {
  e.preventDefault();
  const id = document.getElementById('modal-shelf-id').value;
  const name = document.getElementById('modal-shelf-name').value.trim();
  const unit = parseFloat(document.getElementById('modal-unit-weight').value);
  const min = parseInt(document.getElementById('modal-min-qty').value, 10);

  if (!name || isNaN(unit) || isNaN(min)) {
    showToast('Vui lòng nhập đầy đủ và chính xác thông số!', 'error');
    return;
  }

  publishCommand({
    cmd: 'set',
    id: id,
    name: name,
    unit: unit,
    min: min
  });

  closeConfigModal();
}

function sendTareCmd(shelfId) {
  publishCommand({
    cmd: 'tare',
    id: shelfId
  });
}

function sendSyncCmd() {
  publishCommand({
    cmd: 'status'
  });
}

// ==============================================================================
// 10. DEMO SIMULATION (10 SHELVES GROCERY ACTIVITY)
// ==============================================================================
function toggleDemoMode() {
  isDemoMode = !isDemoMode;
  const btn = document.getElementById('btn-toggle-demo');
  const btnText = document.getElementById('demo-btn-text');

  if (isDemoMode) {
    if (btn) btn.classList.add('active-demo');
    if (btnText) btnText.textContent = 'Tắt Giả lập';
    showToast('Đã BẬT mô phỏng giao dịch mua sắm 10 kệ tạp hóa!', 'info');

    demoInterval = setInterval(() => {
      const dummyUptime = Math.floor(performance.now() / 1000);
      const simulatedShelves = Object.values(shelfState).map(s => {
        let qty = s.qty;
        // 35% chance to simulate purchase or restock
        if (Math.random() < 0.35) {
          const delta = Math.random() > 0.4 ? -1 : 1;
          qty = Math.max(0, Math.min(qty + delta, 12));
        }
        const weight = Math.round(qty * s.unit);
        const low_stock = qty <= s.min;
        return {
          ...s,
          qty: qty,
          weight: weight,
          low_stock: low_stock
        };
      });

      const simulatedAlerts = simulatedShelves.filter(s => s.low_stock).length;

      updateUIWithTelemetry({
        device: 'ESP32_SmartShelf_10Tiers (Giả lập)',
        source: 'browser-demo',
        store: 'TapHoaSmart_Q7',
        uptime: dummyUptime,
        shelves: simulatedShelves,
        total_alerts: simulatedAlerts
      }, true);
    }, 2800);
  } else {
    if (btn) btn.classList.remove('active-demo');
    if (btnText) btnText.textContent = 'Bật Giả lập (Demo)';
    clearInterval(demoInterval);
    const sourceNote = document.getElementById('telemetry-source-note');
    if (sourceNote) {
      sourceNote.textContent = 'Đã dừng mô phỏng trình duyệt; đang chờ telemetry mới từ ESP32.';
      sourceNote.style.color = '#f59e0b';
    }
    if (mqttClient && mqttClient.connected) sendSyncCmd();
    showToast('Đã TẮT chế độ Mô phỏng.', 'info');
  }
}

// ==============================================================================
// 11. DATA MANAGEMENT & LOGS
// ==============================================================================
function addHistoryRecord(shelf) {
  const record = {
    time: new Date().toLocaleTimeString('vi-VN'),
    date: new Date().toLocaleDateString('vi-VN'),
    id: shelf.id,
    name: shelf.name,
    category: shelf.category || 'Tạp hóa',
    weight: shelf.weight,
    unit: shelf.unit,
    qty: shelf.qty,
    min: shelf.min,
    low_stock: shelf.low_stock
  };

  historicalLogs.unshift(record);
  if (historicalLogs.length > MAX_LOGS) {
    historicalLogs.pop();
  }
  saveLogsToStorage();
}

function saveLogsToStorage() {
  try {
    localStorage.setItem('taphoa_smartshelf_logs', JSON.stringify(historicalLogs));
  } catch (e) {
    console.warn('Storage quota exceeded');
  }
}

function loadLogsFromStorage() {
  try {
    const raw = localStorage.getItem('taphoa_smartshelf_logs');
    if (raw) historicalLogs = JSON.parse(raw);
  } catch (e) {
    historicalLogs = [];
  }
}

function renderHistoryTable() {
  const tbody = document.getElementById('history-table-body');
  if (!tbody) return;

  const shelfFilter = document.getElementById('filter-shelf')?.value || 'ALL';
  const statusFilter = document.getElementById('filter-status')?.value || 'ALL';
  const searchFilter = document.getElementById('filter-search')?.value.toLowerCase().trim() || '';

  const filtered = historicalLogs.filter(row => {
    if (shelfFilter !== 'ALL' && row.id !== shelfFilter) return false;
    if (statusFilter === 'ALERT' && !row.low_stock) return false;
    if (statusFilter === 'NORMAL' && row.low_stock) return false;
    if (searchFilter && !row.name.toLowerCase().includes(searchFilter)) return false;
    return true;
  });

  if (filtered.length === 0) {
    tbody.innerHTML = `<tr><td colspan="8" class="text-center empty-state">Không có bản ghi phù hợp với bộ lọc hiện tại.</td></tr>`;
    return;
  }

  tbody.innerHTML = filtered.slice(0, 35).map(row => `
    <tr>
      <td style="font-family: var(--font-mono); color: var(--text-dim);">${escapeHtml(row.date)} ${escapeHtml(row.time)}</td>
      <td><span class="shelf-badge">${escapeHtml(row.id)}</span></td>
      <td><strong>${escapeHtml(row.name)}</strong> <small style="color:var(--text-dim)">(${escapeHtml(row.category)})</small></td>
      <td style="font-family: var(--font-mono);">${escapeHtml(row.weight)}g</td>
      <td style="font-family: var(--font-mono);">${escapeHtml(row.unit)}g</td>
      <td style="font-family: var(--font-mono); font-weight: bold;">${escapeHtml(row.qty)}</td>
      <td style="font-family: var(--font-mono);">${escapeHtml(row.min)}</td>
      <td>
        ${row.low_stock 
          ? '<span class="stock-pill low-stock" style="font-size: 10px; padding: 2px 8px;">THIẾU HÀNG</span>' 
          : '<span class="stock-pill in-stock" style="font-size: 10px; padding: 2px 8px;">ĐỦ HÀNG</span>'}
      </td>
    </tr>
  `).join('');
}

function exportToCSV() {
  if (historicalLogs.length === 0) {
    showToast('Chưa có dữ liệu để xuất file!', 'error');
    return;
  }

  let csvContent = "\uFEFF"; // UTF-8 BOM
  csvContent += "Ngày,Giờ,Mã Kệ,Phân Loại,Tên Sản Phẩm,Khối Lượng (g),Đơn Vị (g),Số Lượng,Ngưỡng Báo Động,Trạng Thái\n";

  historicalLogs.forEach(r => {
    const status = r.low_stock ? "Thieu hang" : "Du hang";
    csvContent += `"${r.date}","${r.time}","${r.id}","${r.category}","${r.name}",${r.weight},${r.unit},${r.qty},${r.min},"${status}"\n`;
  });

  const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.setAttribute('href', url);
  link.setAttribute('download', `TapHoa_KiemKe_${new Date().toISOString().slice(0, 10)}.csv`);
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
  showToast('Đã xuất báo cáo kiểm kê 10 mặt hàng thành công!', 'success');
}

// ==============================================================================
// 12. UI INTERACTIONS & EVENT LISTENERS
// ==============================================================================
function initNavTabs() {
  const tabBtns = document.querySelectorAll('.tab-btn');
  const tabPanes = document.querySelectorAll('.tab-pane');

  tabBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      tabBtns.forEach(b => b.classList.remove('active'));
      tabPanes.forEach(p => p.classList.remove('active'));

      btn.classList.add('active');
      const targetId = btn.getAttribute('data-tab');
      const targetPane = document.getElementById(targetId);
      if (targetPane) {
        targetPane.classList.add('active');
        if (targetId === 'tab-charts' && realtimeChart) {
          realtimeChart.resize();
        }
      }
    });
  });
}

function setupEventListeners() {
  document.getElementById('btn-sync-telemetry')?.addEventListener('click', sendSyncCmd);
  document.getElementById('btn-toggle-demo')?.addEventListener('click', toggleDemoMode);

  const btnWeight = document.getElementById('btn-chart-weight');
  const btnQty = document.getElementById('btn-chart-qty');

  btnWeight?.addEventListener('click', () => {
    currentChartMode = 'weight';
    btnWeight.classList.add('active');
    btnQty.classList.remove('active');
    if (realtimeChart) {
      realtimeChart.options.scales.y.title = { display: true, text: 'Khối lượng (gram)' };
      realtimeChart.update();
    }
  });

  btnQty?.addEventListener('click', () => {
    currentChartMode = 'qty';
    btnQty.classList.add('active');
    btnWeight.classList.remove('active');
    if (realtimeChart) {
      realtimeChart.options.scales.y.title = { display: true, text: 'Số lượng (món)' };
      realtimeChart.update();
    }
  });

  document.getElementById('filter-shelf')?.addEventListener('change', renderHistoryTable);
  document.getElementById('filter-status')?.addEventListener('change', renderHistoryTable);
  document.getElementById('filter-search')?.addEventListener('input', renderHistoryTable);

  document.getElementById('btn-export-csv')?.addEventListener('click', exportToCSV);
  document.getElementById('btn-clear-history')?.addEventListener('click', () => {
    if (confirm('Bạn có chắc chắn muốn xóa toàn bộ lịch sử đo?')) {
      historicalLogs = [];
      saveLogsToStorage();
      renderHistoryTable();
      showToast('Đã xóa sạch nhật ký đo!', 'info');
    }
  });

  document.getElementById('config-modal')?.addEventListener('click', (e) => {
    if (e.target.id === 'config-modal') {
      closeConfigModal();
    }
  });
}

// Toast notification helper
function showToast(message, type = 'info') {
  const container = document.getElementById('toast-container');
  if (!container) return;

  const toast = document.createElement('div');
  toast.className = `toast ${type}`;

  const iconMap = {
    success: 'fa-circle-check',
    error: 'fa-circle-exclamation',
    info: 'fa-circle-info'
  };

  // Thông báo có thể chứa tên sản phẩm nhận từ MQTT, nên gán bằng textContent
  const icon = document.createElement('i');
  icon.className = `fa-solid ${iconMap[type] || 'fa-bell'}`;
  const text = document.createElement('span');
  text.textContent = message;
  toast.append(icon, text);

  container.appendChild(toast);

  setTimeout(() => {
    toast.style.opacity = '0';
    toast.style.transform = 'translateX(100%)';
    toast.style.transition = 'all 0.3s ease';
    setTimeout(() => toast.remove(), 300);
  }, 3200);
}
