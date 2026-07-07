// Compress tab UI
function renderCompress() {
    const root = document.getElementById('compress-root');
    root.innerHTML = `
        <div class="card">
            <div class="card-title">Compression Test</div>
            <div class="card-row">
                <div class="field-group">
                    <label>Region File</label>
                    <input type="text" id="comp-path" placeholder="world/region/r.0.0.mca">
                </div>
                <button class="btn btn-primary" id="comp-test">Test</button>
            </div>
        </div>

        <div class="card" style="display:none;" id="comp-results">
            <div class="card-title">Results</div>
            <div class="stat-grid" id="comp-grid"></div>
            <div style="margin-top:12px;">
                <table style="width:100%;border-collapse:collapse;font-family:var(--font-mono);font-size:12px;">
                    <thead>
                        <tr style="color:var(--text-muted);border-bottom:1px solid var(--border);">
                            <th style="padding:6px 8px;text-align:left;">Method</th>
                            <th style="padding:6px 8px;text-align:right;">Size</th>
                            <th style="padding:6px 8px;text-align:right;">vs Uncomp</th>
                            <th style="padding:6px 8px;text-align:right;">Time</th>
                        </tr>
                    </thead>
                    <tbody id="comp-table-body"></tbody>
                </table>
            </div>
        </div>
    `;

    document.getElementById('comp-test').addEventListener('click', async () => {
        const path = document.getElementById('comp-path').value.trim();
        if (!path) return;

        const results = document.getElementById('comp-results');
        results.style.display = 'block';
        const grid = document.getElementById('comp-grid');
        const tbody = document.getElementById('comp-table-body');

        // Mock results for now
        // 45091 bytes uncompressed
        const uncomp = 45091;
        const methods = [
            { name: 'Uncompressed', size: uncomp, time: 0 },
            { name: 'Zlib (level 6)', size: 8924, time: 1.2 },
            { name: 'Zstd (level 3)', size: 8142, time: 0.8 },
            { name: 'Zstd (level 9)', size: 7706, time: 1.5 },
            { name: 'Zstd (level 19)', size: 7481, time: 4.2 },
            { name: 'Zstd (level 22)', size: 7312, time: 8.1 },
        ];

        grid.innerHTML = `
            <div class="stat-card"><div class="stat-value">${fmtBytes(uncomp)}</div><div class="stat-label">Uncompressed</div></div>
            <div class="stat-card"><div class="stat-value">${fmtBytes(methods[1].size)}</div><div class="stat-label">Zlib Best</div></div>
            <div class="stat-card"><div class="stat-value">${fmtBytes(methods[5].size)}</div><div class="stat-label">Zstd Best</div></div>
            <div class="stat-card"><div class="stat-value">${(uncomp / methods[5].size).toFixed(1)}x</div><div class="stat-label">Zstd Ratio</div></div>
        `;

        let rows = '';
        methods.forEach(m => {
            const ratio = m.size > 0 ? (uncomp / m.size).toFixed(2) + 'x' : '-';
            const pct = m.size > 0 ? (100 - (m.size / uncomp) * 100).toFixed(1) + '%' : '-';
            rows += `<tr style="border-bottom:1px solid var(--border);">
                <td style="padding:6px 8px;">${m.name}</td>
                <td style="padding:6px 8px;text-align:right;">${fmtBytes(m.size)}</td>
                <td style="padding:6px 8px;text-align:right;color:var(--green);">${ratio}</td>
                <td style="padding:6px 8px;text-align:right;">${m.time > 0 ? m.time + 'ms' : '-'}</td>
            </tr>`;
        });
        tbody.innerHTML = rows;
    });
}

document.addEventListener('DOMContentLoaded', renderCompress);