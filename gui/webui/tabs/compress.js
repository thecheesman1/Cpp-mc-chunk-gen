// Compress tab — zstd/zlib compression comparison
document.addEventListener('DOMContentLoaded', () => {
    const method = document.getElementById('comp-method');
    const level = document.getElementById('comp-level');
    const btn = document.querySelector('#compress-root .btn-primary');

    // Update level range based on method
    method.addEventListener('change', () => {
        if (method.value === 'zstd') {
            level.max = 22;
            level.placeholder = '1-22';
        } else if (method.value === 'zlib') {
            level.max = 9;
            level.placeholder = '1-9';
        }
    });

    btn.addEventListener('click', async () => {
        btn.disabled = true;
        btn.textContent = 'Processing...';

        try {
            const result = await API.compress({
                method: method.value,
                level: parseInt(level.value) || 3
            });

            if (result.error) {
                alert('Error: ' + result.error);
            } else {
                alert('Compression complete!\n\n' +
                    'Original: ' + fmtBytes(result.original_size || 0) + '\n' +
                    'Compressed: ' + fmtBytes(result.compressed_size || 0) + '\n' +
                    'Ratio: ' + (result.ratio || 0).toFixed(2) + 'x\n' +
                    'Time: ' + (result.time || 0).toFixed(1) + 'ms');
            }
        } catch(e) {
            alert('Error: ' + e.message);
        }

        btn.disabled = false;
        btn.textContent = 'Start Post-Process';
    });
});