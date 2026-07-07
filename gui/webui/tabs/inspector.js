// Inspector tab UI
function renderInspector() {
    const root = document.getElementById('inspector-root');
    root.innerHTML = `
        <div class="card">
            <div class="card-title">Open Region File</div>
            <div class="card-row">
                <div class="field-group">
                    <label>Path to .mca file</label>
                    <input type="text" id="insp-path" placeholder="world/region/r.0.0.mca">
                </div>
                <button class="btn btn-primary" id="insp-load">Inspect</button>
            </div>
        </div>
        <div id="insp-results"></div>
    `;

    document.getElementById('insp-load').addEventListener('click', async () => {
        const path = document.getElementById('insp-path').value.trim();
        if (!path) return;

        const results = document.getElementById('insp-results');
        results.innerHTML = '<p class="text-dim">Loading...</p>';

        try {
            const data = await API.inspect(path);
            if (data.error) {
                results.innerHTML = `<div class="card"><span class="badge badge-err">Error</span> ${escape(data.error)}</div>`;
                return;
            }

            // Build results display
            let html = '<div class="stat-grid" style="margin-bottom:16px;">';
            html += `<div class="stat-card"><div class="stat-value">${data.chunk_count || 0}</div><div class="stat-label">Chunks</div></div>`;
            html += `<div class="stat-card"><div class="stat-value">${data.data_version || '?'}</div><div class="stat-label">DataVersion</div></div>`;
            html += `<div class="stat-card"><div class="stat-value">${data.sections || '?'}</div><div class="stat-label">Sections</div></div>`;
            html += `<div class="stat-card"><div class="stat-value">${fmtBytes(data.file_size || 0)}</div><div class="stat-label">File Size</div></div>`;
            html += '</div>';

            // Validation
            let valid = true;
            let msgs = [];
            if (data.data_version && data.data_version !== 4671) {
                valid = false;
                msgs.push('DataVersion should be 4671 for 1.21.11 (got ' + data.data_version + ')');
            }
            if (data.sections && data.sections !== 24) {
                valid = false;
                msgs.push('Should have 24 sections for 1.21 world (got ' + data.sections + ')');
            }

            if (msgs.length > 0) {
                html += '<div class="card" style="border-color:var(--red);">';
                html += '<div class="card-title" style="color:var(--red);">Issues Found</div>';
                msgs.forEach(m => {
                    html += '<div class="flex items-center gap-8" style="margin-bottom:4px;">';
                    html += '<span class="badge badge-err">!</span>';
                    html += '<span>' + escape(m) + '</span></div>';
                });
                html += '</div>';
            } else {
                html += '<div class="card" style="border-color:var(--green);">';
                html += '<div class="flex items-center gap-8">';
                html += '<span class="badge badge-ok">✓</span>';
                html += '<span>Chunk format looks valid</span></div>';
                html += '</div>';
            }

            results.innerHTML = html;
        } catch(e) {
            results.innerHTML = `<div class="card"><span class="badge badge-err">Error</span> ${escape(e.message)}</div>`;
        }
    });
}

document.addEventListener('DOMContentLoaded', renderInspector);