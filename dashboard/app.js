const $ = (id) => document.getElementById(id);
const currentPage = document.body.dataset.page; // "dashboard" หรือ "sensors"
const GOAL_ML = 2000;
const SOUND_MAX_P2P = 500; // Scale reference สำหรับคำนวณ % เสียงของ KY-038

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
  week_ml: [1800, 2050, 1500, 1250, 1900, 2100, 1700], // ปริมาณน้ำ 7 วันก่อนหน้า (เก่าสุด -> เมื่อวาน) เป็น mock
  pkts_retried: 95,     // แพ็กเก็ต ESP-NOW ที่ต้องส่งซ้ำ/เก็บคิว (E4) จากทั้งหมด seq
  last_sip_ts: Date.now() - 15 * 60 * 1000,
  last_sip_ml: 45,
  ai_risk: "moderate",  // "normal" (เขียว) | "moderate" (เหลือง) | "high" (แดง)
  alert_state: "idle",  // "idle" (เขียว) | "servo_up" (เหลือง) | "line_sent" (แดง)
  hourly_labels: ["09:00", "10:00", "11:00", "12:00", "13:00", "14:00"],
  hourly_ml: [250, 200, 150, 300, 250, 200],
  sips: [
    { time: "14:20", ml: 45, note: "While coding" },
    { time: "13:45", ml: 155, note: "After lunch break" },
    { time: "12:30", ml: 300, note: "With lunch" },
    { time: "11:10", ml: 150, note: "SG90 flag reminder" },
  ],
  history: [
    { time: "13:50", weight_g: 420, temp: 29, rh: 65 },
    { time: "13:55", weight_g: 420, temp: 29, rh: 66 },
    { time: "14:00", weight_g: 380, temp: 30, rh: 66 },
    { time: "14:05", weight_g: 380, temp: 30, rh: 67 },
    { time: "14:10", weight_g: 357, temp: 30, rh: 67 },
    { time: "14:15", weight_g: 357, temp: 30, rh: 67 },
    { time: "14:20", weight_g: 312.4, temp: 30, rh: 67 },
  ],
};

// ============================================================================
// 2. CHART INITIALIZATION
// ============================================================================
const hasChart = typeof Chart !== "undefined"; // กันสคริปต์พังถ้า CDN ของ Chart.js โหลดไม่ได้
let chartW = null;
let chartRoom = null;

if (currentPage === "sensors" && hasChart) {
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
        plugins: { legend: { display: false } },
        scales: { y: { grid: { color: "#f1f5f9" } } },
      },
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
        animation: false,
        scales: { y: { grid: { color: "#f1f5f9" } } },
      },
    });
  }
}

// Helper ตั้งค่าสีให้วงกลมไอคอนและจุดสถานะด้านขวา (green | yellow | red)
function setStatusRow(iconBoxId, dotId, level) {
  const box = $(iconBoxId);
  const dot = $(dotId);
  if (box) box.className = `ai-circle bg-${level}`;
  if (dot) dot.className = `status-dot dot-${level}`;
}

// ============================================================================
// 3. UI RENDER FUNCTIONS
// ============================================================================
// กราฟแท่ง 7 วันก่อนหน้า (ไม่รวมวันนี้) พร้อมราง, เส้นเป้าหมาย และ tooltip
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
    d.setDate(d.getDate() - (n - i)); // i = n-1 -> เมื่อวาน
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

  // --------------------------------------------------------------------------
  // Row 1: Hydration Risk (เขียว = ปกติ, เหลือง = เริ่มดื่มน้อย, แดง = เสี่ยงขาดน้ำสูง)
  // --------------------------------------------------------------------------
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

  // --------------------------------------------------------------------------
  // Row 2: Desk Presence (เขียว = อยู่ที่โต๊ะ, เหลือง = คุย/เสียงดัง, แดง = ไม่อยู่)
  // --------------------------------------------------------------------------
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

  // --------------------------------------------------------------------------
  // Row 3: Notifications (เขียว = ปกติ, เหลือง = ยกป้ายเตือน, แดง = ส่ง LINE แล้ว)
  // --------------------------------------------------------------------------
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

  // Hourly Chart
  renderWeekBars(v.week_ml || []);

  // Sip History Log
  if ($("sipLogList") && v.sips) {
    $("sipLogList").innerHTML = v.sips
      .map(
        (s) => `
      <li class="sip-item">
        <div>
          <span class="sip-time mono">${s.time}</span>
          <span style="color:#64748b; margin-left:8px;">${s.note}</span>
        </div>
        <span class="sip-amount mono">+${s.ml} ml</span>
      </li>`
      )
      .join("");
  }
}

// E4: สัดส่วนแพ็กเก็ต ESP-NOW ที่ได้ ACK ในครั้งแรก เทียบกับที่ต้องส่งซ้ำ/เก็บคิว
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

function renderSensorsPage(v, historyRows = []) {
  renderLinkSplit(v);
  $("w").textContent = `${Number(v.weight_g).toFixed(1)} g`;
  $("d").textContent = v.dist_cm < 0 ? "no echo" : `${Math.round(v.dist_cm)} cm`;
  $("dDesc").textContent =
    v.dist_cm > 0 && v.dist_cm < 100 ? "User detected at desk (< 100 cm)" : "No user detected at desk";
  $("t").textContent = `${Math.round(v.room_t)} °C`;
  $("rh").textContent = `${Math.round(v.room_rh)} %`;
  $("snd").textContent = `${v.sound_p2p ?? 0} p2p`;
  // ระดับเสียง: Quiet < 100 | Normal 100-300 | Loud > 300 p2p (เกิน 300 = เลื่อนแจ้งเตือน)
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
  $("pirCount").textContent = `PIR ISR Interrupts: ${v.motion_count}`;

  if (v.batt_mv > 0) {
    const battPct = Math.min(100, Math.max(0, Math.round(((v.batt_mv - 3300) / (4200 - 3300)) * 100)));
    $("batt").textContent = `${battPct} %`;
    $("battBar").style.width = `${battPct}%`;
    $("battMv").textContent = `${v.batt_mv.toLocaleString()} mV · Deep Sleep 10s`;
  } else {
    $("batt").textContent = "– %";
    $("battBar").style.width = "0%";
    $("battMv").textContent = "Not yet measured (batt_mv = 0)";
  }

  if (historyRows.length) {
    if (chartW) {
      chartW.data.labels = historyRows.map((r) => r.time);
      chartW.data.datasets[0].data = historyRows.map((r) => r.weight_g);
      chartW.update();
    }
    if (chartRoom) {
      chartRoom.data.labels = historyRows.map((r) => r.time);
      chartRoom.data.datasets[0].data = historyRows.map((r) => r.temp);
      chartRoom.data.datasets[1].data = historyRows.map((r) => r.rh);
      chartRoom.update();
    }
  }
}

function updateAll(v, historyRows = []) {
  $("seqBadge").textContent = `SEQ #${v.seq ?? 0}`;
  $("lastUpdate").textContent = "UPDATED: " + new Date(v.ts).toLocaleTimeString("en-GB");

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
const AI_P_UP = 0.70;   // ตรงกับ gateway/gateway.ino: ความเสี่ยง ≥ ค่านี้ → ยกป้าย
const AI_P_DOWN = 0.62; // ตรงกับ gateway/gateway.ino: ความเสี่ยง ≤ ค่านี้ → ลดป้าย
const DAY_MS = 24 * 60 * 60 * 1000;

const clock = (ts, withSec = false) =>
  new Date(ts).toLocaleTimeString("en-GB", withSec
    ? { hour: "2-digit", minute: "2-digit", second: "2-digit" }
    : { hour: "2-digit", minute: "2-digit" });
const startOfDay = (ts) => { const d = new Date(ts); d.setHours(0, 0, 0, 0); return d.getTime(); };
const sumMl = (rows) => Math.round(rows.reduce((s, d) => s + (d.ml || 0), 0));

function runMock(reason) {
  $("status").textContent = `MOCK DATA MODE (${reason})`;
  updateAll(MOCK_DATA, MOCK_DATA.history);
}

// แปลง /latest และ /drinks ที่ gateway ส่งจริง ให้เป็นรูปแบบที่หน้าเว็บใช้ (ดู dashboard_documentation.md ข้อ 4)
function toView(latest, drinks) {
  const now = Date.now();
  const today0 = startOfDay(now);
  const last = drinks.length ? drinks[drinks.length - 1] : null;
  const week = [];
  for (let i = 7; i >= 1; i--) {
    const from = today0 - i * DAY_MS;
    week.push(sumMl(drinks.filter((d) => d.ts >= from && d.ts < from + DAY_MS)));
  }
  const p = latest.ai_p;
  return {
    ...latest,
    daily_ml: Math.round(latest.ml_today ?? 0),
    hourly_rate: sumMl(drinks.filter((d) => d.ts >= now - 60 * 60 * 1000)),
    last_sip_ts: last ? last.ts : null,
    last_sip_ml: last ? Math.round(last.ml) : null,
    ai_risk: typeof p !== "number" ? null : p >= AI_P_UP ? "high" : p > AI_P_DOWN ? "moderate" : "normal",
    alert_state: latest.estop ? "estop" : latest.sign_up ? "servo_up" : "idle",
    week_ml: drinks.length ? week : [],
    sips: drinks.slice(-6).reverse().map((d) => ({
      time: clock(d.ts),
      ml: Math.round(d.ml),
      note: `Today ${Math.round(d.ml_today ?? 0).toLocaleString()} ml`,
    })),
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
  const { getDatabase, ref, onValue, query, orderByChild, startAt, limitToLast } = fdb;
  const { getAuth, onAuthStateChanged, signInWithEmailAndPassword, signOut } = fauth;
  const app = initializeApp(firebaseConfig);
  const db = getDatabase(app);
  const auth = getAuth(app);

  let latest = null;
  let drinks = [];
  let history = [];
  let unsubs = [];
  const render = () => { if (latest) updateAll(toView(latest, drinks), history); };

  // อ่านไม่ได้เพราะ rules แบบล็อกอิน (E5) → แสดงฟอร์มล็อกอิน
  const onErr = (err) => {
    $("connDot").classList.remove("online");
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
        $("status").textContent = "CONNECTED, WAITING FOR /latest DATA";
        return;
      }
      $("connDot").classList.add("online");
      $("status").textContent = `LIVE FIREBASE${latest.demo ? " (DEMO SPEED)" : ""} | NODE ${latest.node_id}`;
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
    }

    // 3) กราฟย้อนหลัง 30 จุดจาก /readings (หน้า Sensors)
    if (currentPage === "sensors") {
      unsubs.push(onValue(query(ref(db, "readings"), orderByChild("ts"), limitToLast(30)), (snap) => {
        history = [];
        snap.forEach((c) => {
          const r = c.val();
          history.push({ time: clock(r.ts, true), weight_g: r.weight_g, temp: r.room_t, rh: r.room_rh });
        });
        render();
      }, onErr));
    }
  });
}

startLive().catch((err) => runMock("Firebase SDK failed to load: " + err.message));
