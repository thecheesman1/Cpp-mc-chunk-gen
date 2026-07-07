// Server tab — Fabric/MC server control
let serverPollInterval = null;

function serverLog(text, cls = 'info') {
    const log = document.getElementById('server-log');
    if (!log) return;
    const line = document.createElement('span');
    line.className = cls;
    line.textContent = text;
    log.appendChild(line);
    log.scrollTop = log.scrollHeight;
}

document.addEventListener('DOMContentLoaded', () => {
    const startBtn = document.getElementById('btn-server-start');
    const stopBtn = document.getElementById('btn-server-stop');
    const statusDot = document.getElementById('server-status-dot');
    const statusText = document.getElementById('server-status-text');

    startBtn.addEventListener('click', async () => {
        startBtn.disabled = true;
        serverLog('[SYSTEM] Launching Fabric server instance...', 'info');

        try {
            const r = await API.serverStart();
            if (r.ok) {
                statusDot.className = 'status-dot on';
                statusText.textContent = 'SERVER RUNNING';
                statusText.style.color = 'var(--green)';
                startBtn.disabled = true;
                stopBtn.disabled = false;
                serverLog('[SYSTEM] Server started successfully', 'ok');

                // Poll server status
                serverPollInterval = setInterval(async () => {
                    try {
                        const s = await API.status();
                        if (!s.server_running) {
                            clearInterval(serverPollInterval);
                            serverPollInterval = null;
                            statusDot.className = 'status-dot off';
                            statusText.textContent = 'SERVER STOPPED';
                            statusText.style.color = '';
                            startBtn.disabled = false;
                            stopBtn.disabled = true;
                            serverLog('[SYSTEM] Server terminated', 'warn');
                        }
                    } catch(e) { /* backend offline */ }
                }, 2000);
            } else {
                serverLog('[ERROR] ' + (r.error || 'Failed to start'), 'error');
                startBtn.disabled = false;
            }
        } catch(e) {
            serverLog('[ERROR] ' + e.message, 'error');
            startBtn.disabled = false;
        }
    });

    stopBtn.addEventListener('click', async () => {
        stopBtn.disabled = true;
        serverLog('[SYSTEM] Terminating server instance...', 'warn');

        try {
            const r = await API.serverStop();
            if (r.ok) {
                statusDot.className = 'status-dot off';
                statusText.textContent = 'SERVER STOPPED';
                statusText.style.color = '';
                startBtn.disabled = false;
                stopBtn.disabled = true;
                serverLog('[SYSTEM] Server terminated', 'warn');
            } else {
                serverLog('[ERROR] ' + (r.error || 'Failed to stop'), 'error');
                stopBtn.disabled = false;
            }
        } catch(e) {
            serverLog('[ERROR] ' + e.message, 'error');
            stopBtn.disabled = false;
        }
    });
});