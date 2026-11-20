const $ = (id) => document.getElementById(id);
const currentPage = document.body.dataset.page; // "dashboard" หรือ "sensors"
const GOAL_ML = 2000;
const SOUND_MAX_P2P = 500; // Scale reference สำหรับคำนวณ % เสียงของ KY-038
const OFFLINE_TIMEOUT_MS = 30 * 1000; // เกิน 30 วินาทีโดยไม่มีข้อมูลใหม่ = Offline

let lastTelemetryTs = null;
let lastSensorPayload = null;
let isMockMode = false;
let currentNodeId = "coaster-01";
let isDemoSpeed = false;


function formatElapsed(ms) {
  const sec = Math.max(1, Math.floor(ms / 1000));
  if (sec < 60) return `${sec}s`;
  const min = Math.floor(sec / 60);
  if (min < 60) return `${min}m`;
  const hr = Math.floor(min / 60);
  return `${hr}h ${min % 60}m`;
}

// Helper อัปเดตป้าย Online / Offline ของเซนเซอร์แต่ละตัวบนหน้า Sensors
function setSensorBadge(badgeId, cardId, isOnline) {
  const badge = $(badgeId);
  if (badge) {
    badge.className = `sensor-badge ${isOnline ? "is-online" : "is-offline"}`;
    const label = badge.querySelector("span");
    if (label) label.textContent = isOnline ? "Online" : "Offline";
  }
  if (cardId) {
    const card = $(cardId);
    if (card) {
      card.classList.remove("accent-green", "accent-red", "accent-yellow", "accent-blue");
      card.classList.add(isOnline ? "accent-green" : "accent-red");
    }
  }
}

function updatePerSensorStatuses(v, deviceOnline) {
  if (currentPage !== "sensors") return;

  if (!deviceOnline || !v) {
    setSensorBadge("st-hx711", null, false);
    setSensorBadge("st-hcsr04", "card-hcsr04", false);
    setSensorBadge("st-temp", "card-temp", false);
    setSensorBadge("st-rh", "card-rh", false);
    setSensorBadge("st-sound", "card-sound", false);
    setSensorBadge("st-rssi", "card-rssi", false);
    setSensorBadge("st-pir", "card-pir", false);
    return;
  }

  const hx711Ok = typeof v.weight_g === "number" && !Number.isNaN(v.weight_g);
  const hcsr04Ok = typeof v.dist_cm === "number" && !Number.isNaN(v.dist_cm);
  const tempOk = typeof v.room_t === "number" && !Number.isNaN(v.room_t) && v.room_t > 0;
  const rhOk = typeof v.room_rh === "number" && !Number.isNaN(v.room_rh) && v.room_rh > 0;
  const soundOk = typeof v.sound_p2p === "number" && !Number.isNaN(v.sound_p2p) && v.sound_p2p >= 0;
  const rssiOk = typeof v.rssi === "number" && v.rssi !== 0;
  const pirOk = typeof v.motion_count === "number" && !Number.isNaN(v.motion_count);

  setSensorBadge("st-hx711", null, hx711Ok);
  setSensorBadge("st-hcsr04", "card-hcsr04", hcsr04Ok);
  setSensorBadge("st-temp", "card-temp", tempOk);
  setSensorBadge("st-rh", "card-rh", rhOk);
  setSensorBadge("st-sound", "card-sound", soundOk);
  setSensorBadge("st-rssi", "card-rssi", rssiOk);
  setSensorBadge("st-pir", "card-pir", pirOk);
}

function updateDeviceOnlineStatus() {
  const badge = $("deviceStatusBadge");
  const stateText = $("deviceState");
  const connDot = $("connDot");
  const statusEl = $("status");
  const lastUpdateEl = $("lastUpdate");

  if (isMockMode) {
    if (badge) badge.className = "device-status is-mock";
    if (stateText) stateText.textContent = "Mock";
    if (connDot) connDot.className = "pulse-dot";
    updatePerSensorStatuses(lastSensorPayload, true);
    return;
  }

  if (!lastTelemetryTs) {
    if (badge) badge.className = "device-status is-offline";
    if (stateText) stateText.textContent = "Offline";
    if (connDot) connDot.className = "pulse-dot offline";
    updatePerSensorStatuses(null, false);
    return;
  }

  const ageMs = Date.now() - lastTelemetryTs;
  const isOnline = ageMs <= OFFLINE_TIMEOUT_MS;

  if (isOnline) {
    if (badge) badge.className = "device-status is-online";
    if (stateText) stateText.textContent = "Online";
    if (connDot) connDot.className = "pulse-dot online";
    if (statusEl) {
      statusEl.textContent = `LIVE FIREBASE${isDemoSpeed ? " (DEMO SPEED)" : ""} | NODE ${currentNodeId}`;
    }
    if (lastUpdateEl) {
      lastUpdateEl.textContent = "UPDATED: " + new Date(lastTelemetryTs).toLocaleTimeString("en-GB");
    }
  } else {
    const ago = formatElapsed(ageMs);
    if (badge) badge.className = "device-status is-offline";
    if (stateText) stateText.textContent = "Offline";
    if (connDot) connDot.className = "pulse-dot offline";
    if (statusEl) {
      statusEl.textContent = `DEVICE OFFLINE (${ago} ago) | NODE ${currentNodeId}`;
    }
    if (lastUpdateEl) {
      lastUpdateEl.textContent = `LAST SEEN: ${new Date(lastTelemetryTs).toLocaleTimeString("en-GB")} (${ago} ago)`;
    }
  }

  updatePerSensorStatuses(lastSensorPayload, isOnline);
}

setInterval(updateDeviceOnlineStatus, 2000);

function mockWeightSeries(plateaus) {
  const pts = plateaus.flatMap(([w, n]) => Array(n).fill(w));
  const now = Date.now();
  return pts.map((w, i) => {
    const ts = now - (pts.length - 1 - i) * 5 * 60 * 1000;
    return { ts, time: new Date(ts).toLocaleTimeString("en-GB", { hour: "2-digit", minute: "2-digit" }), weight_g: w, temp: 30, rh: 67 };
  });
}

// ============================================================================
// 1. MOCK DATA (ข้อมูลจำลองสำหรับผู้ใช้คนเดียว: coaster-01)
// ============================================================================
const MOCK_DATA = {
  node_id: "coaster-01",
  seq: 1532,
  ts: Date.now(),
  weight_g: 312.4,
  dist_cm: 20,          // > 0 และ < 100 = อยู่ที่โต๊ะ (สีเขียว), -1 = ไม่อยู่ที่โต๊ะ (สีแดง)
  batt_mv: 3910,        // ~68%
  rssi: -54,
  room_t: 30,
  room_rh: 67,
  sound_p2p: 120,       // > 300 = เสียงดัง/คุยอยู่ (สีเหลือง)
  motion_count: 14,
  daily_ml: 1350,
  hourly_rate: 168,
  week_ml: [1800, 2050, 1500, 1250, 1900, 2100, 1700],
  pkts_retried: 95,
  last_sip_ts: Date.now() - 15 * 60 * 1000,
  last_sip_ml: 45,
  ai_risk: "moderate",
  alert_state: "idle",
  hourly_labels: ["09:00", "10:00", "11:00", "12:00", "13:00", "14:00"],
  hourly_ml: [250, 200, 150, 300, 250, 200],
  sips: [
    { time: "14:20", ml: 45, note: "While coding" },
    { time: "13:45", ml: 155, note: "After lunch break" },
    { time: "12:30", ml: 300, note: "With lunch" },
    { time: "11:10", ml: 150, note: "SG90 flag reminder" },
  ],
  // จุดละ 5 นาที: น้ำหนักนิ่ง 3 จุดต่อช่วง แล้วลดลง = ดื่ม, เพิ่มขึ้น = เติมน้ำ (dashboard คำนวณ ml จากน้ำหนักที่หายไปเอง)
  history: mockWeightSeries([[480, 3], [435, 3], [390, 3], [345, 3], [560, 3], [515, 3]]),
};

// ============================================================================
// แถบเลื่อนใต้กราฟ: thumb กว้างตามสัดส่วนช่วงที่เห็น ลาก/กดที่ราง/ใช้ปุ่มลูกศร เพื่อเลื่อนดูช่วงเวลาอื่น
// ============================================================================
// chart       = กราฟ Chart.js (แกน x เป็นแบบ category คือเลขลำดับของข้อมูล)
// ids         = id ของ track / thumb / ข้อความช่วงเวลา
// defaultSpan = จำนวนจุดที่แสดงตอนเริ่มต้น (Infinity = แสดงทั้งหมด)
// limits      = object limits.x ของ plugin zoom (ถ้ามี) ใช้กันเลื่อนเลยขอบข้อมูล
// onChange    = เรียกทุกครั้งที่วาดแถบใหม่ ส่ง { full, atDefault } (full = เห็นข้อมูลทั้งหมดอยู่, atDefault = อยู่ที่มุมมองเริ่มต้น)
// wheelOnChart = true: ล้อเมาส์บนตัวกราฟเลื่อนช่วงเวลา / false: ปล่อยให้ plugin zoom ใช้ล้อซูมแทน (ล้อบนแถบเลื่อนยังเลื่อนได้)
function createChartScroller(chart, ids, { defaultSpan = Infinity, limits = null, onChange = null, wheelOnChart = true } = {}) {
  const track = $(ids.track);
  const thumb = $(ids.thumb);
  const rangeEl = $(ids.range);
  if (!chart || !track || !thumb) return null;

  const MIN_THUMB_PX = 28;
  const state = { min: 0, span: defaultSpan, followEnd: true };
  let total = 0;
  let drag = null;

  // ช่วงที่กราฟแสดงอยู่จริง (index ของจุดแรก, จำนวนจุด)
  function readView() {
    const x = chart.scales.x;
    if (!x || !total) return { min: 0, span: 0 };
    const min = Math.max(0, Math.round(x.min));
    const max = Math.min(total - 1, Math.round(x.max));
    return { min, span: Math.max(1, max - min + 1) };
  }

  function render() {
    const { min, span } = readView();
    const full = !total || span >= total;
    const trackW = track.clientWidth;
    const thumbW = full ? trackW : Math.max(MIN_THUMB_PX, (trackW * span) / total);
    const free = trackW - thumbW;
    thumb.style.width = `${thumbW}px`;
    thumb.style.left = `${full ? 0 : (min / (total - span)) * free}px`;
    track.classList.toggle("is-full", full);
    track.setAttribute("aria-valuemin", "0");
    track.setAttribute("aria-valuemax", String(full ? 0 : total - span));
    track.setAttribute("aria-valuenow", String(min));
    if (rangeEl) {
      const labels = chart.data.labels;
      rangeEl.textContent = total ? `${labels[min]} – ${labels[min + span - 1]}` : "–";
    }
    if (onChange) onChange({ full, atDefault: full || (state.followEnd && Math.abs(span - Math.min(defaultSpan, total)) <= 1) });
  }

  // จำช่วงที่ผู้ใช้เลือก เพื่อคงไว้ตอนมีข้อมูลใหม่เข้ามา (ถ้าอยู่ท้ายสุดจะตามข้อมูลล่าสุดต่อไป)
  function remember() {
    const { min, span } = readView();
    state.min = min;
    state.span = span >= total ? Infinity : span;
    state.followEnd = min + span >= total;
  }

  function apply(min, span) {
    const x = chart.options.scales.x;
    if (span >= total) {
      x.min = undefined;
      x.max = undefined;
    } else {
      x.min = min;
      x.max = min + span - 1;
    }
    chart.update("none");
  }

  function setView(min, span) {
    span = Math.min(Math.max(1, span), total);
    min = Math.min(Math.max(0, min), total - span);
    apply(min, span);
    remember();
    render();
  }

  // เรียกทุกครั้งที่ข้อมูลในกราฟเปลี่ยน
  function refresh() {
    total = chart.data.labels.length;
    if (limits) {
      limits.min = 0;
      limits.max = Math.max(0, total - 1);
    }
    const span = Math.min(state.span, total);
    const min = state.followEnd ? total - span : Math.min(state.min, total - span);
    apply(min, span);
    render();
  }

  // plugin zoom ขยับกราฟเอง: อ่านค่ามาอัปเดตแถบ (ระหว่างลาก) / ดึงกลับให้อยู่ในขอบข้อมูล (ตอนปล่อย)
  function sync() { remember(); render(); }
  function settle() { const v = readView(); setView(v.min, v.span); }

  function reset() {
    state.span = defaultSpan;
    state.followEnd = true;
    refresh();
  }

  track.addEventListener("pointerdown", (e) => {
    const { min, span } = readView();
    if (!total || span >= total) return;
    const free = track.clientWidth - thumb.offsetWidth;
    if (free <= 0) return;
    e.preventDefault();
    track.focus({ preventScroll: true });
    if (e.target === thumb) {
      drag = { startX: e.clientX, startMin: min, span, free };
      track.setPointerCapture(e.pointerId);
      track.classList.add("is-dragging");
    } else {
      // กดที่ราง: ย้ายแถบไปอยู่กึ่งกลางตำแหน่งที่กด
      const rect = track.getBoundingClientRect();
      const ratio = (e.clientX - rect.left - thumb.offsetWidth / 2) / free;
      setView(Math.round(ratio * (total - span)), span);
    }
  });
  track.addEventListener("pointermove", (e) => {
    if (!drag) return;
    const shift = Math.round(((e.clientX - drag.startX) / drag.free) * (total - drag.span));
    setView(drag.startMin + shift, drag.span);
  });
  const endDrag = () => { drag = null; track.classList.remove("is-dragging"); };
  track.addEventListener("pointerup", endDrag);
  track.addEventListener("pointercancel", endDrag);

  track.addEventListener("keydown", (e) => {
    const { min, span } = readView();
    if (!total || span >= total) return;
    const step = Math.max(1, Math.round(span * 0.1));
    const moves = { ArrowLeft: -step, ArrowRight: step, PageUp: -span, PageDown: span };
    if (e.key === "Home") setView(0, span);
    else if (e.key === "End") setView(total - span, span);
    else if (e.key in moves) setView(min + moves[e.key], span);
    else return;
    e.preventDefault();
  });

  // ชี้เมาส์ที่กราฟ (หรือแถบ) แล้วหมุนล้อ/ปัดแทร็กแพดซ้าย-ขวา เพื่อเลื่อนโดยไม่ต้องคลิก
  // ล้อหมุนลง/ปัดขวา = ไปทางข้อมูลใหม่ (ขวา) ล้อหมุนขึ้น/ปัดซ้าย = ย้อนไปข้อมูลเก่า (ซ้าย)
  // ถ้าชนขอบแล้ว (เลื่อนต่อไม่ได้) จะไม่ดักล้อ ปล่อยให้เลื่อนหน้าเว็บตามปกติ
  let wheelRest = 0;
  const WHEEL_UNIT = { 0: 1, 1: 33, 2: 400 }; // deltaMode: pixel / line / page
  const onWheel = (e) => {
    if (e.ctrlKey) return; // Ctrl + ล้อ = ซูมหน้าเว็บของเบราว์เซอร์
    const { min, span } = readView();
    if (!total || span >= total) return;
    const raw = Math.abs(e.deltaX) > Math.abs(e.deltaY) ? e.deltaX : e.deltaY;
    const delta = raw * (WHEEL_UNIT[e.deltaMode] ?? 1);
    if (!delta) return;
    if ((delta < 0 && min <= 0) || (delta > 0 && min + span >= total)) {
      wheelRest = 0;
      return;
    }
    e.preventDefault();
    wheelRest += delta * span * 0.001; // ล้อ 1 คลิก (100) = เลื่อน 10% ของช่วงที่เห็น
    const step = Math.trunc(wheelRest);
    if (!step) return;
    wheelRest -= step;
    setView(min + step, span);
  };
  if (wheelOnChart) chart.canvas.addEventListener("wheel", onWheel, { passive: false });
  track.addEventListener("wheel", onWheel, { passive: false });

  if (typeof ResizeObserver !== "undefined") new ResizeObserver(render).observe(track);

  return { refresh, reset, sync, settle };
}

// ============================================================================
// 2. CHART INITIALIZATION
// ============================================================================
const hasChart = typeof Chart !== "undefined";
let chartW = null;
let chartRoom = null;
let weightScroller = null;
let roomScroller = null;
const ROOM_DEFAULT_POINTS = 30; // กราฟห้องเริ่มต้นแสดง 30 ค่าล่าสุด แล้วเลื่อนย้อนดูได้
const WEIGHT_DEFAULT_POINTS = 60; // กราฟน้ำหนักเริ่มต้นแสดง 60 ค่าล่าสุด แล้วเลื่อนย้อนดูได้ (ซูมได้ด้วยล้อเมาส์/ลากคลุม)
const weightLimitsX = { min: 0, max: 0 }; // แก้ค่าตามจำนวนข้อมูลใน scroller.refresh()

if (currentPage === "sensors" && hasChart) {
  if (window.ChartZoom) {
    Chart.register(window.ChartZoom);
  }

  if ($("chartW")) {
    chartW = new Chart($("chartW"), {
      type: "line",
      data: {
        labels: [],
        datasets: [
          {
            label: "Coaster weight (g)",
            data: [],
            borderColor: "#0284c7",
            backgroundColor: "rgba(2, 132, 199, 0.12)",
            fill: true,
            tension: 0.25,
          },
        ],
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: false,
        interaction: {
          mode: "index",
          intersect: false,
        },
        plugins: {
          legend: { display: false },
          tooltip: {
            callbacks: { label: (ctx) => `${ctx.dataset.label}: ${Math.round(ctx.parsed.y)}` },
          },
          zoom: {
            pan: {
              enabled: true,
              mode: "x",
              modifierKey: "shift",
              onPan: () => { if (weightScroller) weightScroller.sync(); },
              onPanComplete: () => { if (weightScroller) weightScroller.settle(); },
            },
            zoom: {
              wheel: {
                enabled: true,
                speed: 0.1,
              },
              pinch: {
                enabled: true,
              },
              drag: {
                enabled: true,
                backgroundColor: "rgba(26, 141, 174, 0.16)",
                borderColor: "#1a8dae",
                borderWidth: 1,
                threshold: 8,
              },
              mode: "x",
              onZoom: () => { if (weightScroller) weightScroller.sync(); },
              onZoomComplete: () => { if (weightScroller) weightScroller.settle(); },
            },
            limits: {
              x: weightLimitsX,
            },
          },
        },
        scales: {
          x: {
            ticks: { maxRotation: 0, autoSkip: true, maxTicksLimit: 10 },
          },
          y: {
            grid: { color: "#f1f5f9" },
            ticks: { precision: 0 }, // แกนน้ำหนักเป็นจำนวนเต็ม (g)
          },
        },
      },
    });

    // Double-click chart to reset zoom
    $("chartW").addEventListener("dblclick", () => {
      if (weightScroller) weightScroller.reset();
    });
  }

  if ($("chartRoom")) {
    chartRoom = new Chart($("chartRoom"), {
      type: "line",
      data: {
        labels: [],
        datasets: [
          {
            label: "Temperature (°C)",
            data: [],
            borderColor: "#facc15",
            backgroundColor: "rgba(250, 204, 21, 0.15)",
            tension: 0.25,
          },
          {
            label: "Humidity (%)",
            data: [],
            borderColor: "#0284c7",
            backgroundColor: "rgba(2, 132, 199, 0.1)",
            tension: 0.25,
          },
        ],
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: false,
        scales: {
          x: { ticks: { maxRotation: 0, autoSkip: true, maxTicksLimit: 8 } },
          y: { grid: { color: "#f1f5f9" } },
        },
      },
    });
  }

  weightScroller = createChartScroller(
    chartW,
    { track: "weightScrollTrack", thumb: "weightScrollThumb", range: "weightScrollRange" },
    {
      defaultSpan: WEIGHT_DEFAULT_POINTS,
      limits: weightLimitsX,
      wheelOnChart: false, // ล้อบนกราฟน้ำหนัก = ซูม (เลื่อนด้วยแถบ หรือ Shift + ลาก)
      onChange: ({ atDefault }) => { if ($("resetWeightZoom")) $("resetWeightZoom").hidden = atDefault; },
    }
  );
  roomScroller = createChartScroller(
    chartRoom,
    { track: "roomScrollTrack", thumb: "roomScrollThumb", range: "roomScrollRange" },
    { defaultSpan: ROOM_DEFAULT_POINTS }
  );
}

if (currentPage === "sensors" && $("resetWeightZoom")) {
  $("resetWeightZoom").addEventListener("click", () => {
    if (weightScroller) weightScroller.reset();
  });
}

function setStatusRow(iconBoxId, dotId, level) {
  const box = $(iconBoxId);
  const dot = $(dotId);
  if (box) box.className = `ai-circle bg-${level}`;
  if (dot) dot.className = `status-dot dot-${level}`;
}

// ============================================================================
// 3. UI RENDER FUNCTIONS
// ============================================================================
function renderWeekBars(mlList) {
  const el = $("weekBars");
  if (!el) return;
  if (!mlList.length) {
    el.innerHTML = '<div class="hb-empty">No history yet</div>';
    return;
  }
  const n = mlList.length;
  const days = mlList.map((ml, i) => {
    const d = new Date();
    d.setDate(d.getDate() - (n - i));
    return {
      ml,
      short: d.toLocaleDateString("en-GB", { weekday: "short" }),
      full: d.toLocaleDateString("en-GB", { weekday: "short", day: "numeric", month: "short" }),
    };
  });
  const top = Math.ceil((Math.max(GOAL_ML, ...mlList) * 1.1) / 500) * 500;
  const ticks = [];
  for (let t = 0; t <= GOAL_ML; t += 1000) ticks.push(t);
  const pos = (ml) => (ml / top) * 100;

  el.innerHTML = `
    <div class="hb-y">${ticks.map((t) => `<span style="bottom:${pos(t)}%">${t.toLocaleString()}</span>`).join("")}</div>
    <div class="hb-main">
      <div class="hb-plot">
        ${ticks.map((t) => `<div class="hb-line" style="bottom:${pos(t)}%"></div>`).join("")}
        <div class="hb-goal" style="bottom:${pos(GOAL_ML)}%"></div>
        ${days.map((d) => `
          <div class="hb-col" tabindex="0" style="--h:${pos(d.ml)}%" aria-label="${d.full}: ${d.ml} ml">
            <div class="hb-track"><div class="hb-bar${d.ml >= GOAL_ML ? " met" : ""}"></div></div>
            <div class="hb-tip" role="tooltip"><b class="mono">${d.full}</b><span class="mono">${d.ml.toLocaleString()} ml · ${Math.round((d.ml / GOAL_ML) * 100)}% of goal</span></div>
          </div>`).join("")}
      </div>
      <div class="hb-xs">${days.map((d) => `<span>${d.short}</span>`).join("")}</div>
    </div>`;
}

function renderDashboardPage(v) {
  const dailyMl = v.daily_ml ?? 0;
  const pct = Math.min(100, Math.round((dailyMl / GOAL_ML) * 1000) / 10);
  const remain = Math.max(0, GOAL_ML - dailyMl);

  $("pctText").textContent = `${pct}%`;
  $("mlText").textContent = `${dailyMl.toLocaleString()} / ${GOAL_ML.toLocaleString()} ml`;
  $("remainText").textContent = remain > 0
    ? `${remain.toLocaleString()} ml remaining to reach your goal`
    : "Daily goal reached!";
  $("waterLevel").style.height = `${pct}%`;
  $("hourlyRateBadge").textContent = `${v.hourly_rate ?? 150} ml/hr`;
  $("currentWeight").textContent = `${Number(v.weight_g).toFixed(1)} g`;

  $("lastSipTime").textContent = v.last_sip_ts
    ? new Date(v.last_sip_ts).toLocaleTimeString("en-GB", { hour: "2-digit", minute: "2-digit" })
    : "–";
  $("lastSipMl").textContent = v.last_sip_ml ? `+${v.last_sip_ml} ml` : "–";

  const risk = v.ai_risk || (pct >= 75 ? "normal" : pct >= 50 ? "moderate" : "high");
  if (risk === "normal") {
    setStatusRow("aiIconBox", "aiDot", "green");
    $("aiScore").textContent = "NORMAL";
    $("aiStatusText").textContent = "Hydration on track";
  } else if (risk === "moderate") {
    setStatusRow("aiIconBox", "aiDot", "yellow");
    $("aiScore").textContent = "MODERATE";
    $("aiStatusText").textContent = "Drinking less water than usual";
  } else {
    setStatusRow("aiIconBox", "aiDot", "red");
    $("aiScore").textContent = "HIGH RISK";
    $("aiStatusText").textContent = "High dehydration risk (drink now)";
  }
  if (typeof v.ai_p === "number") $("aiScore").textContent += ` (${Math.round(v.ai_p * 100)}%)`;
  $("aiSubText").textContent = `Room temperature ${Math.round(v.room_t)}°C · Humidity ${Math.round(v.room_rh)}%`;

  const atDesk = v.dist_cm > 0 && v.dist_cm < 100;
  const isNoisy = (v.sound_p2p ?? 0) > 300;
  if (!atDesk) {
    setStatusRow("deskIconBox", "deskDot", "red");
    $("deskStatusText").textContent = "Away from desk (alerts disabled)";
    $("deskSubText").textContent = "No ultrasonic presence detected";
  } else if (isNoisy) {
    setStatusRow("deskIconBox", "deskDot", "yellow");
    $("deskStatusText").textContent = "At desk · In conversation (alerts paused)";
    $("deskSubText").textContent = `Distance ${Math.round(v.dist_cm)} cm · Room is noisy (${v.sound_p2p} p2p)`;
  } else {
    setStatusRow("deskIconBox", "deskDot", "green");
    $("deskStatusText").textContent = "At desk (alerts enabled)";
    $("deskSubText").textContent = `Distance ${Math.round(v.dist_cm)} cm · Room is quiet`;
  }

  const alertState = v.alert_state || (v.servo_up ? "servo_up" : "idle");
  if (alertState === "estop") {
    setStatusRow("alertIconBox", "alertDot", "red");
    $("alertStatusText").textContent = "Emergency stop pressed (sign disabled)";
    $("alertSubText").textContent = "SG90: Off | Type c in the gateway Serial Monitor to release";
  } else if (alertState === "idle") {
    setStatusRow("alertIconBox", "alertDot", "green");
    $("alertStatusText").textContent = "System ready (alerts enabled)";
    $("alertSubText").textContent = "SG90: Retracted | LINE: Sent if the flag stays raised for 10 minutes";
  } else if (alertState === "servo_up") {
    setStatusRow("alertIconBox", "alertDot", "yellow");
    $("alertStatusText").textContent = "Reminder flag raised (waiting for sip)";
    $("alertSubText").textContent = "SG90: Raised | LINE: Will send if unheeded for 10 minutes";
  } else {
    setStatusRow("alertIconBox", "alertDot", "red");
    $("alertStatusText").textContent = "LINE alert sent (30m cooldown)";
    $("alertSubText").textContent = "SG90: Raised | LINE: Notification delivered";
  }

  renderWeekBars(v.week_ml || []);

  if ($("sipLogList") && v.sips) {
    $("sipLogList").innerHTML = v.sips
      .map(
        (s) => `
      <li class="sip-item${s.type === "refill" ? " is-refill" : ""}">
        <div>
          <span class="sip-time mono">${s.time}</span>
          ${s.type === "refill" ? '<span class="sip-tag">Refill</span>' : ""}
          <span style="color:#64748b; margin-left:8px;">${s.note}</span>
        </div>
        <span class="sip-amount mono">+${s.ml} ml</span>
      </li>`
      )
      .join("");
  }
}

function renderLinkSplit(v) {
  if (!$("lnkRow")) return;
  const total = v.seq ?? 0;
  const retried = v.pkts_retried;
  if (!total || retried == null) {
    $("lnkTotal").textContent = "No data";
    return;
  }
  const ok = Math.max(0, total - retried);
  const okPct = Math.round((ok / total) * 1000) / 10;
  const rtPct = Math.round((retried / total) * 1000) / 10;
  $("lnkTotal").textContent = `${total.toLocaleString()} packets`;
  $("lnkOkPct").textContent = okPct;
  $("lnkRtPct").textContent = rtPct;
  $("lnkOkN").textContent = `${ok.toLocaleString()} packets`;
  $("lnkRtN").textContent = `${retried.toLocaleString()} packets`;
  $("lnkRow").style.setProperty("--am", `${Math.max(okPct, 1)}fr`);
  $("lnkRow").style.setProperty("--pm", `${Math.max(rtPct, 1)}fr`);
}

function renderWeightChart(rows) {
  if (!chartW) return;
  const now = Date.now();
  const today0 = startOfDay(now); // จำกัดข้อมูลสูงสุดเฉพาะภายในวันนี้ (00:00 - ปัจจุบัน)
  let todayRows = rows.filter((r) => (r.ts ?? now) >= today0);

  if (todayRows.length > 400) {
    const step = Math.ceil(todayRows.length / 400);
    todayRows = todayRows.filter((_, idx) => idx % step === 0 || idx === todayRows.length - 1);
  }

  chartW.data.labels = todayRows.map((r) => r.time);
  // แสดงเป็นกรัมเต็มหน่วย: ความต่างไม่ถึง 1 g ไม่ต้องโชว์ละเอียด
  chartW.data.datasets[0].data = todayRows.map((r) => (Number.isFinite(r.weight_g) ? Math.round(r.weight_g) : r.weight_g));
  chartW.data.datasets[0].pointRadius = todayRows.length > 80 ? 0 : 3;
  if (weightScroller) weightScroller.refresh();
  else chartW.update("none");
}

function renderSensorsPage(v, historyRows = []) {
  renderLinkSplit(v);
  $("w").textContent = Number.isFinite(Number(v.weight_g)) ? `${Math.round(Number(v.weight_g))} g` : "– g";
  $("d").textContent = v.dist_cm < 0 ? "no echo" : `${Math.round(v.dist_cm)} cm`;
  $("dDesc").textContent =
    v.dist_cm > 0 && v.dist_cm < 100 ? "User detected at desk (< 100 cm)" : "No user detected at desk";
  $("t").textContent = `${Math.round(v.room_t)} °C`;
  $("rh").textContent = `${Math.round(v.room_rh)} %`;
  $("snd").textContent = `${v.sound_p2p ?? 0} p2p`;

  const sndP2p = v.sound_p2p ?? 0;
  const sndPct = Math.min(100, Math.max(0, Math.round((sndP2p / SOUND_MAX_P2P) * 100)));
  $("sndBar").style.width = `${sndPct}%`;
  $("sndBar").classList.toggle("is-loud", sndP2p > 300);
  $("sndState").textContent =
    sndP2p > 300 ? "Loud sound / conversation (alerts paused)"
    : sndP2p < 100 ? "Quiet room (alerts enabled)"
    : "Normal sound (alerts enabled)";
  const rhPct = Math.min(100, Math.max(0, Math.round(v.room_rh ?? 0)));
  $("rhBar").style.width = `${rhPct}%`;
  $("rssi").textContent = `${v.rssi} dBm`;
  $("pirCount").textContent = `${v.motion_count ?? "–"} ISR`;

  if (historyRows.length) {
    renderWeightChart(historyRows);

    if (chartRoom) {
      chartRoom.data.labels = historyRows.map((r) => r.time);
      chartRoom.data.datasets[0].data = historyRows.map((r) => r.temp);
      chartRoom.data.datasets[1].data = historyRows.map((r) => r.rh);
      if (roomScroller) roomScroller.refresh();
      else chartRoom.update();
    }
  }
}

function updateAll(v, historyRows = []) {
  lastSensorPayload = v;
  $("seqBadge").textContent = `SEQ #${v.seq ?? 0}`;
  $("lastUpdate").textContent = "UPDATED: " + new Date(v.ts).toLocaleTimeString("en-GB");
  updateDeviceOnlineStatus();

  if (currentPage === "dashboard") {
    renderDashboardPage(v);
  } else if (currentPage === "sensors") {
    renderSensorsPage(v, historyRows);
  }
}

// ============================================================================
// 4. DATA SOURCE: Firebase จริง (ถ้าไม่มี config.js จะแสดง MOCK_DATA แทน)
// ============================================================================
const FB_SDK = "https://www.gstatic.com/firebasejs/10.12.2";
const AI_P_UP = 0.70;
const AI_P_DOWN = 0.62;
const DAY_MS = 24 * 60 * 60 * 1000;

const clock = (ts, withSec = false) =>
  new Date(ts).toLocaleTimeString("en-GB", withSec
    ? { hour: "2-digit", minute: "2-digit", second: "2-digit" }
    : { hour: "2-digit", minute: "2-digit" });
const startOfDay = (ts) => { const d = new Date(ts); d.setHours(0, 0, 0, 0); return d.getTime(); };
const sumMl = (rows) => Math.round(rows.reduce((s, d) => s + (d.ml || 0), 0));

// ตรวจจับการดื่มจากน้ำหนักที่หายไป (พอร์ตจาก gateway/drink_detect.h ใช้ตัวเลขชุดเดียวกัน)
//   น้ำหนักนิ่งใหม่ ต่ำกว่าน้ำหนักนิ่งก่อนหน้า >= drinkMinG → ดื่มไป (ก่อน - หลัง) ml  (น้ำ 1 g ≈ 1 ml)
const DRINK_CFG = { cupMinG: 40, stableG: 5, stableCount: 2, drinkMinG: 15, drinkMaxG: 600, refillMinG: 30, sipMinG: 5 };

function detectDrinks(rows, cfg = DRINK_CFG) {
  const out = [], refills = [];
  let present = false, lifted = false, run = 0, last = NaN, ref = NaN, total = 0, day = null;
  for (const r of rows) {
    const w = r.weight_g;
    if (typeof w !== "number" || Number.isNaN(w)) continue;
    const d0 = startOfDay(r.ts);
    if (d0 !== day) { day = d0; total = 0; }
    if (w < cfg.cupMinG) { if (present) lifted = true; present = false; run = 0; last = w; continue; }   // ยกแก้ว ไม่นับ (จำไว้ว่ายกแล้ว)
    if (!present) { present = true; run = 0; last = NaN; }
    if (!Number.isNaN(last) && Math.abs(w - last) <= cfg.stableG) run++; else run = 0;
    const prev = last; last = w;
    if (run < cfg.stableCount) continue;                                       // ยังไม่นิ่ง
    const level = (w + prev) / 2;
    if (Number.isNaN(ref)) { ref = level; lifted = false; continue; }
    const d = ref - level;
    if (d >= cfg.drinkMinG && d <= cfg.drinkMaxG) {
      total += d;
      out.push({ ts: r.ts, ml: d, w_from: ref, w_to: level, ml_today: total });
      ref = level; lifted = false;
    } else if (-d >= cfg.refillMinG) {
      refills.push({ ts: r.ts, ml: -d, w_from: ref, w_to: level });            // น้ำหนักเพิ่ม = เติมน้ำ
      ref = level; lifted = false;
    } else if (d > cfg.drinkMaxG) {
      ref = level; lifted = false;                                             // เปลี่ยนแก้ว
    } else if (lifted) {
      lifted = false;                                                          // ยกแก้วขึ้นแล้ววางกลับ แต่น้ำลดไม่ถึง drinkMinG
      if (d >= cfg.sipMinG) {                                                  // จิบเล็ก มีหลักฐานการยกแก้ว นับเลย
        total += d;
        out.push({ ts: r.ts, ml: d, w_from: ref, w_to: level, ml_today: total });
        ref = level;
      } else if (d <= 0) ref = level;                                          // ไม่ลด/เพิ่มนิดหน่อย ตั้งฐานใหม่
    } else if (Math.abs(d) < cfg.stableG) {
      ref = level;                                                             // ค่าเลื่อนช้า
    }
  }
  return { drinks: out, refills };
}

function buildMockView() {
  const rows = MOCK_DATA.history;
  const now = Date.now();
  const { drinks, refills } = detectDrinks(rows);
  return {
    ...MOCK_DATA,
    ts: now,
    weight_g: rows[rows.length - 1].weight_g,
    ...drinkStats(drinks, now, refills),
  };
}

// สรุปจากรายการดื่ม (ใช้ทั้ง mock และ Firebase)
function drinkStats(drinks, now = Date.now(), refills = []) {
  const last = drinks.length ? drinks[drinks.length - 1] : null;
  const today0 = startOfDay(now);
  return {
    daily_ml: sumMl(drinks.filter((d) => d.ts >= today0)),
    hourly_rate: sumMl(drinks.filter((d) => d.ts >= now - 60 * 60 * 1000)),
    last_sip_ts: last ? last.ts : null,
    last_sip_ml: last ? Math.round(last.ml) : null,
    // Recent Drinks: รวมการดื่มกับการเติมน้ำ เรียงใหม่สุดก่อน
    sips: [
      ...drinks.map((d) => sipRow(d, "drink")),
      ...refills.map((d) => sipRow(d, "refill")),
    ].sort((a, b) => (b.ts ?? 0) - (a.ts ?? 0)).slice(0, 6),
  };
}

function sipRow(d, type = "drink") {
  return {
    type,
    ts: d.ts,
    time: clock(d.ts),
    ml: Math.round(d.ml),
    note: typeof d.w_from === "number" && typeof d.w_to === "number"
      ? `${Math.round(d.w_from)} g → ${Math.round(d.w_to)} g`
      : `Today ${Math.round(d.ml_today ?? 0).toLocaleString()} ml`,
  };
}

function runMock(reason) {
  isMockMode = true;
  $("status").textContent = `MOCK DATA MODE (${reason})`;
  updateAll(buildMockView(), MOCK_DATA.history);
}

function toView(latest, drinks, refills = []) {
  const now = Date.now();
  const today0 = startOfDay(now);
  const stats = drinkStats(drinks, now, refills);
  const week = [];
  for (let i = 7; i >= 1; i--) {
    const from = today0 - i * DAY_MS;
    week.push(sumMl(drinks.filter((d) => d.ts >= from && d.ts < from + DAY_MS)));
  }
  const p = latest.ai_p;
  return {
    ...latest,
    daily_ml: Math.round(latest.ml_today ?? 0),
    hourly_rate: stats.hourly_rate,
    last_sip_ts: stats.last_sip_ts,
    last_sip_ml: stats.last_sip_ml,
    ai_risk: typeof p !== "number" ? null : p >= AI_P_UP ? "high" : p > AI_P_DOWN ? "moderate" : "normal",
    alert_state: latest.estop ? "estop" : latest.line_sent ? "line_sent" : latest.sign_up ? "servo_up" : "idle",
    week_ml: drinks.length ? week : [],
    sips: stats.sips,
  };
}

async function startLive() {
  $("status").textContent = "CONNECTING TO FIREBASE...";
  let firebaseConfig;
  try {
    ({ firebaseConfig } = await import("./config.js"));
  } catch (e) {
    runMock("no config.js: copy config.example.js to config.js");
    return;
  }

  const [{ initializeApp }, fdb, fauth] = await Promise.all([
    import(`${FB_SDK}/firebase-app.js`),
    import(`${FB_SDK}/firebase-database.js`),
    import(`${FB_SDK}/firebase-auth.js`),
  ]);
  const { getDatabase, ref, onValue, query, orderByChild, startAt } = fdb;
  const { getAuth, onAuthStateChanged, signInWithEmailAndPassword, signOut } = fauth;
  const app = initializeApp(firebaseConfig);
  const db = getDatabase(app);
  const auth = getAuth(app);

  let latest = null;
  let drinks = [];
  let refills = [];
  let history = [];
  let unsubs = [];
  const render = () => { if (latest) updateAll(toView(latest, drinks, refills), history); };

  const onErr = (err) => {
    lastTelemetryTs = null;
    updateDeviceOnlineStatus();
    if (String(err.code || err.message).toLowerCase().includes("permission")) {
      $("login").hidden = false;
      $("status").textContent = "LOGIN REQUIRED (database rules)";
    } else {
      $("status").textContent = "FIREBASE ERROR: " + err.message;
    }
  };

  $("login").addEventListener("submit", async (e) => {
    e.preventDefault();
    try {
      await signInWithEmailAndPassword(auth, $("email").value, $("pass").value);
    } catch (err) {
      $("status").textContent = "LOGIN FAILED: " + err.code;
    }
  });
  $("logout").addEventListener("click", (e) => { e.preventDefault(); signOut(auth); });

  onAuthStateChanged(auth, (user) => {
    unsubs.forEach((u) => u());
    unsubs = [];
    $("login").hidden = true;
    $("who").hidden = !user;
    $("whoEmail").textContent = user ? user.email : "";

    // 1) ค่าล่าสุดจาก /latest
    unsubs.push(onValue(ref(db, "latest"), (snap) => {
      latest = snap.val();
      if (!latest) {
        lastTelemetryTs = null;
        $("status").textContent = "CONNECTED, WAITING FOR /latest DATA";
        updateDeviceOnlineStatus();
        return;
      }
      isMockMode = false;
      lastTelemetryTs = latest.ts || Date.now();
      currentNodeId = latest.node_id || "coaster-01";
      isDemoSpeed = Boolean(latest.demo);
      updateDeviceOnlineStatus();
      render();
    }, onErr));

    // 2) การดื่มย้อนหลัง 7 วัน + วันนี้ จาก /drinks (หน้า Hydration)
    if (currentPage === "dashboard") {
      const since = startOfDay(Date.now()) - 7 * DAY_MS;
      unsubs.push(onValue(query(ref(db, "drinks"), orderByChild("ts"), startAt(since)), (snap) => {
        drinks = [];
        snap.forEach((c) => { drinks.push(c.val()); });
        render();
      }, onErr));

      // การเติมน้ำ จาก /refills (แสดงรวมใน Recent Drinks)
      unsubs.push(onValue(query(ref(db, "refills"), orderByChild("ts"), startAt(since)), (snap) => {
        refills = [];
        snap.forEach((c) => { refills.push(c.val()); });
        render();
      }, onErr));
    }

    // 3) กราฟย้อนหลังเฉพาะภายในวันนี้ (ตั้งแต่ 00:00) จาก /readings (หน้า Sensors)
    if (currentPage === "sensors") {
      const todayStart = startOfDay(Date.now());
      unsubs.push(onValue(query(ref(db, "readings"), orderByChild("ts"), startAt(todayStart)), (snap) => {
        history = [];
        snap.forEach((c) => {
          const r = c.val();
          history.push({ ts: r.ts, time: clock(r.ts, true), weight_g: r.weight_g, temp: r.room_t, rh: r.room_rh });
        });
        render();
      }, onErr));
    }
  });
}

startLive().catch((err) => runMock("Firebase SDK failed to load: " + err.message));