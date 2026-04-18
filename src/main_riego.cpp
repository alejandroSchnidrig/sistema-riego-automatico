#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DS1302.h>

/*
  ESP32 - Sistema de Riego Automático
  - Sirve el HTML subido por el usuario
  - GPIO18 = RTC CLK
  - GPIO19 = RTC DAT
  - GPIO21 = RTC RST
  - Sectores 1-8 en GPIO 13, 14, 16, 17, 32, 33, 25, 26
  - Estado por Serial cada 20 segundos
  - Backend compatible con:
      GET  /estado
      GET  /programas
      POST /configuracion
      POST /parada

  Pensado para PlatformIO:
  - guardar como src/main_riego.cpp
*/

WebServer server(80);

static const char* AP_SSID = "Riego-ESP32";
static const char* AP_PASS = "riego12345";

// RTC Configuration
static const uint8_t RTC_CLK_PIN = 18;
static const uint8_t RTC_DAT_PIN = 19;
static const uint8_t RTC_RST_PIN = 21;
DS1302 rtc(RTC_RST_PIN, RTC_DAT_PIN, RTC_CLK_PIN);

// Sector Pin Configuration (GPIO pins for sectors 1-8)
static const uint8_t SECTOR_PINS[8] = {13, 14, 16, 17, 32, 33, 25, 26};

// Pump relay pin
static const uint8_t PUMP_PIN = 27;

static const uint8_t NUM_SECTORS = 8;
static const uint8_t MAX_PROGRAMS = 10;
static const unsigned long STATUS_PRINT_INTERVAL_MS = 20000UL;

enum SystemState {
  STATE_IDLE,
  STATE_RUNNING,
  STATE_MANUAL_STOP
};

struct SectorStep {
  uint8_t id;
  uint8_t orden;
  uint32_t tiempoRiego;
};

struct Program {
  bool valid;
  uint16_t id;
  char horaInicio[6];
  uint8_t dias;
  uint16_t retardoEntreSectores;
  bool ciclico;
  uint8_t sectorCount;
  SectorStep sectores[NUM_SECTORS];
};

Program programs[MAX_PROGRAMS];
uint16_t nextProgramId = 3;

SystemState systemState = STATE_IDLE;
uint16_t activeProgramId = 0;
uint8_t activeSectorId = 0;
uint32_t remainingTimeSec = 0;
bool pumpOn = false;

// Manual control override
uint16_t manualSectorMask = 0;  // bit0=sector1 ... bit7=sector8

int runningProgramIndex = -1;
int runningStepIndex = -1;
bool waitingBetweenSectors = false;
unsigned long stepStartMs = 0;
unsigned long delayStartMs = 0;
unsigned long lastStatusPrint = 0;
uint16_t lastScheduleYear = 0;
uint8_t lastScheduleMonth = 0;
uint8_t lastScheduleDay = 0;
uint8_t lastScheduleHour = 255;
uint8_t lastScheduleMinute = 255;

const char INDEX_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Sistema de Riego Automático</title>
  <style>
    :root {
      --bg: #f1f5f4;
      --panel: #ffffff;
      --panel-border: #d9e2e0;
      --text: #1b2a27;
      --text-soft: #5a6b67;
      --primary: #2e8b57;
      --primary-dark: #236b42;
      --primary-soft: #e6f2ec;
      --accent: #1d70a2;
      --danger: #c0392b;
      --danger-dark: #962d22;
      --warning: #d98b1f;
      --idle: #6c7a78;
      --running: #2e8b57;
      --stop: #c0392b;
      --shadow: 0 2px 8px rgba(0, 0, 0, 0.06);
      --radius: 10px;
    }

    * { box-sizing: border-box; }

    html, body {
      margin: 0;
      padding: 0;
      background: var(--bg);
      color: var(--text);
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI",
                   Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
      font-size: 15px;
      line-height: 1.4;
    }

    header {
      background: var(--primary);
      color: #fff;
      padding: 1rem 1.25rem;
      display: flex;
      align-items: center;
      justify-content: space-between;
      flex-wrap: wrap;
      gap: 0.75rem;
    }

    header h1 {
      margin: 0;
      font-size: 1.15rem;
      font-weight: 600;
      letter-spacing: 0.2px;
    }

    .header-time {
      display: flex;
      align-items: center;
      gap: 0.5rem;
      background: rgba(255, 255, 255, 0.15);
      padding: 0.35rem 0.75rem;
      border-radius: 8px;
      font-size: 0.95rem;
      font-weight: 500;
      cursor: pointer;
      transition: background 0.15s;
    }

    .header-time:hover {
      background: rgba(255, 255, 255, 0.25);
    }

    .header-time .time-value {
      font-weight: 600;
      font-family: 'Courier New', monospace;
    }

    .header-time .pencil {
      font-size: 0.9rem;
      opacity: 0.7;
      transition: opacity 0.15s;
    }

    .header-time:hover .pencil {
      opacity: 1;
    }

    .status-badge {
      display: inline-flex;
      align-items: center;
      gap: 0.5rem;
      background: rgba(255, 255, 255, 0.15);
      padding: 0.35rem 0.75rem;
      border-radius: 999px;
      font-size: 0.85rem;
      font-weight: 600;
    }

    .status-dot {
      width: 10px;
      height: 10px;
      border-radius: 50%;
      background: #aaa;
    }

    .status-dot.idle    { background: #cfd6d4; }
    .status-dot.running { background: #7ee2a8; animation: pulse 1.4s ease-in-out infinite; }
    .status-dot.stop    { background: #ff8070; }

    @keyframes pulse {
      0%, 100% { opacity: 1;   transform: scale(1); }
      50%      { opacity: 0.5; transform: scale(1.3); }
    }

    main {
      max-width: 1100px;
      margin: 0 auto;
      padding: 1.25rem;
      display: grid;
      gap: 1.25rem;
    }

    .card {
      background: var(--panel);
      border: 1px solid var(--panel-border);
      border-radius: var(--radius);
      box-shadow: var(--shadow);
      padding: 1.25rem;
    }

    .card h2 {
      margin: 0 0 1rem 0;
      font-size: 1rem;
      font-weight: 600;
      color: var(--text);
      text-transform: uppercase;
      letter-spacing: 0.5px;
    }

    /* --- Estado en tiempo real ----------------------------------------- */

    .status-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
      gap: 0.75rem;
    }

    .status-cell {
      background: var(--primary-soft);
      border-radius: 8px;
      padding: 0.75rem;
    }

    .status-cell .label {
      font-size: 0.7rem;
      text-transform: uppercase;
      color: var(--text-soft);
      letter-spacing: 0.5px;
      margin-bottom: 0.25rem;
    }

    .status-cell .value {
      font-size: 1.35rem;
      font-weight: 600;
      color: var(--text);
    }

    .status-cell .value.big {
      font-size: 1.6rem;
      color: var(--primary-dark);
    }

    .pump-indicator {
      display: inline-flex;
      align-items: center;
      gap: 0.5rem;
      font-weight: 600;
      cursor: default;
      padding: 0.25rem 0.5rem;
      border-radius: 6px;
      transition: background 0.2s ease;
      user-select: none;
    }

    .pump-indicator:hover {
      background: transparent;
    }

    .pump-indicator:active .dot {
      transform: none;
    }

    .pump-indicator .dot {
      width: 12px;
      height: 12px;
      border-radius: 50%;
      background: #ccc;
      transition: transform 0.15s;
    }

    .pump-indicator.on .dot { background: var(--primary); box-shadow: 0 0 6px var(--primary); }

    /* --- Sectores ------------------------------------------------------ */

    .sectors-grid {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(110px, 1fr));
      gap: 0.65rem;
    }

    .sector {
      background: #f8faf9;
      border: 1px solid var(--panel-border);
      border-radius: 8px;
      padding: 0.65rem;
      text-align: center;
      transition: all 0.2s ease;
      cursor: pointer;
      user-select: none;
    }

    .sector:hover {
      border-color: var(--primary);
      background: #f0f5f3;
    }

    .sector .name {
      font-size: 0.75rem;
      font-weight: 600;
      color: var(--text-soft);
      text-transform: uppercase;
    }

    .sector .icon {
      font-size: 1.8rem;
      margin: 0.25rem 0;
      color: #c8d2d0;
    }

    .sector.active {
      background: var(--primary);
      border-color: var(--primary-dark);
      color: #fff;
      box-shadow: 0 0 8px rgba(46, 139, 87, 0.4);
    }

    .sector .icon {
      transition: transform 0.15s;
    }

    .sector:active .icon {
      transform: scale(1.15);
    }

    .sector.active .name { color: #e8f5ed; }
    .sector.active .icon { color: #fff; }

    /* --- Botones ------------------------------------------------------- */

    .btn {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      gap: 0.4rem;
      padding: 0.65rem 1.1rem;
      border-radius: 8px;
      border: 1px solid transparent;
      background: var(--primary);
      color: #fff;
      font-size: 0.9rem;
      font-weight: 600;
      cursor: pointer;
      text-decoration: none;
      transition: background 0.15s, transform 0.05s;
    }
    .btn:hover   { background: var(--primary-dark); }
    .btn:active  { transform: translateY(1px); }

    .btn.secondary {
      background: #fff;
      color: var(--text);
      border-color: var(--panel-border);
    }
    .btn.secondary:hover { background: #f4f6f5; }

    .btn.danger       { background: var(--danger); }
    .btn.danger:hover { background: var(--danger-dark); }

    .btn.small {
      padding: 0.4rem 0.7rem;
      font-size: 0.8rem;
    }

    .btn-stop {
      width: 100%;
      padding: 1rem;
      font-size: 1.05rem;
      letter-spacing: 0.5px;
      text-transform: uppercase;
    }

    /* --- Programas ----------------------------------------------------- */

    .programs-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      flex-wrap: wrap;
      gap: 0.5rem;
      margin-bottom: 0.75rem;
    }

    .programs-list {
      display: grid;
      gap: 0.75rem;
    }

    .program-item {
      border: 1px solid var(--panel-border);
      border-radius: 8px;
      padding: 0.9rem;
      background: #fcfdfd;
    }

    .program-item.active {
      border-color: var(--primary);
      background: var(--primary-soft);
    }

    .program-item-header {
      display: flex;
      justify-content: space-between;
      align-items: flex-start;
      flex-wrap: wrap;
      gap: 0.5rem;
    }

    .program-title {
      font-weight: 600;
      font-size: 1rem;
    }

    .program-meta {
      font-size: 0.8rem;
      color: var(--text-soft);
      margin-top: 0.25rem;
      display: flex;
      flex-wrap: wrap;
      gap: 0.75rem;
    }

    .program-meta span::before {
      content: "• ";
      color: var(--primary);
    }

    .program-actions {
      display: flex;
      gap: 0.4rem;
      flex-wrap: wrap;
    }

    .program-sectors {
      margin-top: 0.65rem;
      display: flex;
      flex-wrap: wrap;
      gap: 0.3rem;
    }

    .chip {
      background: #fff;
      border: 1px solid var(--panel-border);
      border-radius: 999px;
      padding: 0.2rem 0.6rem;
      font-size: 0.75rem;
      color: var(--text-soft);
    }

    .program-item.active .chip { background: #fff; }

    /* --- Formulario de programa --------------------------------------- */

    .form-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
      gap: 0.85rem;
    }

    .form-field {
      display: flex;
      flex-direction: column;
      gap: 0.3rem;
    }

    .form-field label {
      font-size: 0.75rem;
      text-transform: uppercase;
      color: var(--text-soft);
      font-weight: 600;
      letter-spacing: 0.3px;
    }

    .form-field input[type="text"],
    .form-field input[type="time"],
    .form-field input[type="number"],
    .form-field select {
      border: 1px solid var(--panel-border);
      border-radius: 6px;
      padding: 0.55rem 0.7rem;
      font-size: 0.9rem;
      background: #fff;
      color: var(--text);
      font-family: inherit;
    }

    .form-field input:focus,
    .form-field select:focus {
      outline: none;
      border-color: var(--primary);
      box-shadow: 0 0 0 2px var(--primary-soft);
    }

    .days-picker {
      display: flex;
      gap: 0.3rem;
      flex-wrap: wrap;
    }

    .day-toggle {
      width: 34px;
      height: 34px;
      border-radius: 50%;
      border: 1px solid var(--panel-border);
      background: #fff;
      color: var(--text-soft);
      font-size: 0.75rem;
      font-weight: 600;
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      transition: all 0.15s;
    }

    .day-toggle.on {
      background: var(--primary);
      color: #fff;
      border-color: var(--primary);
    }

    .sector-config-list {
      display: grid;
      gap: 0.5rem;
    }

    .sector-config-row {
      display: grid;
      grid-template-columns: auto 1fr auto auto;
      align-items: center;
      gap: 0.6rem;
      padding: 0.55rem 0.7rem;
      background: #f8faf9;
      border: 1px solid var(--panel-border);
      border-radius: 6px;
    }

    .sector-config-row input[type="checkbox"] {
      width: 18px;
      height: 18px;
      accent-color: var(--primary);
    }

    .sector-config-row .sector-name {
      font-weight: 600;
      font-size: 0.9rem;
    }

    .sector-config-row input[type="number"] {
      width: 72px;
      border: 1px solid var(--panel-border);
      border-radius: 6px;
      padding: 0.35rem 0.5rem;
      font-size: 0.85rem;
      text-align: right;
    }

    .sector-config-row .unit {
      font-size: 0.75rem;
      color: var(--text-soft);
    }

    /* --- Time Editor Modal ------------------------------------------------- */
    .modal {
      display: none;
      position: fixed;
      top: 0;
      left: 0;
      right: 0;
      bottom: 0;
      background: rgba(0, 0, 0, 0.5);
      z-index: 1000;
      align-items: center;
      justify-content: center;
    }

    .modal.show {
      display: flex;
    }

    .modal-content {
      background: var(--panel);
      border-radius: var(--radius);
      padding: 1.5rem;
      width: 90%;
      max-width: 450px;
      box-shadow: 0 10px 40px rgba(0, 0, 0, 0.2);
    }

    .modal-header {
      font-size: 1.35rem;
      font-weight: 600;
      color: var(--text);
      margin-bottom: 1.5rem;
      text-align: center;
    }

    .modal-body {
      display: flex;
      flex-direction: column;
      gap: 1rem;
      margin-bottom: 1.5rem;
    }

    .datetime-row {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 1rem;
    }

    .modal-body .form-field {
      margin: 0;
    }

    .modal-body input[type="date"],
    .modal-body input[type="time"] {
      border: 1px solid var(--panel-border);
      border-radius: 6px;
      padding: 0.75rem;
      font-size: 1rem;
      background: #fff;
      color: var(--text);
      font-family: inherit;
      width: 100%;
    }

    .modal-body input[type="date"]:focus,
    .modal-body input[type="time"]:focus {
      outline: none;
      border-color: var(--primary);
      box-shadow: 0 0 0 2px var(--primary-soft);
    }

    .modal-actions {
      display: flex;
      gap: 0.75rem;
      justify-content: flex-end;
    }

    .modal-actions .btn {
      padding: 0.65rem 1.2rem;
      font-size: 0.9rem;
      min-width: 100px;
    }

    .form-actions {
      display: flex;
      gap: 0.5rem;
      margin-top: 1rem;
      flex-wrap: wrap;
      justify-content: flex-end;
    }

    /* --- Toast --------------------------------------------------------- */

    #toast {
      position: fixed;
      bottom: 1rem;
      left: 50%;
      transform: translateX(-50%) translateY(120%);
      background: var(--text);
      color: #fff;
      padding: 0.75rem 1.25rem;
      border-radius: 8px;
      font-size: 0.9rem;
      box-shadow: 0 4px 16px rgba(0, 0, 0, 0.2);
      transition: transform 0.3s ease;
      z-index: 1000;
      max-width: 90%;
    }
    #toast.show { transform: translateX(-50%) translateY(0); }
    #toast.error { background: var(--danger); }

    /* --- Sección colapsable ------------------------------------------- */

    .collapsible-header {
      cursor: pointer;
      user-select: none;
      display: flex;
      align-items: center;
      justify-content: space-between;
    }

    .collapsible-header::after {
      content: "▾";
      color: var(--text-soft);
      transition: transform 0.2s;
    }

    .card.collapsed .collapsible-header::after {
      transform: rotate(-90deg);
    }

    .card.collapsed .collapsible-body {
      display: none;
    }

    .muted {
      color: var(--text-soft);
      font-size: 0.85rem;
      font-style: italic;
    }

    /* --- Mobile -------------------------------------------------------- */

    @media (max-width: 540px) {
      header h1 { font-size: 1rem; }
      main { padding: 0.9rem; gap: 0.9rem; }
      .card { padding: 1rem; }
      .sector-config-row {
        grid-template-columns: auto 1fr;
        grid-template-areas:
          "check name"
          "time  time";
      }
      .sector-config-row input[type="checkbox"] { grid-area: check; }
      .sector-config-row .sector-name           { grid-area: name; }
      .sector-config-row .time-wrapper {
        grid-area: time;
        display: flex;
        align-items: center;
        gap: 0.5rem;
        justify-content: flex-end;
      }
    }
  </style>
</head>
<body>

  <header>
    <h1>Sistema de Riego Automático</h1>
    <div class="status-badge">
      <span id="statusDot" class="status-dot idle"></span>
      <span id="statusLabel">Inactivo</span>
    </div>
    <div id="headerTime" class="header-time" title="Click para editar la hora">
      <span class="time-value" id="timeValue">--:--</span>
      <span class="pencil">✎</span>
    </div>
  </header>

  <main>

    <!-- ============ Estado en tiempo real ============ -->
    <section class="card">
      <h2>Estado actual</h2>
      <div class="status-grid">
        <div class="status-cell">
          <div class="label">Programa activo</div>
          <div class="value" id="activeProgram">—</div>
        </div>
        <div class="status-cell">
          <div class="label">Sectores activos</div>
          <div class="value" id="activeSector">—</div>
        </div>
        <div class="status-cell">
          <div class="label">Tiempo restante</div>
          <div class="value big" id="remainingTime">00:00</div>
        </div>
        <div class="status-cell">
          <div class="label">Bomba</div>
          <div class="value">
            <span id="pumpIndicator" class="pump-indicator" title="La bomba sigue automaticamente a los sectores activos">
              <span class="dot"></span><span id="pumpLabel">Apagada</span>
            </span>
          </div>
        </div>
      </div>

      <div style="margin-top: 1rem;">
        <div class="label" style="font-size: 0.7rem; text-transform: uppercase; color: var(--text-soft); margin-bottom: 0.5rem;">
          Sectores
        </div>
        <div id="sectorsGrid" class="sectors-grid"></div>
      </div>
    </section>

    <!-- ============ Parada manual ============ -->
    <section class="card">
      <button id="btnStop" class="btn danger btn-stop">⏹ Parada manual inmediata</button>
    </section>

    <!-- ============ Programas guardados ============ -->
    <section class="card">
      <div class="programs-header">
        <h2 style="margin: 0;">Programas guardados</h2>
        <button id="btnNewProgram" class="btn small">＋ Nuevo programa</button>
      </div>
      <div id="programsList" class="programs-list">
        <p class="muted">Cargando programas…</p>
      </div>
    </section>

    <!-- ============ Editor de programa ============ -->
    <section id="editorCard" class="card collapsed">
      <h2 class="collapsible-header" id="editorToggle">
        <span id="editorTitle">Nuevo programa</span>
      </h2>
      <div class="collapsible-body">
        <form id="programForm" onsubmit="return false;">
          <input type="hidden" id="programId" value="">

          <div class="form-grid">
            <div class="form-field">
              <label for="startTime">Hora de inicio</label>
              <input type="time" id="startTime" value="07:00" required>
            </div>

            <div class="form-field">
              <label for="delayBetween">Retardo entre sectores (s)</label>
              <input type="number" id="delayBetween" min="0" max="3600" value="5">
            </div>

            <div class="form-field">
              <label for="cyclic">Repetición</label>
              <select id="cyclic">
                <option value="false">Ejecución única</option>
                <option value="true">Cíclica</option>
              </select>
            </div>
          </div>

          <div class="form-field" style="margin-top: 1rem;">
            <label>Días de ejecución</label>
            <div class="days-picker" id="daysPicker">
              <button type="button" class="day-toggle" data-bit="0">L</button>
              <button type="button" class="day-toggle" data-bit="1">M</button>
              <button type="button" class="day-toggle" data-bit="2">X</button>
              <button type="button" class="day-toggle" data-bit="3">J</button>
              <button type="button" class="day-toggle" data-bit="4">V</button>
              <button type="button" class="day-toggle" data-bit="5">S</button>
              <button type="button" class="day-toggle" data-bit="6">D</button>
            </div>
          </div>

          <div class="form-field" style="margin-top: 1rem;">
            <label>Sectores incluidos y tiempo de riego</label>
            <div id="sectorConfigList" class="sector-config-list"></div>
          </div>

          <div class="form-actions">
            <button type="button" id="btnCancel"   class="btn secondary">Cancelar</button>
            <button type="button" id="btnSaveProg" class="btn">Guardar programa</button>
          </div>
        </form>
      </div>
    </section>

    <!-- ============ Time Editor Modal ============ -->
    <div id="timeEditorModal" class="modal">
      <div class="modal-content">
        <div class="modal-header">🗓️ Establecer fecha y hora</div>
        <div class="modal-body">
          <div class="datetime-row">
            <div class="form-field">
              <label for="timeEditorDate" style="font-size: 0.75rem; text-transform: uppercase; color: var(--text-soft);">Fecha</label>
              <input type="date" id="timeEditorDate" value="2024-01-01">
            </div>
            <div class="form-field">
              <label for="timeEditorTime" style="font-size: 0.75rem; text-transform: uppercase; color: var(--text-soft);">Hora</label>
              <input type="time" id="timeEditorTime" step="1" value="00:00:00">
            </div>
          </div>
        </div>
        <div class="modal-actions">
          <button type="button" id="btnTimeCancel" class="btn secondary">✕ Cancelar</button>
          <button type="button" id="btnTimeSave" class="btn">✓ Guardar</button>
        </div>
      </div>
    </div>

  </main>

  <div id="toast"></div>

  <script>
    // ==================================================================
    // Sistema de Riego Automático — Panel de control
    // ------------------------------------------------------------------
    // Este archivo es 100% autónomo: no depende de librerías externas.
    // Cuando la API del ESP32 no responde (por ejemplo, al abrir el
    // index.html directamente en un navegador para validar el diseño),
    // el panel entra en "modo demo" y usa datos ficticios para que el
    // usuario pueda probar la interfaz completa.
    // ==================================================================

    const NUM_SECTORS      = 8;
    const POLL_INTERVAL_MS = 2000;

    // --- Estado de la UI ---------------------------------------------
    let demoMode     = false;
    let programs     = [];
    let editingId    = null;
    let selectedDays = 0; // bitmask: bit0=L … bit6=D

    // --- Datos simulados para modo demo ------------------------------
    const demoState = {
      estado: "RUNNING",
      programaActivo: 1,
      sectorActivo: 3,
      sectoresActivos: [3],
      tiempoRestante: 95,
      bomba: true,
      modoManual: false,
      manualSectorMask: 0
    };

    const demoPrograms = [
      {
        id: 1,
        horaInicio: "07:00",
        dias: 0b0111110,           // L-V
        retardoEntreSectores: 5,
        ciclico: true,
        sectores: [
          { id: 1, orden: 1, tiempoRiego: 60  },
          { id: 2, orden: 2, tiempoRiego: 90  },
          { id: 3, orden: 3, tiempoRiego: 120 },
          { id: 5, orden: 4, tiempoRiego: 45  }
        ]
      },
      {
        id: 2,
        horaInicio: "19:30",
        dias: 0b1100000,           // S-D
        retardoEntreSectores: 10,
        ciclico: false,
        sectores: [
          { id: 4, orden: 1, tiempoRiego: 180 },
          { id: 6, orden: 2, tiempoRiego: 180 },
          { id: 8, orden: 3, tiempoRiego: 90  }
        ]
      }
    ];

    // --- Helpers ------------------------------------------------------
    const $  = (sel) => document.querySelector(sel);
    const $$ = (sel) => document.querySelectorAll(sel);

    function toast(msg, isError = false) {
      const t = $("#toast");
      t.textContent = msg;
      t.classList.toggle("error", isError);
      t.classList.add("show");
      clearTimeout(toast._t);
      toast._t = setTimeout(() => t.classList.remove("show"), 2800);
    }

    function formatTime(seconds) {
      if (seconds == null || seconds < 0) return "00:00";
      const m = Math.floor(seconds / 60);
      const s = seconds % 60;
      return `${String(m).padStart(2, "0")}:${String(s).padStart(2, "0")}`;
    }

    function formatDateTime(rtcData) {
      if (!rtcData) return "--:-- --/--/--";
      const d = String(rtcData.day).padStart(2, "0");
      const m = String(rtcData.month).padStart(2, "0");
      const y = String(rtcData.year).slice(-2);
      const h = String(rtcData.hour).padStart(2, "0");
      const min = String(rtcData.minute).padStart(2, "0");
      return `${h}:${min} ${d}/${m}/${y}`;
    }

    function normalizeActiveSectors(data) {
      if (Array.isArray(data.sectoresActivos)) {
        return data.sectoresActivos
          .map(Number)
          .filter((id) => id >= 1 && id <= NUM_SECTORS);
      }

      const sectors = [];
      if (data.sectorActivo) sectors.push(Number(data.sectorActivo));
      if (data.manualSectorId && !sectors.includes(Number(data.manualSectorId))) {
        sectors.push(Number(data.manualSectorId));
      }
      return sectors;
    }

    function isManualSectorActive(sectorId) {
      return (manualSectorMask & (1 << (sectorId - 1))) !== 0;
    }

    async function fetchRTCTime() {
      const data = await api("GET", "/rtc", null);
      if (data) {
        updateTimeDisplay(data);
        return data;
      }
      return null;
    }

    function updateTimeDisplay(rtcData) {
      if (!rtcData) return;
      const timeStr = formatDateTime(rtcData);
      const timeElement = $("#timeValue");
      if (timeElement) {
        timeElement.textContent = timeStr;
      }
    }

    function openTimeEditor(rtcData) {
      if (!rtcData) return;
      const dateStr = String(rtcData.year).padStart(4, '0') + '-' + 
                      String(rtcData.month).padStart(2, '0') + '-' + 
                      String(rtcData.day).padStart(2, '0');
      const timeStr = String(rtcData.hour).padStart(2, '0') + ':' + 
                      String(rtcData.minute).padStart(2, '0') + ':' + 
                      String(rtcData.second).padStart(2, '0');
      $("#timeEditorDate").value = dateStr;
      $("#timeEditorTime").value = timeStr;
      $("#timeEditorModal").classList.add("show");
    }

    function closeTimeEditor() {
      $("#timeEditorModal").classList.remove("show");
    }

    async function saveRTCTime() {
      const dateVal = $("#timeEditorDate").value;
      const timeVal = $("#timeEditorTime").value;
      
      if (!dateVal || !timeVal) {
        toast("Por favor selecciona fecha y hora", true);
        return;
      }
      
      const [year, month, day] = dateVal.split('-').map(Number);
      const [hour, minute, second] = timeVal.split(':').map(Number);

      const timeData = `year=${year}&month=${month}&day=${day}&hour=${hour}&minute=${minute}&second=${second}`;

      try {
        const res = await fetch("/rtc?" + timeData, { method: "POST" });
        const data = await res.json();
        if (data && data.ok) {
          updateTimeDisplay(data.rtc);
          toast("✓ Hora configurada correctamente");
          closeTimeEditor();
        } else {
          toast("Error al configurar la hora", true);
        }
      } catch (err) {
        toast("Error: " + err.message, true);
      }
    }

    function daysMaskToLabel(mask) {
      const labels = ["L", "M", "X", "J", "V", "S", "D"];
      const out = [];
      for (let i = 0; i < 7; i++) {
        if (mask & (1 << i)) out.push(labels[i]);
      }
      return out.length ? out.join(" ") : "Sin días";
    }

    async function api(method, path, body) {
      if (demoMode) return demoApi(method, path, body);
      try {
        const opts = { method, headers: { "Content-Type": "application/json" } };
        if (body) opts.body = JSON.stringify(body);
        const res = await fetch(path, opts);
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        return await res.json();
      } catch (err) {
        // Primera falla → entramos en modo demo y avisamos
        if (!demoMode) {
          demoMode = true;
          toast("Modo demo activo (ESP32 no disponible)");
        }
        return demoApi(method, path, body);
      }
    }

    // Simula las respuestas del ESP32 para poder validar la UI sin
    // tener el firmware funcionando.
    function demoApi(method, path, body) {
      if (path === "/estado" && method === "GET") {
        // Simulamos que el tiempo restante va bajando
        if (demoState.estado === "RUNNING" && demoState.tiempoRestante > 0) {
          demoState.tiempoRestante -= 2;
          if (demoState.tiempoRestante <= 0) {
            demoState.sectorActivo = (demoState.sectorActivo % 8) + 1;
            demoState.sectoresActivos = [demoState.sectorActivo];
            demoState.tiempoRestante = 90;
          }
        }
        return Promise.resolve({ ...demoState });
      }
      if (path === "/programas" && method === "GET") {
        return Promise.resolve({ programas: demoPrograms });
      }
      if (path === "/configuracion" && method === "POST") {
        const prog = body.programa;
        if (prog.id) {
          const idx = demoPrograms.findIndex((p) => p.id === prog.id);
          if (idx >= 0) demoPrograms[idx] = prog;
        } else {
          prog.id = demoPrograms.length
            ? Math.max(...demoPrograms.map((p) => p.id)) + 1
            : 1;
          demoPrograms.push(prog);
        }
        return Promise.resolve({ ok: true, id: prog.id });
      }
      if (path === "/parada" && method === "POST") {
        demoState.estado = "MANUAL_STOP";
        demoState.programaActivo = 0;
        demoState.sectorActivo = 0;
        demoState.sectoresActivos = [];
        demoState.tiempoRestante = 0;
        demoState.bomba = false;
        return Promise.resolve({ ok: true });
      }
      if (path === "/rtc" && method === "GET") {
        const now = new Date();
        return Promise.resolve({
          year: now.getFullYear(),
          month: now.getMonth() + 1,
          day: now.getDate(),
          hour: now.getHours(),
          minute: now.getMinutes(),
          second: now.getSeconds(),
          dayOfWeek: now.getDay(),
          running: true
        });
      }
      if (path === "/rtc" && method === "POST") {
        return Promise.resolve({
          ok: true,
          rtc: {
            year: body.year,
            month: body.month,
            day: body.day,
            hour: body.hour,
            minute: body.minute,
            second: body.second,
            dayOfWeek: new Date(body.year, body.month - 1, body.day).getDay(),
            running: true
          }
        });
      }
      return Promise.resolve({});
    }

    // --- Render: Estado ----------------------------------------------
    function renderStatus(data) {
      const state = data.modoManual ? "MANUAL" : (data.estado || "IDLE");

      const statusMap = {
        IDLE:        { label: "Inactivo",     cls: "idle"    },
        MANUAL:      { label: "Manual",       cls: "running" },
        RUNNING:     { label: "En ejecución", cls: "running" },
        MANUAL_STOP: { label: "Detenido",     cls: "stop"    }
      };
      const s = statusMap[state] || statusMap.IDLE;
      $("#statusLabel").textContent = s.label;
      $("#statusDot").className = "status-dot " + s.cls;
      const activeSectors = normalizeActiveSectors(data);

      $("#activeProgram").textContent =
        data.programaActivo ? `#${data.programaActivo}` : "—";
      $("#activeSector").textContent =
        data.sectorActivo ? `S${data.sectorActivo}` : "—";
      $("#activeSector").textContent =
        activeSectors.length ? activeSectors.map((id) => `S${id}`).join(", ") : "—";
      $("#remainingTime").textContent = formatTime(data.tiempoRestante);

      const pumpOn = !!data.bomba;
      const pump = $("#pumpIndicator");
      pump.classList.toggle("on", pumpOn);
      $("#pumpLabel").textContent = pumpOn ? "Encendida" : "Apagada";

      // Update manual control state
      manualSectorMask = Number(data.manualSectorMask || 0);

      // Sectores: marcar el activo
      $$(".sector").forEach((el) => {
        const id = parseInt(el.dataset.id, 10);
        el.classList.toggle("active", activeSectors.includes(id));
      });
    }

    function buildSectorsGrid() {
      const grid = $("#sectorsGrid");
      grid.innerHTML = "";
      for (let i = 1; i <= NUM_SECTORS; i++) {
        const el = document.createElement("div");
        el.className = "sector";
        el.dataset.id = i;
        el.innerHTML = `
          <div class="name">Sector ${i}</div>
          <div class="icon">◉</div>
        `;
        el.addEventListener("click", () => toggleSectorManual(i));
        grid.appendChild(el);
      }
    }

    // Toggle manual control for a sector
    function toggleSectorManual(sectorId) {
      const isActive = isManualSectorActive(sectorId);
      const newState = isActive ? 0 : 1;  // toggle
      fetch(`/control?type=sector&id=${sectorId}&state=${newState}`)
        .then(r => r.json())
        .then(data => {
          if (data.ok) refreshStatus();
        })
        .catch(e => console.error(e));
    }

    // Store manual state globally
    let manualSectorMask = 0;

    // --- Render: Programas -------------------------------------------
    function renderPrograms(list, activeId) {
      const container = $("#programsList");
      if (!list.length) {
        container.innerHTML = '<p class="muted">No hay programas configurados.</p>';
        return;
      }
      container.innerHTML = "";
      list.forEach((p) => {
        const item = document.createElement("div");
        item.className = "program-item" + (p.id === activeId ? " active" : "");

        const sectorChips = p.sectores
          .slice()
          .sort((a, b) => a.orden - b.orden)
          .map(
            (s) =>
              `<span class="chip">S${s.id} · ${formatTime(s.tiempoRiego)}</span>`
          )
          .join("");

        item.innerHTML = `
          <div class="program-item-header">
            <div>
              <div class="program-title">Programa #${p.id} · ${p.horaInicio}</div>
              <div class="program-meta">
                <span>${daysMaskToLabel(p.dias)}</span>
                <span>Retardo ${p.retardoEntreSectores}s</span>
                <span>${p.ciclico ? "Cíclico" : "Ejecución única"}</span>
              </div>
            </div>
            <div class="program-actions">
              <button class="btn small secondary" data-action="edit"   data-id="${p.id}">Editar</button>
              <button class="btn small"           data-action="run"    data-id="${p.id}">Ejecutar</button>
              <button class="btn small danger"    data-action="delete" data-id="${p.id}">Borrar</button>
            </div>
          </div>
          <div class="program-sectors">${sectorChips}</div>
        `;
        container.appendChild(item);
      });
    }

    // --- Editor de programas -----------------------------------------
    function buildSectorConfigList() {
      const list = $("#sectorConfigList");
      list.innerHTML = "";
      for (let i = 1; i <= NUM_SECTORS; i++) {
        const row = document.createElement("div");
        row.className = "sector-config-row";
        row.innerHTML = `
          <input type="checkbox" id="sec_${i}_enabled" data-sector="${i}">
          <label class="sector-name" for="sec_${i}_enabled">Sector ${i}</label>
          <div class="time-wrapper">
            <input type="number" id="sec_${i}_time" min="0" max="3600" value="60" disabled>
            <span class="unit">seg</span>
          </div>
        `;
        list.appendChild(row);

        row.querySelector('input[type="checkbox"]').addEventListener("change", (e) => {
          const t = $(`#sec_${i}_time`);
          t.disabled = !e.target.checked;
        });
      }
    }

    function openEditor(program) {
      $("#editorCard").classList.remove("collapsed");
      $("#editorCard").scrollIntoView({ behavior: "smooth", block: "start" });

      if (program) {
        editingId = program.id;
        $("#editorTitle").textContent = `Editar programa #${program.id}`;
        $("#programId").value          = program.id;
        $("#startTime").value          = program.horaInicio;
        $("#delayBetween").value       = program.retardoEntreSectores;
        $("#cyclic").value             = program.ciclico ? "true" : "false";
        selectedDays                   = program.dias;
        updateDaysUI();
        // Reset sector checkboxes
        for (let i = 1; i <= NUM_SECTORS; i++) {
          $(`#sec_${i}_enabled`).checked = false;
          $(`#sec_${i}_time`).value = 60;
          $(`#sec_${i}_time`).disabled = true;
        }
        program.sectores.forEach((s) => {
          const cb = $(`#sec_${s.id}_enabled`);
          const ti = $(`#sec_${s.id}_time`);
          if (cb && ti) {
            cb.checked = true;
            ti.disabled = false;
            ti.value = s.tiempoRiego;
          }
        });
      } else {
        editingId = null;
        $("#editorTitle").textContent = "Nuevo programa";
        $("#programId").value    = "";
        $("#startTime").value    = "07:00";
        $("#delayBetween").value = 5;
        $("#cyclic").value       = "false";
        selectedDays = 0;
        updateDaysUI();
        for (let i = 1; i <= NUM_SECTORS; i++) {
          $(`#sec_${i}_enabled`).checked = false;
          $(`#sec_${i}_time`).value = 60;
          $(`#sec_${i}_time`).disabled = true;
        }
      }
    }

    function closeEditor() {
      $("#editorCard").classList.add("collapsed");
      editingId = null;
    }

    function updateDaysUI() {
      $$("#daysPicker .day-toggle").forEach((btn) => {
        const bit = parseInt(btn.dataset.bit, 10);
        btn.classList.toggle("on", (selectedDays & (1 << bit)) !== 0);
      });
    }

    function collectProgramFromForm() {
      const sectores = [];
      let orden = 1;
      for (let i = 1; i <= NUM_SECTORS; i++) {
        if ($(`#sec_${i}_enabled`).checked) {
          sectores.push({
            id: i,
            orden: orden++,
            tiempoRiego: parseInt($(`#sec_${i}_time`).value, 10) || 0
          });
        }
      }
      return {
        id: editingId || null,
        horaInicio:           $("#startTime").value,
        dias:                 selectedDays,
        retardoEntreSectores: parseInt($("#delayBetween").value, 10) || 0,
        ciclico:              $("#cyclic").value === "true",
        sectores
      };
    }

    async function saveProgram() {
      const prog = collectProgramFromForm();
      if (!prog.horaInicio) {
        toast("Indicá una hora de inicio", true);
        return;
      }
      if (!prog.dias) {
        toast("Elegí al menos un día", true);
        return;
      }
      if (!prog.sectores.length) {
        toast("Agregá al menos un sector", true);
        return;
      }
      const res = await api("POST", "/configuracion", { programa: prog });
      if (res && res.ok) {
        toast("Programa guardado");
        closeEditor();
        loadPrograms();
      } else {
        toast("No se pudo guardar", true);
      }
    }

    async function loadPrograms() {
      const res = await api("GET", "/programas");
      programs = (res && res.programas) || [];
      const st = await api("GET", "/estado");
      renderPrograms(programs, st.programaActivo || 0);
    }

    async function runProgram(id) {
      const res = await api("POST", "/configuracion", { ejecutar: id });
      if (res && res.ok !== false) {
        toast(`Ejecutando programa #${id}`);
        // En modo demo, forzamos el estado
        if (demoMode) {
          demoState.estado = "RUNNING";
          demoState.programaActivo = id;
          const p = demoPrograms.find((x) => x.id === id);
          if (p && p.sectores.length) {
            demoState.sectorActivo = p.sectores[0].id;
            demoState.sectoresActivos = [p.sectores[0].id];
            demoState.tiempoRestante = p.sectores[0].tiempoRiego;
            demoState.bomba = true;
          }
        }
      } else {
        toast("No se pudo ejecutar", true);
      }
    }

    async function deleteProgram(id) {
      if (!confirm(`¿Borrar el programa #${id}?`)) return;
      if (demoMode) {
        const idx = demoPrograms.findIndex((p) => p.id === id);
        if (idx >= 0) demoPrograms.splice(idx, 1);
      } else {
        await api("POST", "/configuracion", { borrar: id });
      }
      toast("Programa borrado");
      loadPrograms();
    }

    async function manualStop() {
      if (!confirm("¿Confirmás la parada manual inmediata?")) return;
      const res = await api("POST", "/parada");
      if (res && res.ok !== false) toast("Sistema detenido");
      else toast("No se pudo detener", true);
      refreshStatus();
    }

    // --- Polling ------------------------------------------------------
    async function refreshStatus() {
      const data = await api("GET", "/estado");
      if (data) renderStatus(data);
    }

    // --- Bootstrap ----------------------------------------------------
    function wireEvents() {
      // Days picker
      $$("#daysPicker .day-toggle").forEach((btn) => {
        btn.addEventListener("click", () => {
          const bit = parseInt(btn.dataset.bit, 10);
          selectedDays ^= (1 << bit);
          updateDaysUI();
        });
      });

      // Botones del formulario
      $("#btnSaveProg").addEventListener("click", saveProgram);
      $("#btnCancel").addEventListener("click", closeEditor);

      // Nuevo programa
      $("#btnNewProgram").addEventListener("click", () => openEditor(null));

      // Toggle editor
      $("#editorToggle").addEventListener("click", () => {
        $("#editorCard").classList.toggle("collapsed");
      });

      // Parada manual
      $("#btnStop").addEventListener("click", manualStop);

      // Delegación de eventos en la lista de programas
      $("#programsList").addEventListener("click", (e) => {
        const btn = e.target.closest("button[data-action]");
        if (!btn) return;
        const id = parseInt(btn.dataset.id, 10);
        const action = btn.dataset.action;
        if (action === "edit") {
          const prog = programs.find((p) => p.id === id);
          if (prog) openEditor(prog);
        } else if (action === "run") {
          runProgram(id);
        } else if (action === "delete") {
          deleteProgram(id);
        }
      });

      // Time editor events
      $("#headerTime").addEventListener("click", async () => {
        const rtcData = await fetchRTCTime();
        openTimeEditor(rtcData);
      });

      $("#btnTimeCancel").addEventListener("click", closeTimeEditor);
      $("#btnTimeSave").addEventListener("click", saveRTCTime);

      // Close modal when clicking outside
      $("#timeEditorModal").addEventListener("click", (e) => {
        if (e.target === $("#timeEditorModal")) {
          closeTimeEditor();
        }
      });

    }

    function init() {
      buildSectorsGrid();
      buildSectorConfigList();
      wireEvents();
      loadPrograms();
      refreshStatus();
      fetchRTCTime();
      setInterval(refreshStatus, POLL_INTERVAL_MS);
      setInterval(fetchRTCTime, POLL_INTERVAL_MS);
    }

    document.addEventListener("DOMContentLoaded", init);
  </script>
</body>
</html>

)=====";

// Controls pump relay output
void setPump(bool on) {
  pumpOn = on;
  digitalWrite(PUMP_PIN, on ? HIGH : LOW);
}

uint16_t sectorIdToMask(uint8_t sectorId) {
  if (sectorId < 1 || sectorId > NUM_SECTORS) {
    return 0;
  }

  return (uint16_t)1U << (sectorId - 1);
}

uint8_t firstSectorFromMask(uint16_t sectorMask) {
  for (uint8_t i = 1; i <= NUM_SECTORS; i++) {
    if ((sectorMask & sectorIdToMask(i)) != 0) {
      return i;
    }
  }

  return 0;
}

uint16_t getOutputSectorMask() {
  return manualSectorMask | sectorIdToMask(activeSectorId);
}

bool isSectorActive(uint8_t sectorId) {
  return (getOutputSectorMask() & sectorIdToMask(sectorId)) != 0;
}

// Applies the full sector mask to the hardware outputs
void setSectorHardware(uint16_t sectorMask) {
  for (uint8_t i = 0; i < NUM_SECTORS; i++) {
    digitalWrite(SECTOR_PINS[i], (sectorMask & sectorIdToMask(i + 1)) != 0 ? HIGH : LOW);
  }
}

// Clears all manual overrides so automatic programs can own the outputs
void clearManualOverrides() {
  manualSectorMask = 0;
}

// Applies the current runtime/manual state to the hardware outputs
void applyOutputsFromState() {
  const uint16_t sectorMask = getOutputSectorMask();
  setSectorHardware(sectorMask);
  setPump(sectorMask != 0);
}

bool isManualControlActive() {
  return manualSectorMask != 0;
}

uint8_t getEffectiveSectorId() {
  return firstSectorFromMask(getOutputSectorMask());
}

uint8_t getFirstManualSectorId() {
  return firstSectorFromMask(manualSectorMask);
}

String buildSectorArrayJson(uint16_t sectorMask) {
  String json = "[";
  bool first = true;

  for (uint8_t i = 1; i <= NUM_SECTORS; i++) {
    if ((sectorMask & sectorIdToMask(i)) == 0) {
      continue;
    }

    if (!first) {
      json += ",";
    }
    first = false;
    json += String(i);
  }

  json += "]";
  return json;
}

String formatSectorMaskForSerial(uint16_t sectorMask) {
  if (sectorMask == 0) {
    return "ninguno";
  }

  String text;
  for (uint8_t i = 1; i <= NUM_SECTORS; i++) {
    if ((sectorMask & sectorIdToMask(i)) == 0) {
      continue;
    }

    if (text.length() > 0) {
      text += ", ";
    }
    text += "S";
    text += String(i);
  }

  return text;
}

// Converts system state enum to string representation
const char* stateToString(SystemState state) {
  switch (state) {
    case STATE_RUNNING: return "RUNNING";
    case STATE_MANUAL_STOP: return "MANUAL_STOP";
    case STATE_IDLE:
    default: return "IDLE";
  }
}

// Converts boolean to JSON string format
String boolToJson(bool value) {
  return value ? "true" : "false";
}

// Escapes special characters for JSON encoding
String escapeJson(const String& input) {
  String out;
  out.reserve(input.length() + 8);

  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else out += c;
  }
  return out;
}

// Retrieves request body from WebServer
String getRequestBody() {
  if (server.hasArg("plain")) {
    return server.arg("plain");
  }
  return "";
}

// Extracts integer from string at specified position
bool extractIntAt(const String& src, int startPos, int& value, int& endPos) {
  int i = startPos;
  while (i < (int)src.length() && (src[i] == ' ' || src[i] == '\t' || src[i] == '\n' || src[i] == '\r')) {
    i++;
  }

  bool negative = false;
  if (i < (int)src.length() && src[i] == '-') {
    negative = true;
    i++;
  }

  if (i >= (int)src.length() || !isDigit(src[i])) {
    return false;
  }

  long result = 0;
  while (i < (int)src.length() && isDigit(src[i])) {
    result = result * 10 + (src[i] - '0');
    i++;
  }

  value = negative ? -result : result;
  endPos = i;
  return true;
}

// Extracts integer field from JSON object
bool extractIntField(const String& json, const String& key, int& out) {
  String pattern = "\"" + key + "\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) return false;

  pos = json.indexOf(':', pos);
  if (pos < 0) return false;
  pos++;

  int endPos = pos;
  return extractIntAt(json, pos, out, endPos);
}

// Extracts boolean field from JSON object
bool extractBoolField(const String& json, const String& key, bool& out) {
  String pattern = "\"" + key + "\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) return false;

  pos = json.indexOf(':', pos);
  if (pos < 0) return false;
  pos++;

  while (pos < (int)json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
    pos++;
  }

  if (json.startsWith("true", pos)) {
    out = true;
    return true;
  }
  if (json.startsWith("false", pos)) {
    out = false;
    return true;
  }
  return false;
}

// Extracts string field from JSON object
bool extractStringField(const String& json, const String& key, String& out) {
  String pattern = "\"" + key + "\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) return false;

  pos = json.indexOf(':', pos);
  if (pos < 0) return false;
  pos++;

  while (pos < (int)json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
    pos++;
  }

  if (pos >= (int)json.length() || json[pos] != '"') {
    return false;
  }

  pos++;
  int end = json.indexOf('"', pos);
  if (end < 0) return false;

  out = json.substring(pos, end);
  return true;
}

// Extracts optional ID field from JSON, defaults to zero if missing
bool extractNullableId(const String& json, int& out) {
  String pattern = "\"id\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) {
    out = 0;
    return true;
  }

  pos = json.indexOf(':', pos);
  if (pos < 0) return false;
  pos++;

  while (pos < (int)json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
    pos++;
  }

  if (json.startsWith("null", pos)) {
    out = 0;
    return true;
  }

  int endPos = pos;
  return extractIntAt(json, pos, out, endPos);
}

// Extracts nested object field from JSON
bool extractObjectField(const String& json, const String& key, String& out) {
  String pattern = "\"" + key + "\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) return false;

  pos = json.indexOf(':', pos);
  if (pos < 0) return false;

  int start = json.indexOf('{', pos);
  if (start < 0) return false;

  int depth = 0;
  for (int i = start; i < (int)json.length(); i++) {
    if (json[i] == '{') depth++;
    else if (json[i] == '}') {
      depth--;
      if (depth == 0) {
        out = json.substring(start, i + 1);
        return true;
      }
    }
  }
  return false;
}

// Extracts array field from JSON
bool extractArrayField(const String& json, const String& key, String& out) {
  String pattern = "\"" + key + "\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) return false;

  pos = json.indexOf(':', pos);
  if (pos < 0) return false;

  int start = json.indexOf('[', pos);
  if (start < 0) return false;

  int depth = 0;
  for (int i = start; i < (int)json.length(); i++) {
    if (json[i] == '[') depth++;
    else if (json[i] == ']') {
      depth--;
      if (depth == 0) {
        out = json.substring(start, i + 1);
        return true;
      }
    }
  }
  return false;
}

// Sorts program sectors by execution order
void sortProgramSectors(Program& program) {
  for (uint8_t i = 0; i < program.sectorCount; i++) {
    for (uint8_t j = i + 1; j < program.sectorCount; j++) {
      if (program.sectores[j].orden < program.sectores[i].orden) {
        SectorStep tmp = program.sectores[i];
        program.sectores[i] = program.sectores[j];
        program.sectores[j] = tmp;
      }
    }
  }
}

// Parses HH:MM and validates the range
bool parseHourMinute(const char* value, uint8_t& hour, uint8_t& minute) {
  if (value == nullptr ||
      strlen(value) != 5 ||
      !isDigit(value[0]) ||
      !isDigit(value[1]) ||
      value[2] != ':' ||
      !isDigit(value[3]) ||
      !isDigit(value[4])) {
    return false;
  }

  hour = (uint8_t)((value[0] - '0') * 10 + (value[1] - '0'));
  minute = (uint8_t)((value[3] - '0') * 10 + (value[4] - '0'));
  return hour < 24 && minute < 60;
}

// Parses program configuration from JSON string
bool parseProgramFromJson(const String& programJson, Program& outProgram) {
  memset(&outProgram, 0, sizeof(outProgram));
  outProgram.valid = true;

  int id = 0;
  extractNullableId(programJson, id);
  outProgram.id = (uint16_t)id;

  String hora;
  uint8_t startHour = 0;
  uint8_t startMinute = 0;
  if (!extractStringField(programJson, "horaInicio", hora) ||
      !parseHourMinute(hora.c_str(), startHour, startMinute)) {
    return false;
  }
  hora.toCharArray(outProgram.horaInicio, sizeof(outProgram.horaInicio));

  int dias = 0;
  if (!extractIntField(programJson, "dias", dias) || dias < 0 || dias > 0x7F) return false;
  outProgram.dias = (uint8_t)dias;

  int retardo = 0;
  if (!extractIntField(programJson, "retardoEntreSectores", retardo) ||
      retardo < 0 ||
      retardo > 65535) {
    return false;
  }
  outProgram.retardoEntreSectores = (uint16_t)retardo;

  bool ciclico = false;
  if (!extractBoolField(programJson, "ciclico", ciclico)) return false;
  outProgram.ciclico = ciclico;

  String sectoresArray;
  if (!extractArrayField(programJson, "sectores", sectoresArray)) return false;

  int pos = 0;
  while (pos < (int)sectoresArray.length() && outProgram.sectorCount < NUM_SECTORS) {
    int objStart = sectoresArray.indexOf('{', pos);
    if (objStart < 0) break;

    int depth = 0;
    int objEnd = -1;
    for (int i = objStart; i < (int)sectoresArray.length(); i++) {
      if (sectoresArray[i] == '{') depth++;
      else if (sectoresArray[i] == '}') {
        depth--;
        if (depth == 0) {
          objEnd = i;
          break;
        }
      }
    }

    if (objEnd < 0) break;

    String item = sectoresArray.substring(objStart, objEnd + 1);
    int sectorId = 0, orden = 0, tiempo = 0;
    if (extractIntField(item, "id", sectorId) &&
        extractIntField(item, "orden", orden) &&
        extractIntField(item, "tiempoRiego", tiempo)) {
      if (sectorId >= 1 &&
          sectorId <= NUM_SECTORS &&
          orden >= 1 &&
          orden <= NUM_SECTORS &&
          tiempo > 0) {
        SectorStep& s = outProgram.sectores[outProgram.sectorCount++];
        s.id = (uint8_t)sectorId;
        s.orden = (uint8_t)orden;
        s.tiempoRiego = (uint32_t)tiempo;
      }
    }

    pos = objEnd + 1;
  }

  if (outProgram.sectorCount == 0) return false;

  sortProgramSectors(outProgram);
  return true;
}

// Finds program storage index by ID
int findProgramIndexById(uint16_t id) {
  for (uint8_t i = 0; i < MAX_PROGRAMS; i++) {
    if (programs[i].valid && programs[i].id == id) {
      return i;
    }
  }
  return -1;
}

// Finds next available program storage slot
int findFreeProgramSlot() {
  for (uint8_t i = 0; i < MAX_PROGRAMS; i++) {
    if (!programs[i].valid) {
      return i;
    }
  }
  return -1;
}

// Stops current program execution and resets runtime state
void stopRuntime(SystemState newState) {
  systemState = newState;
  activeProgramId = 0;
  activeSectorId = 0;
  remainingTimeSec = 0;
  runningProgramIndex = -1;
  runningStepIndex = -1;
  waitingBetweenSectors = false;
  stepStartMs = 0;
  delayStartMs = 0;
  applyOutputsFromState();
}

// Starts specified program step and activates sector
void startStep(int programIndex, int stepIndex) {
  if (programIndex < 0 || !programs[programIndex].valid) {
    stopRuntime(STATE_IDLE);
    return;
  }

  Program& p = programs[programIndex];
  if (stepIndex < 0 || stepIndex >= p.sectorCount) {
    stopRuntime(STATE_IDLE);
    return;
  }

  runningProgramIndex = programIndex;
  runningStepIndex = stepIndex;
  waitingBetweenSectors = false;
  systemState = STATE_RUNNING;
  activeProgramId = p.id;
  activeSectorId = p.sectores[stepIndex].id;
  remainingTimeSec = p.sectores[stepIndex].tiempoRiego;
  stepStartMs = millis();

  applyOutputsFromState();
}

// Starts program execution by ID
bool startProgramById(uint16_t id) {
  int index = findProgramIndexById(id);
  if (index < 0) return false;

  Program& p = programs[index];
  if (p.sectorCount == 0) return false;

  clearManualOverrides();
  startStep(index, 0);
  return true;
}

// Updates program execution state and manages sector timing
void updateRuntime() {
  if (systemState != STATE_RUNNING || runningProgramIndex < 0) {
    return;
  }

  Program& p = programs[runningProgramIndex];
  unsigned long now = millis();

  if (waitingBetweenSectors) {
    remainingTimeSec = 0;
    if (now - delayStartMs >= (unsigned long)p.retardoEntreSectores * 1000UL) {
      int nextStep = runningStepIndex + 1;
      if (nextStep >= p.sectorCount) {
        if (p.ciclico) {
          startStep(runningProgramIndex, 0);
        } else {
          stopRuntime(STATE_IDLE);
        }
      } else {
        startStep(runningProgramIndex, nextStep);
      }
    }
    return;
  }

  uint32_t stepDurationSec = p.sectores[runningStepIndex].tiempoRiego;
  unsigned long elapsedMs = now - stepStartMs;
  uint32_t elapsedSec = (uint32_t)(elapsedMs / 1000UL);

  if (elapsedSec >= stepDurationSec) {
    activeSectorId = 0;
    applyOutputsFromState();

    int nextStep = runningStepIndex + 1;
    if (nextStep >= p.sectorCount) {
      if (p.ciclico) {
        if (p.retardoEntreSectores > 0) {
          waitingBetweenSectors = true;
          delayStartMs = now;
        } else {
          startStep(runningProgramIndex, 0);
        }
      } else {
        stopRuntime(STATE_IDLE);
      }
    } else {
      if (p.retardoEntreSectores > 0) {
        waitingBetweenSectors = true;
        delayStartMs = now;
        remainingTimeSec = 0;
      } else {
        startStep(runningProgramIndex, nextStep);
      }
    }
    return;
  }

  remainingTimeSec = stepDurationSec - elapsedSec;
}

// Builds system status as JSON
String buildEstadoJson() {
  const uint16_t activeSectorMask = getOutputSectorMask();
  String json = "{";
  json += "\"estado\":\"" + String(stateToString(systemState)) + "\",";
  json += "\"programaActivo\":" + String(activeProgramId) + ",";
  json += "\"sectorActivo\":" + String(getEffectiveSectorId()) + ",";
  json += "\"sectoresActivos\":" + buildSectorArrayJson(activeSectorMask) + ",";
  json += "\"tiempoRestante\":" + String(remainingTimeSec) + ",";
  json += "\"bomba\":" + boolToJson(pumpOn) + ",";
  json += "\"modoManual\":" + boolToJson(isManualControlActive()) + ",";
  json += "\"manualSectorMask\":" + String(manualSectorMask) + ",";
  json += "\"manualSectorId\":" + String(getFirstManualSectorId()) + ",";
  json += "\"manualPumpOn\":false";
  json += "}";
  return json;
}

// Builds all programs list as JSON
String buildProgramasJson() {
  String json = "{\"programas\":[";
  bool firstProgram = true;

  for (uint8_t i = 0; i < MAX_PROGRAMS; i++) {
    if (!programs[i].valid) continue;

    if (!firstProgram) json += ",";
    firstProgram = false;

    Program& p = programs[i];
    json += "{";
    json += "\"id\":" + String(p.id) + ",";
    json += "\"horaInicio\":\"" + escapeJson(String(p.horaInicio)) + "\",";
    json += "\"dias\":" + String(p.dias) + ",";
    json += "\"retardoEntreSectores\":" + String(p.retardoEntreSectores) + ",";
    json += "\"ciclico\":" + boolToJson(p.ciclico) + ",";
    json += "\"sectores\":[";

    for (uint8_t s = 0; s < p.sectorCount; s++) {
      if (s > 0) json += ",";
      json += "{";
      json += "\"id\":" + String(p.sectores[s].id) + ",";
      json += "\"orden\":" + String(p.sectores[s].orden) + ",";
      json += "\"tiempoRiego\":" + String(p.sectores[s].tiempoRiego);
      json += "}";
    }

    json += "]";
    json += "}";
  }

  json += "]}";
  return json;
}

// Builds JSON response with status and optional extra data
String buildOkJson(bool ok, const String& extra = "") {
  String json = "{\"ok\":" + boolToJson(ok);
  if (extra.length() > 0) {
    json += "," + extra;
  }
  json += "}";
  return json;
}

// RTC Helper Functions
// Pads single digit with leading zero
String twoDigits(uint8_t value) {
  if (value < 10) {
    return "0" + String(value);
  }
  return String(value);
}

// Formats RTC date as YYYY/MM/DD
String formatRTCDate(const Time& t) {
  return String(t.yr) + "/" + twoDigits(t.mon) + "/" + twoDigits(t.date);
}

// Formats RTC time as HH:MM:SS
String formatRTCTime(const Time& t) {
  return twoDigits(t.hr) + ":" + twoDigits(t.min) + ":" + twoDigits(t.sec);
}

// Calculates day of week from date
Time::Day calculateDayOfWeek(uint16_t year, uint8_t month, uint8_t day) {
  static const int monthTable[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int y = year;
  if (month < 3) {
    y -= 1;
  }
  int dow = (y + y / 4 - y / 100 + y / 400 + monthTable[month - 1] + day) % 7;
  return static_cast<Time::Day>(dow + 1);
}

bool isLeapYear(uint16_t year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t daysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

  if (month < 1 || month > 12) {
    return 0;
  }

  if (month == 2 && isLeapYear(year)) {
    return 29;
  }

  return daysPerMonth[month - 1];
}

bool isValidDateTime(uint16_t year,
                     uint8_t month,
                     uint8_t day,
                     uint8_t hour,
                     uint8_t minute,
                     uint8_t second) {
  if (year < 2000 || year > 2099) return false;
  if (month < 1 || month > 12) return false;
  if (day < 1 || day > daysInMonth(year, month)) return false;
  if (hour > 23) return false;
  if (minute > 59) return false;
  if (second > 59) return false;
  return true;
}

// Converts the selected date into the program bitmask used by the UI (Mon=bit0 ... Sun=bit6)
uint8_t dayMaskBitFromDate(uint16_t year, uint8_t month, uint8_t day) {
  const uint8_t dow = (uint8_t)calculateDayOfWeek(year, month, day);

  switch (dow) {
    case 2: return 0;  // Monday
    case 3: return 1;  // Tuesday
    case 4: return 2;  // Wednesday
    case 5: return 3;  // Thursday
    case 6: return 4;  // Friday
    case 7: return 5;  // Saturday
    case 1: return 6;  // Sunday
    default: return 255;
  }
}

bool shouldStartProgramNow(const Program& program, const Time& now) {
  if (!program.valid || program.sectorCount == 0) {
    return false;
  }

  if (!isValidDateTime(now.yr, now.mon, now.date, now.hr, now.min, now.sec)) {
    return false;
  }

  const uint8_t dayBit = dayMaskBitFromDate(now.yr, now.mon, now.date);
  if (dayBit > 6 || (program.dias & (1U << dayBit)) == 0) {
    return false;
  }

  uint8_t programHour = 0;
  uint8_t programMinute = 0;
  if (!parseHourMinute(program.horaInicio, programHour, programMinute)) {
    return false;
  }

  return programHour == now.hr && programMinute == now.min;
}

void rememberScheduleMinute(const Time& now) {
  lastScheduleYear = now.yr;
  lastScheduleMonth = now.mon;
  lastScheduleDay = now.date;
  lastScheduleHour = now.hr;
  lastScheduleMinute = now.min;
}

bool isSameScheduleMinute(const Time& now) {
  return now.yr == lastScheduleYear &&
         now.mon == lastScheduleMonth &&
         now.date == lastScheduleDay &&
         now.hr == lastScheduleHour &&
         now.min == lastScheduleMinute;
}

void checkScheduledPrograms() {
  const Time now = rtc.time();
  if (!isValidDateTime(now.yr, now.mon, now.date, now.hr, now.min, now.sec)) {
    return;
  }

  if (isSameScheduleMinute(now)) {
    return;
  }

  rememberScheduleMinute(now);

  if (systemState == STATE_RUNNING || isManualControlActive()) {
    return;
  }

  for (uint8_t i = 0; i < MAX_PROGRAMS; i++) {
    if (shouldStartProgramNow(programs[i], now)) {
      startProgramById(programs[i].id);
      return;
    }
  }
}

// Builds RTC state as JSON
String buildRTCJson() {
  Time now = rtc.time();
  String json = "{";
  json += "\"year\":" + String(now.yr) + ",";
  json += "\"month\":" + String(now.mon) + ",";
  json += "\"day\":" + String(now.date) + ",";
  json += "\"hour\":" + String(now.hr) + ",";
  json += "\"minute\":" + String(now.min) + ",";
  json += "\"second\":" + String(now.sec);
  json += "}";
  return json;
}

// Serves main HTML interface
void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

// Returns current system status as JSON
void handleEstado() {
  server.send(200, "application/json", buildEstadoJson());
}

// Returns list of all stored programs as JSON
void handleProgramas() {
  server.send(200, "application/json", buildProgramasJson());
}

// Handles manual control of sectors and pump
void handleControl() {
  if (!server.hasArg("type")) {
    server.send(400, "application/json", buildOkJson(false, "\"error\":\"Missing type\""));
    return;
  }

  String type = server.arg("type");
  int state = server.hasArg("state") ? server.arg("state").toInt() : 0;

  if (type == "sector") {
    if (!server.hasArg("id")) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"Missing id\""));
      return;
    }
    int id = server.arg("id").toInt();
    if (id < 1 || id > NUM_SECTORS) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"Invalid id\""));
      return;
    }

    if (state) {
      manualSectorMask |= sectorIdToMask((uint8_t)id);
      stopRuntime(STATE_IDLE);
    } else {
      manualSectorMask &= (uint16_t)~sectorIdToMask((uint8_t)id);
      applyOutputsFromState();
    }
    server.send(200, "application/json", buildOkJson(true));
  } else if (type == "pump") {
    server.send(409, "application/json",
                buildOkJson(false, "\"error\":\"La bomba sigue automaticamente a los sectores activos\""));
  } else {
    server.send(400, "application/json", buildOkJson(false, "\"error\":\"Invalid type\""));
  }
}

// Immediately stops current program execution
void handleParada() {
  clearManualOverrides();
  stopRuntime(STATE_MANUAL_STOP);
  server.send(200, "application/json", buildOkJson(true));
}

// Handles program configuration: save, delete, or execute
void handleConfiguracion() {
  String body = getRequestBody();
  if (body.length() == 0) {
    server.send(400, "application/json", buildOkJson(false, "\"error\":\"body vacio\""));
    return;
  }

  if (body.indexOf("\"ejecutar\"") >= 0) {
    int runId = 0;
    if (!extractIntField(body, "ejecutar", runId) || runId <= 0) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"ejecutar invalido\""));
      return;
    }

    bool ok = startProgramById((uint16_t)runId);
    server.send(ok ? 200 : 404, "application/json", buildOkJson(ok));
    return;
  }

  if (body.indexOf("\"borrar\"") >= 0) {
    int deleteId = 0;
    if (!extractIntField(body, "borrar", deleteId) || deleteId <= 0) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"borrar invalido\""));
      return;
    }

    int idx = findProgramIndexById((uint16_t)deleteId);
    if (idx < 0) {
      server.send(404, "application/json", buildOkJson(false, "\"error\":\"programa no encontrado\""));
      return;
    }

    if (activeProgramId == (uint16_t)deleteId) {
      stopRuntime(STATE_IDLE);
    }

    programs[idx].valid = false;
    server.send(200, "application/json", buildOkJson(true));
    return;
  }

  if (body.indexOf("\"programa\"") >= 0) {
    String programJson;
    if (!extractObjectField(body, "programa", programJson)) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"programa invalido\""));
      return;
    }

    Program parsed;
    if (!parseProgramFromJson(programJson, parsed)) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"no se pudo parsear programa\""));
      return;
    }

    int slot = -1;
    if (parsed.id > 0) {
      slot = findProgramIndexById(parsed.id);
    }

    if (slot < 0) {
      slot = findFreeProgramSlot();
      if (slot < 0) {
        server.send(507, "application/json", buildOkJson(false, "\"error\":\"sin espacio para mas programas\""));
        return;
      }
      parsed.id = nextProgramId++;
    }

    parsed.valid = true;
    programs[slot] = parsed;

    String extra = "\"id\":" + String(parsed.id);
    server.send(200, "application/json", buildOkJson(true, extra));
    return;
  }

  server.send(400, "application/json", buildOkJson(false, "\"error\":\"accion no reconocida\""));
}

// Returns empty response for favicon requests
void handleFavicon() {
  server.send(204, "text/plain", "");
}

// Serves main HTML for unknown routes
void handleNotFound() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

// Prints system status to serial every 20 seconds
void printPeriodicStatus() {
  unsigned long now = millis();
  if (now - lastStatusPrint < STATUS_PRINT_INTERVAL_MS) {
    return;
  }

  lastStatusPrint = now;

  Serial.println();
  Serial.println("===== ESTADO CADA 20s =====");
  
  // Print RTC time
  Time rtcNow = rtc.time();
  Serial.print("Hora RTC: ");
  Serial.print(formatRTCDate(rtcNow));
  Serial.print(" ");
  Serial.println(formatRTCTime(rtcNow));
  
  Serial.print("Estado: ");
  Serial.println(isManualControlActive() ? "MANUAL" : stateToString(systemState));
  Serial.print("Programa activo: ");
  Serial.println(activeProgramId);
  Serial.print("Sectores activos: ");
  Serial.println(formatSectorMaskForSerial(getOutputSectorMask()));
  Serial.print("Tiempo restante: ");
  Serial.print(remainingTimeSec);
  Serial.println(" s");
  Serial.print("Bomba (GPIO");
  Serial.print(PUMP_PIN);
  Serial.print("): ");
  Serial.println(pumpOn ? "ON" : "OFF");
  Serial.print("Modo manual: ");
  Serial.println(isManualControlActive() ? "SI" : "NO");

  for (uint8_t i = 1; i <= NUM_SECTORS; i++) {
    Serial.print("- Sector ");
    Serial.print(i);
    Serial.print(" (GPIO");
    Serial.print(SECTOR_PINS[i - 1]);
    Serial.print("): ");
    Serial.println(isSectorActive(i) ? "ACTIVO" : "inactivo");
  }

  Serial.println("===========================");
}

// Gets or sets RTC date and time
void handleRTC() {
  if (server.method() == HTTP_GET) {
    // GET request for current RTC time
    server.send(200, "application/json", buildRTCJson());
    return;
  }
  
  if (server.method() == HTTP_POST) {
    // POST request to set RTC time
    if (!server.hasArg("year") || !server.hasArg("month") || !server.hasArg("day") ||
        !server.hasArg("hour") || !server.hasArg("minute") || !server.hasArg("second")) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"Missing parameters\""));
      return;
    }

    int year = server.arg("year").toInt();
    int month = server.arg("month").toInt();
    int day = server.arg("day").toInt();
    int hour = server.arg("hour").toInt();
    int minute = server.arg("minute").toInt();
    int second = server.arg("second").toInt();

    if (!isValidDateTime((uint16_t)year,
                         (uint8_t)month,
                         (uint8_t)day,
                         (uint8_t)hour,
                         (uint8_t)minute,
                         (uint8_t)second)) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"Invalid date/time\""));
      return;
    }

    Time::Day dow = calculateDayOfWeek((uint16_t)year, (uint8_t)month, (uint8_t)day);
    Time newTime((uint16_t)year, (uint8_t)month, (uint8_t)day,
                 (uint8_t)hour, (uint8_t)minute, (uint8_t)second, dow);

    rtc.writeProtect(false);
    delay(10);
    rtc.halt(false);
    delay(10);
    rtc.time(newTime);
    delay(50);
    rtc.halt(false);
    rtc.writeProtect(true);
    
    server.send(200, "application/json", buildOkJson(true, "\"rtc\":" + buildRTCJson()));
  }
}

// Initializes default irrigation programs
void seedDefaultPrograms() {
  memset(programs, 0, sizeof(programs));

  programs[0].valid = true;
  programs[0].id = 1;
  strncpy(programs[0].horaInicio, "07:00", sizeof(programs[0].horaInicio));
  programs[0].dias = 0b0111110;
  programs[0].retardoEntreSectores = 5;
  programs[0].ciclico = true;
  programs[0].sectorCount = 4;
  programs[0].sectores[0] = {1, 1, 60};
  programs[0].sectores[1] = {2, 2, 90};
  programs[0].sectores[2] = {3, 3, 120};
  programs[0].sectores[3] = {5, 4, 45};

  programs[1].valid = true;
  programs[1].id = 2;
  strncpy(programs[1].horaInicio, "19:30", sizeof(programs[1].horaInicio));
  programs[1].dias = 0b1100000;
  programs[1].retardoEntreSectores = 10;
  programs[1].ciclico = false;
  programs[1].sectorCount = 3;
  programs[1].sectores[0] = {4, 1, 180};
  programs[1].sectores[1] = {6, 2, 180};
  programs[1].sectores[2] = {8, 3, 90};
}

// Initializes ESP32, RTC, WiFi and HTTP server
void setup() {
  Serial.begin(115200);
  delay(300);

  // Configure all sector GPIO pins as outputs
  for (uint8_t i = 0; i < NUM_SECTORS; i++) {
    pinMode(SECTOR_PINS[i], OUTPUT);
    digitalWrite(SECTOR_PINS[i], LOW);

    if (SECTOR_PINS[i] >= 34 && SECTOR_PINS[i] <= 39) {
      Serial.print("WARNING: GPIO");
      Serial.print(SECTOR_PINS[i]);
      Serial.println(" is input-only on ESP32 and cannot drive a relay output.");
    }
  }

  // Configure pump GPIO pin
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);

  // Initialize RTC
  Serial.println("\n\nStarting RTC initialization...");
  rtc.writeProtect(false);
  rtc.halt(false);
  delay(50);

  Time now = rtc.time();
  Serial.print("RTC read: ");
  Serial.print(formatRTCDate(now));
  Serial.print(" ");
  Serial.println(formatRTCTime(now));

  if (!isValidDateTime(now.yr, now.mon, now.date, now.hr, now.min, now.sec)) {
    Serial.println("RTC contains invalid data, attempting to initialize...");

    bool initOk = false;
    const Time defaultTime(2024, 1, 1, 12, 0, 0, calculateDayOfWeek(2024, 1, 1));

    for (int attempt = 0; attempt < 5; attempt++) {
      Serial.print("RTC init attempt ");
      Serial.println(attempt + 1);

      rtc.halt(false);
      delay(20);
      rtc.time(defaultTime);
      delay(100);

      Time verify = rtc.time();
      Serial.print("  Read back: ");
      Serial.print(formatRTCDate(verify));
      Serial.print(" ");
      Serial.println(formatRTCTime(verify));

      if (isValidDateTime(verify.yr, verify.mon, verify.date, verify.hr, verify.min, verify.sec)) {
        Serial.println("RTC initialization successful!");
        initOk = true;
        break;
      }
    }

    if (!initOk) {
      Serial.println("WARNING: RTC still reports invalid data after initialization attempts.");
    }
  } else {
    Serial.println("RTC has valid data, no initialization needed");
  }

  rtc.writeProtect(true);

  seedDefaultPrograms();
  stopRuntime(STATE_IDLE);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  Serial.println();
  Serial.println("==================================");
  Serial.println("ESP32 SISTEMA DE RIEGO INICIADO");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASS);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("Abrir en navegador: http://192.168.4.1");
  Serial.println("RTC pins - CLK: GPIO18, DAT: GPIO19, RST: GPIO21");
  Serial.print("Pump pin: GPIO");
  Serial.println(PUMP_PIN);
  Serial.println("Sector pins (1-8): GPIO 13, 14, 16, 17, 32, 33, 25, 26");
  Serial.println("==================================");

  server.on("/", HTTP_GET, handleRoot);
  server.on("/index.html", HTTP_GET, handleRoot);
  server.on("/estado", HTTP_GET, handleEstado);
  server.on("/programas", HTTP_GET, handleProgramas);
  server.on("/configuracion", HTTP_POST, handleConfiguracion);
  server.on("/control", HTTP_GET, handleControl);
  server.on("/rtc", HTTP_GET, handleRTC);
  server.on("/rtc", HTTP_POST, handleRTC);
  server.on("/parada", HTTP_POST, handleParada);
  server.on("/favicon.ico", HTTP_GET, handleFavicon);
  server.onNotFound(handleNotFound);
  server.begin();

  lastStatusPrint = millis();
}

// Main program loop: handles HTTP requests and updates runtime state
void loop() {
  server.handleClient();
  checkScheduledPrograms();
  updateRuntime();
  printPeriodicStatus();
}
