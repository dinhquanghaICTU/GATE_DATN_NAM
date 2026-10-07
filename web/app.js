/**
 * Smart HVAC & Gateway Control Center
 * MQTT WebSocket Client & UI Controller
 */

// ── Cấu hình mặc định (kết nối HiveMQ Cloud) ──────────────────────────────
const DEFAULT_CONFIG = {
  brokerUrl: "wss://20476a36ce36478d90de6d5676587638.s1.eu.hivemq.cloud:8884/mqtt",
  username: "quanghaictu",
  password: "Zdinhquangha1234",
  topicCmd: "gate/command",
  topicStatus: "gate/status",
  topicTelemetry: "gate/telemetry"
};

// ── State của ứng dụng ────────────────────────────────────────────────────
let client = null;
let lastHeartbeatTime = 0;
let isGatewayOnline = false;

// Trạng thái thiết bị điều hòa Funiki
const acState = {
  id: "3992007",
  name: "Điều hòa FUNIKI",
  power: 0,
  temp: 26,
  mode: 1, // 1: Cool, 2: Heat, 3: Dry, 4: Fan, 0: Auto
  fan: 0   // 0: Auto, 1: Low, 3: Med, 5: High
};

// Trạng thái Relay
const relayState = {
  ch1: 0,
  ch2: 0
};

// ── DOM Elements ──────────────────────────────────────────────────────────
const mqttBadge = document.getElementById("mqtt-status-badge");
const mqttText = document.getElementById("mqtt-status-text");

const gwPill = document.getElementById("gw-pill");
const gwOnlineLabel = document.getElementById("gw-online-label");
const gwUptime = document.getElementById("gw-uptime");
const gwHeap = document.getElementById("gw-heap");
const gwRssi = document.getElementById("gw-rssi");
const gwPackets = document.getElementById("gw-packets");
const gwHunonicWs = document.getElementById("gw-hunonic-ws");
const gwFw = document.getElementById("gw-fw");

// AC Elements
const acCard = document.querySelector(".ac-card");
const acNameDisplay = document.getElementById("ac-name-display");
const acIdDisplay = document.getElementById("ac-id-display");
const btnAcPower = document.getElementById("btn-ac-power");
const acTempVal = document.getElementById("ac-temp-val");
const acPowerLabel = document.getElementById("ac-power-label");
const btnTempDown = document.getElementById("btn-temp-down");
const btnTempUp = document.getElementById("btn-temp-up");
const modeBtns = document.querySelectorAll(".mode-btn");
const fanBtns = document.querySelectorAll(".fan-btn");

// Relays
const switchRelay1 = document.getElementById("switch-relay-1");
const switchRelay2 = document.getElementById("switch-relay-2");
const relayCard1 = document.getElementById("relay-card-1");
const relayCard2 = document.getElementById("relay-card-2");

// Sensors
const sensorTemp = document.getElementById("sensor-temp");
const sensorHum = document.getElementById("sensor-hum");
const sensorBatt = document.getElementById("sensor-batt");
const sensorRssi = document.getElementById("sensor-rssi");
const nodeStatusBadge = document.getElementById("node-status-badge");

// Console & Toast
const consoleLogs = document.getElementById("console-logs");
const btnClearLog = document.getElementById("btn-clear-log");
const appToast = document.getElementById("app-toast");

// Settings Modal
const btnToggleSettings = document.getElementById("btn-toggle-settings");
const modalSettings = document.getElementById("modal-settings");
const btnCloseSettings = document.getElementById("btn-close-settings");
const btnSaveSettings = document.getElementById("btn-save-settings");
const btnResetSettings = document.getElementById("btn-reset-settings");
const btnSyncDevices = document.getElementById("btn-sync-devices");
const btnRebootGate = document.getElementById("btn-reboot-gate");

// ── Hàm ghi log ra Console UI ─────────────────────────────────────────────
function addLog(text, type = "system") {
  const now = new Date();
  const timeStr = now.toTimeString().split(" ")[0] + "." + String(now.getMilliseconds()).padStart(3, "0");
  const div = document.createElement("div");
  div.className = `log-entry log-${type}`;
  div.innerText = `[${timeStr}] ${text}`;
  consoleLogs.appendChild(div);
  consoleLogs.scrollTop = consoleLogs.scrollHeight;
}

function showToast(msg, duration = 3000) {
  appToast.innerText = msg;
  appToast.classList.remove("hidden");
  setTimeout(() => {
    appToast.classList.add("hidden");
  }, duration);
}

// ── Kết nối MQTT qua WebSocket ────────────────────────────────────────────
function initMQTT() {
  const broker = localStorage.getItem("cfg_broker") || DEFAULT_CONFIG.brokerUrl;
  const username = localStorage.getItem("cfg_user") || DEFAULT_CONFIG.username;
  const password = localStorage.getItem("cfg_pass") || DEFAULT_CONFIG.password;

  addLog(`Đang kết nối tới MQTT Broker HiveMQ Cloud...`, "system");
  mqttBadge.className = "status-badge";
  mqttText.innerText = "Đang kết nối...";

  const clientId = "web_dashboard_" + Math.random().toString(16).substring(2, 10);

  const options = {
    clean: true,
    connectTimeout: 8000,
    clientId: clientId,
    username: username,
    password: password
  };

  try {
    if (client) {
      client.end(true);
    }

    client = mqtt.connect(broker, options);

    client.on("connect", () => {
      addLog(`Kết nối HiveMQ Cloud thành công! (ClientId: ${clientId})`, "system");
      mqttBadge.className = "status-badge connected";
      mqttText.innerText = "Cloud Online";
      showToast("Đã kết nối với HiveMQ Cloud Broker!");

      const topicStatus = localStorage.getItem("cfg_topic_status") || DEFAULT_CONFIG.topicStatus;
      const topicTele = localStorage.getItem("cfg_topic_tele") || DEFAULT_CONFIG.topicTelemetry;

      client.subscribe([topicStatus, topicTele], (err) => {
        if (!err) {
          addLog(`Đã đăng ký nhận tin từ topic: ${topicStatus}, ${topicTele}`, "system");
          // Gửi lệnh yêu cầu Gateway báo cáo danh sách thiết bị
          syncDevices();
        } else {
          addLog(`Lỗi subscribe topic: ${err}`, "err");
        }
      });
    });

    client.on("error", (err) => {
      addLog(`Lỗi MQTT: ${err.message}`, "err");
      mqttBadge.className = "status-badge disconnected";
      mqttText.innerText = "Lỗi kết nối";
    });

    client.on("close", () => {
      mqttBadge.className = "status-badge disconnected";
      mqttText.innerText = "Mất kết nối";
      markGatewayOffline();
    });

    client.on("message", (topic, payload) => {
      handleIncomingMessage(topic, payload.toString());
    });

  } catch (e) {
    addLog(`Ngoại lệ khi khởi tạo MQTT: ${e.message}`, "err");
  }
}

// ── Xử lý bản tin nhận từ Gateway ─────────────────────────────────────────
function handleIncomingMessage(topic, payloadStr) {
  addLog(`← [${topic}]: ${payloadStr}`, "in");

  try {
    const data = JSON.parse(payloadStr);

    // 1. Bản tin Heartbeat định kỳ của Gateway
    if (data.event === "heartbeat") {
      markGatewayOnline();
      lastHeartbeatTime = Date.now();

      if (data.uptime_sec !== undefined) {
        gwUptime.innerText = formatUptime(data.uptime_sec);
      }
      if (data.free_heap !== undefined) {
        gwHeap.innerText = (data.free_heap / 1024).toFixed(1) + " KB";
      }
      if (data.rssi !== undefined) {
        gwRssi.innerText = `${data.rssi} dBm`;
      }
      if (data.total_node_pkts !== undefined) {
        gwPackets.innerText = data.total_node_pkts;
      }
      if (data.ws_connected !== undefined) {
        gwHunonicWs.innerText = data.ws_connected ? "Đã kết nối" : "Chưa kết nối";
        gwHunonicWs.className = data.ws_connected ? "metric-value text-emerald" : "metric-value text-dim";
      }
      if (data.fw_ver) {
        gwFw.innerText = "v" + data.fw_ver;
      }

      // Cập nhật Relay states
      if (data.relay1 !== undefined) updateRelayUI(1, data.relay1);
      if (data.relay2 !== undefined) updateRelayUI(2, data.relay2);

      // Cập nhật AC
      if (data.ac_id) acState.id = data.ac_id;
      if (data.ac_name) acState.name = data.ac_name;
      if (data.ac_power !== undefined) acState.power = data.ac_power;
      if (data.ac_temp !== undefined) acState.temp = data.ac_temp;
      updateAcUI();
    }

    // 2. Danh sách thiết bị (Phản hồi khi ấn Đồng bộ)
    else if (data.event === "device_list") {
      markGatewayOnline();
      showToast("Đã đồng bộ thông tin thiết bị từ Gateway!");
      if (data.relay1 !== undefined) updateRelayUI(1, data.relay1);
      if (data.relay2 !== undefined) updateRelayUI(2, data.relay2);

      if (data.ac) {
        acState.id = data.ac.id;
        acState.name = data.ac.name;
        acState.power = data.ac.power;
        acState.temp = data.ac.temp;
        acState.mode = data.ac.mode;
        acState.fan = data.ac.fan;
        updateAcUI();
      }
    }

    // 3. Phản hồi trạng thái Điều Hòa Funiki (từ Hunonic WebSocket)
    else if (data.event === "ac_state") {
      markGatewayOnline();
      if (data.power !== undefined) acState.power = data.power;
      if (data.temp !== undefined && data.temp > 0) acState.temp = data.temp;
      if (data.mode !== undefined) acState.mode = data.mode;
      if (data.fan !== undefined) acState.fan = data.fan;
      updateAcUI();
      showToast(`Đã nhận phản hồi từ Điều hòa: ${acState.power ? "BẬT" : "TẮT"} (${acState.temp}°C)`);
    }

    // 4. Phản hồi Relay
    else if (data.event === "relay_state") {
      updateRelayUI(data.ch, data.state);
      showToast(`Relay ${data.ch}: ${data.state ? "BẬT" : "TẮT"}`);
    }

    // 5. Bản tin cảm biến từ ESP-NOW Sensor Node (gate/telemetry)
    else if (data.node_id !== undefined || topic.includes("telemetry")) {
      markGatewayOnline();
      nodeStatusBadge.innerText = `Node #${data.node_id || 1} Online`;
      nodeStatusBadge.className = "node-badge text-emerald";

      if (data.val1 !== undefined) sensorTemp.innerText = Number(data.val1).toFixed(1);
      if (data.val2 !== undefined) sensorHum.innerText = Number(data.val2).toFixed(1);
      if (data.battery !== undefined) sensorBatt.innerText = Number(data.battery).toFixed(2);
      if (data.rssi !== undefined) sensorRssi.innerText = data.rssi;
    }

  } catch (e) {
    // Không phải JSON
  }
}

// ── Hàm gửi lệnh tới Gateway qua MQTT ──────────────────────────────────────
function sendGateCommand(cmdObj) {
  if (!client || !client.connected) {
    showToast("Không thể gửi lệnh: Chưa kết nối MQTT Broker!", 4000);
    return;
  }

  const topicCmd = localStorage.getItem("cfg_topic_cmd") || DEFAULT_CONFIG.topicCmd;
  const payloadStr = JSON.stringify(cmdObj);

  client.publish(topicCmd, payloadStr, { qos: 1 }, (err) => {
    if (!err) {
      addLog(`→ [${topicCmd}]: ${payloadStr}`, "out");
    } else {
      addLog(`Lỗi gửi lệnh: ${err}`, "err");
    }
  });
}

// ── Các thao tác điều khiển ───────────────────────────────────────────────
function syncDevices() {
  addLog("Yêu cầu Gateway đồng bộ danh sách thiết bị...", "system");
  sendGateCommand({ cmd: "get_devices" });
}

function sendAcCommand() {
  sendGateCommand({
    cmd: "hunonic_ac",
    id: acState.id,
    power: acState.power,
    temp: acState.temp,
    mode: acState.mode,
    fan: acState.fan
  });
}

function updateAcUI() {
  acNameDisplay.innerText = acState.name || "Điều hòa FUNIKI";
  acIdDisplay.innerText = acState.id || "3992007";
  acTempVal.innerText = acState.temp || 26;

  if (acState.power === 1) {
    btnAcPower.classList.add("on");
    acCard.classList.add("active");
    acPowerLabel.innerText = "Đang BẬT";
    acPowerLabel.style.color = "var(--accent-cyan)";
  } else {
    btnAcPower.classList.remove("on");
    acCard.classList.remove("active");
    acPowerLabel.innerText = "Đang TẮT";
    acPowerLabel.style.color = "var(--text-dim)";
  }

  // Update mode buttons
  modeBtns.forEach(btn => {
    if (parseInt(btn.dataset.mode) === acState.mode) {
      btn.classList.add("active");
    } else {
      btn.classList.remove("active");
    }
  });

  // Update fan buttons
  fanBtns.forEach(btn => {
    if (parseInt(btn.dataset.fan) === acState.fan) {
      btn.classList.add("active");
    } else {
      btn.classList.remove("active");
    }
  });
}

function updateRelayUI(ch, state) {
  if (ch === 1) {
    relayState.ch1 = state;
    switchRelay1.checked = state === 1;
    if (state === 1) relayCard1.classList.add("active");
    else relayCard1.classList.remove("active");
  } else if (ch === 2) {
    relayState.ch2 = state;
    switchRelay2.checked = state === 1;
    if (state === 1) relayCard2.classList.add("active");
    else relayCard2.classList.remove("active");
  }
}

function markGatewayOnline() {
  isGatewayOnline = true;
  gwPill.className = "gateway-online-pill online";
  gwOnlineLabel.innerText = "Online";
}

function markGatewayOffline() {
  isGatewayOnline = false;
  gwPill.className = "gateway-online-pill";
  gwOnlineLabel.innerText = "Chờ Gateway...";
}

function formatUptime(seconds) {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  return `${String(h).padStart(2, "0")}h ${String(m).padStart(2, "0")}m ${String(s).padStart(2, "0")}s`;
}

// ── Event Listeners ───────────────────────────────────────────────────────
// Nút Bật / Tắt điều hòa
btnAcPower.addEventListener("click", () => {
  acState.power = acState.power === 1 ? 0 : 1;
  updateAcUI();
  sendAcCommand();
});

// Nút Tăng / Giảm nhiệt độ
btnTempDown.addEventListener("click", () => {
  if (acState.temp > 16) {
    acState.temp--;
    updateAcUI();
    sendAcCommand();
  }
});

btnTempUp.addEventListener("click", () => {
  if (acState.temp < 30) {
    acState.temp++;
    updateAcUI();
    sendAcCommand();
  }
});

// Chế độ Mode
modeBtns.forEach(btn => {
  btn.addEventListener("click", () => {
    acState.mode = parseInt(btn.dataset.mode);
    updateAcUI();
    sendAcCommand();
  });
});

// Tốc độ quạt Fan
fanBtns.forEach(btn => {
  btn.addEventListener("click", () => {
    acState.fan = parseInt(btn.dataset.fan);
    updateAcUI();
    sendAcCommand();
  });
});

// Công tắc Relay 1 & 2
switchRelay1.addEventListener("change", (e) => {
  const state = e.target.checked ? 1 : 0;
  sendGateCommand({ cmd: "relay", ch: 1, state: state });
});

switchRelay2.addEventListener("change", (e) => {
  const state = e.target.checked ? 1 : 0;
  sendGateCommand({ cmd: "relay", ch: 2, state: state });
});

// Nút Khởi động lại Gateway
btnRebootGate.addEventListener("click", () => {
  if (confirm("Bạn có chắc chắn muốn khởi động lại (Reboot) ESP32-S3 Gateway?")) {
    sendGateCommand({ cmd: "reboot" });
    showToast("Đã gửi lệnh Reboot Gateway!");
  }
});

// Nút Đồng bộ
btnSyncDevices.addEventListener("click", () => {
  syncDevices();
});

// Nút Xóa Log
btnClearLog.addEventListener("click", () => {
  consoleLogs.innerHTML = "";
  addLog("Đã xóa nhật ký.", "system");
});

// Modal Settings
btnToggleSettings.addEventListener("click", () => {
  modalSettings.classList.remove("hidden");
});

btnCloseSettings.addEventListener("click", () => {
  modalSettings.classList.add("hidden");
});

btnResetSettings.addEventListener("click", () => {
  document.getElementById("cfg-broker").value = DEFAULT_CONFIG.brokerUrl;
  document.getElementById("cfg-user").value = DEFAULT_CONFIG.username;
  document.getElementById("cfg-pass").value = DEFAULT_CONFIG.password;
  document.getElementById("cfg-topic-cmd").value = DEFAULT_CONFIG.topicCmd;
  document.getElementById("cfg-topic-status").value = DEFAULT_CONFIG.topicStatus;
});

btnSaveSettings.addEventListener("click", () => {
  localStorage.setItem("cfg_broker", document.getElementById("cfg-broker").value.trim());
  localStorage.setItem("cfg_user", document.getElementById("cfg-user").value.trim());
  localStorage.setItem("cfg_pass", document.getElementById("cfg-pass").value.trim());
  localStorage.setItem("cfg_topic_cmd", document.getElementById("cfg-topic-cmd").value.trim());
  localStorage.setItem("cfg_topic_status", document.getElementById("cfg-topic-status").value.trim());
  modalSettings.classList.add("hidden");
  showToast("Đã lưu cấu hình, đang kết nối lại...");
  initMQTT();
});

// Kiểm tra Gateway liveness
setInterval(() => {
  if (lastHeartbeatTime > 0 && Date.now() - lastHeartbeatTime > 20000) {
    markGatewayOffline();
  }
}, 5000);

// Khởi chạy khi load trang
document.addEventListener("DOMContentLoaded", () => {
  updateAcUI();
  initMQTT();
});
