/**
 * @file pagina.h
 * @brief Tablero de control local servido por el ESP32.
 *
 * No usa librerías externas (CDN) porque la WLAN de las autoridades puede no
 * tener Internet: las gráficas se dibujan con <canvas> a mano.
 * Consulta /api/estado cada 2 s y /api/historia cada 10 s.
 */
#pragma once
#ifndef PAGINA_H
#define PAGINA_H

const char* PAGINA_HTML = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Tablero Hídrico - Sabana Centro</title>
  <!-- Chart.js para las gráficas en tiempo real -->
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <style>
    :root {
      --bg-color: #f4f7f6;
      --card-bg: #ffffff;
      --text-main: #333333;
      --primary: #0275d8;
      --success: #5cb85c;
      --warning: #f0ad4e;
      --danger: #d9534f;
      --border-radius: 10px;
    }
    body {
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      background-color: var(--bg-color);
      color: var(--text-main);
      margin: 0;
      padding: 20px;
    }
    .container {
      max-width: 1100px;
      margin: 0 auto;
    }
    header {
      text-align: center;
      margin-bottom: 20px;
    }
    h1 {
      color: var(--primary);
      margin-bottom: 5px;
    }
    
    /* 1. Botón de silenciar y barra de estado en la parte superior */
    .top-action-bar {
      background: var(--card-bg);
      padding: 15px 20px;
      border-radius: var(--border-radius);
      box-shadow: 0 4px 6px rgba(0,0,0,0.05);
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 20px;
      border-left: 6px solid var(--primary);
    }
    .top-action-bar.alerta-activa {
      border-left-color: var(--danger);
      background-color: #fff5f5;
      animation: pulse-border 1.5s infinite;
    }
    @keyframes pulse-border {
      0% { box-shadow: 0 0 0 0 rgba(217, 83, 79, 0.4); }
      70% { box-shadow: 0 0 0 10px rgba(217, 83, 79, 0); }
      100% { box-shadow: 0 0 0 0 rgba(217, 83, 79, 0); }
    }
    .btn-silenciar {
      background-color: var(--danger);
      color: white;
      border: none;
      padding: 10px 20px;
      font-size: 1em;
      font-weight: bold;
      border-radius: 5px;
      cursor: pointer;
      transition: background 0.2s;
    }
    .btn-silenciar:hover {
      background-color: #c9302c;
    }
    .btn-silenciar:disabled {
      background-color: #cccccc;
      cursor: not-allowed;
    }

    /* Tarjetas de métricas actuales */
    .grid-cards {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
      gap: 15px;
      margin-bottom: 25px;
    }
    .card {
      background: var(--card-bg);
      padding: 20px;
      border-radius: var(--border-radius);
      box-shadow: 0 4px 6px rgba(0,0,0,0.05);
      text-align: center;
    }
    .card .label {
      font-size: 0.85em;
      color: #777;
      text-transform: uppercase;
      font-weight: bold;
    }
    .card .value {
      font-size: 2.2em;
      font-weight: bold;
      color: #222;
      margin: 10px 0 5px 0;
    }
    .card .unit {
      font-size: 0.5em;
      color: #888;
    }

    /* 2. Sección de Gráficas */
    .charts-section {
      background: var(--card-bg);
      padding: 20px;
      border-radius: var(--border-radius);
      box-shadow: 0 4px 6px rgba(0,0,0,0.05);
      margin-bottom: 25px;
    }
    .chart-container {
      position: relative;
      height: 250px;
      width: 100%;
      margin-bottom: 20px;
    }

    /* 3. Histórico de datos en la parte inferior (Tabla con colores alternantes) */
    .history-section {
      background: var(--card-bg);
      padding: 20px;
      border-radius: var(--border-radius);
      box-shadow: 0 4px 6px rgba(0,0,0,0.05);
    }
    h2 {
      font-size: 1.3em;
      color: var(--primary);
      margin-top: 0;
    }
    .table-responsive {
      overflow-x: auto;
      max-height: 350px;
      overflow-y: auto;
    }
    table {
      width: 100%;
      border-collapse: collapse;
      font-size: 0.9em;
    }
    th, td {
      padding: 10px 12px;
      text-align: center;
    }
    th {
      background-color: var(--primary);
      color: white;
      position: sticky;
      top: 0;
    }
    /* Filas de colores alternantes (Zebra Striping) */
    tr:nth-child(even) {
      background-color: #f9f9f9;
    }
    tr:nth-child(odd) {
      background-color: #ffffff;
    }
    tr:hover {
      background-color: #f1f1f1;
    }
    .badge {
      padding: 4px 8px;
      border-radius: 4px;
      font-weight: bold;
      color: white;
      font-size: 0.8em;
    }
    .bg-normal { background-color: var(--success); }
    .bg-precaucion { background-color: #17a2b8; }
    .bg-alerta { background-color: var(--warning); color: #333; }
    .bg-critico { background-color: var(--danger); }
  </style>
</head>
<body>
  <div class="container">
    <header>
      <h1>Vigilancia Hídrica - Sabana Centro</h1>
      <p>Monitoreo Inteligente y Gráficas en Tiempo Real</p>
    </header>

    <!-- BOTÓN DE SILENCIAR EN LA PARTE SUPERIOR -->
    <div id="actionBar" class="top-action-bar">
      <div>
        <strong id="estadoAlertaTxt">Estado del Sistema: Cargando...</strong>
        <div id="causasTxt" style="font-size: 0.85em; color: #666; margin-top: 3px;"></div>
      </div>
      <button id="btnSilenciar" class="btn-silenciar" onclick="silenciarAlarma()" disabled>🔇 Silenciar Alarma</button>
    </div>

    <!-- TARJETAS DE MÉTRICAS ACTUALES -->
    <div class="grid-cards">
      <div class="card">
        <div class="label">Nivel de Agua</div>
        <div class="value" id="valNivel">--<span class="unit">%</span></div>
      </div>
      <div class="card">
        <div class="label">Temperatura</div>
        <div class="value" id="valTemp">--<span class="unit">°C</span></div>
      </div>
      <div class="card">
        <div class="label">Humedad Relativa</div>
        <div class="value" id="valHum">--<span class="unit">%</span></div>
      </div>
      <div class="card">
        <div class="label">Evaporación</div>
        <div class="value" id="valEvap">--<span class="unit">mm/h</span></div>
      </div>
    </div>

    <!-- GRÁFICAS PARA LOS DATOS SENSADOS -->
    <div class="charts-section">
      <h2>Tendencias Gráficas</h2>
      <div class="chart-container">
        <canvas id="chartNivel"></canvas>
      </div>
      <div class="chart-container" style="margin-top: 35px;">
        <canvas id="chartClima"></canvas>
      </div>
    </div>

    <!-- HISTÓRICO DE DATOS EN LA PARTE INFERIOR -->
    <div class="history-section">
      <h2>Histórico de Registros</h2>
      <div class="table-responsive">
        <table id="tablaHistorico">
          <thead>
            <tr>
              <th>Tiempo (s)</th>
              <th>Nivel (%)</th>
              <th>Caudal (L/min)</th>
              <th>Temp (°C)</th>
              <th>Humedad (%)</th>
              <th>Evap. (mm/h)</th>
              <th>Alerta</th>
            </tr>
          </thead>
          <tbody>
            <tr><td colspan="7">Cargando histórico...</td></tr>
          </tbody>
        </table>
      </div>
    </div>
  </div>

  <script>
    const nombresAlerta = ["NORMAL", "PRECAUCION", "ALERTA", "CRITICO"];
    const clasesAlerta = ["bg-normal", "bg-precaucion", "bg-alerta", "bg-critico"];

    // Inicializar Gráfica de Nivel
    const ctxNivel = document.getElementById('chartNivel').getContext('2d');
    const chartNivel = new Chart(ctxNivel, {
      type: 'line',
      data: {
        labels: [],
        datasets: [{
          label: 'Nivel del Tanque (%)',
          data: [],
          borderColor: '#0275d8',
          backgroundColor: 'rgba(2, 117, 216, 0.1)',
          borderWidth: 2,
          fill: true,
          tension: 0.3
        }]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        scales: { y: { min: 0, max: 100 } }
      }
    });

    // Inicializar Gráfica de Clima (Temperatura y Humedad)
    const ctxClima = document.getElementById('chartClima').getContext('2d');
    const chartClima = new Chart(ctxClima, {
      type: 'line',
      data: {
        labels: [],
        datasets: [
          {
            label: 'Temperatura (°C)',
            data: [],
            borderColor: '#d9534f',
            backgroundColor: 'transparent',
            borderWidth: 2,
            tension: 0.3,
            yAxisID: 'y'
          },
          {
            label: 'Humedad (%)',
            data: [],
            borderColor: '#5cb85c',
            backgroundColor: 'transparent',
            borderWidth: 2,
            tension: 0.3,
            yAxisID: 'y1'
          }
        ]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        scales: {
          y: { type: 'linear', position: 'left', title: {display: true, text: 'Temp (°C)'} },
          y1: { type: 'linear', position: 'right', min: 0, max: 100, grid: {drawOnChartArea: false}, title: {display: true, text: 'Humedad (%)'} }
        }
      }
    });

    // Petición AJAX para estado actual
    function actualizarEstado() {
      fetch('/api/estado')
        .then(res => res.json())
        .then(data => {
          document.getElementById('valNivel').innerHTML = (data.nivel_pct !== null ? data.nivel_pct.toFixed(1) : '--') + '<span class="unit">%</span>';
          document.getElementById('valTemp').innerHTML = (data.temp_c !== null ? data.temp_c.toFixed(1) : '--') + '<span class="unit">°C</span>';
          document.getElementById('valHum').innerHTML = (data.hum_pct !== null ? data.hum_pct.toFixed(0) : '--') + '<span class="unit">%</span>';
          document.getElementById('valEvap').innerHTML = (data.evap_mmh !== null ? data.evap_mmh.toFixed(2) : '--') + '<span class="unit">mm/h</span>';

          const actionBar = document.getElementById('actionBar');
          const estadoTxt = document.getElementById('estadoAlertaTxt');
          const causasTxt = document.getElementById('causasTxt');
          const btnSilenciar = document.getElementById('btnSilenciar');

          estadoTxt.innerText = `Estado: ${data.nombre} (Puntaje: ${data.puntaje})`;
          causasTxt.innerText = `Causas: ${data.causas}`;

          if (data.alerta >= 2) {
            actionBar.classList.add('alerta-activa');
            btnSilenciar.disabled = data.silenciada;
            btnSilenciar.innerText = data.silenciada ? "✓ Alarma Silenciada" : "🔇 Silenciar Alarma";
          } else {
            actionBar.classList.remove('alerta-activa');
            btnSilenciar.disabled = true;
            btnSilenciar.innerText = "🔇 Silenciar Alarma";
          }
        })
        .catch(err => console.error("Error al consultar estado:", err));
    }

    // Petición AJAX para históricos y gráficas
    function actualizarHistoria() {
      fetch('/api/historia')
        .then(res => res.json())
        .then(data => {
          if (!data.t || data.t.length === 0) return;

          // Actualizar gráficas Chart.js
          chartNivel.data.labels = data.t;
          chartNivel.data.datasets[0].data = data.nivel;
          chartNivel.update();

          chartClima.data.labels = data.t;
          chartClima.data.datasets[0].data = data.temp;
          chartClima.data.datasets[1].data = data.hum;
          chartClima.update();

          // Llenar tabla inferior con filas alternantes
          const tbody = document.querySelector('#tablaHistorico tbody');
          tbody.innerHTML = '';

          for (let i = data.t.length - 1; i >= 0; i--) {
            const tr = document.createElement('tr');
            const alertIdx = data.alerta[i];
            const badgeClass = clasesAlerta[alertIdx] || 'bg-normal';
            const alertName = nombresAlerta[alertIdx] || 'NORMAL';

            tr.innerHTML = `
              <td>${data.t[i]}s</td>
              <td>${data.nivel[i] !== null ? data.nivel[i].toFixed(1) : '--'}</td>
              <td>${data.caudal[i] !== null ? data.caudal[i].toFixed(2) : '--'}</td>
              <td>${data.temp[i] !== null ? data.temp[i].toFixed(1) : '--'}</td>
              <td>${data.hum[i] !== null ? data.hum[i].toFixed(0) : '--'}</td>
              <td>${data.evap[i] !== null ? data.evap[i].toFixed(2) : '--'}</td>
              <td><span class="badge ${badgeClass}">${alertName}</span></td>
            `;
            tbody.appendChild(tr);
          }
        })
        .catch(err => console.error("Error al consultar historia:", err));
    }

    function silenciarAlarma() {
      fetch('/api/silenciar', { method: 'POST' })
        .then(res => res.json())
        .then(response => {
          if (response.ok) {
            actualizarEstado();
          }
        })
        .catch(err => console.error("Error al silenciar:", err));
    }

    // Temporizadores de actualización automática
    setInterval(actualizarEstado, 1000);
    setInterval(actualizarHistoria, 5000);

    // Carga inicial
    actualizarEstado();
    actualizarHistoria();
  </script>
</body>
</html>
)rawliteral";

#endif