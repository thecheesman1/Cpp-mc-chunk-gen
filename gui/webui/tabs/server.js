// Server tab UI
function renderServer() {
    const root = document.getElementById('server-root');
    root.innerHTML = `
        <div class="card">
            <div class="card-title">Server Status</div>
            <div class="flex items-center gap-12">
                <span id="srv-indicator">
                    <span class="status-dot off"></span>
                    <span class="text-dim">Stopped</span>
                </span>
                <button class="btn btn-primary btn-sm" id="srv-start">Start Server</button>
                <button class="btn btn-danger btn-sm" id="srv-stop" disabled>Stop</button>
            </div>
        </div>

        <div class="card">
            <div class="card-title">Server Configuration</div>
            <div class="card-row">
                <div class="field-group small">
                    <label>Port</label>
                    <input type="number" id="srv-port" value="25565">
                </div>
                <div class="field-group small">
                    <label>RAM (MB)</label>
                    <input type="number" id="srv-ram" value="1024">
                </div>
                <div class="field-group">
                    <label>Server JAR</label>
                    <input type="text" id="srv-jar" value="server.jar">
                </div>
            </div>
            <div class="card-row">
                <div class="field-group">
                    <label>World Path</label>
                    <input type="text" id="srv-world" value="world">
                </div>
                <div class="field-group small">
                    <label>View Distance</label>
                    <input type="number" id="srv-viewdist" value="8" min="2" max="32">
                </div>
            </div>
            <div class="card-row">
                <div class="field-group" style="display:flex;align-items:center;gap:12px;">
                    <label style="margin:0;text-transform:none;letter-spacing:0;display:flex;align-items:center;gap:6px;cursor:pointer;color:var(--text-dim);">
                        <input type="checkbox" id="srv-online" checked> Online Mode
                    </label>
                    <label style="margin:0;text-transform:none;letter-spacing:0;display:flex;align-items:center;gap:6px;cursor:pointer;color:var(--text-dim);">
                        <input type="checkbox" id="srv-genstructures" checked> Generate Structures
                    </label>
                </div>
            </div>
        </div>

        <div class="card">
            <div class="card-title">Server Log</div>
            <div class="log-box" id="srv-log">Server not running. Click Start to launch.</div>
        </div>
    `;

    // Event handlers
    document.getElementById('srv-start').addEventListener('click', async () => {
        const ind = document.getElementById('srv-indicator');
        const logBox = document.getElementById('srv-log');
        const startBtn = document.getElementById('srv-start');
        const stopBtn = document.getElementById('srv-stop');

        try {
            const r = await API.serverStart();
            if (r.ok) {
                ind.innerHTML = '<span class="status-dot on"></span><span style="color:var(--green);">Running</span>';
                logBox.textContent = '[SERVER] Starting on port ' + document.getElementById('srv-port').value + '...\n';
                logBox.innerHTML += '<span class="ok">[SERVER] Started successfully</span>\n';
                startBtn.disabled = true;
                stopBtn.disabled = false;
            }
        } catch(e) {
            logBox.innerHTML += '<span class="error">[ERROR] ' + escape(e.message) + '</span>\n';
        }
    });

    document.getElementById('srv-stop').addEventListener('click', async () => {
        const ind = document.getElementById('srv-indicator');
        const logBox = document.getElementById('srv-log');
        const startBtn = document.getElementById('srv-start');
        const stopBtn = document.getElementById('srv-stop');

        try {
            const r = await API.serverStop();
            ind.innerHTML = '<span class="status-dot off"></span><span class="text-dim">Stopped</span>';
            logBox.innerHTML += '<span class="warn">[SERVER] Stopped</span>\n';
            startBtn.disabled = false;
            stopBtn.disabled = true;
        } catch(e) {
            logBox.innerHTML += '<span class="error">[ERROR] ' + escape(e.message) + '</span>\n';
        }
    });
}

document.addEventListener('DOMContentLoaded', renderServer);