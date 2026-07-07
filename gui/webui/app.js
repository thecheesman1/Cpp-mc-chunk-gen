// mcchunkgen — main app logic
// Tab switching, API client, shared helpers

// ================================================================
// Tab navigation
// ================================================================
document.querySelectorAll('.nav-tab').forEach(btn => {
    btn.addEventListener('click', () => {
        const tab = btn.dataset.tab;
        if (!tab) return;

        // Update sidebar
        document.querySelectorAll('.nav-tab').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');

        // Update content
        document.querySelectorAll('.tab-content').forEach(tc => tc.classList.remove('active'));
        const target = document.getElementById('tab-' + tab);
        if (target) target.classList.add('active');
    });
});

// ================================================================
// API client
// ================================================================
const API = {
    async get(path) {
        const r = await fetch(path);
        return r.json();
    },
    async post(path, body = {}) {
        const r = await fetch(path, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body)
        });
        return r.json();
    },
    async ping() {
        return this.get('/api/ping');
    },
    async status() {
        return this.get('/api/status');
    },
    async generate(params) {
        return this.post('/api/generate', params);
    },
    async inspect(path) {
        return this.get('/api/inspect?path=' + encodeURIComponent(path));
    },
    async benchmark() {
        return this.post('/api/benchmark');
    },
    async compress(params) {
        return this.post('/api/compress', params);
    },
    async serverStart() {
        return this.post('/api/server/start');
    },
    async serverStop() {
        return this.post('/api/server/stop');
    }
};

// ================================================================
// Shared helpers
// ================================================================
function $(sel) { return document.querySelector(sel); }
function $$(sel) { return document.querySelectorAll(sel); }

function escape(str) {
    const d = document.createElement('div');
    d.textContent = str;
    return d.innerHTML;
}

// Format bytes to human-readable
function fmtBytes(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
}

// Poll status periodically
let statusInterval = null;

function startStatusPoll(interval = 2000) {
    stopStatusPoll();
    statusInterval = setInterval(async () => {
        try {
            const s = await API.status();
            // Update active tab with status
            document.dispatchEvent(new CustomEvent('status', { detail: s }));
        } catch (e) {
            // backend not responding
        }
    }, interval);
}

function stopStatusPoll() {
    if (statusInterval) {
        clearInterval(statusInterval);
        statusInterval = null;
    }
}

// Start polling on load
document.addEventListener('DOMContentLoaded', () => {
    startStatusPoll();
});