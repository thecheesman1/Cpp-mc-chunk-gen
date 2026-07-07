// Generator tab UI
function renderGenerator() {
    const root = document.getElementById('generator-root');
    root.innerHTML = `
        <div class="card">
            <div class="card-title">World Settings</div>
            <div class="card-row">
                <div class="field-group">
                    <label>Seed</label>
                    <input type="number" id="gen-seed" value="42">
                </div>
                <div class="field-group small">
                    <label>Radius</label>
                    <input type="number" id="gen-radius" value="5" min="1" max="64">
                </div>
                <div class="field-group small">
                    <label>Threads</label>
                    <input type="number" id="gen-threads" value="4" min="1" max="32">
                </div>
            </div>
            <div class="card-row">
                <div class="field-group small">
                    <label>Center X</label>
                    <input type="number" id="gen-cx" value="0">
                </div>
                <div class="field-group small">
                    <label>Center Z</label>
                    <input type="number" id="gen-cz" value="0">
                </div>
                <div class="field-group">
                    <label>World Path</label>
                    <input type="text" id="gen-path" value="world">
                </div>
            </div>
            <div class="card-row" style="margin-top: 8px;">
                <button class="btn btn-primary" id="gen-start">Generate</button>
                <label style="display:flex;align-items:center;gap:6px;text-transform:none;letter-spacing:0;font-size:13px;cursor:pointer;margin:0;color:var(--text-dim);">
                    <input type="checkbox" id="gen-vulkan"> Use Vulkan
                </label>
            </div>
        </div>

        <div class="card" id="gen-progress-card" style="display:none;">
            <div class="card-title">Progress</div>
            <div class="progress-bar"><div class="progress-fill" id="gen-progress-fill"></div></div>
            <div class="flex items-center gap-8">
                <span id="gen-progress-text" class="progress-text">0 / 0 chunks</span>
                <span id="gen-status-text" class="progress-text">idle</span>
            </div>
        </div>

        <div class="card" id="gen-result-card" style="display:none;">
            <div class="card-title">Result</div>
            <div id="gen-result" class="text-mono" style="white-space:pre-wrap;"></div>
        </div>
    `;

    // Event handlers
    document.getElementById('gen-start').addEventListener('click', async () => {
        const btn = document.getElementById('gen-start');
        btn.disabled = true;
        btn.textContent = 'Generating...';

        const progCard = document.getElementById('gen-progress-card');
        progCard.style.display = 'block';
        const fill = document.getElementById('gen-progress-fill');
        const ptext = document.getElementById('gen-progress-text');
        const stext = document.getElementById('gen-status-text');

        try {
            const result = await API.generate({
                seed: parseInt(document.getElementById('gen-seed').value),
                radius: parseInt(document.getElementById('gen-radius').value),
                center_x: parseInt(document.getElementById('gen-cx').value),
                center_z: parseInt(document.getElementById('gen-cz').value),
                threads: parseInt(document.getElementById('gen-threads').value),
                world_path: document.getElementById('gen-path').value,
                use_vulkan: document.getElementById('gen-vulkan').checked
            });

            if (result.error) {
                stext.textContent = result.error;
                stext.style.color = 'var(--red)';
                btn.disabled = false;
                btn.textContent = 'Generate';
                return;
            }

            // Poll progress
            const pollInterval = setInterval(async () => {
                try {
                    const status = await API.status();
                    const total = status.chunks_total || result.total || 1;
                    const done = status.chunks_done || 0;
                    const pct = Math.min(100, (done / total) * 100);

                    fill.style.width = pct + '%';
                    ptext.textContent = done + ' / ' + total + ' chunks';

                    if (!status.generating) {
                        clearInterval(pollInterval);
                        fill.style.width = '100%';
                        ptext.textContent = total + ' / ' + total + ' chunks';
                        stext.textContent = 'done ✓';
                        stext.style.color = 'var(--green)';
                        btn.disabled = false;
                        btn.textContent = 'Generate';
                    }
                } catch(e) {
                    clearInterval(pollInterval);
                }
            }, 500);
        } catch(e) {
            btn.disabled = false;
            btn.textContent = 'Generate';
            document.getElementById('gen-status-text').textContent = 'Error: ' + e.message;
            document.getElementById('gen-status-text').style.color = 'var(--red)';
        }
    });
}

// Initialize on page load
document.addEventListener('DOMContentLoaded', renderGenerator);