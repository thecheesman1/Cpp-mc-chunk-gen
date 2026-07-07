// Benchmark tab UI
function renderBenchmark() {
    const root = document.getElementById('benchmark-root');
    root.innerHTML = `
        <div class="card">
            <div class="card-title">Benchmark Configuration</div>
            <div class="card-row">
                <div class="field-group small">
                    <label>Seed</label>
                    <input type="number" id="bench-seed" value="42">
                </div>
                <div class="field-group small">
                    <label>Radius</label>
                    <input type="number" id="bench-radius" value="16" min="1" max="64">
                </div>
                <div class="field-group small">
                    <label>Threads</label>
                    <input type="number" id="bench-threads" value="4" min="1" max="32">
                </div>
                <div class="field-group" style="flex:0 0 160px;">
                    <label>Backend</label>
                    <select id="bench-backend">
                        <option value="cuda">CUDA (GPU)</option>
                        <option value="vulkan">Vulkan (GPU)</option>
                        <option value="cpu">CPU</option>
                    </select>
                </div>
            </div>
            <button class="btn btn-primary" id="bench-start">Run Benchmark</button>
        </div>

        <div class="card" id="bench-progress" style="display:none;">
            <div class="card-title">Running...</div>
            <div class="progress-bar"><div class="progress-fill" id="bench-fill"></div></div>
            <div id="bench-status" class="progress-text"></div>
        </div>

        <div id="bench-results"></div>
    `;

    document.getElementById('bench-start').addEventListener('click', async () => {
        const btn = document.getElementById('bench-start');
        btn.disabled = true;
        btn.textContent = 'Running...';

        const prog = document.getElementById('bench-progress');
        prog.style.display = 'block';
        const fill = document.getElementById('bench-fill');
        const status = document.getElementById('bench-status');

        // Simulated benchmark for now (replace with real API call)
        const seed = parseInt(document.getElementById('bench-seed').value);
        const radius = parseInt(document.getElementById('bench-radius').value);
        const total = (2 * radius + 1) * (2 * radius + 1);

        for (let i = 0; i <= total; i += Math.max(1, Math.floor(total / 20))) {
            const pct = Math.min(100, (i / total) * 100);
            fill.style.width = pct + '%';
            status.textContent = `Generated ${Math.min(i, total)} / ${total} chunks...`;
            await new Promise(r => setTimeout(r, 50));
        }

        fill.style.width = '100%';
        status.textContent = 'Done!';

        // Show mock results
        const results = document.getElementById('bench-results');
        results.innerHTML = `
            <div class="stat-grid">
                <div class="stat-card"><div class="stat-value">4676</div><div class="stat-label">CPS</div></div>
                <div class="stat-card"><div class="stat-value">${total}</div><div class="stat-label">Chunks</div></div>
                <div class="stat-card"><div class="stat-value">${(total / 4676).toFixed(1)}s</div><div class="stat-label">Time</div></div>
                <div class="stat-card"><div class="stat-value">93.5x</div><div class="stat-label">vs Chunky</div></div>
            </div>
            <div class="card mt-16">
                <div class="card-title">Details</div>
                <pre class="text-mono" style="font-size:12px;line-height:1.8;">
Benchmark: seed=${seed}, radius=${radius}, ${total} chunks
Backend: CUDA
GPU: NVIDIA RTX 4050 (Ada Lovelace)
CPS: 4676 chunks/second
Speedup vs Chunky (50 CPS): 93.5x
                </pre>
            </div>
        `;

        btn.disabled = false;
        btn.textContent = 'Run Benchmark';
    });
}

document.addEventListener('DOMContentLoaded', renderBenchmark);