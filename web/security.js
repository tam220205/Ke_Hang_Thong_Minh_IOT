/**
 * Bảo mật dashboard (OWASP IoT I2/I3)
 * - Ký lệnh gửi xuống ESP32 bằng HMAC-SHA256, kèm ts (ms) và nonce để chống phát lại.
 * - Xác minh chữ ký và độ mới của telemetry; bản tin giả hoặc cũ bị loại.
 * - Khóa ký và tài khoản MQTT chỉ giữ trong sessionStorage của tab, không ghi vào mã nguồn.
 *
 * Định dạng bản tin đã ký (giống firmware và Node-RED):
 *   S = JSON gốc (không khoảng trắng thừa, kết thúc bằng '}')
 *   bản tin = S bỏ '}' cuối + ,"sig":"<hex HMAC-SHA256(khóa, S)>"}
 */
const SEC_STORE_KEY = 'smartshelf.security';
const DEFAULT_BROKER_URL = 'wss://broker.hivemq.com:8884/mqtt';
const SIG_SUFFIX_RE = /,"sig":"([0-9a-f]{64})"\}$/;
const TELEMETRY_MAX_AGE_SEC = 300;
const MIN_KEY_LENGTH = 16;

let securitySettings = loadSecuritySettings();
let hmacCryptoKey = null;
let rejectedTelemetryCount = 0;
let lastRejectToastAt = 0;

function loadSecuritySettings() {
  try {
    return JSON.parse(sessionStorage.getItem(SEC_STORE_KEY)) || {};
  } catch (e) {
    return {};
  }
}

function saveSecuritySettings(settings) {
  try {
    sessionStorage.setItem(SEC_STORE_KEY, JSON.stringify(settings));
  } catch (e) {
    console.warn('[SECURITY] Không lưu được cấu hình vào sessionStorage', e);
  }
}

function getBrokerUrl() {
  return securitySettings.brokerUrl || DEFAULT_BROKER_URL;
}

function getMqttCredentials() {
  const { mqttUser, mqttPass } = securitySettings;
  return mqttUser ? { username: mqttUser, password: mqttPass || '' } : {};
}

function hasSigningKey() {
  return hmacCryptoKey !== null;
}

async function initSigningKey() {
  hmacCryptoKey = null;
  const secret = securitySettings.hmacKey || '';
  if (secret.length < MIN_KEY_LENGTH) return false;
  if (!window.crypto || !crypto.subtle) {
    showToast('Trình duyệt không hỗ trợ WebCrypto (cần HTTPS hoặc localhost).', 'error');
    return false;
  }
  hmacCryptoKey = await crypto.subtle.importKey(
    'raw', new TextEncoder().encode(secret),
    { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']
  );
  return true;
}

async function hmacHex(text) {
  const mac = await crypto.subtle.sign('HMAC', hmacCryptoKey, new TextEncoder().encode(text));
  return Array.from(new Uint8Array(mac), b => b.toString(16).padStart(2, '0')).join('');
}

function timingSafeEqualHex(a, b) {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

function randomNonce() {
  const bytes = crypto.getRandomValues(new Uint8Array(8));
  return Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');
}

// Trả về chuỗi bản tin đã ký, sẵn sàng publish.
async function signCommand(cmdObj) {
  const body = JSON.stringify({ ...cmdObj, ts: Date.now(), nonce: randomNonce() });
  const sig = await hmacHex(body);
  return body.slice(0, -1) + ',"sig":"' + sig + '"}';
}

// Trả về { ok, data, reason }. Chỉ data của bản tin hợp lệ mới được đưa lên giao diện.
async function verifyTelemetry(raw) {
  const match = SIG_SUFFIX_RE.exec(raw);
  if (!match) return { ok: false, reason: 'thiếu chữ ký' };
  const body = raw.slice(0, match.index) + '}';
  const expected = await hmacHex(body);
  if (!timingSafeEqualHex(expected, match[1])) return { ok: false, reason: 'chữ ký sai' };

  const data = JSON.parse(body);
  const ageSec = Math.abs(Date.now() / 1000 - Number(data.ts || 0));
  if (!data.ts || ageSec > TELEMETRY_MAX_AGE_SEC) {
    return { ok: false, reason: 'bản tin cũ hoặc bị phát lại (ts không hợp lệ)' };
  }
  return { ok: true, data };
}

function reportRejectedTelemetry(reason) {
  rejectedTelemetryCount++;
  console.warn('[SECURITY] Loại telemetry:', reason);
  const counter = document.getElementById('sec-rejected-count');
  if (counter) counter.textContent = String(rejectedTelemetryCount);
  if (Date.now() - lastRejectToastAt > 30000) {
    lastRejectToastAt = Date.now();
    showToast(`Đã loại bản tin telemetry không hợp lệ (${reason}).`, 'error');
  }
}

function updateSecurityBadge() {
  const badge = document.getElementById('security-badge-text');
  if (!badge) return;
  badge.textContent = hasSigningKey() ? 'Đã xác thực' : 'Chưa nhập khóa';
  document.getElementById('btn-security')?.classList.toggle('sec-ready', hasSigningKey());
}

function openSecurityModal() {
  document.getElementById('sec-broker-url').value = getBrokerUrl();
  document.getElementById('sec-mqtt-user').value = securitySettings.mqttUser || '';
  document.getElementById('sec-mqtt-pass').value = '';
  document.getElementById('sec-hmac-key').value = '';
  document.getElementById('security-modal')?.classList.add('open');
}

function closeSecurityModal() {
  document.getElementById('security-modal')?.classList.remove('open');
}

async function handleSaveSecurity(e) {
  e.preventDefault();
  const brokerUrl = document.getElementById('sec-broker-url').value.trim() || DEFAULT_BROKER_URL;
  const mqttUser = document.getElementById('sec-mqtt-user').value.trim();
  const mqttPass = document.getElementById('sec-mqtt-pass').value;
  const hmacKey = document.getElementById('sec-hmac-key').value;

  // Chỉ cho ws:// (không mã hóa) với broker nội bộ trên chính máy này.
  const isLocalWs = /^ws:\/\/(localhost|127\.0\.0\.1)(:\d+)?\//.test(brokerUrl);
  if (!/^wss:\/\//.test(brokerUrl) && !isLocalWs) {
    showToast('Broker phải dùng WebSocket có TLS (wss://); ws:// chỉ dùng cho localhost.', 'error');
    return;
  }
  if (hmacKey && hmacKey.length < MIN_KEY_LENGTH) {
    showToast(`Khóa ký cần ít nhất ${MIN_KEY_LENGTH} ký tự.`, 'error');
    return;
  }

  const brokerChanged = brokerUrl !== getBrokerUrl() || mqttUser !== (securitySettings.mqttUser || '') || mqttPass;
  securitySettings = {
    brokerUrl,
    mqttUser,
    // Để trống ô mật khẩu/khóa nghĩa là giữ giá trị cũ.
    mqttPass: mqttPass || securitySettings.mqttPass || '',
    hmacKey: hmacKey || securitySettings.hmacKey || ''
  };
  saveSecuritySettings(securitySettings);
  await initSigningKey();
  updateSecurityBadge();
  closeSecurityModal();
  showToast(hasSigningKey() ? 'Đã lưu khóa bảo mật cho phiên này.' : 'Đã lưu cấu hình broker.', 'success');

  if (brokerChanged && typeof reconnectMQTT === 'function') reconnectMQTT();
}

function clearSecuritySettings() {
  securitySettings = {};
  hmacCryptoKey = null;
  try { sessionStorage.removeItem(SEC_STORE_KEY); } catch (e) { /* bỏ qua */ }
  updateSecurityBadge();
  closeSecurityModal();
  showToast('Đã xóa khóa và tài khoản MQTT khỏi phiên này.', 'info');
  if (typeof reconnectMQTT === 'function') reconnectMQTT();
}
