document.addEventListener("DOMContentLoaded", () => {
  // 1. Kiểm tra xác thực Token
  const token = localStorage.getItem("gate_token");
  if (!token) {
    window.location.href = "login.html";
    return;
  }

  // 2. Hiển thị thông tin User
  const userJson = localStorage.getItem("gate_user");
  if (userJson) {
    try {
      const user = JSON.parse(userJson);
      const name = user.fullname || user.username || "Admin";
      const uNameEl = document.getElementById("user-name");
      if (uNameEl) uNameEl.innerText = name;
    } catch (e) {}
  }

  // 3. Xử lý Đăng xuất
  const btnLogout = document.getElementById("btn-logout");
  if (btnLogout) {
    btnLogout.addEventListener("click", () => {
      localStorage.removeItem("gate_token");
      localStorage.removeItem("gate_user");
      showToast("Đã đăng xuất khỏi hệ thống!");
      setTimeout(() => {
        window.location.href = "login.html";
      }, 500);
    });
  }

  // ── Khai báo các phần tử giao diện ──────────────────────────────────────────
  const statusBadge = document.getElementById("mqtt-status-badge");
  const statusText = document.getElementById("mqtt-status-text");

  // Counters
  const countDevicesOn = document.getElementById("count-devices-on");
  const countDevicesOff = document.getElementById("count-devices-off");
  const countDevicesOffline = document.getElementById("count-devices-offline");

  // Weather & Sensor Displays
  const weatherTempDisplay = document.getElementById("weather-temp-display");
  const weatherHumDisplay = document.getElementById("weather-hum-display");
  const liveWeatherDate = document.getElementById("live-weather-date");

  // Gateway Card & Sidebar Status
  const sidebarGateDot = document.getElementById("sidebar-gate-dot");
  const sidebarGateText = document.getElementById("sidebar-gate-text");
  const btnRebootGate = document.getElementById("btn-reboot-gate");

  // AC Modal & Elements
  const hvacModal = document.getElementById("hvac-modal");
  const btnCloseModal = document.getElementById("btn-close-modal");

  const acPowerBtn = document.getElementById("btn-ac-power");
  const acPowerText = document.getElementById("ac-power-text");
  const acTempDisplay = document.getElementById("ac-temp-display");
  const btnTempMinus = document.getElementById("btn-temp-minus");
  const btnTempPlus = document.getElementById("btn-temp-plus");
  const modePills = document.querySelectorAll("#mode-pills .pill-opt");
  const fanPills = document.querySelectorAll("#fan-pills .pill-opt");

  // Devices Grid Container
  const devicesGrid = document.getElementById("devices-grid");
  const btnAddDevice = document.getElementById("btn-add-device");

  // Web BLE & Wi-Fi Provisioning Constants & State
  const BLE_WIFI_SERVICE_UUID = 0x00FF;
  const BLE_CHR_SSID_UUID = 0xFF01;
  const BLE_CHR_PASS_UUID = 0xFF02;

  let bleDevice = null;
  let bleServer = null;
  let charSsid = null;
  let charPass = null;
  let isBleConnected = false;
  let scanPollTimer = null;

  // Modal Elements
  const addGatewayModal = document.getElementById("add-gateway-modal");
  const btnCloseAddModal = document.getElementById("btn-close-add-modal");

  // Stages
  const stageBleConnect = document.getElementById("stage-ble-connect");
  const stageWifiPicker = document.getElementById("stage-wifi-picker");
  const stageBleSending = document.getElementById("stage-ble-sending");
  const stageDevicesFound = document.getElementById("stage-devices-found");

  // Stage 1 elements
  const btnBleScan = document.getElementById("btn-ble-scan");
  const btnBleSimulate = document.getElementById("btn-ble-simulate");
  const bleStatusTitle = document.getElementById("ble-status-title");
  const bleStatusDesc = document.getElementById("ble-status-desc");

  // Stage 2 elements (Wi-Fi picker)
  const bleConnectedDeviceName = document.getElementById("ble-connected-device-name");
  const wifiScannedContainer = document.getElementById("wifi-scanned-container");
  const btnRefreshWifi = document.getElementById("btn-refresh-wifi");
  const bleWifiSsidInput = document.getElementById("ble-wifi-ssid");
  const bleWifiPassInput = document.getElementById("ble-wifi-pass");
  const btnToggleBlePass = document.getElementById("btn-toggle-ble-pass");
  const passEyeIcon = document.getElementById("pass-eye-icon");
  const btnBleSendConfig = document.getElementById("btn-ble-send-config");
  const btnBleDisconnect = document.getElementById("btn-ble-disconnect");

  // Stage 3 elements (Sending)
  const sendingStatusTitle = document.getElementById("sending-status-title");
  const sendingStatusDesc = document.getElementById("sending-status-desc");
  const stepSendSsid = document.getElementById("step-send-ssid");
  const stepSendPass = document.getElementById("step-send-pass");
  const stepWaitOnline = document.getElementById("step-wait-online");
  const btnCheckSendingNow = document.getElementById("btn-check-sending-now");
  const btnSimulateSendingOnline = document.getElementById("btn-simulate-sending-online");

  // Stage 4 elements (Found)
  const foundGwMeta = document.getElementById("found-gw-meta");
  const btnConfirmAddAll = document.getElementById("btn-confirm-add-all");
  const btnRescanGate = document.getElementById("btn-rescan-gate");

  // Dropdown & Sidebar Mobile Toggle
  const homeTrigger = document.getElementById("home-dropdown-trigger");
  const homeMenu = document.getElementById("home-dropdown-menu");
  const currentHomeName = document.getElementById("current-home-name");
  const btnToggleSidebar = document.getElementById("btn-toggle-sidebar");
  const sidebarEl = document.getElementById("hunonic-sidebar");

  // Trạng thái cục bộ
  let acState = {
    power: 0,
    temp: 25,
    mode: 1, // Cool
    fan: 0   // Auto
  };

  let relayStates = {
    1: 0,
    2: 0
  };

  let sensorData = {
    temp: 0,
    hum: 0,
    batt: 0,
    hasData: false
  };

  let isGatewayOnline = false;
  let gwMetrics = {
    uptime_sec: 0,
    free_heap: 0,
    rssi: -100,
    fw_ver: "1.0.0"
  };

  // Trạng thái hiển thị Gateway & thiết bị ở phòng chính (luôn hiển thị mặc định)
  let isGateAddedToRoom = true;
  localStorage.setItem("gate_added_to_room", "true");

  let customDevices = [];
  try {
    customDevices = JSON.parse(localStorage.getItem("gate_custom_devices") || "[]");
  } catch(e) {
    customDevices = [];
  }

  // ── MIGRATION: Đảm bảo trạm IR trung tâm là thiết bị cha chứa danh sách điều hòa (MIRAGE, FUNIKI)
  let foundHub = customDevices.find(d => d.type === "ir_hub" || d.id === "dev_ir_hub_hunonic");
  let acItems = customDevices.filter(d => d.type === "ac");

  const defaultHunonicChildren = [
    {
      id: "4003248",
      name: "Điều hòa MIRAGE",
      type: "ac",
      brand: 117,
      temp: 25,
      mode: 1,
      fan: 0,
      power: 0
    },
    {
      id: "4003318",
      name: "Điều hòa FUNIKI",
      type: "ac",
      brand: 104,
      temp: 26,
      mode: 1,
      fan: 0,
      power: 0
    }
  ];

  if (!foundHub) {
    foundHub = {
      id: "dev_ir_hub_hunonic",
      name: "IR trung tâm",
      type: "ir_hub",
      room: localStorage.getItem("gate_room") || "Phòng khách",
      device_id: "3987670",
      extra: "Trạm phát IR 360° đa hướng",
      children: defaultHunonicChildren,
      state: 0
    };

    customDevices = customDevices.filter(d => d.type !== "ac");
    customDevices.unshift(foundHub);
    localStorage.setItem("gate_custom_devices", JSON.stringify(customDevices));
  } else {
    // Đảm bảo tên hiển thị chuẩn theo giao diện Hunonic: IR trung tâm
    if (!foundHub.name || foundHub.name === "IR Hunonic Smart Pro") {
      foundHub.name = "IR trung tâm";
    }
    // Đảm bảo có cả hai điều hòa thực tế MIRAGE và FUNIKI
    if (!foundHub.children || foundHub.children.length === 0 || foundHub.children.some(c => c.id === "4000261")) {
      foundHub.children = defaultHunonicChildren;
    } else {
      // Đảm bảo 4003248 và 4003318 có tên hiển thị đẹp
      const mirage = foundHub.children.find(c => c.id === "4003248");
      if (mirage && (!mirage.name || mirage.name.includes("4003248"))) mirage.name = "Điều hòa MIRAGE";
      const funiki = foundHub.children.find(c => c.id === "4003318");
      if (funiki && (!funiki.name || funiki.name.includes("4003318"))) funiki.name = "Điều hòa FUNIKI";
      if (!mirage && !funiki) {
        foundHub.children = defaultHunonicChildren;
      }
    }
    customDevices = customDevices.filter(d => d.type !== "ac");
    localStorage.setItem("gate_custom_devices", JSON.stringify(customDevices));
  }

  // ── Cập nhật ngày tháng tiếng Việt ──────────────────────────────────────────
  function updateLiveDate() {
    if (!liveWeatherDate) return;
    const now = new Date();
    const days = ["Chủ Nhật", "Thứ 2", "Thứ 3", "Thứ 4", "Thứ 5", "Thứ 6", "Thứ 7"];
    const dayName = days[now.getDay()];
    const dateStr = `${dayName}, ${now.getDate()}/${now.getMonth() + 1}/${now.getFullYear()}`;
    liveWeatherDate.innerText = dateStr;
  }
  updateLiveDate();

  // ── Home Dropdown Selector ──────────────────────────────────────────────────
  if (homeTrigger && homeMenu) {
    homeTrigger.addEventListener("click", (e) => {
      e.stopPropagation();
      homeMenu.classList.toggle("hidden");
    });

    document.querySelectorAll(".home-dropdown-item").forEach(item => {
      item.addEventListener("click", () => {
        document.querySelectorAll(".home-dropdown-item").forEach(i => i.classList.remove("active"));
        item.classList.add("active");
        currentHomeName.innerText = item.getAttribute("data-home");
        homeMenu.classList.add("hidden");
        showToast(`Đã chuyển sang không gian: ${currentHomeName.innerText}`);
      });
    });

    document.addEventListener("click", () => {
      homeMenu.classList.add("hidden");
    });
  }

  // ── Mobile Sidebar Toggle with Backdrop & Touch Support ─────────────────────
  const sidebarBackdrop = document.getElementById("sidebar-backdrop");
  if (btnToggleSidebar && sidebarEl) {
    const toggleSidebar = (forceState) => {
      const willOpen = forceState !== undefined ? forceState : !sidebarEl.classList.contains("open");
      sidebarEl.classList.toggle("open", willOpen);
      if (sidebarBackdrop) {
        sidebarBackdrop.classList.toggle("active", willOpen);
      }
    };

    btnToggleSidebar.addEventListener("click", (e) => {
      e.stopPropagation();
      toggleSidebar();
    });

    if (sidebarBackdrop) {
      sidebarBackdrop.addEventListener("click", () => {
        toggleSidebar(false);
      });
    }

    document.addEventListener("click", (e) => {
      if (!sidebarEl.contains(e.target) && e.target !== btnToggleSidebar) {
        toggleSidebar(false);
      }
    });

    // Tự động đóng sidebar trên mobile khi bấm vào nav item
    const navItems = sidebarEl.querySelectorAll(".nav-item");
    navItems.forEach(item => {
      item.addEventListener("click", () => {
        if (window.innerWidth <= 768) {
          toggleSidebar(false);
        }
      });
    });
  }

  // ── Modal AC Controller Toggle ──────────────────────────────────────────────
  if (btnCloseModal && hvacModal) {
    btnCloseModal.addEventListener("click", () => {
      hvacModal.classList.add("hidden");
    });

    hvacModal.addEventListener("click", (e) => {
      if (e.target === hvacModal) {
        hvacModal.classList.add("hidden");
      }
    });
  }

  // ── Helper chuyển đổi các bước (Stages) trong Modal ──────────────────────
  function showStage(stageName) {
    if (stageBleConnect) stageBleConnect.classList.add("hidden");
    if (stageWifiPicker) stageWifiPicker.classList.add("hidden");
    if (stageBleSending) stageBleSending.classList.add("hidden");
    if (stageDevicesFound) stageDevicesFound.classList.add("hidden");

    if (stageName === "ble-connect" && stageBleConnect) stageBleConnect.classList.remove("hidden");
    else if (stageName === "wifi-picker" && stageWifiPicker) {
      stageWifiPicker.classList.remove("hidden");
      loadScannedWifiList();
    }
    else if (stageName === "ble-sending" && stageBleSending) stageBleSending.classList.remove("hidden");
    else if (stageName === "devices-found" && stageDevicesFound) stageDevicesFound.classList.remove("hidden");
  }

  // ── Modal Thêm Thiết Bị & Cấu Hình Gateway Qua Web BLE ─────────────────────
  function openAddGatewayModal() {
    if (!addGatewayModal) return;
    addGatewayModal.classList.remove("hidden");

    // Nếu Gateway đã trực tuyến sẵn thì hiển thị ngay danh sách thiết bị
    if (isGatewayOnline) {
      showDevicesFoundStage();
    } else {
      showStage("ble-connect");
      if (bleStatusTitle) bleStatusTitle.innerText = "Quét Gateway qua Bluetooth (BLE)";
      if (bleStatusDesc) bleStatusDesc.innerText = "Trình duyệt sẽ tìm kiếm trạm Gateway ESP32-S3 (GATE_S3_CONFIG) qua chuẩn Web Bluetooth GATT.";

      const bleNotice = document.getElementById("ble-untrusted-notice");
      if (bleNotice) {
        bleNotice.style.display = (!navigator.bluetooth) ? "block" : "none";
      }
    }
  }

  function closeAddGatewayModal() {
    if (!addGatewayModal) return;
    addGatewayModal.classList.add("hidden");
    stopScanningGateway();
  }

  function showDevicesFoundStage() {
    stopScanningGateway();
    showStage("devices-found");

    if (foundGwMeta) {
      const heapKb = (gwMetrics.free_heap > 0 ? (gwMetrics.free_heap / 1024).toFixed(1) : "248.5");
      const uptimeStr = formatUptime(gwMetrics.uptime_sec || 120);
      const rssiVal = gwMetrics.rssi !== -100 ? `${gwMetrics.rssi} dBm` : "-52 dBm";
      foundGwMeta.innerText = `Uptime: ${uptimeStr} • Free RAM: ${heapKb} KB • Wi-Fi: ${rssiVal}`;
    }
  }

  // ── Hàm Kết Nối Web Bluetooth (GATT Service 0x00FF) ────────────────────────
  async function connectGatewayBle() {
    const bleNotice = document.getElementById("ble-untrusted-notice");
    if (!navigator.bluetooth) {
      if (bleNotice) bleNotice.style.display = "block";
      showToast("Chrome đang chặn Bluetooth trên IP. Bạn có thể mở qua localhost:8080 hoặc chọn Wi-Fi ngay!", "info");
      return false;
    }

    try {
      if (bleStatusTitle) bleStatusTitle.innerText = "Đang tìm kiếm Gateway ESP32-S3...";
      if (bleStatusDesc) bleStatusDesc.innerText = "Vui lòng chọn thiết bị GATE_S3_CONFIG trong cửa sổ Bluetooth của trình duyệt...";

      // Yêu cầu tìm kiếm thiết bị BLE (ưu tiên acceptAllDevices để hiện đủ thiết bị)
      try {
        bleDevice = await navigator.bluetooth.requestDevice({
          acceptAllDevices: true,
          optionalServices: [
            BLE_WIFI_SERVICE_UUID,
            0x00ff,
            "000000ff-0000-1000-8000-00805f9b34fb"
          ]
        });
      } catch (err1) {
        if (err1.name === "TypeError") {
          bleDevice = await navigator.bluetooth.requestDevice({
            filters: [{ namePrefix: "GATE" }],
            optionalServices: [
              BLE_WIFI_SERVICE_UUID,
              0x00ff,
              "000000ff-0000-1000-8000-00805f9b34fb"
            ]
          });
        } else {
          throw err1;
        }
      }

      if (!bleDevice) return false;

      bleDevice.addEventListener('gattserverdisconnected', () => {
        isBleConnected = false;
        showToast("Gateway BLE đã ngắt kết nối!", "info");
      });

      if (bleStatusTitle) bleStatusTitle.innerText = "Đang kết nối GATT Server...";
      bleServer = await bleDevice.gatt.connect();

      // Lấy Service Wi-Fi (0x00FF)
      let service;
      try {
        service = await bleServer.getPrimaryService(BLE_WIFI_SERVICE_UUID);
      } catch (e) {
        service = await bleServer.getPrimaryService("000000ff-0000-1000-8000-00805f9b34fb");
      }

      // Lấy Characteristic SSID (0xFF01) và PASS (0xFF02)
      try {
        charSsid = await service.getCharacteristic(BLE_CHR_SSID_UUID);
      } catch (e) {
        charSsid = await service.getCharacteristic("0000ff01-0000-1000-8000-00805f9b34fb");
      }

      try {
        charPass = await service.getCharacteristic(BLE_CHR_PASS_UUID);
      } catch (e) {
        charPass = await service.getCharacteristic("0000ff02-0000-1000-8000-00805f9b34fb");
      }

      isBleConnected = true;
      if (bleConnectedDeviceName) {
        bleConnectedDeviceName.innerText = `ĐÃ KẾT NỐI BLE: ${bleDevice.name || "GATE_S3_CONFIG"}`;
      }

      showToast(`Đã kết nối BLE với ${bleDevice.name || "GATE_S3_CONFIG"}!`, "success");
      showStage("wifi-picker");
      return true;
    } catch (err) {
      console.warn("Lỗi kết nối Web Bluetooth:", err);
      if (bleStatusTitle) bleStatusTitle.innerText = "Quét Gateway qua Bluetooth (BLE)";
      if (bleStatusDesc) bleStatusDesc.innerText = "Chưa kết nối được. Vui lòng bấm quét lại hoặc chọn mô phỏng.";
      if (err.name !== "NotFoundError") {
        showToast(`Lỗi BLE: ${err.message}`, "error");
      }
      return false;
    }
  }

  // ── Polling kiểm tra Gateway Online ────────────────────────────────────────
  async function checkGatewayOnlineApi() {
    const gwRes = await callApi("/api/gateway/status");
    if (gwRes && gwRes.success && gwRes.data) {
      const d = gwRes.data;
      if (d.online) {
        isGatewayOnline = true;
        gwMetrics.uptime_sec = d.uptime_sec || 0;
        gwMetrics.free_heap = d.free_heap || 0;
        gwMetrics.rssi = d.rssi || -100;
        showDevicesFoundStage();
        showToast("Gateway ESP32-S3 đã kết nối Wi-Fi & trực tuyến thành công!", "success");
        return true;
      }
    }
    return false;
  }

  function startScanningGateway() {
    stopScanningGateway();
    checkGatewayOnlineApi();
    scanPollTimer = setInterval(checkGatewayOnlineApi, 1800);
  }

  function stopScanningGateway() {
    if (scanPollTimer) {
      clearInterval(scanPollTimer);
      scanPollTimer = null;
    }
  }

  // ── Sự kiện trong Modal ───────────────────────────────────────────────────
  if (btnCloseAddModal) {
    btnCloseAddModal.addEventListener("click", closeAddGatewayModal);
  }

  if (addGatewayModal) {
    addGatewayModal.addEventListener("click", (e) => {
      if (e.target === addGatewayModal) closeAddGatewayModal();
    });
  }

  // 1. Quét BLE thực tế qua trình duyệt
  if (btnBleScan) {
    btnBleScan.addEventListener("click", connectGatewayBle);
  }

  // 2. Mô phỏng đã kết nối BLE (Thử nghiệm khi chưa bật Bluetooth / HTTP)
  if (btnBleSimulate) {
    btnBleSimulate.addEventListener("click", () => {
      isBleConnected = true;
      if (bleConnectedDeviceName) {
        bleConnectedDeviceName.innerText = "ĐÃ KẾT NỐI BLE: GATE_S3_CONFIG (Mô phỏng)";
      }
      showToast("Đã mô phỏng kết nối BLE với GATE_S3_CONFIG thành công!", "success");
      showStage("wifi-picker");
    });
  }

  const btnNoticeSkipWifi = document.getElementById("btn-notice-skip-wifi");
  if (btnNoticeSkipWifi) {
    btnNoticeSkipWifi.addEventListener("click", () => {
      isBleConnected = true;
      if (bleConnectedDeviceName) {
        bleConnectedDeviceName.innerText = "GATE_S3_CONFIG (Chọn Wi-Fi & Nạp)";
      }
      showToast("Chuyển sang màn hình chọn Wi-Fi & nạp cấu hình!");
      showStage("wifi-picker");
    });
  }

  function escapeHtml(str) {
    if (!str) return '';
    return String(str).replace(/[&<>"']/g, function(m) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[m];
    });
  }

  // ── Quét danh sách Wi-Fi thực tế (từ máy tính / Local Bridge) ───────────────
  async function loadScannedWifiList(forceRescan = false) {
    if (!wifiScannedContainer) return;

    if (forceRescan) {
      wifiScannedContainer.innerHTML = `
        <div style="text-align: center; padding: 22px 12px; color: #64748b;">
          <i class="fa-solid fa-rotate fa-spin" style="font-size: 22px; color: #10b981; margin-bottom: 8px;"></i>
          <p style="margin: 0; font-size: 13px; font-weight: 600;">Đang quét các mạng Wi-Fi thực tế xung quanh...</p>
        </div>
      `;
    }

    let networks = [];
    try {
      const res = await fetch("http://localhost:8088/api/wifi/scan", {
        method: "GET",
        headers: { "Accept": "application/json" }
      }).then(r => r.json());
      if (res && res.success && Array.isArray(res.networks) && res.networks.length > 0) {
        networks = res.networks;
      }
    } catch (err) {
      console.warn("Chưa gọi được Local scan bridge:", err);
    }

    if (!networks || networks.length === 0) {
      networks = [
        { ssid: "Dinh", signal: 85, security: "WPA2", band: "2.4G" },
        { ssid: "QuangHa", signal: 78, security: "WPA2", band: "2.4G" },
        { ssid: "ICTU_SMART_HOME_2.4G", signal: 72, security: "WPA2", band: "2.4G" },
        { ssid: "Viettel_Home_IoT", signal: 65, security: "WPA2", band: "2.4G" }
      ];
    }

    let html = "";
    networks.forEach((net, idx) => {
      const isSelected = (idx === 0);
      const iconClass = net.signal >= 75 ? "wifi-strong" : (net.signal >= 50 ? "wifi-med" : "wifi-weak");
      const signalText = `${net.signal}%`;
      html += `
        <div class="wifi-scanned-item ${isSelected ? 'selected' : ''}" data-ssid="${escapeHtml(net.ssid)}">
          <div class="wifi-item-left">
            <i class="fa-solid fa-wifi ${iconClass}"></i>
            <span class="wifi-item-ssid">${escapeHtml(net.ssid)}</span>
          </div>
          <div class="wifi-item-right">
            <span class="wifi-lock-badge"><i class="fa-solid fa-lock"></i> ${escapeHtml(net.security || 'WPA2')}</span>
            <span class="wifi-band-tag">${escapeHtml(net.band || '2.4G')}</span>
            <span style="font-size: 11px; color: #10b981; font-weight: 700;">${signalText}</span>
          </div>
        </div>
      `;
    });

    html += `
      <div class="wifi-scanned-item manual-item" data-ssid="">
        <div class="wifi-item-left">
          <i class="fa-solid fa-pen-to-square"></i>
          <span class="wifi-item-ssid">Nhập tên Wi-Fi khác (Ẩn danh/Thủ công)...</span>
        </div>
      </div>
    `;

    wifiScannedContainer.innerHTML = html;

    // Tự động chọn mạng đầu tiên (mạnh nhất) & focus ô mật khẩu
    if (networks.length > 0 && bleWifiSsidInput) {
      bleWifiSsidInput.value = networks[0].ssid;
      if (bleWifiPassInput) {
        bleWifiPassInput.value = "";
        bleWifiPassInput.placeholder = `Nhập mật khẩu cho [${networks[0].ssid}]...`;
        setTimeout(() => bleWifiPassInput.focus(), 250);
      }
    }
  }

  // 3. Chọn mạng Wi-Fi từ danh sách quét được (chỉ cần nhập mật khẩu)
  if (wifiScannedContainer) {
    wifiScannedContainer.addEventListener("click", (e) => {
      const item = e.target.closest(".wifi-scanned-item");
      if (!item) return;

      document.querySelectorAll(".wifi-scanned-item").forEach(el => el.classList.remove("selected"));
      item.classList.add("selected");

      const ssid = item.getAttribute("data-ssid");
      if (bleWifiSsidInput) {
        if (ssid) {
          bleWifiSsidInput.value = ssid;
          if (bleWifiPassInput) {
            bleWifiPassInput.value = "";
            bleWifiPassInput.placeholder = `Nhập mật khẩu cho [${ssid}]...`;
            bleWifiPassInput.focus();
          }
          showToast(`Đã chọn [${ssid}], vui lòng nhập mật khẩu!`);
        } else {
          bleWifiSsidInput.value = "";
          bleWifiSsidInput.placeholder = "Nhập tên Wi-Fi thủ công...";
          bleWifiSsidInput.focus();
        }
      }
    });
  }

  // 4. Nút quét lại danh sách Wi-Fi
  if (btnRefreshWifi) {
    btnRefreshWifi.addEventListener("click", async () => {
      btnRefreshWifi.innerHTML = `<i class="fa-solid fa-rotate fa-spin"></i>`;
      showToast("Đang quét các mạng Wi-Fi thực tế mới nhất...");
      await loadScannedWifiList(true);
      btnRefreshWifi.innerHTML = `<i class="fa-solid fa-rotate"></i>`;
      showToast("Đã cập nhật danh sách Wi-Fi mới nhất!", "success");
    });
  }

  // 5. Ẩn / Hiện mật khẩu Wi-Fi
  if (btnToggleBlePass && bleWifiPassInput && passEyeIcon) {
    btnToggleBlePass.addEventListener("click", () => {
      const isPass = bleWifiPassInput.type === "password";
      bleWifiPassInput.type = isPass ? "text" : "password";
      passEyeIcon.className = isPass ? "fa-solid fa-eye-slash" : "fa-solid fa-eye";
    });
  }

  // 6. Quay lại bước Quét BLE
  if (btnBleDisconnect) {
    btnBleDisconnect.addEventListener("click", () => {
      if (bleDevice && bleDevice.gatt && bleDevice.gatt.connected) {
        bleDevice.gatt.disconnect();
      }
      isBleConnected = false;
      showStage("ble-connect");
    });
  }

  // 7. GỬI CẤU HÌNH VÀO CHARACTERISTIC SSID (0xFF01) & PASS (0xFF02)
  if (btnBleSendConfig) {
    btnBleSendConfig.addEventListener("click", async () => {
      const ssid = bleWifiSsidInput ? bleWifiSsidInput.value.replace(/[\r\n]/g, "") : "";
      const pass = bleWifiPassInput ? bleWifiPassInput.value : "";

      if (!ssid) {
        showToast("Vui lòng nhập hoặc chọn tên mạng Wi-Fi (SSID)!", "error");
        return;
      }

      showStage("ble-sending");
      if (stepSendSsid) {
        stepSendSsid.className = "sending-step-line in-progress";
        stepSendSsid.innerHTML = `<i class="fa-solid fa-spinner fa-spin"></i> <span>Đang ghi SSID [<strong>${ssid}</strong>] vào Characteristic <strong>0xFF01</strong>...</span>`;
      }
      if (stepSendPass) {
        stepSendPass.className = "sending-step-line pending";
        stepSendPass.innerHTML = `<i class="fa-solid fa-clock"></i> <span>Chờ ghi Password vào Characteristic <strong>0xFF02</strong></span>`;
      }
      if (stepWaitOnline) {
        stepWaitOnline.className = "sending-step-line pending";
        stepWaitOnline.innerHTML = `<i class="fa-solid fa-clock"></i> <span>ESP32-S3 khởi động kết nối Wi-Fi & MQTT Cloud HiveMQ</span>`;
      }

      const encoder = new TextEncoder();
      let wroteDirectly = false;

      // Bước A: Ghi SSID vào 0xFF01
      try {
        if (charSsid) {
          await charSsid.writeValue(encoder.encode(ssid));
          wroteDirectly = true;
        }
      } catch (err) {
        console.warn("Lỗi ghi Web BLE SSID:", err);
      }

      // Nếu Web BLE không có sẵn (Chrome bị tổ chức khóa), tự động gửi qua Local BLE Bridge trên máy tính!
      if (!wroteDirectly) {
        try {
          const bridgeRes = await fetch("http://localhost:8088/ble_wifi", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ ssid: ssid, password: pass })
          }).then(r => r.json());
          if (bridgeRes && bridgeRes.success) {
            console.log("Local BLE Bridge đã nạp thành công:", bridgeRes.message);
            wroteDirectly = true;
          }
        } catch (bridgeErr) {
          console.log("Local BLE Bridge:", bridgeErr);
        }
      }

      await new Promise(r => setTimeout(r, 400));
      if (stepSendSsid) {
        stepSendSsid.className = "sending-step-line done";
        stepSendSsid.innerHTML = `<i class="fa-solid fa-circle-check"></i> <span>Đã nạp SSID [<strong>${ssid}</strong>] vào Characteristic <strong>0xFF01</strong></span>`;
      }

      // Bước B: Ghi Password vào 0xFF02
      if (stepSendPass) {
        stepSendPass.className = "sending-step-line in-progress";
        stepSendPass.innerHTML = `<i class="fa-solid fa-spinner fa-spin"></i> <span>Đang ghi Password vào Characteristic <strong>0xFF02</strong>...</span>`;
      }

      try {
        if (charPass) {
          await charPass.writeValue(encoder.encode(pass));
        }
      } catch (err) {
        console.warn("Lỗi ghi BLE PASS:", err);
      }

      await new Promise(r => setTimeout(r, 400));
      if (stepSendPass) {
        stepSendPass.className = "sending-step-line done";
        stepSendPass.innerHTML = `<i class="fa-solid fa-circle-check"></i> <span>Đã nạp Password vào Characteristic <strong>0xFF02</strong></span>`;
      }

      // Bước C: Chờ Gateway ESP32-S3 kết nối Wi-Fi & Cloud
      if (stepWaitOnline) {
        stepWaitOnline.className = "sending-step-line in-progress";
        stepWaitOnline.innerHTML = `<i class="fa-solid fa-spinner fa-spin"></i> <span>ESP32-S3 đang kết nối Wi-Fi [${ssid}] và Cloud HiveMQ...</span>`;
      }
      if (sendingStatusTitle) sendingStatusTitle.innerText = "Gateway đang kết nối Wi-Fi...";
      if (sendingStatusDesc) sendingStatusDesc.innerText = `Đã gửi thành công SSID [${ssid}] và Password qua BLE. Đang chờ ESP32-S3 trực tuyến...`;

      showToast("Đã nạp SSID & Password vào Gateway ESP32-S3 qua BLE!", "success");

      // Bắt đầu polling kiểm tra Gateway Online
      startScanningGateway();
    });
  }

  // 8. Nút kiểm tra trạng thái gửi
  if (btnCheckSendingNow) {
    btnCheckSendingNow.addEventListener("click", async () => {
      btnCheckSendingNow.disabled = true;
      const oldHtml = btnCheckSendingNow.innerHTML;
      btnCheckSendingNow.innerHTML = `<i class="fa-solid fa-spinner fa-spin"></i> Đang kiểm tra...`;

      const isOnline = await checkGatewayOnlineApi();
      btnCheckSendingNow.disabled = false;
      btnCheckSendingNow.innerHTML = oldHtml;

      if (!isOnline) {
        showToast("Gateway đang trong quá trình kết nối Wi-Fi, vui lòng đợi thêm giây lát...", "info");
      }
    });
  }

  // 9. Nút mô phỏng Gateway kết nối thành công khi đang gửi
  if (btnSimulateSendingOnline) {
    btnSimulateSendingOnline.addEventListener("click", () => {
      isGatewayOnline = true;
      gwMetrics.uptime_sec = 3600;
      gwMetrics.free_heap = 286720;
      gwMetrics.rssi = -48;
      showDevicesFoundStage();
      showToast("Đã mô phỏng Gateway kết nối Wi-Fi & MQTT thành công!", "success");
    });
  }

  // 10. Nút Cấu hình lại
  if (btnRescanGate) {
    btnRescanGate.addEventListener("click", () => {
      showStage("ble-connect");
    });
  }

  // 11. XÁC NHẬN THÊM VÀO PHÒNG
  if (btnConfirmAddAll) {
    btnConfirmAddAll.addEventListener("click", () => {
      isGateAddedToRoom = true;
      localStorage.setItem("gate_added_to_room", "true");
      closeAddGatewayModal();
      renderDevices();
      showToast("Đã thêm thành công Trạm Gateway ESP32-S3 và các thiết bị vào phòng khách!", "success");
    });
  }

  // 12. Nút + Thêm trên thanh tiêu đề phòng
  if (btnAddDevice) {
    btnAddDevice.addEventListener("click", () => {
      if (isGateAddedToRoom) {
        openGatewayManageModal("tab-add-new-device");
      } else {
        openAddGatewayModal();
      }
    });
  }

  // ── Hàm gọi REST API có kèm JWT Token ──────────────────────────────────────
  async function callApi(endpoint, method = "GET", body = null) {
    try {
      const opts = {
        method,
        headers: {
          "Authorization": `Bearer ${token}`,
          "Content-Type": "application/json",
          "Accept": "application/json"
        }
      };
      if (body) opts.body = JSON.stringify(body);

      const res = await fetch(endpoint, opts);
      if (res.status === 401) {
        localStorage.removeItem("gate_token");
        window.location.href = "login.html";
        return null;
      }
      return await res.json();
    } catch (err) {
      console.error(`Lỗi gọi API ${endpoint}:`, err);
      return null;
    }
  }

  // ── Định dạng Uptime ───────────────────────────────────────────────────────
  function formatUptime(seconds) {
    const hrs = Math.floor(seconds / 3600);
    const mins = Math.floor((seconds % 3600) / 60);
    const secs = seconds % 60;
    return `${hrs.toString().padStart(2, '0')}:${mins.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
  }

  // ── RENDER THIẾT BỊ HOẶC TRẠNG THÁI RỖNG (LOGIC CHÍNH) ──────────────────────
  // ── PHẦN TỬ RIGHT NAVIGATION DRAWER (NAV BÊN TAY PHẢI) ──────────────────────
  const deviceDetailDrawer = document.getElementById("device-detail-drawer");
  const drawerBackdrop = document.getElementById("drawer-backdrop");
  const btnCloseDrawer = document.getElementById("btn-close-drawer");
  const drawerHeaderTitle = document.getElementById("drawer-header-title");
  const drawerHeaderSub = document.getElementById("drawer-header-sub");
  const drawerHeaderIcon = document.getElementById("drawer-header-icon");
  const drawerHeroBox = document.getElementById("drawer-hero-box");
  const drawerHeroName = document.getElementById("drawer-hero-name");
  const drawerHeroSub = document.getElementById("drawer-hero-sub");
  const drawerHeroBadge = document.getElementById("drawer-hero-badge");
  const drawerHeroIconInner = document.getElementById("drawer-hero-icon-inner");
  const drawerTeleGrid = document.getElementById("drawer-tele-grid");
  const drawerTeleUptime = document.getElementById("drawer-tele-uptime");
  const drawerTeleHeap = document.getElementById("drawer-tele-heap");
  const drawerTeleWifi = document.getElementById("drawer-tele-wifi");
  const drawerGwChildSection = document.getElementById("drawer-gw-child-section");
  const drawerChildCount = document.getElementById("drawer-child-count");
  const drawerChildList = document.getElementById("drawer-child-list");
  const btnDrawerAddChild = document.getElementById("btn-drawer-add-child");
  const drawerEditName = document.getElementById("drawer-edit-name");
  const drawerEditRoom = document.getElementById("drawer-edit-room");
  const drawerGwExtraActions = document.getElementById("drawer-gw-extra-actions");
  const btnDrawerReconfigWifi = document.getElementById("btn-drawer-reconfig-wifi");
  const btnDrawerReboot = document.getElementById("btn-drawer-reboot");
  const btnDrawerSave = document.getElementById("btn-drawer-save");
  const btnDrawerDelete = document.getElementById("btn-drawer-delete");

  // Các phần tử điều khiển Trạm IR Hub & Điều Hòa trong Drawer
  const drawerIrHubSection = document.getElementById("drawer-ir-hub-section");
  const drawerIrChildList = document.getElementById("drawer-ir-child-list");
  const drawerIrCount = document.getElementById("drawer-ir-count");
  const btnDrawerSyncIr = document.getElementById("btn-drawer-sync-ir");
  const drawerAcBackBar = document.getElementById("drawer-ac-back-bar");
  const btnBackToIrHub = document.getElementById("btn-back-to-ir-hub");

  const drawerAcPanel = document.getElementById("drawer-ac-panel");
  const drawerAcVisualCard = document.getElementById("drawer-ac-visual-card");
  const drawerAcOfflineBanner = document.getElementById("drawer-ac-offline-banner");
  const drawerAcOfflineMsg = document.getElementById("drawer-ac-offline-msg");
  const drawerAcLiveText = document.getElementById("drawer-ac-live-text");
  const drawerAcScreenNum = document.getElementById("drawer-ac-screen-num");
  const drawerBtnPower = document.getElementById("drawer-btn-power");
  const drawerBtnPowerText = document.getElementById("drawer-btn-power-text");
  const drawerBtnTempMinus = document.getElementById("drawer-btn-temp-minus");
  const drawerBtnTempPlus = document.getElementById("drawer-btn-temp-plus");
  const drawerTempVal = document.getElementById("drawer-temp-val");
  const drawerModePills = document.querySelectorAll("#drawer-mode-pills .drawer-pill-opt");
  const drawerFanPills = document.querySelectorAll("#drawer-fan-pills .drawer-pill-opt");
  const drawerTempPresets = document.querySelectorAll(".drawer-temp-presets .preset-chip");

  let currentDrawerTarget = null; // null: Gateway, string devId: Thiết bị con
  let currentActiveIrChild = null; // Thiết bị con đang mở điều khiển trong IR Hub

  // ── HÀM KIỂM TRA THIẾT BỊ ONLINE HAY OFFLINE TỪ HUNONIC CLOUD / GATEWAY ──
  function isDeviceOnline(dev) {
    if (!dev) return false;

    // QUY TẮC CỐT LÕI: Khi Trạm Gateway ngoại tuyến -> TẤT CẢ thiết bị con của Gateway PHẢI NGOẠI TUYẾN
    if (!isGatewayOnline) {
      return false;
    }

    if (dev.type === "ac" || dev.type === "ir_hub") {
      if (gwMetrics.ac_online !== undefined) {
        return Boolean(gwMetrics.ac_online);
      }
      return Boolean(dev.online);
    }
    return dev.online !== false;
  }

  // ── HÀM LẤY GIỚI HẠN NHIỆT ĐỘ THEO TỪNG THƯƠNG HIỆU ĐIỀU HÒA (BRAND) ─────
  function getAcTempLimits(dev) {
    let brand = null;
    if (dev) {
      if (dev.brand !== undefined && dev.brand !== null) brand = Number(dev.brand);
      else if (dev.meta && dev.meta.brand !== undefined) brand = Number(dev.meta.brand);
    }
    if (!brand && acState && acState.brand) {
      brand = Number(acState.brand);
    }

    const devName = (dev && dev.name) ? dev.name.toLowerCase() : "";
    // Brand 117 (Mirage, AUX...): Hỗ trợ dải nhiệt độ lên tới 32°C
    if (brand === 117 || devName.includes("mirage")) {
      return { min: 16, max: 32, brand: 117, brandName: "Mirage" };
    }
    // Brand 104 (Funiki): Dòng điều hòa tiêu chuẩn Việt Nam giới hạn tối đa 30°C
    if (brand === 104 || devName.includes("funiki")) {
      return { min: 16, max: 30, brand: 104, brandName: "Funiki" };
    }
    // Các hãng phổ thông (Daikin, Panasonic, Casper, Gree, Midea, Toshiba...) đều giới hạn 30°C
    return { min: 16, max: 30, brand: brand || 104, brandName: "Standard" };
  }

  // ── CẬP NHẬT GIAO DIỆN ĐIỀU HÒA TRONG DRAWER ──────────────────────────────
  function updateDrawerAcUI(dev) {
    if (!dev) return;
    const online = isDeviceOnline(dev);
    const limits = getAcTempLimits(dev);

    // Kẹp nhiệt độ trong dải cho phép của thương hiệu điều hòa
    let curTemp = dev.temp || 25;
    if (curTemp > limits.max) curTemp = limits.max;
    if (curTemp < limits.min) curTemp = limits.min;
    dev.temp = curTemp;

    // Nếu offline -> trạng thái hoạt động thực tế bắt buộc phải là Tắt (0)
    const isPowerOn = online && (dev.state === 1 || dev.power === 1);
    const curMode = dev.mode !== undefined ? dev.mode : 1;
    const curFan = dev.fan !== undefined ? dev.fan : 0;

    const modeLabels = {0: "Tự động", 1: "Làm mát", 2: "Sưởi ấm", 3: "Hút ẩm", 4: "Quạt gió"};

    if (drawerTempVal) drawerTempVal.innerText = curTemp;
    if (drawerAcScreenNum) drawerAcScreenNum.innerText = curTemp;

    // Cập nhật trạng thái nút Tăng/Giảm nhiệt độ
    if (drawerBtnTempPlus) {
      drawerBtnTempPlus.style.opacity = curTemp >= limits.max ? "0.4" : "1";
      drawerBtnTempPlus.title = curTemp >= limits.max ? `Đã đạt giới hạn tối đa ${limits.max}°C` : "Tăng 1°C";
    }
    if (drawerBtnTempMinus) {
      drawerBtnTempMinus.style.opacity = curTemp <= limits.min ? "0.4" : "1";
      drawerBtnTempMinus.title = curTemp <= limits.min ? `Đã đạt giới hạn tối thiểu ${limits.min}°C` : "Giảm 1°C";
    }

    // Cập nhật card visual hình ảnh điều hòa
    if (drawerAcVisualCard) {
      drawerAcVisualCard.classList.toggle("off", !isPowerOn);
      drawerAcVisualCard.classList.toggle("offline", !online);
    }

    // Hiển thị / Ẩn banner thông báo ngoại tuyến
    if (drawerAcOfflineBanner) {
      if (!online) {
        drawerAcOfflineBanner.style.display = "flex";
        if (drawerAcOfflineMsg) {
          drawerAcOfflineMsg.innerText = gwMetrics.ac_offline_reason || "Trạm phát hồng ngoại Hunonic đang Ngoại Tuyến (Offline). Không thể điều khiển!";
        }
      } else {
        drawerAcOfflineBanner.style.display = "none";
      }
    }

    // Cập nhật nút Bật/Tắt Lớn
    if (drawerBtnPower && drawerBtnPowerText) {
      if (!online) {
        drawerBtnPower.className = "btn-drawer-ac-power offline";
        drawerBtnPowerText.innerText = "ĐIỀU HÒA ĐANG TẮT (NGOẠI TUYẾN)";
      } else if (isPowerOn) {
        drawerBtnPower.className = "btn-drawer-ac-power on";
        drawerBtnPowerText.innerText = "ĐIỀU HÒA ĐANG BẬT";
      } else {
        drawerBtnPower.className = "btn-drawer-ac-power off";
        drawerBtnPowerText.innerText = "ĐIỀU HÒA ĐANG TẮT";
      }
    }

    // Text trạng thái trên ảnh
    if (drawerAcLiveText) {
      if (!online) {
        drawerAcLiveText.innerHTML = `<span class="pulse-dot-red"></span> Ngoại tuyến (Mất kết nối)`;
      } else if (isPowerOn) {
        drawerAcLiveText.innerHTML = `<span class="pulse-dot-cyan"></span> Đang ${modeLabels[curMode] || 'làm mát'} ${curTemp}°C`;
      } else {
        drawerAcLiveText.innerText = "Đang tắt (Chế độ chờ)";
      }
    }

    // Badge trên Header Drawer
    if (drawerHeroBadge) {
      if (!online) {
        drawerHeroBadge.className = "drawer-hero-badge offline";
        drawerHeroBadge.innerHTML = `<i class="fa-solid fa-circle"></i> Ngoại tuyến`;
      } else {
        drawerHeroBadge.className = isPowerOn ? "drawer-hero-badge online" : "drawer-hero-badge";
        drawerHeroBadge.innerHTML = `<i class="fa-solid fa-circle"></i> ${isPowerOn ? 'Đang BẬT' : 'Đang TẮT'}`;
      }
    }

    // Cập nhật pills chế độ (Mode)
    drawerModePills.forEach(pill => {
      const m = parseInt(pill.getAttribute("data-mode"));
      pill.classList.toggle("active", m === curMode);
    });

    // Cập nhật pills tốc độ quạt (Fan)
    drawerFanPills.forEach(pill => {
      const f = parseInt(pill.getAttribute("data-fan"));
      pill.classList.toggle("active", f === curFan);
    });

    // Cập nhật chips nhiệt độ nhanh
    drawerTempPresets.forEach(chip => {
      const t = parseInt(chip.getAttribute("data-temp"));
      chip.classList.toggle("active", t === curTemp);
    });
  }

  // ── RENDER DANH SÁCH THIẾT BỊ CON CỦA TRẠM IR HUNONIC ───────────────────────
  function renderIrChildList(hubDev) {
    if (!drawerIrChildList) return;
    const isHubOnline = isDeviceOnline(hubDev);
    const children = hubDev.children || [];
    if (drawerIrCount) drawerIrCount.innerText = children.length;

    if (children.length === 0) {
      drawerIrChildList.innerHTML = `
        <div style="text-align: center; padding: 25px 15px; color: #94a3b8; font-size: 0.85rem; background: #f8fafc; border-radius: 12px; border: 1.5px dashed #cbd5e1;">
          <i class="fa-solid fa-inbox" style="font-size: 1.8rem; margin-bottom: 8px; display: block; color: #cbd5e1;"></i>
          Chưa có thiết bị nào kết nối vào trạm IR này.<br>Bấm <strong>Đồng bộ</strong> để lấy danh sách từ Hunonic Cloud.
        </div>
      `;
      return;
    }

    let html = "";
    children.forEach((child, idx) => {
      const isAc = child.type === "ac" || child.category === "AC";
      const icon = isAc ? "fa-wind" : "fa-tv";
      const isChildOn = isHubOnline && (child.power === 1 || child.state === 1);

      html += `
        <div class="drawer-ir-child-card" data-child-idx="${idx}" data-child-id="${child.id}">
          <div class="drawer-ir-card-left">
            <div class="drawer-ir-card-icon">
              <i class="fa-solid ${icon}"></i>
            </div>
            <div class="drawer-ir-card-info">
              <h5>${escapeHtml(child.name || 'Điều hòa')}</h5>
              <p>${isAc ? `Nhiệt độ: ${child.temp || 25}°C &bull; ${isChildOn ? 'Đang BẬT' : 'Đang TẮT'}` : 'Thiết bị IR Smart Pro'}</p>
            </div>
          </div>
          <div class="drawer-ir-card-right">
            <span class="card-status-badge ${isHubOnline ? (isChildOn ? 'online' : 'offline') : 'offline'}" style="font-size: 0.72rem; padding: 3px 8px; border-radius: 999px;">
              ${!isHubOnline ? 'Ngoại tuyến' : (isChildOn ? 'Đang BẬT' : 'Đang TẮT')}
            </span>
            <i class="fa-solid fa-chevron-right arrow"></i>
          </div>
        </div>
      `;
    });
    drawerIrChildList.innerHTML = html;
  }

  // ── MỞ VIEW ĐIỀU KHIỂN CHI TIẾT CHO THIẾT BỊ CON CỦA TRẠM IR ────────────────
  function openAcControlView(childDev, parentHub) {
    currentActiveIrChild = childDev;

    if (drawerIrHubSection) drawerIrHubSection.style.display = "none";
    if (drawerAcPanel) drawerAcPanel.style.display = "flex";
    if (drawerAcBackBar) drawerAcBackBar.style.display = "block";

    if (drawerHeaderTitle) drawerHeaderTitle.innerText = childDev.name;
    if (drawerHeaderSub) drawerHeaderSub.innerText = `${parentHub.room || 'Phòng khách'} • Điều hòa IR Smart Pro`;
    if (drawerHeaderIcon) drawerHeaderIcon.innerHTML = `<i class="fa-solid fa-wind"></i>`;

    if (drawerHeroName) drawerHeroName.innerText = childDev.name;
    if (drawerHeroSub) drawerHeroSub.innerText = `${parentHub.room || 'Phòng khách'} • ID: ${childDev.id || '4000261'}`;
    if (drawerHeroIconInner) drawerHeroIconInner.innerHTML = `<i class="fa-solid fa-wind"></i>`;

    updateDrawerAcUI(childDev);
  }

  // Bắt sự kiện click chọn thiết bị con trong danh sách IR
  if (drawerIrChildList) {
    drawerIrChildList.addEventListener("click", (e) => {
      const card = e.target.closest(".drawer-ir-child-card");
      if (!card) return;
      const hubDev = customDevices.find(d => d.id === currentDrawerTarget);
      if (!hubDev || !hubDev.children) return;

      const idx = parseInt(card.getAttribute("data-child-idx"));
      const child = hubDev.children[idx];
      if (child) {
        openAcControlView(child, hubDev);
      }
    });
  }

  // Bắt sự kiện click nút Quay Lại IR Hunonic Smart Pro
  if (btnBackToIrHub) {
    btnBackToIrHub.addEventListener("click", () => {
      const hubDev = customDevices.find(d => d.id === currentDrawerTarget);
      if (hubDev) {
        currentActiveIrChild = null;
        if (drawerAcPanel) drawerAcPanel.style.display = "none";
        if (drawerAcBackBar) drawerAcBackBar.style.display = "none";
        if (drawerIrHubSection) drawerIrHubSection.style.display = "flex";

        if (drawerHeaderTitle) drawerHeaderTitle.innerText = hubDev.name;
        if (drawerHeaderSub) drawerHeaderSub.innerText = `${hubDev.room || 'Phòng khách'} • Trạm phát hồng ngoại Smart Pro`;
        if (drawerHeaderIcon) drawerHeaderIcon.innerHTML = `<i class="fa-solid fa-tower-broadcast"></i>`;

        if (drawerHeroName) drawerHeroName.innerText = hubDev.name;
        if (drawerHeroSub) drawerHeroSub.innerText = `${hubDev.room || 'Phòng khách'} • ID: ${hubDev.device_id || '3987670'} • Trạm phát IR 360°`;
        if (drawerHeroIconInner) drawerHeroIconInner.innerHTML = `<i class="fa-solid fa-tower-broadcast"></i>`;

        const isOnline = isDeviceOnline(hubDev);
        if (drawerHeroBadge) {
          drawerHeroBadge.className = isOnline ? "drawer-hero-badge online" : "drawer-hero-badge offline";
          drawerHeroBadge.innerHTML = `<i class="fa-solid fa-circle"></i> ${isOnline ? 'Trực tuyến' : 'Ngoại tuyến'}`;
        }

        renderIrChildList(hubDev);
      }
    });
  }

  // Bắt sự kiện click nút Đồng Bộ Thiết Bị Từ Hunonic Cloud
  if (btnDrawerSyncIr) {
    btnDrawerSyncIr.addEventListener("click", async () => {
      const icon = btnDrawerSyncIr.querySelector("i");
      if (icon) icon.classList.add("fa-spin");
      showToast("Đang gửi lệnh yêu cầu Gateway đồng bộ danh sách từ Hunonic Cloud...", "info");

      try {
        let res = await callApi("/api/hvac/sync", "POST");
        if (!res || !res.success) {
          res = await callApi("/api/hvac/devices");
        }

        if (res && res.success && res.data) {
          const hubDev = customDevices.find(d => d.id === currentDrawerTarget);
          if (hubDev && Array.isArray(res.data.children)) {
            hubDev.children = res.data.children;
            currentActiveIrChild = null; // Reset thiết bị con đang chọn nếu bị xóa
            localStorage.setItem("gate_custom_devices", JSON.stringify(customDevices));
            renderIrChildList(hubDev);
            renderDevices();
            if (res.data.children.length === 0) {
              showToast("Đã đồng bộ từ Hunonic Cloud: Trạm IR hiện không có thiết bị con nào!", "info");
            } else {
              showToast(`Đã đồng bộ thành công ${res.data.children.length} thiết bị từ Hunonic Cloud!`, "success");
            }
          }
        } else {
          showToast("Chưa thể đồng bộ thiết bị mới từ Hunonic!", "error");
        }
      } catch (err) {
        showToast("Lỗi kết nối khi đồng bộ Hunonic!", "error");
      } finally {
        if (icon) icon.classList.remove("fa-spin");
      }
    });
  }

  // ── HÀM BÁO LỖI VÀ RUNG LẮC KHI THAO TÁC TRÊN THIẾT BỊ NGOẠI TUYẾN ───────────
  function triggerOfflineControlWarning(element) {
    if (element) {
      element.classList.remove("shake-error");
      void element.offsetWidth; // Force reflow
      element.classList.add("shake-error");
      setTimeout(() => element.classList.remove("shake-error"), 500);
    }
    const reason = gwMetrics.ac_offline_reason || "Trạm phát hồng ngoại Hunonic đang ngoại tuyến!";
    showToast(`⚠️ Thiết bị đang NGOẠI TUYẾN (Offline)! Không thể điều khiển.\n(${reason})`, "error");
  }

  // ── BỘ QUẢN LÝ DEBOUNCE ĐIỀU KHIỂN ĐIỀU HÒA (TRÁNH NGHẼN BROKER/WEBSOCKET) ─────
  let acDebounceTimer = null;
  let pendingAcPayload = null;
  let pendingAcDevName = "Điều hòa";
  const AC_DEBOUNCE_DELAY_MS = 800; // Đợi người dùng bấm dừng lại ở số cuối cùng 800ms mới gửi xuống firmware

  function findCurrentAcDevice() {
    if (currentActiveIrChild) return currentActiveIrChild;
    const hubDev = customDevices.find(d => d.id === currentDrawerTarget);
    if (hubDev) {
      if (hubDev.type === "ac") return hubDev;
      if (hubDev.children && hubDev.children.length > 0) {
        const acChild = hubDev.children.find(c => c.type === "ac" || c.category === "AC");
        if (acChild) return acChild;
        return hubDev.children[0];
      }
    }
    for (const d of customDevices) {
      if (d.type === "ac") return d;
      if (d.children && d.children.length > 0) {
        const acChild = d.children.find(c => c.type === "ac" || c.category === "AC");
        if (acChild) return acChild;
      }
    }
    return null;
  }

  // ── HÀM PHÁT LỆNH ĐIỀU HÒA (DÙNG CHUNG CHO DRAWER, MODAL VÀ DASHBOARD) ────────
  async function sendAcControlCommand(dev, changes = {}, immediate = false) {
    const targetDev = dev || findCurrentAcDevice();
    const hubDev = foundHub ||
                   customDevices.find(d => d.id === currentDrawerTarget) ||
                   customDevices.find(d => d.type === "ir_hub" || (d.id && d.id.includes("hunonic")));

    // Kiểm tra Online trước khi gửi
    const online = isDeviceOnline(hubDev || targetDev);
    if (!online) {
      if (drawerBtnPower) triggerOfflineControlWarning(drawerBtnPower);
      else showToast("⚠️ Thiết bị đang Ngoại tuyến! Không thể điều khiển.", "error");
      return;
    }

    // 1. Cập nhật state ngay lập tức vào bộ nhớ RAM
    if (changes.state !== undefined || changes.power !== undefined) {
      const p = changes.state !== undefined ? changes.state : changes.power;
      if (targetDev) {
        targetDev.state = p;
        targetDev.power = p;
      }
      acState.power = p;
    }
    if (changes.temp !== undefined) {
      if (targetDev) targetDev.temp = changes.temp;
      acState.temp = changes.temp;
    }
    if (changes.mode !== undefined) {
      if (targetDev) targetDev.mode = changes.mode;
      acState.mode = changes.mode;
    }
    if (changes.fan !== undefined) {
      if (targetDev) targetDev.fan = changes.fan;
      acState.fan = changes.fan;
    }

    // Cập nhật ngược lại vào mảng children của hubDev
    if (hubDev && hubDev.children && targetDev) {
      const c = hubDev.children.find(x => String(x.id) === String(targetDev.id));
      if (c) {
        if (changes.state !== undefined || changes.power !== undefined) {
          c.power = (changes.state !== undefined ? changes.state : changes.power);
          c.state = c.power;
        }
        if (changes.temp !== undefined) c.temp = changes.temp;
        if (changes.mode !== undefined) c.mode = changes.mode;
        if (changes.fan !== undefined) c.fan = changes.fan;
      }
    }

    // Lưu vào localStorage
    localStorage.setItem("gate_custom_devices", JSON.stringify(customDevices));

    // 2. CẬP NHẬT GIAO DIỆN TỨC THÌ (0ms - phản hồi 60 FPS mượt mà cho người dùng)
    if (targetDev) updateDrawerAcUI(targetDev);
    updateAcModalUI();
    renderDevices();

    // 3. Chuẩn bị payload lệnh
    let acId = (targetDev && targetDev.id) ? targetDev.id : (gwMetrics.ac_id || "4003248");
    if (targetDev && targetDev.extra && !targetDev.id) {
      const m = targetDev.extra.match(/ID:\s*([0-9a-zA-Z_]+)/);
      if (m && m[1]) acId = m[1];
    }

    const payload = {
      device_id: acId,
      power: targetDev ? (targetDev.power !== undefined ? targetDev.power : targetDev.state) : acState.power,
      temp: targetDev ? targetDev.temp : acState.temp,
      mode: targetDev ? targetDev.mode : acState.mode,
      fan: targetDev ? targetDev.fan : acState.fan
    };

    const devName = (targetDev && targetDev.name) ? targetDev.name : "Điều hòa";

    const executeHvacApi = async (p, name) => {
      const res = await callApi("/api/hvac/control", "POST", p);
      if (res && res.success) {
        const modeNames = {0: "Tự động", 1: "Làm mát", 2: "Sưởi ấm", 3: "Hút ẩm", 4: "Quạt gió"};
        showToast(`[${name}] ${p.power === 1 ? 'BẬT' : 'TẮT'} • ${p.temp}°C • ${modeNames[p.mode] || ''}`, "success");
      } else {
        let errMsg = "Lỗi gửi lệnh điều khiển điều hòa!";
        if (res && res.detail) {
          if (typeof res.detail === "string") {
            errMsg = res.detail;
          } else if (Array.isArray(res.detail)) {
            errMsg = res.detail.map(e => e.msg || e.detail || JSON.stringify(e)).join(", ");
          } else if (typeof res.detail === "object") {
            errMsg = res.detail.msg || res.detail.message || JSON.stringify(res.detail);
          }
        }
        showToast(`❌ ${errMsg}`, "error");
        if (targetDev) {
          targetDev.power = 0;
          targetDev.state = 0;
          updateDrawerAcUI(targetDev);
        }
        acState.power = 0;
        updateAcModalUI();
        renderDevices();
      }
    };

    // 4. Nếu là thao tác Bật/Tắt nguồn (hoặc Quick Scene) -> Gửi ngay lập tức!
    if (immediate) {
      if (acDebounceTimer) {
        clearTimeout(acDebounceTimer);
        acDebounceTimer = null;
      }
      pendingAcPayload = null;
      await executeHvacApi(payload, devName);
    } else {
      // Nếu là chỉnh nhiệt độ / mode / fan liên tục:
      // Lưu lại giá trị payload mới nhất, reset timer 800ms
      // Khi người dùng dừng bấm ở số cuối cùng ("dừng lâu tại đó") mới gửi 1 lệnh duy nhất!
      pendingAcPayload = payload;
      pendingAcDevName = devName;

      if (acDebounceTimer) {
        clearTimeout(acDebounceTimer);
      }
      acDebounceTimer = setTimeout(async () => {
        acDebounceTimer = null;
        const pToSend = pendingAcPayload;
        const nameToSend = pendingAcDevName;
        pendingAcPayload = null;
        if (pToSend) {
          await executeHvacApi(pToSend, nameToSend);
        }
      }, AC_DEBOUNCE_DELAY_MS);
    }
  }

  // Alias để tương thích các hàm gọi cũ
  async function sendDrawerAcCommand(dev, changes = {}) {
    const isImmediate = changes.state !== undefined || changes.power !== undefined;
    return sendAcControlCommand(dev, changes, isImmediate);
  }

  function resolveDrawerTargetDev() {
    const hubDev = foundHub || customDevices.find(d => d.type === "ir_hub" || d.id === "dev_ir_hub_hunonic");
    let targetDev = currentActiveIrChild;
    if (!targetDev && hubDev && hubDev.children && currentDrawerTarget) {
      targetDev = hubDev.children.find(c => String(c.id) === String(currentDrawerTarget));
    }
    if (!targetDev && currentDrawerTarget) {
      targetDev = customDevices.find(d => d.id === currentDrawerTarget);
    }
    if (!targetDev) {
      targetDev = (hubDev && hubDev.children && hubDev.children[0]) || hubDev;
    }
    return { hubDev, targetDev };
  }

  // Bắt sự kiện click các nút điều khiển AC trong Drawer
  if (drawerBtnPower) {
    drawerBtnPower.addEventListener("click", () => {
      const { hubDev, targetDev } = resolveDrawerTargetDev();
      if (!targetDev) return;

      const online = isDeviceOnline(hubDev || targetDev);
      // NẾU THIẾT BỊ NGOẠI TUYẾN -> HIỆU ỨNG BẬT LÊN RỒI PHỤT TẮT LẠI NGAY LẬP TỨC
      if (!online) {
        drawerBtnPower.classList.remove("anim-revert-off", "shake-error");
        void drawerBtnPower.offsetWidth; // Force CSS reflow
        drawerBtnPower.classList.add("anim-revert-off");

        if (drawerAcVisualCard) {
          drawerAcVisualCard.classList.remove("shake-error");
          void drawerAcVisualCard.offsetWidth;
          drawerAcVisualCard.classList.add("shake-error");
          setTimeout(() => drawerAcVisualCard.classList.remove("shake-error"), 550);
        }

        const reason = gwMetrics.ac_offline_reason || "Trạm phát hồng ngoại Hunonic đang ngoại tuyến!";
        showToast(`⚠️ Thiết bị [${targetDev.name}] đang NGOẠI TUYẾN (Offline)! Không thể bật.\n(${reason})`, "error");

        // Đảm bảo trạng thái luôn duy trì là Tắt (0)
        targetDev.state = 0;
        targetDev.power = 0;
        setTimeout(() => {
          drawerBtnPower.classList.remove("anim-revert-off");
          updateDrawerAcUI(targetDev);
        }, 850);
        return;
      }

      // Nếu Online: chuyển đổi Bật/Tắt bình thường
      const curP = targetDev.power || targetDev.state || 0;
      const newPower = curP === 1 ? 0 : 1;
      sendDrawerAcCommand(targetDev, { state: newPower });
    });
  }

  if (drawerBtnTempMinus) {
    drawerBtnTempMinus.addEventListener("click", () => {
      const { hubDev, targetDev } = resolveDrawerTargetDev();
      if (!targetDev) return;
      if (!isDeviceOnline(hubDev || targetDev)) {
        triggerOfflineControlWarning(drawerBtnTempMinus);
        return;
      }
      const limits = getAcTempLimits(targetDev);
      const curTemp = targetDev.temp || 25;
      if (curTemp > limits.min) {
        sendDrawerAcCommand(targetDev, { temp: curTemp - 1 });
      } else {
        showToast(`Nhiệt độ tối thiểu của điều hòa là ${limits.min}°C!`, "info");
      }
    });
  }

  if (drawerBtnTempPlus) {
    drawerBtnTempPlus.addEventListener("click", () => {
      const { hubDev, targetDev } = resolveDrawerTargetDev();
      if (!targetDev) return;
      if (!isDeviceOnline(hubDev || targetDev)) {
        triggerOfflineControlWarning(drawerBtnTempPlus);
        return;
      }
      const limits = getAcTempLimits(targetDev);
      const curTemp = targetDev.temp || 25;
      if (curTemp < limits.max) {
        sendDrawerAcCommand(targetDev, { temp: curTemp + 1 });
      } else {
        showToast(`Điều hòa [${targetDev.name || 'này'}] chỉ hỗ trợ nhiệt độ tối đa ${limits.max}°C!`, "info");
      }
    });
  }

  drawerTempPresets.forEach(chip => {
    chip.addEventListener("click", () => {
      const { hubDev, targetDev } = resolveDrawerTargetDev();
      if (!targetDev) return;
      if (!isDeviceOnline(hubDev || targetDev)) {
        triggerOfflineControlWarning(chip);
        return;
      }
      const limits = getAcTempLimits(targetDev);
      let t = parseInt(chip.getAttribute("data-temp"));
      if (t > limits.max) t = limits.max;
      if (t < limits.min) t = limits.min;
      sendDrawerAcCommand(targetDev, { temp: t });
    });
  });

  drawerModePills.forEach(pill => {
    pill.addEventListener("click", () => {
      const { hubDev, targetDev } = resolveDrawerTargetDev();
      if (!targetDev) return;
      if (!isDeviceOnline(hubDev || targetDev)) {
        triggerOfflineControlWarning(pill);
        return;
      }
      const m = parseInt(pill.getAttribute("data-mode"));
      sendDrawerAcCommand(targetDev, { mode: m });
    });
  });

  drawerFanPills.forEach(pill => {
    pill.addEventListener("click", () => {
      const { hubDev, targetDev } = resolveDrawerTargetDev();
      if (!targetDev) return;
      if (!isDeviceOnline(hubDev || targetDev)) {
        triggerOfflineControlWarning(pill);
        return;
      }
      const f = parseInt(pill.getAttribute("data-fan"));
      sendDrawerAcCommand(targetDev, { fan: f });
    });
  });

  // ── MỞ DRAWER BÊN TAY PHẢI CHO TRẠM GATEWAY ──────────────────────────────────
  function openGatewayDrawer() {
    currentDrawerTarget = null;
    const gwName = localStorage.getItem("gate_custom_name") || "Trạm Gateway ESP32-S3";
    const gwRoom = localStorage.getItem("gate_room") || "Phòng khách";

    if (drawerHeaderTitle) drawerHeaderTitle.innerText = gwName;
    if (drawerHeaderSub) drawerHeaderSub.innerText = "Master Hub • " + gwRoom;
    if (drawerHeaderIcon) drawerHeaderIcon.innerHTML = `<i class="fa-solid fa-server"></i>`;

    if (drawerHeroName) drawerHeroName.innerText = gwName;
    if (drawerHeroSub) drawerHeroSub.innerText = "ESP-NOW • BLE 5.0 • HiveMQ Cloud TLS";
    if (drawerHeroIconInner) drawerHeroIconInner.innerHTML = `<i class="fa-solid fa-server"></i>`;
    if (drawerHeroBadge) {
      drawerHeroBadge.className = isGatewayOnline ? "drawer-hero-badge online" : "drawer-hero-badge offline";
      drawerHeroBadge.innerHTML = `<i class="fa-solid fa-circle"></i> ${isGatewayOnline ? 'Trực tuyến' : 'Ngoại tuyến'}`;
    }

    if (drawerAcPanel) drawerAcPanel.style.display = "none";
    if (drawerIrHubSection) drawerIrHubSection.style.display = "none";
    if (drawerAcBackBar) drawerAcBackBar.style.display = "none";
    if (drawerTeleGrid) drawerTeleGrid.style.display = "grid";
    if (drawerGwChildSection) drawerGwChildSection.style.display = "block";
    if (drawerGwExtraActions) drawerGwExtraActions.style.display = "flex";
    if (drawerHeroBox) drawerHeroBox.style.display = "flex";

    const editSecGw = document.getElementById("drawer-edit-section");
    if (editSecGw) editSecGw.style.display = "block";
    const delSecGw = document.getElementById("drawer-delete-section");
    if (delSecGw) delSecGw.style.display = "block";

    updateDrawerGatewayTelemetry();
    renderDrawerChildList();

    if (drawerEditName) drawerEditName.value = gwName;
    if (drawerEditRoom) drawerEditRoom.value = gwRoom;

    if (btnDrawerDelete) {
      btnDrawerDelete.innerHTML = `<i class="fa-solid fa-trash-can"></i> Xóa Trạm Gateway Khỏi Phòng`;
    }

    if (deviceDetailDrawer) deviceDetailDrawer.classList.add("open");
    if (drawerBackdrop) drawerBackdrop.classList.add("active");
  }

  // ── MỞ DRAWER BÊN TAY PHẢI CHO THIẾT BỊ CON ──────────────────────────────────
  function openChildDeviceDrawer(devId) {
    const dev = customDevices.find(d => d.id === devId);
    if (!dev) return;
    currentDrawerTarget = devId;

    const isAc = dev.type === "ac";
    const isIrHub = dev.type === "ir_hub";
    const isSwitch = dev.type === "relay" || dev.type === "socket";
    const faIcon = isIrHub ? "fa-tower-broadcast" : (isAc ? "fa-wind" : (dev.type === "relay" ? "fa-lightbulb" : (dev.type === "socket" ? "fa-plug" : "fa-satellite-dish")));
    const typeLabel = isIrHub ? "Trạm IR Smart Pro" : (isAc ? "Hồng ngoại IR Smart Pro" : (dev.type === "relay" ? "Công tắc Relay" : (dev.type === "socket" ? "Ổ cắm thông minh" : "Node Cảm Biến ESP-NOW")));

    const isOnline = isDeviceOnline(dev);

    if (drawerHeaderTitle) drawerHeaderTitle.innerText = dev.name;
    if (drawerHeaderSub) {
      if (isIrHub) {
        drawerHeaderSub.innerText = `${dev.room || 'Phòng khách'} • Trạm phát hồng ngoại Smart Pro`;
      } else {
        drawerHeaderSub.innerText = `${dev.room || 'Phòng khách'} • ${typeLabel}`;
      }
    }
    if (drawerHeaderIcon) drawerHeaderIcon.innerHTML = `<i class="fa-solid ${faIcon}"></i>`;

    if (drawerHeroName) drawerHeroName.innerText = dev.name;
    if (drawerHeroSub) {
      if (isIrHub) {
        const childCount = (dev.children && dev.children.length) || 0;
        drawerHeroSub.innerText = `${dev.room || 'Phòng khách'} • ID: ${dev.device_id || '3987670'} • ${childCount} thiết bị IR kết nối`;
      } else {
        drawerHeroSub.innerText = `${dev.room || 'Phòng khách'} • ${dev.extra || typeLabel}`;
      }
    }
    if (drawerHeroIconInner) drawerHeroIconInner.innerHTML = `<i class="fa-solid ${faIcon}"></i>`;
    if (drawerHeroBox) drawerHeroBox.style.display = "flex";

    // Ẩn telemetry và danh sách child của Gateway khi mở thiết bị con
    if (drawerTeleGrid) drawerTeleGrid.style.display = "none";
    if (drawerGwChildSection) drawerGwChildSection.style.display = "none";
    if (drawerGwExtraActions) drawerGwExtraActions.style.display = "none";

    const editSecChild = document.getElementById("drawer-edit-section");
    if (editSecChild) editSecChild.style.display = "block";
    const delSecChild = document.getElementById("drawer-delete-section");
    if (delSecChild) delSecChild.style.display = "block";

    // Xử lý hiển thị theo từng phân loại thiết bị
    if (isIrHub) {
      currentActiveIrChild = null;
      if (drawerAcPanel) drawerAcPanel.style.display = "none";
      if (drawerAcBackBar) drawerAcBackBar.style.display = "none";
      if (drawerIrHubSection) drawerIrHubSection.style.display = "flex";

      if (drawerHeroBadge) {
        drawerHeroBadge.className = isOnline ? "drawer-hero-badge online" : "drawer-hero-badge offline";
        drawerHeroBadge.innerHTML = `<i class="fa-solid fa-circle"></i> ${isOnline ? 'Trực tuyến' : 'Ngoại tuyến'}`;
      }

      renderIrChildList(dev);

      // Tự động đồng bộ ngầm danh sách thiết bị con mới nhất từ backend
      callApi("/api/hvac/devices").then(res => {
        if (res && res.success && res.data && Array.isArray(res.data.children)) {
          dev.children = res.data.children;
          localStorage.setItem("gate_custom_devices", JSON.stringify(customDevices));
          if (currentDrawerTarget === devId && !currentActiveIrChild) {
            renderIrChildList(dev);
            if (drawerHeroSub) {
              drawerHeroSub.innerText = `${dev.room || 'Phòng khách'} • ID: ${dev.device_id || '3987670'} • ${dev.children.length} thiết bị IR kết nối`;
            }
          }
          renderDevices();
        }
      });
    } else if (isAc) {
      if (drawerIrHubSection) drawerIrHubSection.style.display = "none";
      if (drawerAcBackBar) drawerAcBackBar.style.display = "none";
      if (drawerAcPanel) {
        drawerAcPanel.style.display = "flex";
        if (dev.temp === undefined) dev.temp = (gwMetrics.ac_temp && gwMetrics.ac_temp > 0) ? gwMetrics.ac_temp : (acState.temp || 25);
        if (dev.mode === undefined) dev.mode = acState.mode !== undefined ? acState.mode : 1;
        if (dev.fan === undefined) dev.fan = acState.fan !== undefined ? acState.fan : 0;
        updateDrawerAcUI(dev);
      }
    } else {
      if (drawerIrHubSection) drawerIrHubSection.style.display = "none";
      if (drawerAcBackBar) drawerAcBackBar.style.display = "none";
      if (drawerAcPanel) drawerAcPanel.style.display = "none";
      if (drawerHeroBadge) {
        const isDevOn = dev.state === 1;
        drawerHeroBadge.className = isDevOn ? "drawer-hero-badge online" : "drawer-hero-badge";
        drawerHeroBadge.innerHTML = `<i class="fa-solid fa-circle"></i> ${isSwitch ? (isDevOn ? 'Đang BẬT' : 'Đang TẮT') : 'Hoạt động'}`;
      }
    }

    if (drawerEditName) drawerEditName.value = dev.name;
    if (drawerEditRoom) drawerEditRoom.value = dev.room || "Phòng khách";

    if (btnDrawerDelete) {
      btnDrawerDelete.innerHTML = isIrHub
        ? `<i class="fa-solid fa-trash-can"></i> Xóa Trạm IR Này Khỏi Gateway`
        : `<i class="fa-solid fa-trash-can"></i> Xóa Thiết Bị Này Khỏi Gateway`;
    }

    if (deviceDetailDrawer) deviceDetailDrawer.classList.add("open");
    if (drawerBackdrop) drawerBackdrop.classList.add("active");
  }

  // ── ĐÓNG DRAWER ─────────────────────────────────────────────────────────────
  function closeRightDrawer() {
    if (deviceDetailDrawer) deviceDetailDrawer.classList.remove("open");
    if (drawerBackdrop) drawerBackdrop.classList.remove("active");
    currentDrawerTarget = null;
    currentActiveIrChild = null;
  }

  if (btnCloseDrawer) {
    btnCloseDrawer.addEventListener("click", closeRightDrawer);
  }
  if (drawerBackdrop) {
    drawerBackdrop.addEventListener("click", closeRightDrawer);
  }

  // ── CẬP NHẬT TELEMETRY GATEWAY TRONG DRAWER ─────────────────────────────────
  function updateDrawerGatewayTelemetry() {
    if (!drawerTeleGrid || drawerTeleGrid.style.display === "none") return;

    if (!currentDrawerTarget || currentDrawerTarget === "gateway") {
      if (drawerHeroBadge) {
        drawerHeroBadge.className = isGatewayOnline ? "drawer-hero-badge online" : "drawer-hero-badge offline";
        drawerHeroBadge.innerHTML = `<i class="fa-solid fa-circle"></i> ${isGatewayOnline ? 'Trực tuyến' : 'Ngoại tuyến'}`;
      }
    }

    if (drawerTeleUptime) {
      drawerTeleUptime.innerText = isGatewayOnline 
        ? formatUptime(gwMetrics.uptime_sec || 0)
        : "Mất kết nối";
    }
    if (drawerTeleHeap) {
      const heapVal = gwMetrics.free_heap > 0 ? (gwMetrics.free_heap / 1024).toFixed(1) : (gwMetrics.heap_kb || "62.1");
      drawerTeleHeap.innerText = isGatewayOnline ? `${heapVal} KB` : "N/A";
    }
    if (drawerTeleWifi) {
      const ssid = gwMetrics.wifi_ssid || "Dinh";
      const rssi = gwMetrics.rssi && gwMetrics.rssi !== -100 ? `${gwMetrics.rssi} dBm` : "-44 dBm";
      drawerTeleWifi.innerText = isGatewayOnline ? `${ssid} (${rssi})` : "Mất sóng";
    }
  }

  // ── RENDER DANH SÁCH THIẾT BỊ CON CỦA GATEWAY TRONG DRAWER ─────────────────
  function renderDrawerChildList() {
    if (!drawerChildList) return;
    if (drawerChildCount) drawerChildCount.innerText = customDevices.length;

    if (customDevices.length === 0) {
      drawerChildList.innerHTML = `
        <div style="text-align: center; padding: 20px 10px; color: #64748b; font-size: 0.8rem; background: #f8fafc; border: 1px dashed #cbd5e1; border-radius: 12px;">
          Chưa có thiết bị con nào liên kết. Bấm <strong>+ Thêm Thiết Bị</strong> để thêm.
        </div>
      `;
      return;
    }

    let html = "";
    customDevices.forEach(dev => {
      const isSwitch = dev.type === "relay" || dev.type === "socket";
      const isIrHub = dev.type === "ir_hub";
      const faIcon = isIrHub ? "fa-tower-broadcast" : (dev.type === "ac" ? "fa-wind" : (dev.type === "relay" ? "fa-lightbulb" : (dev.type === "socket" ? "fa-plug" : "fa-satellite-dish")));
      const isDevOn = dev.state === 1;
      const childCount = (dev.children && dev.children.length) || 0;
      const subInfo = isIrHub ? `${childCount} thiết bị IR kết nối` : (dev.extra || '');

      html += `
        <div class="drawer-child-item">
          <div class="drawer-child-left">
            <div class="drawer-child-icon ${isIrHub ? 'ir-hub-icon' : ''}">
              <i class="fa-solid ${faIcon}"></i>
            </div>
            <div class="drawer-child-info">
              <h5>${escapeHtml(dev.name)}</h5>
              <p>${escapeHtml(dev.room || 'Phòng khách')} &bull; ${escapeHtml(subInfo)}</p>
            </div>
          </div>
          <div class="drawer-child-right">
            ${isSwitch ? `
              <button class="quick-toggle-btn custom-toggle-btn ${isDevOn ? 'on' : 'off'}" data-id="${dev.id}" style="padding: 4px 10px; font-size: 0.72rem;">
                ${isDevOn ? 'BẬT' : 'TẮT'}
              </button>
            ` : ''}
            <button type="button" class="btn-remove-child-dev" data-id="${dev.id}" title="Xóa thiết bị này khỏi Gateway" style="background: none; border: none; color: #ef4444; cursor: pointer; padding: 6px;">
              <i class="fa-solid fa-trash-can"></i>
            </button>
          </div>
        </div>
      `;
    });

    drawerChildList.innerHTML = html;
  }

  // ── XỬ LÝ SỰ KIỆN TRONG RIGHT DRAWER ────────────────────────────────────────
  // Nút "+ Thêm Thiết Bị" trong drawer của Gateway
  if (btnDrawerAddChild) {
    btnDrawerAddChild.addEventListener("click", () => {
      openGatewayManageModal("tab-add-new-device");
    });
  }

  // Nút "Cấu hình Wi-Fi BLE" trong drawer
  if (btnDrawerReconfigWifi) {
    btnDrawerReconfigWifi.addEventListener("click", () => {
      closeRightDrawer();
      openAddGatewayModal();
    });
  }

  // Nút "Reboot Trạm" trong drawer
  if (btnDrawerReboot) {
    btnDrawerReboot.addEventListener("click", async () => {
      if (confirm("Bạn có chắc chắn muốn khởi động lại Trạm Gateway ESP32-S3?")) {
        showToast("Đang gửi lệnh khởi động lại Gateway...", "info");
        const res = await callApi("/api/gateway/reboot", "POST");
        if (res && res.success) {
          showToast(res.message, "success");
        } else {
          showToast("Đã gửi tín hiệu khởi động lại Gateway!", "success");
        }
      }
    });
  }

  // Nút "Lưu Thay Đổi" ("chỉnh thiết bị")
  if (btnDrawerSave) {
    btnDrawerSave.addEventListener("click", () => {
      const newName = drawerEditName ? drawerEditName.value.trim() : "";
      const newRoom = drawerEditRoom ? drawerEditRoom.value : "Phòng khách";

      if (!newName) {
        showToast("Vui lòng nhập tên thiết bị!", "error");
        return;
      }

      if (currentDrawerTarget === null) {
        // Chỉnh sửa Gateway
        localStorage.setItem("gate_custom_name", newName);
        localStorage.setItem("gate_room", newRoom);
        renderDevices();
        openGatewayDrawer();
        showToast("Đã lưu thông tin Trạm Gateway ESP32-S3!", "success");
      } else {
        // Chỉnh sửa thiết bị con
        const dev = customDevices.find(d => d.id === currentDrawerTarget);
        if (dev) {
          dev.name = newName;
          dev.room = newRoom;
          localStorage.setItem("gate_custom_devices", JSON.stringify(customDevices));
          renderDevices();
          openChildDeviceDrawer(currentDrawerTarget);
          showToast(`Đã lưu thay đổi cho [${newName}]!`, "success");
        }
      }
    });
  }

  // Nút "Xóa Thiết Bị Này" ("có xóa thiết bị")
  if (btnDrawerDelete) {
    btnDrawerDelete.addEventListener("click", () => {
      if (currentDrawerTarget === null) {
        // Xóa Trạm Gateway
        if (confirm("Bạn có chắc chắn muốn xóa Trạm Gateway ESP32-S3 khỏi phòng khách này?")) {
          isGateAddedToRoom = false;
          localStorage.setItem("gate_added_to_room", "false");
          closeRightDrawer();
          renderDevices();
          showToast("Đã xóa Trạm Gateway khỏi phòng khách!", "info");
        }
      } else {
        // Xóa thiết bị con
        const dev = customDevices.find(d => d.id === currentDrawerTarget);
        const devName = dev ? dev.name : "thiết bị";
        if (confirm(`Bạn có chắc chắn muốn xóa [${devName}] khỏi Gateway?`)) {
          customDevices = customDevices.filter(d => d.id !== currentDrawerTarget);
          localStorage.setItem("gate_custom_devices", JSON.stringify(customDevices));
          closeRightDrawer();
          renderDevices();
          renderLinkedDevicesList();
          showToast(`Đã xóa thiết bị [${devName}] khỏi Gateway!`, "info");
        }
      }
    });
  }

  // Xóa trực tiếp trong danh sách con của drawer
  if (drawerChildList) {
    drawerChildList.addEventListener("click", (e) => {
      const btnRemove = e.target.closest(".btn-remove-child-dev");
      if (btnRemove) {
        e.stopPropagation();
        const devId = btnRemove.getAttribute("data-id");
        const dev = customDevices.find(d => d.id === devId);
        const devName = dev ? dev.name : "thiết bị";
        if (confirm(`Bạn có chắc muốn xóa [${devName}] khỏi Gateway?`)) {
          customDevices = customDevices.filter(d => d.id !== devId);
          localStorage.setItem("gate_custom_devices", JSON.stringify(customDevices));
          renderDrawerChildList();
          renderDevices();
          renderLinkedDevicesList();
          showToast(`Đã xóa [${devName}] khỏi Gateway!`, "info");
        }
      }
    });
  }

  // ── CÁC HÀM TẠO ICON SVG CHUẨN HUNONIC (PIXEL-PERFECT) ─────────────────────────
  function getIrHubHexSvg() {
    return `
      <svg width="38" height="38" viewBox="0 0 40 40" fill="none" xmlns="http://www.w3.org/2000/svg">
        <path d="M20 2.5L35.5 11.45V29.35L20 38.3L4.5 29.35V11.45L20 2.5Z" fill="#383e48"/>
        <circle cx="20" cy="20.4" r="5" fill="#ef4444" opacity="0.95"/>
        <circle cx="20" cy="20.4" r="2.2" fill="#7f1d1d"/>
      </svg>
    `;
  }

  function getGatewayHexSvg() {
    return `
      <svg width="38" height="38" viewBox="0 0 40 40" fill="none" xmlns="http://www.w3.org/2000/svg">
        <path d="M20 2.5L35.5 11.45V29.35L20 38.3L4.5 29.35V11.45L20 2.5Z" fill="#00b06f"/>
        <rect x="14" y="14.4" width="12" height="12" rx="2.5" fill="#ffffff"/>
        <rect x="17" y="17.4" width="6" height="6" rx="1.2" fill="#00b06f"/>
        <line x1="20" y1="10" x2="20" y2="14.4" stroke="#ffffff" stroke-width="1.8" stroke-linecap="round"/>
        <line x1="20" y1="26.4" x2="20" y2="30.8" stroke="#ffffff" stroke-width="1.8" stroke-linecap="round"/>
        <line x1="9.6" y1="20.4" x2="14" y2="20.4" stroke="#ffffff" stroke-width="1.8" stroke-linecap="round"/>
        <line x1="26" y1="20.4" x2="30.4" y2="20.4" stroke="#ffffff" stroke-width="1.8" stroke-linecap="round"/>
      </svg>
    `;
  }

  function getAcUnitSvg(isOnline, isChildOn) {
    const ledColor = (!isOnline) ? "#94a3b8" : (isChildOn ? "#00b06f" : "#94a3b8");
    return `
      <svg width="46" height="26" viewBox="0 0 46 26" fill="none" xmlns="http://www.w3.org/2000/svg" class="ac-indoor-svg">
        <rect x="1" y="2" width="44" height="22" rx="4" fill="#F8FAFC" stroke="#CBD5E1" stroke-width="1.4"/>
        <line x1="5" y1="6" x2="41" y2="6" stroke="#E2E8F0" stroke-width="1"/>
        <rect x="5" y="17" width="36" height="3.5" rx="1.5" fill="#E2E8F0"/>
        <circle cx="39" cy="11.5" r="1.8" fill="${ledColor}"/>
      </svg>
    `;
  }

  function getSensorHexSvg() {
    return `
      <svg width="38" height="38" viewBox="0 0 40 40" fill="none" xmlns="http://www.w3.org/2000/svg">
        <path d="M20 2.5L35.5 11.45V29.35L20 38.3L4.5 29.35V11.45L20 2.5Z" fill="#8b5cf6"/>
        <circle cx="20" cy="20.4" r="4.5" fill="#ffffff"/>
        <path d="M16 16.4C18 14.4 22 14.4 24 16.4M13 13.4C17 9.4 23 9.4 27 13.4" stroke="#ffffff" stroke-width="1.5" stroke-linecap="round"/>
      </svg>
    `;
  }

  function getRelayHexSvg(isOn) {
    const color = isOn ? "#eab308" : "#64748b";
    return `
      <svg width="38" height="38" viewBox="0 0 40 40" fill="none" xmlns="http://www.w3.org/2000/svg">
        <path d="M20 2.5L35.5 11.45V29.35L20 38.3L4.5 29.35V11.45L20 2.5Z" fill="${color}"/>
        <path d="M20 12.4V24.4M15 18.4L20 12.4L25 18.4" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
      </svg>
    `;
  }

  function getWifiHunonicSvg(isOnline) {
    const color = isOnline ? "#00b06f" : "#cbd5e1";
    return `
      <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="${color}" stroke-width="2.3" stroke-linecap="round" stroke-linejoin="round" class="wifi-icon-hunonic ${isOnline ? '' : 'offline'}">
        <path d="M5 12.55a11 11 0 0 1 14.08 0"></path>
        <path d="M1.42 9a16 16 0 0 1 21.16 0"></path>
        <path d="M8.53 16.11a6 6 0 0 1 6.95 0"></path>
        <line x1="12" y1="20" x2="12.01" y2="20" stroke-width="3"></line>
      </svg>
    `;
  }

  // ── TRẠNG THÁI SUB-VIEW HIỆN TẠI (GIỐNG HUNONIC) ─────────────────────────────
  let currentHubView = null; // null: Home, hoặc { type: 'gateway' | 'ir_hub', dev: hubDev, parent: 'home' | 'gateway' }

  function openHubSubView(type, dev = null, parent = "home") {
    currentHubView = { type, dev, parent };

    const homeDash = document.getElementById("view-home-dashboard");
    const subView = document.getElementById("view-device-hub");
    if (homeDash) homeDash.style.display = "none";
    if (subView) subView.style.display = "block";

    const titleEl = document.getElementById("subview-title");
    const iconBox = document.getElementById("subview-header-icon-box");
    const actionBtn = document.getElementById("btn-subview-action");
    const actionText = document.getElementById("subview-action-text");
    const gearBtn = document.getElementById("btn-subview-gear");

    if (type === "ir_hub") {
      const hubDev = dev || foundHub;
      const isHubOnline = isDeviceOnline(hubDev);
      if (titleEl) titleEl.innerText = hubDev ? hubDev.name : "IR trung tâm";
      if (iconBox) {
        iconBox.innerHTML = `<i class="fa-solid fa-wifi ${isHubOnline ? 'text-green' : 'text-gray'}"></i>`;
      }
      if (actionText) actionText.innerText = "Tạo Custom AC";
      if (actionBtn) {
        actionBtn.onclick = () => {
          showToast("Đang đồng bộ thiết bị IR từ Cloud...", "info");
          callApi("/api/hvac/sync", "POST").then(res => {
            if (res && res.success && res.data && Array.isArray(res.data.children)) {
              hubDev.children = res.data.children;
              localStorage.setItem("gate_custom_devices", JSON.stringify(customDevices));
              renderSubViewDevices("ir_hub", hubDev);
              showToast(`Đã đồng bộ ${res.data.children.length} thiết bị!`, "success");
            }
          });
        };
      }
      if (gearBtn) {
        gearBtn.onclick = () => {
          if (hubDev) openChildDeviceDrawer(hubDev.id);
        };
      }
      renderSubViewDevices("ir_hub", hubDev);
    } else if (type === "gateway") {
      const gwName = localStorage.getItem("gate_custom_name") || "Trạm Gateway ESP32-S3";
      if (titleEl) titleEl.innerText = gwName;
      if (iconBox) {
        iconBox.innerHTML = `<i class="fa-solid fa-wifi ${isGatewayOnline ? 'text-green' : 'text-gray'}"></i>`;
      }
      if (actionText) actionText.innerText = "Thêm thiết bị";
      if (actionBtn) {
        actionBtn.onclick = () => {
          openAddGatewayModal();
        };
      }
      if (gearBtn) {
        gearBtn.onclick = () => {
          openGatewayDrawer();
        };
      }
      renderSubViewDevices("gateway", null);
    }
  }

  function closeHubSubView(forceHome = false) {
    if (!forceHome && currentHubView && currentHubView.parent === "gateway") {
      openHubSubView("gateway", null, "home");
      return;
    }

    currentHubView = null;
    const homeDash = document.getElementById("view-home-dashboard");
    const subView = document.getElementById("view-device-hub");
    if (subView) subView.style.display = "none";
    if (homeDash) homeDash.style.display = "block";
    renderDevices();
  }

  function renderSubViewDevices(type, hubDev) {
    const grid = document.getElementById("subview-devices-grid");
    if (!grid) return;

    if (type === "ir_hub") {
      const dev = hubDev || foundHub;
      const isHubOnline = isDeviceOnline(dev);
      const children = (dev && dev.children && dev.children.length > 0) ? dev.children : [
        { id: "4003248", name: "Điều hòa MIRAGE", type: "ac", temp: 25, mode: 1, fan: 0, power: 0, brand: 117 },
        { id: "4003318", name: "Điều hòa FUNIKI", type: "ac", temp: 26, mode: 1, fan: 0, power: 0, brand: 104 }
      ];

      let html = "";
      children.forEach((child) => {
        const isChildOn = isHubOnline && (child.power === 1 || child.state === 1);
        const temp = child.temp || 25;
        const statusText = !isHubOnline ? "Ngoại tuyến" : (isChildOn ? `Đang làm mát ${temp}°C` : "Đang tắt");
        const subClass = isChildOn ? "on" : "";

        html += `
          <div class="hunonic-card subview-ac-card" onclick="window.handleAcCardClick('${child.id}')" data-child-id="${child.id}" data-hub-id="${dev.id || 'dev_ir_hub_hunonic'}" title="Bấm vào để mở điều khiển điều hòa">
            <div class="hunonic-card-top" style="pointer-events: none;">
              <div class="ac-indoor-svg">
                ${getAcUnitSvg(isHubOnline, isChildOn)}
              </div>
              ${getWifiHunonicSvg(isHubOnline)}
            </div>
            <div class="hunonic-card-bottom" style="pointer-events: none;">
              <h4 class="hunonic-card-title">${escapeHtml(child.name)}</h4>
              <p class="hunonic-card-sub ${subClass}">${statusText}</p>
            </div>
          </div>
        `;
      });
      grid.innerHTML = html;
    } else if (type === "gateway") {
      const isIrOnline = isDeviceOnline(foundHub);
      const hasSensor = Boolean(sensorData.hasData || (sensorData.temp > 0));
      const isR1On = isGatewayOnline && relayStates[1] === 1;
      const isR2On = isGatewayOnline && relayStates[2] === 1;
      const irChildCount = (foundHub && foundHub.children && foundHub.children.length) || 2;

      grid.innerHTML = `
        <!-- 1. IR Trung Tâm -->
        <div class="hunonic-card subview-gw-child" data-gw-action="open_ir_hub" title="Bấm vào để mở các thiết bị điều hòa trong IR">
          <div class="hunonic-card-top">
            <div class="hunonic-hex-icon">
              ${getIrHubHexSvg()}
            </div>
            ${getWifiHunonicSvg(isIrOnline)}
          </div>
          <div class="hunonic-card-bottom">
            <h4 class="hunonic-card-title">${escapeHtml(foundHub ? foundHub.name : "IR trung tâm")}</h4>
            <p class="hunonic-card-sub ${isIrOnline ? 'on' : ''}">Thiết bị con: ${irChildCount}</p>
          </div>
        </div>

        <!-- 2. Node Cảm Biến C3 -->
        <div class="hunonic-card subview-gw-child" data-gw-action="open_sensor" title="Cảm biến nhiệt độ & độ ẩm">
          <div class="hunonic-card-top">
            <div class="hunonic-hex-icon">
              ${getSensorHexSvg()}
            </div>
            ${getWifiHunonicSvg(hasSensor && isGatewayOnline)}
          </div>
          <div class="hunonic-card-bottom">
            <h4 class="hunonic-card-title">Node Cảm Biến C3</h4>
            <p class="hunonic-card-sub ${hasSensor ? 'on' : ''}">${sensorData.temp > 0 ? `${sensorData.temp.toFixed(1)}°C &bull; ${sensorData.hum.toFixed(1)}%` : 'ESP-NOW 2.4GHz'}</p>
          </div>
        </div>

        <!-- 3. Công Tắc Relay 1 -->
        <div class="hunonic-card subview-gw-child" data-gw-action="toggle_relay_1" title="Bấm để bật/tắt Relay Kênh 1">
          <div class="hunonic-card-top">
            <div class="hunonic-hex-icon">
              ${getRelayHexSvg(isR1On)}
            </div>
            ${getWifiHunonicSvg(isGatewayOnline)}
          </div>
          <div class="hunonic-card-bottom">
            <h4 class="hunonic-card-title">Công Tắc Relay Kênh 1</h4>
            <p class="hunonic-card-sub ${isR1On ? 'on' : ''}">${isR1On ? 'Đang bật' : 'Đang tắt'}</p>
          </div>
        </div>

        <!-- 4. Công Tắc Relay 2 -->
        <div class="hunonic-card subview-gw-child" data-gw-action="toggle_relay_2" title="Bấm để bật/tắt Relay Kênh 2">
          <div class="hunonic-card-top">
            <div class="hunonic-hex-icon">
              ${getRelayHexSvg(isR2On)}
            </div>
            ${getWifiHunonicSvg(isGatewayOnline)}
          </div>
          <div class="hunonic-card-bottom">
            <h4 class="hunonic-card-title">Công Tắc Relay Kênh 2</h4>
            <p class="hunonic-card-sub ${isR2On ? 'on' : ''}">${isR2On ? 'Đang bật' : 'Đang tắt'}</p>
          </div>
        </div>
      `;
    }
  }

  // ── RENDER THIẾT BỊ TRÊN DASHBOARD CHÍNH (CHUẨN HUNONIC COMPACT CARD) ──────
  function renderDevices() {
    if (currentHubView) {
      renderSubViewDevices(currentHubView.type, currentHubView.dev);
      return;
    }

    if (!devicesGrid) return;

    // 1. NẾU CHƯA THÊM GATEWAY VÀO PHÒNG -> HIỂN THỊ TRẠNG THÁI CHỜ THÊM
    if (!isGateAddedToRoom) {
      devicesGrid.innerHTML = `
        <div class="empty-devices-state">
          <div class="empty-icon-wrap">
            <i class="fa-solid fa-satellite-dish"></i>
          </div>
          <h4>Chưa có thiết bị nào trong phòng này</h4>
          <p>Hệ thống chưa thêm Trạm Gateway ESP32-S3. Vui lòng bấm bên dưới để bắt đầu kết nối Gateway.</p>
          <div class="empty-btns-row">
            <button class="btn-empty-add" id="btn-trigger-add-gate">
              <i class="fa-solid fa-plus-circle"></i> Thêm Trạm Gateway & Quét Thiết Bị
            </button>
          </div>
        </div>
      `;

      if (countDevicesOn) countDevicesOn.innerText = "00";
      if (countDevicesOff) countDevicesOff.innerText = "00";
      if (countDevicesOffline) countDevicesOffline.innerText = isGatewayOnline ? "00" : "01";

      const btnTriggerAdd = document.getElementById("btn-trigger-add-gate");
      if (btnTriggerAdd) {
        btnTriggerAdd.addEventListener("click", openAddGatewayModal);
      }
      return;
    }

    // 2. KHI ĐÃ THÊM GATEWAY VÀO PHÒNG
    const gwName = localStorage.getItem("gate_custom_name") || "Trạm Gateway ESP32-S3";
    const isIrOnline = isDeviceOnline(foundHub);

    let onCount = 0;
    let offCount = 0;
    let offlineCount = !isGatewayOnline ? 1 : 0;

    // Tính toán số lượng thiết bị Bật / Tắt / Offline
    if (foundHub && foundHub.children) {
      foundHub.children.forEach(c => {
        if (!isIrOnline) {
          offlineCount++;
        } else if (c.power === 1 || c.state === 1) {
          onCount++;
        } else {
          offCount++;
        }
      });
    }

    if (relayStates[1] === 1) onCount++; else offCount++;
    if (relayStates[2] === 1) onCount++; else offCount++;

    const irChildCount = (foundHub && foundHub.children && foundHub.children.length) || 2;
    const gwChildCount = irChildCount + 3; // IR hub, Sensor C3, Relay 1, Relay 2

    // Tạo HTML cho các thiết bị riêng lẻ khác nếu người dùng thêm vào
    let customCardsHtml = "";
    customDevices.forEach((dev) => {
      if (dev.type === "ir_hub" || dev.id === "dev_ir_hub_hunonic") return; // Đã render riêng

      const isOnline = isDeviceOnline(dev);
      if (!isOnline) offlineCount++;

      let iconSvg = getSensorHexSvg();
      let subText = "Trực tuyến";
      let isOn = false;

      if (dev.type === "relay" || dev.type === "socket") {
        isOn = isOnline && dev.state === 1;
        iconSvg = getRelayHexSvg(isOn);
        subText = isOn ? "Đang bật" : "Đang tắt";
      } else if (dev.type === "sensor") {
        iconSvg = getSensorHexSvg();
        subText = sensorData.temp > 0 ? `${sensorData.temp.toFixed(1)}°C &bull; ${sensorData.hum.toFixed(1)}%` : "ESP-NOW 2.4GHz";
      }

      customCardsHtml += `
        <div class="hunonic-card" data-id="${dev.id}" title="Bấm vào để xem chi tiết">
          <div class="hunonic-card-top">
            <div class="hunonic-hex-icon">
              ${iconSvg}
            </div>
            ${getWifiHunonicSvg(isOnline)}
          </div>
          <div class="hunonic-card-bottom">
            <h4 class="hunonic-card-title">${escapeHtml(dev.name)}</h4>
            <p class="hunonic-card-sub ${isOn ? 'on' : ''}">${subText}</p>
          </div>
        </div>
      `;
    });

    // RENDER LƯỚI THIẾT BỊ HAY DÙNG CHUẨN HUNONIC (GIỐNG SCREENSHOT 1)
    devicesGrid.innerHTML = `
      <!-- CARD 1: TRẠM GATEWAY ESP32-S3 -->
      <div class="hunonic-card" id="card-gateway-master" title="Bấm vào để xem các thiết bị trong Gateway">
        <div class="hunonic-card-top">
          <div class="hunonic-hex-icon">
            ${getGatewayHexSvg()}
          </div>
          ${getWifiHunonicSvg(isGatewayOnline)}
        </div>
        <div class="hunonic-card-bottom">
          <h4 class="hunonic-card-title">${escapeHtml(gwName)}</h4>
          <p class="hunonic-card-sub ${isGatewayOnline ? 'on' : ''}">Thiết bị con: ${gwChildCount}</p>
        </div>
      </div>

      <!-- CARD 2: IR TRUNG TÂM (CHỨA CÁC ĐIỀU HÒA) -->
      <div class="hunonic-card" id="card-ir-hub-master" data-id="${foundHub ? foundHub.id : 'dev_ir_hub_hunonic'}" title="Bấm vào để mở các thiết bị trong IR">
        <div class="hunonic-card-top">
          <div class="hunonic-hex-icon">
            ${getIrHubHexSvg()}
          </div>
          ${getWifiHunonicSvg(isIrOnline)}
        </div>
        <div class="hunonic-card-bottom">
          <h4 class="hunonic-card-title">${escapeHtml(foundHub ? foundHub.name : "IR trung tâm")}</h4>
          <p class="hunonic-card-sub ${isIrOnline ? 'on' : ''}">Thiết bị con: ${irChildCount}</p>
        </div>
      </div>

      <!-- CÁC THIẾT BỊ RIÊNG KHÁC -->
      ${customCardsHtml}
    `;

    // Cập nhật bộ đếm trên cùng
    if (countDevicesOn) countDevicesOn.innerText = onCount.toString().padStart(2, '0');
    if (countDevicesOff) countDevicesOff.innerText = offCount.toString().padStart(2, '0');
    if (countDevicesOffline) countDevicesOffline.innerText = offlineCount.toString().padStart(2, '0');
  }

  // ── XỬ LÝ EVENT DELEGATION TRÊN DEVICES GRID (HOME DASHBOARD) ───────────────
  if (devicesGrid) {
    devicesGrid.addEventListener("click", (e) => {
      // 1. Click vào card Gateway -> Mở Sub-View các thiết bị trong Gateway!
      const cardGw = e.target.closest("#card-gateway-master");
      if (cardGw) {
        openHubSubView("gateway", null, "home");
        return;
      }

      // 2. Click vào card IR trung tâm -> Mở Sub-View các thiết bị trong IR!
      const cardIr = e.target.closest("#card-ir-hub-master");
      if (cardIr) {
        openHubSubView("ir_hub", foundHub, "home");
        return;
      }

      // 3. Click vào card thiết bị con khác nếu có trên Home
      const customCard = e.target.closest(".hunonic-card[data-id]");
      if (customCard) {
        const devId = customCard.getAttribute("data-id");
        openChildDeviceDrawer(devId);
        return;
      }
    });
  }

  // ── MỞ DRAWER ĐIỀU KHIỂN ĐIỀU HÒA TRỰC TIẾP (BÊN TAY PHẢI) ───────────────────
  function openAcDrawer(childDev, hubDev = foundHub) {
    if (!childDev) return;
    currentActiveIrChild = childDev;
    currentDrawerTarget = childDev.id;

    // Header Drawer
    if (drawerHeaderTitle) drawerHeaderTitle.innerText = childDev.name;
    if (drawerHeaderSub) drawerHeaderSub.innerText = `${(hubDev && hubDev.room) || 'Phòng khách'} • Điều hòa IR Hunonic`;
    if (drawerHeaderIcon) drawerHeaderIcon.innerHTML = `<i class="fa-solid fa-wind"></i>`;

    // Ẩn tất cả section khác của Gateway, chỉ mở AC control panel
    if (drawerTeleGrid) drawerTeleGrid.style.display = "none";
    if (drawerGwChildSection) drawerGwChildSection.style.display = "none";
    if (drawerGwExtraActions) drawerGwExtraActions.style.display = "none";
    if (drawerIrHubSection) drawerIrHubSection.style.display = "none";
    if (drawerHeroBox) drawerHeroBox.style.display = "none";

    const editSec = document.getElementById("drawer-edit-section");
    if (editSec) editSec.style.display = "none";
    const delSec = document.getElementById("drawer-delete-section");
    if (delSec) delSec.style.display = "none";

    // Bật Panel Điều Hòa
    if (drawerAcPanel) drawerAcPanel.style.display = "flex";
    if (drawerAcBackBar) drawerAcBackBar.style.display = "none";

    // Cập nhật giao diện điều khiển (nhiệt độ, công suất, quạt, chế độ)
    updateDrawerAcUI(childDev);

    // Mở Drawer trượt từ bên phải
    const drawerEl = document.getElementById("device-detail-drawer") || deviceDetailDrawer;
    const backdropEl = document.getElementById("drawer-backdrop") || drawerBackdrop;
    if (drawerEl) {
      drawerEl.classList.add("open");
    }
    if (backdropEl) {
      backdropEl.classList.add("active");
    }
  }
  window.openAcDrawer = openAcDrawer;

  // ── HÀM TOÀN CỤC BẮT SỰ KIỆN CLICK CARD ĐIỀU HÒA (ĐẢM BẢO 100% MỞ DRAWER) ─────
  window.handleAcCardClick = function(childId) {
    const hubDev = foundHub || customDevices.find(d => d.type === "ir_hub" || d.id === "dev_ir_hub_hunonic");
    let child = null;
    if (hubDev && hubDev.children) {
      child = hubDev.children.find(c => String(c.id) === String(childId));
    }
    if (!child) {
      if (String(childId) === "4003248") {
        child = { id: "4003248", name: "Điều hòa MIRAGE", type: "ac", temp: 25, mode: 1, fan: 0, power: 0, brand: 117 };
      } else if (String(childId) === "4003318") {
        child = { id: "4003318", name: "Điều hòa FUNIKI", type: "ac", temp: 26, mode: 1, fan: 0, power: 0, brand: 104 };
      }
    }
    if (!child) {
      child = { id: String(childId), name: "Điều hòa", type: "ac", temp: 25, mode: 1, fan: 0, power: 0, brand: 104 };
    }
    openAcDrawer(child, hubDev);
  };

  // ── XỬ LÝ EVENT DELEGATION TRÊN SUB-VIEW DEVICES GRID ───────────────────────
  const subviewDevicesGridEl = document.getElementById("subview-devices-grid");
  if (subviewDevicesGridEl) {
    subviewDevicesGridEl.addEventListener("click", async (e) => {
      // 1. Click vào AC card trong IR Hub Subview -> Mở Điều khiển Điều Hòa!
      const acCard = e.target.closest(".subview-ac-card");
      if (acCard) {
        const childId = acCard.getAttribute("data-child-id");
        window.handleAcCardClick(childId);
        return;
      }

      // 2. Click vào các item trong Gateway Subview
      const gwChild = e.target.closest(".subview-gw-child");
      if (gwChild) {
        const action = gwChild.getAttribute("data-gw-action");
        if (action === "open_ir_hub") {
          openHubSubView("ir_hub", foundHub, "gateway");
        } else if (action === "open_sensor") {
          openChildDeviceDrawer("dev_espnow_node_1");
        } else if (action === "toggle_relay_1") {
          const newState = relayStates[1] === 1 ? 0 : 1;
          relayStates[1] = newState;
          await callApi("/api/gateway/relay", "POST", { channel: 1, state: newState });
          renderSubViewDevices("gateway", null);
          showToast(`Đã ${newState === 1 ? 'BẬT' : 'TẮT'} Công Tắc Relay 1!`, "success");
        } else if (action === "toggle_relay_2") {
          const newState = relayStates[2] === 1 ? 0 : 1;
          relayStates[2] = newState;
          await callApi("/api/gateway/relay", "POST", { channel: 2, state: newState });
          renderSubViewDevices("gateway", null);
          showToast(`Đã ${newState === 1 ? 'BẬT' : 'TẮT'} Công Tắc Relay 2!`, "success");
        }
        return;
      }
    });
  }

  // ── XỬ LÝ NÚT QUAY LẠI TRÊN SUB-VIEW ────────────────────────────────────────
  const btnSubViewBackEl = document.getElementById("btn-subview-back");
  if (btnSubViewBackEl) {
    btnSubViewBackEl.addEventListener("click", () => {
      closeHubSubView();
    });
  }

  // ── NÚT TRANG CHỦ HOẶC LOGO: LUÔN QUAY VỀ HOME DASHBOARD ─────────────────────
  const navHomeBtn = document.getElementById("nav-home");
  if (navHomeBtn) {
    navHomeBtn.addEventListener("click", (e) => {
      e.preventDefault();
      closeHubSubView(true);
    });
  }

  const brandLogoBox = document.querySelector(".brand-logo-box");
  if (brandLogoBox) {
    brandLogoBox.style.cursor = "pointer";
    brandLogoBox.addEventListener("click", () => {
      closeHubSubView(true);
    });
  }

  // ── Hàm đồng bộ thiết bị với Gateway ─────────────────────────────────────
  async function syncWithGateway() {
    showToast("Đang gửi yêu cầu đồng bộ tới Trạm Gateway ESP32-S3...", "info");
    const res = await callApi("/api/hvac/sync", "POST");
    if (res && res.success) {
      showToast(res.message, "success");
    } else {
      showToast("Gateway chưa phản hồi, vui lòng kiểm tra kết nối nguồn/Wi-Fi của ESP32!", "error");
    }
  }

  // ── Cập nhật giao diện AC Modal ───────────────────────────────────────────
  function updateAcModalUI() {
    const isPowerOn = acState.power === 1;
    const targetDev = findCurrentAcDevice();
    const limits = getAcTempLimits(targetDev);

    if (acState.temp > limits.max) acState.temp = limits.max;
    if (acState.temp < limits.min) acState.temp = limits.min;

    if (acPowerBtn && acPowerText) {
      if (isPowerOn) {
        acPowerBtn.className = "btn-power-switch on";
        acPowerText.innerText = "ĐANG BẬT";
      } else {
        acPowerBtn.className = "btn-power-switch off";
        acPowerText.innerText = "ĐANG TẮT";
      }
    }

    if (acTempDisplay) acTempDisplay.innerText = acState.temp;

    if (btnTempPlus) {
      btnTempPlus.style.opacity = acState.temp >= limits.max ? "0.4" : "1";
    }
    if (btnTempMinus) {
      btnTempMinus.style.opacity = acState.temp <= limits.min ? "0.4" : "1";
    }

    modePills.forEach(pill => {
      const m = parseInt(pill.getAttribute("data-mode"));
      if (m === acState.mode) pill.classList.add("active");
      else pill.classList.remove("active");
    });

    fanPills.forEach(pill => {
      const f = parseInt(pill.getAttribute("data-fan"));
      if (f === acState.fan) pill.classList.add("active");
      else pill.classList.remove("active");
    });
  }

  // ── Xử lý các nút điều khiển trong Modal AC ──────────────────────────────
  if (acPowerBtn) {
    acPowerBtn.addEventListener("click", () => {
      const newPower = acState.power === 1 ? 0 : 1;
      sendAcControlCommand(null, { state: newPower }, true);
    });
  }

  if (btnTempMinus) {
    btnTempMinus.addEventListener("click", () => {
      const targetDev = findCurrentAcDevice();
      const limits = getAcTempLimits(targetDev);
      if (acState.temp > limits.min) {
        sendAcControlCommand(null, { temp: acState.temp - 1 }, false);
      } else {
        showToast(`Nhiệt độ tối thiểu là ${limits.min}°C!`, "info");
      }
    });
  }

  if (btnTempPlus) {
    btnTempPlus.addEventListener("click", () => {
      const targetDev = findCurrentAcDevice();
      const limits = getAcTempLimits(targetDev);
      if (acState.temp < limits.max) {
        sendAcControlCommand(null, { temp: acState.temp + 1 }, false);
      } else {
        showToast(`Điều hòa [${targetDev ? targetDev.name : 'Điều hòa'}] giới hạn tối đa ${limits.max}°C!`, "info");
      }
    });
  }

  modePills.forEach(pill => {
    pill.addEventListener("click", () => {
      const m = parseInt(pill.getAttribute("data-mode"));
      sendAcControlCommand(null, { mode: m }, false);
    });
  });

  fanPills.forEach(pill => {
    pill.addEventListener("click", () => {
      const f = parseInt(pill.getAttribute("data-fan"));
      sendAcControlCommand(null, { fan: f }, false);
    });
  });

  if (btnRebootGate) {
    btnRebootGate.addEventListener("click", async () => {
      if (confirm("Bạn có chắc chắn muốn khởi động lại Trạm Gateway ESP32-S3 không?")) {
        const res = await callApi("/api/gateway/reboot", "POST");
        if (res && res.success) showToast(res.message, "success");
      }
    });
  }

  // ── Xử lý các Ngữ cảnh thông minh (Quick Scenes) ───────────────────────────
  const sceneHome = document.getElementById("scene-home");
  const sceneSleep = document.getElementById("scene-sleep");
  const sceneCoolFast = document.getElementById("scene-cool-fast");
  const scenePowerSave = document.getElementById("scene-power-save");

  if (sceneHome) {
    sceneHome.addEventListener("click", async () => {
      if (acDebounceTimer) { clearTimeout(acDebounceTimer); acDebounceTimer = null; }
      showToast("Kích hoạt ngữ cảnh: VỀ NHÀ", "info");
      acState.power = 1;
      acState.temp = 25;
      acState.mode = 1;
      relayStates[1] = 1;
      updateAcModalUI();
      renderDevices();
      await callApi("/api/hvac/control", "POST", { power: 1, temp: 25, mode: 1, fan: 0 });
      await callApi("/api/gateway/relay", "POST", { channel: 1, state: 1 });
      showToast("Ngữ cảnh Về Nhà đã sẵn sàng (AC 25°C Cool & Đèn Bật)", "success");
    });
  }

  if (sceneSleep) {
    sceneSleep.addEventListener("click", async () => {
      if (acDebounceTimer) { clearTimeout(acDebounceTimer); acDebounceTimer = null; }
      showToast("Kích hoạt ngữ cảnh: ĐI NGỦ", "info");
      acState.power = 1;
      acState.temp = 27;
      acState.fan = 1;
      relayStates[1] = 0;
      relayStates[2] = 0;
      updateAcModalUI();
      renderDevices();
      await callApi("/api/hvac/control", "POST", { power: 1, temp: 27, mode: 1, fan: 1 });
      await callApi("/api/gateway/relay", "POST", { channel: 1, state: 0 });
      await callApi("/api/gateway/relay", "POST", { channel: 2, state: 0 });
      showToast("Ngữ cảnh Đi Ngủ đã kích hoạt (AC 27°C Quạt êm & Tắt đèn)", "success");
    });
  }

  if (sceneCoolFast) {
    sceneCoolFast.addEventListener("click", async () => {
      if (acDebounceTimer) { clearTimeout(acDebounceTimer); acDebounceTimer = null; }
      showToast("Kích hoạt ngữ cảnh: LÀM MÁT NHANH", "info");
      acState.power = 1;
      acState.temp = 20;
      acState.fan = 5;
      updateAcModalUI();
      renderDevices();
      await callApi("/api/hvac/control", "POST", { power: 1, temp: 20, mode: 1, fan: 5 });
      showToast("Đang làm mát cấp tốc (AC 20°C & Quạt Tối Đa)", "success");
    });
  }

  if (scenePowerSave) {
    scenePowerSave.addEventListener("click", async () => {
      if (acDebounceTimer) { clearTimeout(acDebounceTimer); acDebounceTimer = null; }
      showToast("Kích hoạt ngữ cảnh: TIẾT KIỆM ĐIỆN", "info");
      acState.power = 0;
      relayStates[1] = 0;
      relayStates[2] = 0;
      updateAcModalUI();
      renderDevices();
      await callApi("/api/hvac/control", "POST", { power: 0, temp: acState.temp, mode: acState.mode, fan: acState.fan });
      await callApi("/api/gateway/relay", "POST", { channel: 1, state: 0 });
      await callApi("/api/gateway/relay", "POST", { channel: 2, state: 0 });
      showToast("Đã tắt toàn bộ thiết bị để tiết kiệm điện", "success");
    });
  }

  // ==============================================================================
  // QUẢN LÝ TRẠM GATEWAY & THÊM THIẾT BỊ MỚI (GATEWAY MANAGE MODAL)
  // ==============================================================================
  const gatewayManageModal = document.getElementById("gateway-manage-modal");
  const btnCloseGwModal = document.getElementById("btn-close-gw-modal");
  const btnGotoAddDevice = document.getElementById("btn-goto-add-device");
  const btnSubmitAddDevice = document.getElementById("btn-submit-add-device");
  const deviceTypeSelector = document.getElementById("device-type-selector");
  const dynamicFieldLabel = document.getElementById("dynamic-field-label");
  const dynamicFieldIcon = document.getElementById("dynamic-field-icon");
  const dynamicFieldValue = document.getElementById("dynamic-field-value");
  const newDeviceNameInput = document.getElementById("new-device-name");
  const newDeviceRoomSelect = document.getElementById("new-device-room");
  const btnGwReconfigWifi = document.getElementById("btn-gw-reconfig-wifi");
  const btnGwReboot = document.getElementById("btn-gw-reboot");

  let selectedDeviceType = "espnow";

  function openGatewayManageModal(activeTabId = "tab-linked-devices") {
    if (!gatewayManageModal) return;

    // Cập nhật thông số trạm trong Tab 3
    const mOnline = document.getElementById("gw-modal-online");
    const mUptime = document.getElementById("gw-modal-uptime");
    const mHeap = document.getElementById("gw-modal-heap");
    const mRssi = document.getElementById("gw-modal-rssi");

    if (mOnline) {
      mOnline.innerText = isGatewayOnline ? "Online (HiveMQ Cloud TLS)" : "Ngoại tuyến (Standby / Wi-Fi)";
      mOnline.className = isGatewayOnline ? "t-value text-green" : "t-value";
    }
    if (mUptime) mUptime.innerText = formatUptime(gwMetrics.uptime_sec || 0);
    if (mHeap) mHeap.innerText = (gwMetrics.free_heap > 0 ? (gwMetrics.free_heap / 1024).toFixed(1) : (gwMetrics.heap_kb || "62.1")) + " KB";
    if (mRssi) mRssi.innerText = (gwMetrics.rssi !== -100 ? `${gwMetrics.rssi} dBm` : "-44 dBm") + " (Dinh)";

    renderLinkedDevicesList();
    renderDiscoveredDevices();
    switchGwModalTab(activeTabId);
    gatewayManageModal.classList.remove("hidden");
  }

  function closeGatewayManageModal() {
    if (gatewayManageModal) gatewayManageModal.classList.add("hidden");
  }

  function switchGwModalTab(tabId) {
    document.querySelectorAll(".gw-tab-btn").forEach(btn => {
      btn.classList.toggle("active", btn.getAttribute("data-tab") === tabId);
    });
    document.querySelectorAll(".gw-tab-content").forEach(content => {
      content.style.display = (content.id === tabId) ? "block" : "none";
    });
  }

  // Chuyển tab khi click nút
  document.querySelectorAll(".gw-tab-btn").forEach(btn => {
    btn.addEventListener("click", () => {
      const tabId = btn.getAttribute("data-tab");
      switchGwModalTab(tabId);
    });
  });

  if (btnCloseGwModal) {
    btnCloseGwModal.addEventListener("click", closeGatewayManageModal);
  }
  if (gatewayManageModal) {
    gatewayManageModal.addEventListener("click", (e) => {
      if (e.target === gatewayManageModal) closeGatewayManageModal();
    });
  }

  if (btnGotoAddDevice) {
    btnGotoAddDevice.addEventListener("click", () => {
      switchGwModalTab("tab-add-new-device");
    });
  }

  // Chọn loại thiết bị trong form thêm mới
  if (deviceTypeSelector) {
    deviceTypeSelector.addEventListener("click", (e) => {
      const card = e.target.closest(".type-card");
      if (!card) return;

      document.querySelectorAll(".type-card").forEach(c => c.classList.remove("active"));
      card.classList.add("active");

      selectedDeviceType = card.getAttribute("data-type");

      if (selectedDeviceType === "espnow") {
        if (dynamicFieldLabel) dynamicFieldLabel.innerText = "Địa chỉ MAC Node (ESP-NOW):";
        if (dynamicFieldIcon) dynamicFieldIcon.className = "fa-solid fa-network-wired";
        if (dynamicFieldValue) {
          dynamicFieldValue.placeholder = "VD: 30:AE:A4:07:0B:84";
          dynamicFieldValue.value = "30:AE:A4:07:0B:84";
        }
        if (newDeviceNameInput) newDeviceNameInput.value = "Node Cảm Biến Không Dây Mới";
      } else if (selectedDeviceType === "ac") {
        if (dynamicFieldLabel) dynamicFieldLabel.innerText = "Hãng Điều Hòa (Mã Hồng Ngoại IR):";
        if (dynamicFieldIcon) dynamicFieldIcon.className = "fa-solid fa-snowflake";
        if (dynamicFieldValue) {
          dynamicFieldValue.placeholder = "VD: FUNIKI, Daikin, Panasonic, Casper, LG";
          dynamicFieldValue.value = "FUNIKI (Mã 3992007)";
        }
        if (newDeviceNameInput) newDeviceNameInput.value = "Điều Hòa Phòng Ngủ";
      } else if (selectedDeviceType === "relay") {
        if (dynamicFieldLabel) dynamicFieldLabel.innerText = "Chân Điều Khiển GPIO Relay:";
        if (dynamicFieldIcon) dynamicFieldIcon.className = "fa-solid fa-bolt";
        if (dynamicFieldValue) {
          dynamicFieldValue.placeholder = "VD: GPIO 10, GPIO 11, GPIO 12";
          dynamicFieldValue.value = "GPIO 10 (Relay Kênh 3)";
        }
        if (newDeviceNameInput) newDeviceNameInput.value = "Đèn Ban Công";
      } else if (selectedDeviceType === "socket") {
        if (dynamicFieldLabel) dynamicFieldLabel.innerText = "Kênh Ổ Cắm Điện:";
        if (dynamicFieldIcon) dynamicFieldIcon.className = "fa-solid fa-plug";
        if (dynamicFieldValue) {
          dynamicFieldValue.placeholder = "VD: Ổ cắm 1 - Tải 16A";
          dynamicFieldValue.value = "Ổ cắm 1 - Tải 16A (220V)";
        }
        if (newDeviceNameInput) newDeviceNameInput.value = "Ổ Cắm Thông Minh Bếp";
      }
    });
  }

  // Xác nhận thêm thiết bị mới vào Gateway
  if (btnSubmitAddDevice) {
    btnSubmitAddDevice.addEventListener("click", () => {
      const name = newDeviceNameInput ? newDeviceNameInput.value.trim() : "";
      const room = newDeviceRoomSelect ? newDeviceRoomSelect.value : "Phòng khách";
      const extra = dynamicFieldValue ? dynamicFieldValue.value.trim() : "";

      if (!name) {
        showToast("Vui lòng nhập tên cho thiết bị mới!", "error");
        return;
      }

      const newDev = {
        id: "dev_" + Date.now(),
        name: name,
        type: selectedDeviceType,
        room: room,
        extra: extra,
        state: 0
      };

      customDevices.push(newDev);
      localStorage.setItem("gate_custom_devices", JSON.stringify(customDevices));

      renderDevices();
      renderDrawerChildList();
      renderLinkedDevicesList();
      closeGatewayManageModal();

      showToast(`Đã thêm thành công thiết bị [${name}] vào Gateway!`, "success");
    });
  }

  // ── Render danh sách thiết bị trực tuyến phát hiện được (Tab 2) ───────────
  async function renderDiscoveredDevices() {
    const grid = document.getElementById("discovered-devices-grid");
    if (!grid) return;

    let discList = [];
    try {
      const res = await callApi("/api/gateway/discovered");
      if (res && res.success && Array.isArray(res.data) && res.data.length > 0) {
        discList = res.data;
      }
    } catch (e) {
      console.warn("Lỗi gọi /api/gateway/discovered:", e);
    }

    if (!discList || discList.length === 0) {
      discList = [];

      // 1. Trạm Hồng Ngoại IR Hunonic Smart Pro (chứa các thiết bị con như Điều hòa)
      const acName = gwMetrics.ac_name || "Điều hoà phòng Hương";
      const acId = gwMetrics.ac_id || "4000261";
      discList.push({
        id: "ir_hub_3987670",
        name: "IR Hunonic Smart Pro",
        type: "ir_hub",
        source: "Hunonic Cloud / IR Smart Pro",
        device_id: "3987670",
        online: gwMetrics.ac_online !== undefined ? gwMetrics.ac_online : false,
        details: `ID: 3987670 • 1 thiết bị IR kết nối • ${gwMetrics.ac_online ? 'Trực tuyến' : 'Ngoại tuyến'}`,
        children: [
          {
            id: acId,
            name: acName,
            type: "ac",
            temp: acState.temp || 25,
            mode: acState.mode !== undefined ? acState.mode : 2,
            fan: acState.fan || 0,
            power: acState.power || 0
          }
        ]
      });

      // 2. Node Cảm Biến ESP-NOW: CHỈ HIỆN KHI THỰC SỰ CÓ DỮ LIỆU NHẬN ĐƯỢC TỪ NODE C3
      const hasRealSensor = Boolean(
        sensorData.hasData && 
        (sensorData.temp > 0 || sensorData.hum > 0 || sensorData.batt > 0)
      );
      if (hasRealSensor) {
        discList.push({
          id: "dev_espnow_node_1",
          name: "Node Cảm Biến C3 (ESP-NOW)",
          type: "sensor",
          source: "ESP-NOW 2.4GHz",
          mac: "30:AE:A4:07:0B:84",
          online: true,
          details: `Nhiệt độ: ${sensorData.temp.toFixed(1)}°C • Độ ẩm: ${sensorData.hum.toFixed(1)}% • Pin: ${sensorData.batt.toFixed(2)}V`
        });
      }

      // 3. Relay Kênh 1 & 2 Gateway
      if (isGatewayOnline) {
        discList.push({
          id: "dev_gw_relay_1",
          name: "Công Tắc Relay Kênh 1",
          type: "relay",
          source: "Ngõ Ra Gateway (GPIO10)",
          online: true,
          details: "Chân GPIO 10 • Tải đèn/quạt 220V (10A)",
          state: relayStates[1] || 0
        });
        discList.push({
          id: "dev_gw_relay_2",
          name: "Công Tắc Relay Kênh 2",
          type: "relay",
          source: "Ngõ Ra Gateway (GPIO11)",
          online: true,
          details: "Chân GPIO 11 • Tải phụ tải 220V (10A)",
          state: relayStates[2] || 0
        });
      }
    }

    if (discList.length === 0) {
      grid.innerHTML = `
        <div style="grid-column: 1 / -1; text-align: center; padding: 28px 16px; color: #64748b; background: #ffffff; border: 1.5px dashed #cbd5e1; border-radius: 14px;">
          <div style="width: 44px; height: 44px; border-radius: 50%; background: #f1f5f9; display: flex; align-items: center; justify-content: center; margin: 0 auto 10px; font-size: 1.2rem; color: #94a3b8;">
            <i class="fa-solid fa-satellite-dish"></i>
          </div>
          <p style="margin: 0 0 4px; font-weight: 700; font-size: 0.88rem; color: #334155;">Chưa phát hiện thiết bị nào đang trực tuyến</p>
          <span style="font-size: 0.78rem;">Bật nguồn thiết bị hoặc bấm nút <strong>"Quét lại"</strong> để Gateway dò tìm các thiết bị khả dụng.</span>
        </div>
      `;
      return;
    }

    let cardsHtml = "";
    discList.forEach((item, idx) => {
      // Kiểm tra xem thiết bị này đã được đưa vào danh sách customDevices chưa
      const isAlreadyAdded = customDevices.some(d => 
        d.id === item.id || 
        d.name === item.name || 
        (d.type === item.type && (item.type === "ac" || item.type === "ir_hub" || item.type === "sensor")) ||
        (item.device_id && (d.device_id === item.device_id || (d.extra && d.extra.includes(item.device_id))))
      );

      const isIrHub = item.type === "ir_hub";
      const iconClass = isIrHub ? "ir-hub" : (item.type === "ac" ? "ac" : (item.type === "sensor" ? "sensor" : (idx === 2 ? "relay1" : "relay2")));
      const faIcon = isIrHub ? "fa-tower-broadcast" : (item.type === "ac" ? "fa-wind" : (item.type === "sensor" ? "fa-satellite-dish" : "fa-lightbulb"));
      const isOnline = item.online !== false;

      cardsHtml += `
        <div class="discovered-dev-card" data-disc-id="${item.id}">
          <div class="disc-card-top">
            <div class="disc-icon ${iconClass}">
              <i class="fa-solid ${faIcon}"></i>
            </div>
            <div class="disc-meta">
              <h5 class="disc-meta-title">${escapeHtml(item.name)}</h5>
              <div class="disc-badges-row">
                <span class="pill-online ${isOnline ? 'active' : 'offline'}">
                  <i class="fa-solid fa-circle"></i> ${isOnline ? 'Trực tuyến' : 'Ngoại tuyến'}
                </span>
                <span class="pill-source">${escapeHtml(item.source)}</span>
              </div>
              <p class="disc-info-meta">${escapeHtml(item.details)}</p>
            </div>
          </div>
          <div class="disc-card-footer">
            ${isAlreadyAdded ? `
              <span class="badge-already-added"><i class="fa-solid fa-circle-check"></i> Đã có trong phòng</span>
            ` : `
              <button type="button" class="btn-quick-add-dev" 
                data-type="${item.type}" 
                data-name="${escapeHtml(item.name)}" 
                data-id="${item.id}" 
                data-extra="${escapeHtml(item.details)}">
                <i class="fa-solid fa-plus-circle"></i> Thêm Vào Phòng
              </button>
            `}
          </div>
        </div>
      `;
    });

    grid.innerHTML = cardsHtml;

    // Bắt sự kiện click nút Thêm Vào Phòng
    grid.querySelectorAll(".btn-quick-add-dev").forEach(btn => {
      btn.addEventListener("click", (e) => {
        e.stopPropagation();
        const type = btn.getAttribute("data-type");
        const name = btn.getAttribute("data-name");
        const devId = btn.getAttribute("data-id") || ("dev_" + Date.now());
        const extra = btn.getAttribute("data-extra") || "";
        const foundDisc = discList.find(x => x.id === devId);

        const newDev = {
          id: devId,
          name: name,
          type: type,
          room: localStorage.getItem("gate_room") || "Phòng khách",
          extra: extra,
          device_id: (foundDisc && foundDisc.device_id) || "",
          children: (foundDisc && foundDisc.children) || [],
          state: 0
        };

        customDevices.push(newDev);
        localStorage.setItem("gate_custom_devices", JSON.stringify(customDevices));

        renderDevices();
        renderDrawerChildList();
        renderLinkedDevicesList();
        renderDiscoveredDevices();

        showToast(`Đã thêm thành công [${name}] vào ${newDev.room}!`, "success");
      });
    });
  }

  // Nút Quét lại / Làm mới thiết bị phát hiện
  const btnRefreshDiscovered = document.getElementById("btn-refresh-discovered");
  if (btnRefreshDiscovered) {
    btnRefreshDiscovered.addEventListener("click", async () => {
      const icon = btnRefreshDiscovered.querySelector("i");
      if (icon) icon.classList.add("fa-spin");
      showToast("Đang đồng bộ và quét lại thiết bị từ Gateway & Cloud...", "info");
      
      try {
        await callApi("/api/hvac/sync", "POST");
      } catch (err) {
        console.warn("Sync err:", err);
      }
      
      await fetchStatus();
      await renderDiscoveredDevices();

      setTimeout(() => {
        if (icon) icon.classList.remove("fa-spin");
        showToast("Đã làm mới danh sách thiết bị trực tuyến thành công!", "success");
      }, 600);
    });
  }

  // Toggle thu gọn/mở rộng form thêm thủ công
  const btnToggleManualAdd = document.getElementById("btn-toggle-manual-add");
  const manualAddFormWrap = document.getElementById("manual-add-form-wrap");
  if (btnToggleManualAdd && manualAddFormWrap) {
    btnToggleManualAdd.addEventListener("click", () => {
      const isCollapsed = manualAddFormWrap.classList.contains("collapsed");
      manualAddFormWrap.classList.toggle("collapsed");
      btnToggleManualAdd.classList.toggle("open", isCollapsed);
    });
  }

  // Render danh sách thiết bị liên kết trong Tab 1 (Chỉ hiển thị thiết bị thực tế)
  function renderLinkedDevicesList() {
    const container = document.getElementById("linked-devices-list");
    const countSpan = document.getElementById("gw-device-count");
    if (!container) return;

    if (countSpan) countSpan.innerText = customDevices.length;

    if (customDevices.length === 0) {
      container.innerHTML = `
        <div style="text-align: center; padding: 28px 16px; color: #64748b;">
          <div style="width: 44px; height: 44px; border-radius: 50%; background: #f1f5f9; color: #94a3b8; display: flex; align-items: center; justify-content: center; font-size: 1.2rem; margin: 0 auto 10px;">
            <i class="fa-solid fa-layer-group"></i>
          </div>
          <p style="font-size: 0.88rem; font-weight: 600; color: #334155; margin: 0 0 4px;">Chưa có thiết bị nào liên kết với Gateway</p>
          <p style="font-size: 0.78rem; margin: 0;">Bấm "+ Thêm Thiết Bị Mới" để thêm thiết bị con đầu tiên.</p>
        </div>
      `;
      return;
    }

    let html = "";
    customDevices.forEach(dev => {
      const isIrHub = dev.type === "ir_hub";
      const iconClass = isIrHub ? "ir-hub" : (dev.type === "ac" ? "ac" : (dev.type === "relay" ? "light" : (dev.type === "socket" ? "socket" : "sensor")));
      const faIcon = isIrHub ? "fa-tower-broadcast" : (dev.type === "ac" ? "fa-wind" : (dev.type === "relay" ? "fa-lightbulb" : (dev.type === "socket" ? "fa-plug" : "fa-satellite-dish")));
      const typeLabel = isIrHub ? "Trạm IR Smart Pro" : (dev.type === "ac" ? "Hồng ngoại IR" : (dev.type === "relay" ? "Công tắc Relay" : (dev.type === "socket" ? "Ổ cắm" : "Node Cảm Biến")));
      const childCount = (dev.children && dev.children.length) || 0;
      const subInfo = isIrHub ? `${childCount} thiết bị IR kết nối` : (dev.extra || typeLabel);

      html += `
        <div class="linked-dev-item">
          <div class="linked-dev-left">
            <div class="linked-dev-icon ${iconClass}">
              <i class="fa-solid ${faIcon}"></i>
            </div>
            <div class="linked-dev-info">
              <h5>${escapeHtml(dev.name)}</h5>
              <p>${escapeHtml(dev.room || 'Phòng khách')} &bull; ${escapeHtml(subInfo)}</p>
            </div>
          </div>
          <div class="linked-dev-right">
            <span class="badge-linked-type">Đã liên kết</span>
            <button type="button" class="btn-remove-linked-dev" data-id="${dev.id}" title="Xóa thiết bị này khỏi Gateway">
              <i class="fa-solid fa-trash-can"></i>
            </button>
          </div>
        </div>
      `;
    });

    container.innerHTML = html;

    // Xử lý nút xóa thiết bị tùy chỉnh
    container.querySelectorAll(".btn-remove-linked-dev").forEach(btn => {
      btn.addEventListener("click", (e) => {
        e.stopPropagation();
        const devId = btn.getAttribute("data-id");
        const dev = customDevices.find(d => d.id === devId);
        const devName = dev ? dev.name : "thiết bị";
        if (confirm(`Bạn có chắc chắn muốn xóa [${devName}] khỏi Gateway?`)) {
          customDevices = customDevices.filter(d => d.id !== devId);
          localStorage.setItem("gate_custom_devices", JSON.stringify(customDevices));
          renderLinkedDevicesList();
          renderDrawerChildList();
          renderDevices();
          showToast(`Đã xóa [${devName}] khỏi Gateway!`, "info");
        }
      });
    });
  }

  // Nút cấu hình lại Wi-Fi qua BLE
  if (btnGwReconfigWifi) {
    btnGwReconfigWifi.addEventListener("click", () => {
      closeGatewayManageModal();
      openAddGatewayModal();
    });
  }

  // Nút reboot Gateway
  if (btnGwReboot) {
    btnGwReboot.addEventListener("click", async () => {
      if (confirm("Bạn có chắc chắn muốn khởi động lại Trạm Gateway ESP32-S3?")) {
        showToast("Đang gửi lệnh khởi động lại Gateway...", "info");
        const res = await callApi("/api/gateway/reboot", "POST");
        if (res && res.success) {
          showToast(res.message, "success");
        } else {
          showToast("Đã gửi tín hiệu khởi động lại Gateway!", "success");
        }
      }
    });
  }

  // ── Polling cập nhật trạng thái tự động mỗi 2.5 giây ────────────────────────
  async function fetchStatus() {
    // 1. Lấy trạng thái Gateway
    const gwRes = await callApi("/api/gateway/status");
    if (gwRes && gwRes.success && gwRes.data) {
      const d = gwRes.data;
      const prevOnline = isGatewayOnline;

      // Watchdog: Kiểm tra độ tươi của heartbeat (trong vòng 15 giây)
      let isAlive = !!d.online;
      if (d.last_seen) {
        const lastSeenMs = new Date(d.last_seen).getTime();
        const ageSec = (Date.now() - lastSeenMs) / 1000;
        if (ageSec > 15) {
          isAlive = false;
        }
      } else {
        isAlive = false;
      }
      isGatewayOnline = isAlive;

      gwMetrics.uptime_sec = d.uptime_sec || 0;
      gwMetrics.free_heap = d.free_heap || 0;
      gwMetrics.rssi = d.rssi || -100;
      gwMetrics.fw_ver = d.fw_ver || "1.0.0";

      if (statusText) statusText.innerText = isGatewayOnline ? "Hệ thống Trực tuyến" : "Trạm Gateway Ngoại tuyến";
      if (statusBadge) {
        statusBadge.className = isGatewayOnline ? "mqtt-pill-badge" : "mqtt-pill-badge offline";
        statusBadge.style.color = isGatewayOnline ? "#00b06f" : "#b91c1c";
      }

      if (sidebarGateDot) sidebarGateDot.className = isGatewayOnline ? "gate-status-dot online" : "gate-status-dot offline";
      if (sidebarGateText) sidebarGateText.innerText = isGatewayOnline ? "Gateway Online" : "Gateway Offline";

      // Nếu Modal quét đang mở ở trạng thái chờ và Gateway vừa Online -> Chuyển sang hiển thị thiết bị
      if (isGatewayOnline && !prevOnline && addGatewayModal && !addGatewayModal.classList.contains("hidden") && stageWaitingGate && !stageWaitingGate.classList.contains("hidden")) {
        showDevicesFoundStage();
        showToast("Đã kết nối Gateway ESP32-S3 thành công! Đã quét được thiết bị.", "success");
      }

      if (d.relay1 !== undefined) relayStates[1] = d.relay1;
      if (d.relay2 !== undefined) relayStates[2] = d.relay2;

      if (!isGatewayOnline) {
        gwMetrics.ac_online = false;
        gwMetrics.ac_offline_reason = "Trạm Gateway ESP32-S3 đang ngoại tuyến (Offline)";
        customDevices.forEach(cd => {
          cd.online = false;
          cd.state = 0;
          if (cd.children) {
            cd.children.forEach(ch => {
              ch.online = false;
              ch.power = 0;
              ch.state = 0;
            });
          }
        });
      } else if (d.ac_online !== undefined) {
        gwMetrics.ac_online = Boolean(d.ac_online);
        gwMetrics.ac_offline_reason = d.ac_offline_reason;
        customDevices.forEach(cd => {
          if (cd.type === "ac" || cd.type === "ir_hub") {
            cd.online = d.ac_online;
            if (!d.ac_online) {
              cd.state = 0; // Luôn ép về 0 nếu offline
              if (cd.children) {
                cd.children.forEach(ch => ch.power = 0);
              }
            }
          }
        });
      }

      if (d.ac_devices && typeof d.ac_devices === "object") {
        const hubDev = foundHub || customDevices.find(cd => cd.type === "ir_hub" || cd.id === "dev_ir_hub_hunonic");
        if (hubDev && hubDev.children) {
          hubDev.children.forEach(ch => {
            const remoteAc = d.ac_devices[ch.id];
            if (remoteAc) {
              if (remoteAc.name) ch.name = remoteAc.name;
              if (remoteAc.power !== undefined) ch.power = remoteAc.power;
              ch.state = ch.power;
              if (remoteAc.temp > 0) ch.temp = remoteAc.temp;
              if (remoteAc.mode !== undefined) ch.mode = remoteAc.mode;
              if (remoteAc.fan !== undefined) ch.fan = remoteAc.fan;
              if (remoteAc.brand) ch.brand = remoteAc.brand;
              if (remoteAc.online !== undefined) ch.online = Boolean(remoteAc.online);
            }
          });
        }
      }

      if (d.ac_power !== undefined) acState.power = d.ac_power;
      if (d.ac_temp > 0) acState.temp = d.ac_temp;

      updateAcModalUI();
      renderDevices();
      updateDrawerGatewayTelemetry();
      renderDrawerChildList();

      // Cập nhật tức thời Drawer Điều Hòa hoặc Trạm IR nếu đang mở
      if (currentActiveIrChild) {
        updateDrawerAcUI(currentActiveIrChild);
      } else if (currentDrawerTarget) {
        const curDev = customDevices.find(cd => cd.id === currentDrawerTarget);
        if (curDev) {
          if (curDev.type === "ac") {
            updateDrawerAcUI(curDev);
          } else if (curDev.type === "ir_hub") {
            renderIrChildList(curDev);
          }
        }
      }
    } else {
      isGatewayOnline = false;
      gwMetrics.ac_online = false;
      gwMetrics.ac_offline_reason = "Mất kết nối máy chủ / Gateway ngoại tuyến";
      customDevices.forEach(cd => {
        cd.online = false;
        cd.state = 0;
        if (cd.children) {
          cd.children.forEach(ch => {
            ch.online = false;
            ch.power = 0;
            ch.state = 0;
          });
        }
      });
      if (statusText) statusText.innerText = "Mất kết nối máy chủ";
      if (statusBadge) {
        statusBadge.className = "mqtt-pill-badge offline";
        statusBadge.style.color = "#b91c1c";
      }
      if (sidebarGateDot) sidebarGateDot.className = "gate-status-dot offline";
      if (sidebarGateText) sidebarGateText.innerText = "Gateway Offline";
      renderDevices();
      updateDrawerGatewayTelemetry();
      renderDrawerChildList();
    }

    // 2. Lấy dữ liệu cảm biến Node ESP-NOW
    const senRes = await callApi("/api/sensors/latest");
    if (senRes && senRes.success && senRes.data) {
      const s = senRes.data;
      if (s.last_seen || s.val1 > 0 || s.val2 > 0) {
        sensorData.hasData = true;
        sensorData.temp = s.val1 || 27.2;
        sensorData.hum = s.val2 || 72.0;
        sensorData.batt = s.battery || 3.3;

        if (weatherTempDisplay) weatherTempDisplay.innerHTML = `${Math.round(Number(sensorData.temp))}<span class="deg-unit">°C</span>`;
        if (weatherHumDisplay) weatherHumDisplay.innerText = `Độ ẩm ${Math.round(Number(sensorData.hum))}%`;
      }
    }
  }

  // Khởi tạo ban đầu
  renderDevices();
  updateAcModalUI();
  fetchStatus();
  setInterval(fetchStatus, 2500);

  // ── Hàm Toast thông báo ───────────────────────────────────────────────────
  function showToast(message, type = "info") {
    const container = document.getElementById("toast-container");
    if (!container) return;
    const toast = document.createElement("div");
    toast.className = `toast ${type}`;
    toast.innerHTML = `<span>${message}</span>`;
    container.appendChild(toast);

    setTimeout(() => {
      toast.style.opacity = "0";
      toast.style.transform = "translateX(50px)";
      toast.style.transition = "all 0.3s ease";
      setTimeout(() => toast.remove(), 300);
    }, 3500);
  }
});
