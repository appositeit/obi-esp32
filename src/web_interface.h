/**
 * Embedded Web Interface for OBI ESP32
 *
 * FUNCTIONAL REQUIREMENTS:
 * 1. Serve single-page web application for battery diagnostics
 * 2. Provide REST API endpoints for battery operations
 * 3. Display real-time battery status via AJAX polling
 *
 * AI-generated on 2025-12-16
 */

#ifndef WEB_INTERFACE_H
#define WEB_INTERFACE_H

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>OBI - Open Battery Information</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: #1a1a2e;
            color: #eee;
            min-height: 100vh;
            padding: 20px;
        }
        .header {
            text-align: center;
            margin-bottom: 20px;
            color: #00d4ff;
        }
        .container {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
            gap: 20px;
            max-width: 1600px;
            margin: 0 auto;
        }
        .card {
            background: #16213e;
            border-radius: 12px;
            padding: 20px;
            box-shadow: 0 4px 6px rgba(0,0,0,0.3);
        }
        .card-wide {
            grid-column: 1 / -1;
        }
        @media (min-width: 1000px) {
            .card-wide { grid-column: span 2; }
        }
        .card h2 {
            color: #00d4ff;
            margin-bottom: 15px;
            font-size: 1.2em;
            border-bottom: 1px solid #333;
            padding-bottom: 10px;
        }
        .btn {
            background: #0f4c75;
            color: white;
            border: none;
            padding: 12px 24px;
            border-radius: 8px;
            cursor: pointer;
            font-size: 1em;
            margin: 5px;
            transition: background 0.3s;
        }
        .btn:hover { background: #1b6ca8; }
        .btn:disabled { background: #333; cursor: not-allowed; }
        .btn-danger { background: #c0392b; }
        .btn-danger:hover { background: #e74c3c; }
        .btn-success { background: #27ae60; }
        .btn-success:hover { background: #2ecc71; }
        .status {
            padding: 10px;
            border-radius: 8px;
            margin: 10px 0;
            font-family: monospace;
        }
        .status-ok { background: #1e4620; }
        .status-error { background: #4a1515; }
        .status-waiting { background: #3d3d00; }
        .data-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 10px;
        }
        .data-item {
            background: #0f3460;
            padding: 12px;
            border-radius: 8px;
        }
        .data-label {
            font-size: 0.85em;
            color: #888;
            margin-bottom: 4px;
        }
        .data-value {
            font-size: 1.2em;
            font-weight: bold;
            color: #00d4ff;
        }
        .cell-grid {
            display: grid;
            grid-template-columns: repeat(5, 1fr);
            gap: 8px;
            margin-top: 10px;
        }
        .cell {
            background: #0f3460;
            padding: 10px;
            border-radius: 8px;
            text-align: center;
        }
        .cell-voltage {
            font-size: 1.1em;
            font-weight: bold;
        }
        .cell-ok { color: #2ecc71; }
        .cell-warn { color: #f39c12; }
        .cell-bad { color: #e74c3c; }
        .debug {
            background: #0a0a15;
            padding: 10px;
            border-radius: 8px;
            font-family: monospace;
            font-size: 0.85em;
            max-height: 200px;
            overflow-y: auto;
            white-space: pre-wrap;
        }
        .buttons { text-align: center; margin: 15px 0; }
        @media (max-width: 600px) {
            .data-grid { grid-template-columns: 1fr; }
            .cell-grid { grid-template-columns: repeat(3, 1fr); }
        }
    </style>
</head>
<body>
    <h1 class="header">🔋 Open Battery Information</h1>
    <div class="container">
        <div class="card">
            <h2>Connection Status</h2>
            <div id="connectionStatus" class="status status-waiting">
                Waiting for battery connection...
            </div>
            <div class="buttons">
                <button class="btn btn-success" onclick="readBattery()">Read Battery</button>
                <button class="btn" onclick="readVoltages()">Read Voltages</button>
            </div>
        </div>

        <div class="card">
            <h2>Battery Information</h2>
            <div class="data-grid">
                <div class="data-item">
                    <div class="data-label">Model</div>
                    <div class="data-value" id="model">--</div>
                </div>
                <div class="data-item">
                    <div class="data-label">Status</div>
                    <div class="data-value" id="status">--</div>
                </div>
                <div class="data-item">
                    <div class="data-label">Charge Count</div>
                    <div class="data-value" id="chargeCount">--</div>
                </div>
                <div class="data-item">
                    <div class="data-label">Manufacturing Date</div>
                    <div class="data-value" id="mfgDate">--</div>
                </div>
                <div class="data-item">
                    <div class="data-label">Capacity</div>
                    <div class="data-value" id="capacity">--</div>
                </div>
                <div class="data-item">
                    <div class="data-label">Pack Voltage</div>
                    <div class="data-value" id="packVoltage">--</div>
                </div>
            </div>
        </div>

        <div class="card">
            <h2>Cell Voltages</h2>
            <div class="cell-grid" id="cellGrid">
                <div class="cell"><div class="data-label">Cell 1</div><div class="cell-voltage" id="cell1">--</div></div>
                <div class="cell"><div class="data-label">Cell 2</div><div class="cell-voltage" id="cell2">--</div></div>
                <div class="cell"><div class="data-label">Cell 3</div><div class="cell-voltage" id="cell3">--</div></div>
                <div class="cell"><div class="data-label">Cell 4</div><div class="cell-voltage" id="cell4">--</div></div>
                <div class="cell"><div class="data-label">Cell 5</div><div class="cell-voltage" id="cell5">--</div></div>
            </div>
            <div class="data-grid" style="margin-top: 15px;">
                <div class="data-item">
                    <div class="data-label">Cell Difference</div>
                    <div class="data-value" id="cellDiff">--</div>
                </div>
                <div class="data-item">
                    <div class="data-label">Cell Temp</div>
                    <div class="data-value" id="tempCell">--</div>
                </div>
                <div class="data-item">
                    <div class="data-label">MOSFET Temp</div>
                    <div class="data-value" id="tempMosfet">--</div>
                </div>
                <div class="data-item">
                    <div class="data-label">Error Code</div>
                    <div class="data-value" id="errorCode">--</div>
                </div>
            </div>
        </div>

        <div class="card">
            <h2>Actions</h2>
            <div class="buttons">
                <button class="btn" onclick="testLeds(true)">LEDs ON</button>
                <button class="btn" onclick="testLeds(false)">LEDs OFF</button>
                <button class="btn btn-danger" onclick="resetErrors()">Reset Errors</button>
            </div>
        </div>

        <div class="card">
            <h2>Debug Log</h2>
            <div class="debug" id="debugLog">OBI ESP32 Web Interface Ready\n</div>
        </div>
    </div>

    <script>
        function log(msg) {
            const el = document.getElementById('debugLog');
            const time = new Date().toLocaleTimeString();
            el.textContent += `[${time}] ${msg}\n`;
            el.scrollTop = el.scrollHeight;
        }

        function setStatus(msg, type) {
            const el = document.getElementById('connectionStatus');
            el.textContent = msg;
            el.className = 'status status-' + type;
        }

        function setCellClass(el, voltage) {
            el.classList.remove('cell-ok', 'cell-warn', 'cell-bad');
            if (voltage >= 3.5) el.classList.add('cell-ok');
            else if (voltage >= 3.0) el.classList.add('cell-warn');
            else el.classList.add('cell-bad');
        }

        async function apiCall(endpoint) {
            try {
                const response = await fetch('/api/' + endpoint);
                return await response.json();
            } catch (e) {
                log('Error: ' + e.message);
                return null;
            }
        }

        async function readBattery() {
            setStatus('Reading battery...', 'waiting');
            log('Reading battery info...');

            const data = await apiCall('read');
            if (data && data.success) {
                document.getElementById('model').textContent = data.model || '--';
                document.getElementById('status').textContent = data.locked ? 'LOCKED' : 'OK';
                document.getElementById('chargeCount').textContent = data.chargeCount || '--';
                document.getElementById('mfgDate').textContent = data.mfgDate || '--';
                document.getElementById('capacity').textContent = data.capacity ? data.capacity + ' Ah' : '--';
                document.getElementById('errorCode').textContent = data.errorCode;
                document.getElementById('packVoltage').textContent = data.packVoltage.toFixed(2) + ' V';

                for (let i = 1; i <= 5; i++) {
                    const el = document.getElementById('cell' + i);
                    const v = data['cell' + i];
                    el.textContent = v.toFixed(3) + ' V';
                    setCellClass(el, v);
                }

                document.getElementById('cellDiff').textContent = data.cellDiff.toFixed(3) + ' V';
                document.getElementById('tempCell').textContent = data.tempCell.toFixed(1) + ' °C';
                document.getElementById('tempMosfet').textContent = data.tempMosfet ? data.tempMosfet.toFixed(1) + ' °C' : '--';

                setStatus('Battery connected: ' + (data.model || 'Unknown'), 'ok');
                log('Battery read successful: ' + data.model);
            } else {
                setStatus('Failed to read battery', 'error');
                log('Battery read failed');
            }
        }

        async function readVoltages() {
            log('Reading voltages...');

            const data = await apiCall('voltages');
            if (data && data.success) {
                document.getElementById('packVoltage').textContent = data.packVoltage.toFixed(2) + ' V';

                for (let i = 1; i <= 5; i++) {
                    const el = document.getElementById('cell' + i);
                    const v = data['cell' + i];
                    el.textContent = v.toFixed(3) + ' V';
                    setCellClass(el, v);
                }

                document.getElementById('cellDiff').textContent = data.cellDiff.toFixed(3) + ' V';
                document.getElementById('tempCell').textContent = data.tempCell.toFixed(1) + ' °C';
                document.getElementById('tempMosfet').textContent = data.tempMosfet ? data.tempMosfet.toFixed(1) + ' °C' : '--';
                log('Voltages read successful');
            } else {
                log('Voltage read failed');
            }
        }

        async function testLeds(on) {
            log('LEDs ' + (on ? 'ON' : 'OFF'));
            await apiCall('leds?state=' + (on ? '1' : '0'));
        }

        async function resetErrors() {
            if (confirm('Are you sure you want to reset battery errors?')) {
                log('Resetting errors...');
                const data = await apiCall('reset');
                if (data && data.success) {
                    log('Errors reset successfully');
                    alert('Errors reset. Re-read battery to verify.');
                } else {
                    log('Error reset failed');
                }
            }
        }

        // Initial status check
        log('Checking connection...');
    </script>
</body>
</html>
)rawliteral";

#endif // WEB_INTERFACE_H
