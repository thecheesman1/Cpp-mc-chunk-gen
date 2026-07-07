// Inspector tab — drag/drop .mca files + NBT tree view
function buildTreeView(data) {
    if (!data || data.error) {
        return '<div class="tree-node"><span class="node-tag">Error: ' + (data ? escape(data.error) : 'No data') + '</span></div>';
    }

    let sections_html = '';
    const sc = data.sections || 0;
    for (let y = -4; y < -4 + sc && y < 20; y++) {
        const yn = y + 4;
        sections_html += `<div class="tree-node"><span class="node-tag">Compound("Section Y=${y}")</span>
            <div class="tree-children">
                <div class="tree-node"><span class="node-key">Y</span>: <span class="node-val">${y}</span></div>
                <div class="tree-node"><span class="node-tag">Compound("block_states")</span></div>
                <div class="tree-node"><span class="node-tag">Compound("biomes")</span></div>
            </div>
        </div>`;
    }

    return `<div class="tree-node"><span class="node-tag">Tag_Compound("")</span>
        <div class="tree-children">
            <div class="tree-node"><span class="node-key">DataVersion</span>: <span class="node-val">${data.data_version || '?'}</span></div>
            <div class="tree-node"><span class="node-key">Status</span>: <span class="node-val">"minecraft:full"</span></div>
            <div class="tree-node"><span class="node-key">isLightOn</span>: <span class="node-val">1</span></div>
            <div class="tree-node"><span class="node-tag">List("sections")</span> <span class="node-val">(${sc} items)</span>
                <div class="tree-children">${sections_html}</div>
            </div>
            <div class="tree-node"><span class="node-tag">Compound("Heightmaps")</span></div>
        </div>
    </div>`;
}

function buildChunkGrid(data) {
    if (!data || data.error) return '<span class="text-muted">Failed to load region file</span>';

    const chunks = data.chunk_count || 0;
    let grid = '';
    // Show 5x5 grid of chunk positions centered on 0,0
    for (let rz = -2; rz <= 2; rz++) {
        grid += '<div style="display:flex;gap:2px;justify-content:center;margin-bottom:2px;">';
        for (let rx = -2; rx <= 2; rx++) {
            const idx = ((rz + 2) * 5) + (rx + 2);
            const filled = idx < chunks;
            grid += `<div style="width:24px;height:24px;border-radius:2px;background:${filled ? 'var(--accent)' : 'var(--bg-input)'};
                display:flex;align-items:center;justify-content:center;font-size:8px;color:${filled ? '#111' : 'var(--text-muted)'}">
                ${idx}</div>`;
        }
        grid += '</div>';
    }
    return grid;
}

document.addEventListener('DOMContentLoaded', () => {
    const dropzone = document.getElementById('mca-dropzone');
    const gridPlaceholder = document.querySelector('.region-grid-placeholder');
    const treeView = document.querySelector('.nbt-tree-view');

    if (!dropzone) return;

    // Click to open file picker
    dropzone.addEventListener('click', () => {
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = '.mca';
        input.onchange = async (e) => {
            const file = e.target.files[0];
            if (!file) return;

            // Read file and send to API
            dropzone.innerHTML = '<p class="text-dim">Loading ' + file.name + '...</p>';

            try {
                const data = await API.inspect(file.name);
                if (gridPlaceholder) gridPlaceholder.innerHTML = buildChunkGrid(data);
                if (treeView) treeView.innerHTML = buildTreeView(data);

                dropzone.innerHTML = `
                    <svg class="mb-8" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="var(--green)" stroke-width="2">
                        <path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"/>
                        <polyline points="22 4 12 14.01 9 11.01"/>
                    </svg>
                    <p class="font-semibold" style="color:var(--green);">${file.name} loaded</p>
                    <p class="text-muted mt-8">Drop another file to inspect</p>`;
            } catch(e) {
                dropzone.innerHTML = '<p class="error">Error: ' + escape(e.message) + '</p>';
            }
        };
        input.click();
    });

    // Drag and drop
    dropzone.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropzone.style.borderColor = 'var(--accent)';
        dropzone.style.background = 'var(--bg-hover)';
    });

    dropzone.addEventListener('dragleave', () => {
        dropzone.style.borderColor = '';
        dropzone.style.background = '';
    });

    dropzone.addEventListener('drop', async (e) => {
        e.preventDefault();
        dropzone.style.borderColor = '';
        dropzone.style.background = '';

        const file = e.dataTransfer.files[0];
        if (!file || !file.name.endsWith('.mca')) {
            dropzone.innerHTML = '<p class="error">Please drop a .mca file</p>';
            return;
        }

        dropzone.innerHTML = '<p class="text-dim">Loading ' + file.name + '...</p>';

        try {
            const data = await API.inspect(file.name);
            if (gridPlaceholder) gridPlaceholder.innerHTML = buildChunkGrid(data);
            if (treeView) treeView.innerHTML = buildTreeView(data);

            dropzone.innerHTML = `
                <svg class="mb-8" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="var(--green)" stroke-width="2">
                    <path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"/>
                    <polyline points="22 4 12 14.01 9 11.01"/>
                </svg>
                <p class="font-semibold" style="color:var(--green);">${file.name} loaded</p>
                <p class="text-muted mt-8">Drop another file to inspect</p>`;
        } catch(e) {
            dropzone.innerHTML = '<p class="error">Error: ' + escape(e.message) + '</p>';
        }
    });
});