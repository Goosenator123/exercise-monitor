"use strict";

const byId = (id) => document.getElementById(id);

const accelerationHistory = [];
const heartHistory = [];

let isConnected = false;
let isResetPending = false;

let lastResetId = null;
let lastSampleTime = null;
let lastSampleReceivedAt = 0;
let resetRequestedAt = 0;
let swayThreshold = 3;

let socket = null;
let reconnectTimer = null;
let reconnectDelayMs = 1000;
let connectionStartedAt = 0;
let lastMessageAt = 0;

function showNotice(message, isError = false) {
  byId("notice").textContent = message;
  byId("notice").classList.toggle("error", isError);
}

function setConnection(connected, message) {
  isConnected = connected;

  byId("connection").classList.toggle("live", connected);
  byId("connectionText").textContent = message;
  document.body.classList.toggle("stale", !connected);

  byId("resetButton").disabled = !connected || isResetPending;
}

function setWarning(id, active) {
  const element = byId(id);

  element.classList.toggle("active", active);
  element.querySelector("b").textContent = active ? "Detected" : "Clear";
}

// Validate incoming readings, update the dashboard, and record fresh graph samples.
function receiveReadings(data) {
  if (typeof data.sampleMs !== "number" || typeof data.ready !== "boolean") {
    throw new Error("Invalid device response");
  }

  const now = performance.now();

  if (!data.ready) {
    setConnection(false, "Calibrating");
    showNotice("Tracker connected. Keep the motion sensor still during calibration.");
    return;
  }

  if (![data.angle, data.reps, data.sideAcceleration, data.bpm, data.resetId].every(Number.isFinite)) {
    throw new Error("Invalid sensor readings");
  }

  // Avoid duplicate samples and clear history after a device restart or set reset.
  if (data.sampleMs !== lastSampleTime) {
    if (
      (lastSampleTime !== null && data.sampleMs < lastSampleTime) ||
      (lastResetId !== null && data.resetId !== lastResetId)
    ) {
      accelerationHistory.length = 0;
      heartHistory.length = 0;
    }

    const resetConfirmed = lastResetId !== null && data.resetId !== lastResetId;

    if (resetConfirmed) isResetPending = false;

    lastResetId = data.resetId;
    lastSampleTime = data.sampleMs;
    lastSampleReceivedAt = now;

    accelerationHistory.push({ time: now, value: data.sideAcceleration });

    // Missing BPM values create gaps in the graph instead of displaying zero.
    heartHistory.push({
      time: now,
      value: data.heartConnected && data.fingerPresent && data.bpmAvailable ? data.bpm : null,
    });

    if (resetConfirmed) {
      showNotice("Set reset. Ready for your next rep.");
    }
  }

  if (now - lastSampleReceivedAt > 2500) {
    setConnection(false, "Readings paused");
    showNotice("The device responds, but sensor readings have stopped updating.", true);
    return;
  }

  setConnection(true, "Connected");

  byId("reps").textContent = Math.round(data.reps);
  byId("angle").textContent = Math.round(data.angle);

  const hasBpm = data.heartConnected && data.fingerPresent && data.bpmAvailable;

  byId("bpm").textContent = hasBpm ? Math.round(data.bpm) : "—";

  byId("heartStatus").textContent = !data.heartConnected
    ? "Sensor unavailable"
    : !data.fingerPresent
      ? "Place finger on sensor"
      : !hasBpm
        ? "Detecting… Hold still"
        : "Live heart-rate estimate";

  byId("heartGraphStatus").textContent = hasBpm ? "Receiving readings" : "No current BPM";

  const progress = Math.max(0, Math.min(120, data.angle));

  byId("progressFill").style.width = `${(progress / 120) * 100}%`;
  byId("progress").setAttribute("aria-valuenow", String(Math.round(progress)));
  byId("progressLabel").textContent = `${Math.round(data.angle)}° / 120°`;

  setWarning("speedWarning", data.tooFast);
  setWarning("swayWarning", data.excessiveSway);

  swayThreshold = Number.isFinite(data.swayThreshold) ? data.swayThreshold : 3;

  // Allow another reset attempt if incoming readings do not confirm it within five seconds.
  if (isResetPending && now - resetRequestedAt > 5000) {
    isResetPending = false;
    byId("resetButton").disabled = false;
    showNotice("Reset was not confirmed. Check the tracker and try again.", true);
  } else if (isResetPending) {
    showNotice("Reset requested. Waiting for the tracker…");
  } else {
    showNotice("Live readings · Warnings use your configured thresholds.");
  }
}

function markDisconnected() {
  setConnection(false, "Disconnected");
  showNotice("Live connection lost. Retrying automatically; keep connected to the device Wi-Fi.", true);

  byId("heartStatus").textContent = "Connection lost · readings are stale";
  byId("heartGraphStatus").textContent = "Connection lost";
}

// Schedule one retry at a time, doubling the delay up to ten seconds.
function scheduleReconnect() {
  if (reconnectTimer !== null) return;

  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    connectWebSocket();
  }, reconnectDelayMs);

  reconnectDelayMs = Math.min(reconnectDelayMs * 2, 10000);
}

// Maintain one WebSocket connection to the device serving this page.
function connectWebSocket() {
  if (socket && (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)) return;

  setConnection(false, "Connecting");

  connectionStartedAt = performance.now();
  lastMessageAt = connectionStartedAt;

  const host = window.location.hostname;

  if (!host || window.location.protocol !== "http:") {
    showNotice("Open this dashboard from http://192.168.4.1 while connected to the device Wi-Fi.", true);
    return;
  }

  let currentSocket;

  try {
    currentSocket = new WebSocket(`ws://${window.location.host}/ws`);
  } catch (error) {
    markDisconnected();
    scheduleReconnect();
    return;
  }

  socket = currentSocket;

  // Each handler ignores events from connections that have already been replaced.
  currentSocket.onopen = () => {
    if (socket !== currentSocket) return;

    lastMessageAt = performance.now();

    setConnection(false, "Waiting for readings");
    showNotice("WebSocket connected. Waiting for sensor readings…");
  };

  currentSocket.onmessage = (event) => {
    if (socket !== currentSocket) return;

    try {
      receiveReadings(JSON.parse(event.data));

      lastMessageAt = performance.now();
      reconnectDelayMs = 1000;
    } catch (error) {
      console.error("Invalid tracker message:", error, event.data);

      setConnection(false, "Invalid data");
      showNotice("Received invalid device data. Reconnecting…", true);

      currentSocket.close();
    }
  };

  currentSocket.onerror = () => {
    if (socket === currentSocket) currentSocket.close();
  };

  currentSocket.onclose = () => {
    if (socket !== currentSocket) return;

    socket = null;

    // Never resend a reset automatically: the device may already have applied it.
    isResetPending = false;

    markDisconnected();
    scheduleReconnect();
  };
};

// Send one reset request; a changed reset ID in subsequent readings confirms it.
byId("resetButton").addEventListener("click", () => {
  if (!isConnected || isResetPending || !socket || socket.readyState !== WebSocket.OPEN) return;

  if (socket.bufferedAmount > 0) {
    showNotice("Connection is busy. Try resetting again in a moment.", true);
    return;
  }

  isResetPending = true;
  resetRequestedAt = performance.now();

  byId("resetButton").disabled = true;
  showNotice("Reset requested. Waiting for the tracker…");

  try {
    socket.send("reset");
  } catch (error) {
    isResetPending = false;

    markDisconnected();
    socket.close();
  }
});

// Draw a scrolling graph and discard samples outside its time window.
function drawChart(canvas, history, duration, isHeartChart) {
  const now = performance.now();

  while (history.length && history[0].time < now - duration) history.shift();

  const width = canvas.clientWidth;
  const height = canvas.clientHeight;

  // Scale the drawing buffer for sharper rendering on high-density screens.
  const ratio = Math.min(window.devicePixelRatio || 1, 2);

  if (canvas.width !== Math.round(width * ratio) || canvas.height !== Math.round(height * ratio)) {
    canvas.width = Math.round(width * ratio);
    canvas.height = Math.round(height * ratio);
  }

  const context = canvas.getContext("2d");

  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  context.clearRect(0, 0, width, height);

  const left = 35;
  const right = width - 7;
  const top = 12;
  const bottom = height - 28;

  const values = history.filter((point) => point.value !== null).map((point) => point.value);

  let min;
  let max;

  // Fit the vertical scale to the readings, keeping acceleration centered on zero.
  if (isHeartChart) {
    min = values.length ? Math.max(0, Math.floor((Math.min(...values) - 10) / 20) * 20) : 40;
    max = values.length ? Math.max(min + 40, Math.ceil((Math.max(...values) + 10) / 20) * 20) : 140;
  } else {
    max = Math.max(4, Math.ceil(Math.max(0, ...values.map(Math.abs))));
    min = -max;
  }

  // Convert timestamps and measurement values into canvas coordinates.
  const x = (time) => right - ((now - time) / duration) * (right - left);
  const y = (value) => bottom - ((value - min) / (max - min)) * (bottom - top);

  context.font = "10px Arial";
  context.lineWidth = 1;

  // Draw grid lines and axis labels.
  for (let tick = 0; tick <= 4; tick++) {
    const value = min + ((max - min) * tick) / 4;

    context.strokeStyle = "#303d36";
    context.beginPath();
    context.moveTo(left, y(value));
    context.lineTo(right, y(value));
    context.stroke();

    context.fillStyle = "#98a69b";
    context.textAlign = "right";
    context.fillText(Number(value.toFixed(1)), left - 8, y(value) + 3);

    context.textAlign = "center";
    context.fillText(
      tick === 4 ? "0" : String(Math.round((duration / 1000) * (1 - tick / 4))),
      left + ((right - left) * tick) / 4,
      height - 7,
    );
  }

  // Clip graph lines to the plot area so they cannot overlap the labels.
  context.save();
  context.beginPath();
  context.rect(left, top, right - left, bottom - top);
  context.clip();

  if (!isHeartChart) {
    context.setLineDash([4, 5]);
    context.strokeStyle = "#9da88b";

    for (const threshold of [-swayThreshold, swayThreshold]) {
      context.beginPath();
      context.moveTo(left, y(threshold));
      context.lineTo(right, y(threshold));
      context.stroke();
    }

    context.setLineDash([]);
  }

  context.strokeStyle = isHeartChart ? "#f5a38e" : "#83cfb4";
  context.lineWidth = 2;
  context.lineJoin = "round";
  context.beginPath();

  let previous = null;

  // Break the line for unavailable readings or sample gaps of at least 1.2 seconds.
  for (const point of history) {
    if (point.value === null) {
      previous = null;
      continue;
    }

    if (previous && point.time - previous.time < 1200) {
      context.lineTo(x(point.time), y(point.value));
    } else {
      context.moveTo(x(point.time), y(point.value));
    }

    previous = point;
  }

  context.stroke();
  context.restore();

  if (!values.length) {
    context.textAlign = "center";
    context.fillStyle = "#a2afa4";
    context.fillText("Waiting for readings", (left + right) / 2, (top + bottom) / 2);
  }
}

function redraw() {
  const now = performance.now();

  const hasConnectTimeout = socket && socket.readyState === WebSocket.CONNECTING && now - connectionStartedAt > 10000;
  const hasDataTimeout = socket && socket.readyState === WebSocket.OPEN && now - lastMessageAt > 10000;

  // Reconnect if connection establishment or incoming data stalls for ten seconds.
  if (hasConnectTimeout || hasDataTimeout) {
    const oldSocket = socket;

    socket = null;
    isResetPending = false;

    markDisconnected();
    oldSocket.close();
    scheduleReconnect();
  }

  drawChart(byId("accelChart"), accelerationHistory, 30000, false);
  drawChart(byId("heartChart"), heartHistory, 120000, true);
}

setInterval(redraw, 200);
window.addEventListener("resize", redraw);

redraw();
connectWebSocket();