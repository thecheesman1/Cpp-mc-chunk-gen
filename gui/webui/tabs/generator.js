// Generator tab — split-pane layout
let genPollInterval = null;

function appendTerminal(text, cls = 'text-dim') {
    const term = document.getElementById('gen-terminal');
    if (!term) return;
    const line = document.createElement('div');
    line.className = cls;
    line.textContent = text;
    term.appendChild(line);
    term.scrollTop = term.scrollHeight;
}

function updateMonitor(stats) {
    const cps = document.getElementById('mon-cps');
    const chunks = document.getElementById('mon-chunks');
    const time = document.getElementById('mon-time');
    const pct = document.getElementById('mon-percent');
    const bar = document.getElementById('mon-bar');
    if (!cps) return;

    const total = stats.chunks_total || 1;
    const done = stats.chunks_done || 0;
    const p = Math.min(100, (done / total) * 100);

    cps.textContent = (stats.cps || 0).toFixed(1);
    chunks.textContent = done + ' / ' + total.toLocaleString();
    time.textContent = (stats.elapsed || 0).toFixed(1) + 's';
    pct.textContent = Math.round(p) + '%';
    bar.style.width = p + '%';
}

document.addEventListener('DOMContentLoaded', () => {
    const startBtn = document.getElementById('btn-start-gen');
    const copyBtn = document.getElementById('btn-export-cli');

    startBtn.addEventListener('click', async () => {
        startBtn.disabled = true;
        startBtn.innerHTML = '<svg width="14" height="14" viewBox="0 0 16 16" fill="currentColor"><circle cx="8" cy="8" r="3"/></svg> Running...';

        appendTerminal('$ ./chunkgen_offline --world ' + document.getElementById('world-path').value +
            ' --seed ' + document.getElementById('seed').value +
            ' --radius ' + document.getElementById('radius').value +
            ' --threads ' + document.getElementById('threads').value, 'system-line');
        appendTerminal('[McChunkGen] Starting generation...');

        try {
            const result = await API.generate({
                seed: parseInt(document.getElementById('seed').value),
                radius: parseInt(document.getElementById('radius').value),
                center_x: parseInt(document.getElementById('center-x').value),
                center_z: parseInt(document.getElementById('center-z').value),
                threads: parseInt(document.getElementById('threads').value),
                world_path: document.getElementById('world-path').value,
                backend: document.getElementById('backend').value,
                quiet: document.getElementById('quiet-mode').checked
            });

            if (result.error) {
                appendTerminal('[ERROR] ' + result.error, 'error');
                startBtn.disabled = false;
                startBtn.innerHTML = '<svg width="14" height="14" viewBox="0 0 16 16" fill="currentColor"><path d="M11.596 8.697l-6.363 3.692c-.54.313-1.233-.066-1.233-.697V4.308c0-.63.692-1.01 1.233-.696l6.363 3.692a.802.802 0 0 1 0 1.393z"/></svg> Execute Run';
                return;
            }

            appendTerminal('[McChunkGen] Generating ' + (result.total || '?') + ' chunks...', 'info');

            // Poll progress
            genPollInterval = setInterval(async () => {
                try {
                    const status = await API.status();
                    updateMonitor(status);

                    if (!status.generating) {
                        clearInterval(genPollInterval);
                        genPollInterval = null;
                        updateMonitor({ chunks_done: status.chunks_total, chunks_total: status.chunks_total,
                            cps: status.cps, elapsed: status.elapsed });
                        appendTerminal('[McChunkGen] Done! ' + status.chunks_total + ' chunks generated.', 'system-line');
                        startBtn.disabled = false;
                        startBtn.innerHTML = '<svg width="14" height="14" viewBox="0 0 16 16" fill="currentColor"><path d="M11.596 8.697l-6.363 3.692c-.54.313-1.233-.066-1.233-.697V4.308c0-.63.692-1.01 1.233-.696l6.363 3.692a.802.802 0 0 1 0 1.393z"/></svg> Execute Run';
                    }
                } catch(e) { /* backend offline */ }
            }, 500);
        } catch(e) {
            appendTerminal('[ERROR] ' + e.message, 'error');
            startBtn.disabled = false;
            startBtn.innerHTML = '<svg width="14" height="14" viewBox="0 0 16 16" fill="currentColor"><path d="M11.596 8.697l-6.363 3.692c-.54.313-1.233-.066-1.233-.697V4.308c0-.63.692-1.01 1.233-.696l6.363 3.692a.802.802 0 0 1 0 1.393z"/></svg> Execute Run';
        }
    });

    copyBtn.addEventListener('click', () => {
        const w = document.getElementById('world-path').value;
        const s = document.getElementById('seed').value;
        const r = document.getElementById('radius').value;
        const cx = document.getElementById('center-x').value;
        const cz = document.getElementById('center-z').value;
        const t = document.getElementById('threads').value;
        const cmd = `chunkgen_offline --world "${w}" --seed ${s} --radius ${r} --center-x ${cx} --center-z ${cz} --threads ${t}`;
        navigator.clipboard.writeText(cmd).then(() => {
            appendTerminal('[CLI] Command copied to clipboard.', 'info');
        }).catch(() => {});
    });
});