const csrf = document.body.dataset.csrf;
const input = document.getElementById('message');
const count = document.getElementById('char-count');
const preview = document.getElementById('preview');
const sendButton = document.getElementById('send');
const actionMessage = document.getElementById('action-message');
let previewTimer;
let previewController;
let previewUrl;
let pairCodeActive = false;
let pairCodeVisible = false;
let pairExpiryMs = 0;

function hidePairCode() {
  document.getElementById('pair-code-result').hidden = true;
  document.getElementById('pair-code').textContent = '';
  pairCodeVisible = false;
  pairExpiryMs = 0;
}

function updatePairCountdown() {
  if (!pairCodeVisible) return;
  const remaining = Math.max(0, Math.ceil((pairExpiryMs - Date.now()) / 1000));
  document.getElementById('pair-countdown').textContent =
    `${String(Math.floor(remaining / 60)).padStart(2, '0')}:${String(remaining % 60).padStart(2, '0')}`;
  if (!remaining) {
    hidePairCode();
    document.getElementById('pair-status').textContent = '配对码已过期，请重新生成。';
  }
}

function showAction(message, error = false) {
  actionMessage.textContent = message;
  actionMessage.classList.toggle('error', error);
}

function characterCount() { return Array.from(input.value).length; }

function validateInput() {
  const length = characterCount();
  count.textContent = `${length} / 80`;
  count.classList.toggle('error', length > 80);
  sendButton.disabled = !input.value.trim() || length > 80;
  if (length > 80) showAction('最多 80 个字符', true);
  return !sendButton.disabled;
}

async function fetchError(response) {
  try { const body = await response.json(); return body.error || `请求失败 (${response.status})`; }
  catch { return `请求失败 (${response.status})`; }
}

async function updatePreview() {
  if (!validateInput()) return;
  if (previewController) previewController.abort();
  previewController = new AbortController();
  try {
    const response = await fetch('/api/owner/preview', {
      method: 'POST', headers: {'Content-Type':'application/json', 'X-CSRF-Token':csrf},
      body: JSON.stringify({text:input.value}), signal:previewController.signal
    });
    if (!response.ok) throw new Error(await fetchError(response));
    const blob = await response.blob();
    const nextUrl = URL.createObjectURL(blob);
    preview.src = nextUrl;
    if (previewUrl) URL.revokeObjectURL(previewUrl);
    previewUrl = nextUrl;
    if (actionMessage.classList.contains('error')) showAction('');
  } catch (error) {
    if (error.name !== 'AbortError') showAction(error.message, true);
  }
}

function fmt(iso) { return iso ? new Date(iso).toLocaleString('zh-CN', {hour12:false}) : '—'; }

function renderStatus(status) {
  const connection = document.getElementById('connection');
  connection.className = `status-value ${status.deviceOnline ? 'online' : 'offline'}`;
  connection.innerHTML = '<i class="status-dot"></i>';
  connection.append(document.createTextNode(status.deviceOnline ? '已连接' : '离线'));
  const delivery = document.getElementById('delivery');
  if (!status.revision) delivery.textContent = '尚未发送';
  else if (status.lastReceivedRevision === status.revision) delivery.textContent = `第 ${status.revision} 条 · 已收到`;
  else delivery.textContent = `第 ${status.revision} 条 · 等待接收`;
  document.getElementById('last-seen').textContent = fmt(status.lastSeen);
  document.getElementById('rotate-key').textContent = status.tokenConfigured ? '重新生成密钥' : '生成设备密钥';
  pairCodeActive = status.pairCodeActive;
  const pairStatus = document.getElementById('pair-status');
  if (pairCodeActive) {
    pairStatus.textContent = pairCodeVisible ? '等待桌宠输入配对码…' : '已有有效配对码；若没有记下，请重新生成。';
  } else {
    if (pairCodeVisible) hidePairCode();
    pairStatus.textContent = status.tokenConfigured ? '当前没有待输入的配对码，可按需生成。' : '尚未配对，请生成配对码。';
  }
}

async function refreshStatus() {
  try {
    const response = await fetch('/api/owner/status', {cache:'no-store'});
    if (!response.ok) throw new Error(await fetchError(response));
    renderStatus(await response.json());
  } catch { document.getElementById('connection').textContent = '状态暂不可用'; }
}

input.addEventListener('input', () => {
  validateInput();
  clearTimeout(previewTimer);
  previewTimer = setTimeout(updatePreview, 280);
});

sendButton.addEventListener('click', async () => {
  if (!validateInput()) return;
  sendButton.disabled = true;
  showAction('发送中…');
  try {
    const response = await fetch('/api/owner/send', {
      method:'POST', headers:{'Content-Type':'application/json', 'X-CSRF-Token':csrf},
      body:JSON.stringify({text:input.value})
    });
    if (!response.ok) throw new Error(await fetchError(response));
    const result = await response.json();
    showAction(`第 ${result.revision} 条已保存到网站，等待桌宠接收`);
    await refreshStatus();
  } catch(error) { showAction(error.message, true); }
  finally { validateInput(); }
});

document.getElementById('rotate-key').addEventListener('click', async () => {
  if (!window.confirm('重新生成会让旧设备密钥立即失效。继续吗？')) return;
  try {
    const response = await fetch('/api/owner/device-key', {method:'POST', headers:{'X-CSRF-Token':csrf}});
    if (!response.ok) throw new Error(await fetchError(response));
    const result = await response.json();
    document.getElementById('device-token').textContent = result.deviceToken;
    document.getElementById('key-result').hidden = false;
    hidePairCode();
    await refreshStatus();
  } catch(error) { showAction(error.message, true); }
});

document.getElementById('make-pair-code').addEventListener('click', async () => {
  if (pairCodeActive && !window.confirm('重新生成会立即使上一组配对码失效。继续吗？')) return;
  const button = document.getElementById('make-pair-code');
  button.disabled = true;
  try {
    const response = await fetch('/api/owner/pair-code', {method:'POST', headers:{'X-CSRF-Token':csrf}});
    if (!response.ok) throw new Error(await fetchError(response));
    const result = await response.json();
    document.getElementById('pair-code').textContent = result.code;
    document.getElementById('pair-code-result').hidden = false;
    document.getElementById('key-result').hidden = true;
    document.getElementById('device-token').textContent = '';
    pairCodeVisible = true;
    pairExpiryMs = Date.now() + result.expiresInSeconds * 1000;
    updatePairCountdown();
    await refreshStatus();
  } catch(error) { document.getElementById('pair-status').textContent = error.message; }
  finally { button.disabled = false; }
});

document.getElementById('copy-key').addEventListener('click', async () => {
  try {
    await navigator.clipboard.writeText(document.getElementById('device-token').textContent);
    document.getElementById('copy-key').textContent = '已复制';
  } catch { document.getElementById('copy-key').textContent = '请手动复制'; }
});

validateInput();
refreshStatus();
setInterval(refreshStatus, 3000);
setInterval(updatePairCountdown, 1000);
