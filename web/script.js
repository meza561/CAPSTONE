document.addEventListener('DOMContentLoaded', () => {
    const canvas = document.getElementById('heatCanvas');
    const ctx = canvas.getContext('2d');
    const runBtn = document.getElementById('runBtn');
    const statusText = document.getElementById('status');

    const inputs = {
        rows: document.getElementById('rows'),
        cols: document.getElementById('cols'),
        top: document.getElementById('top'),
        bottom: document.getElementById('bottom'),
        left: document.getElementById('left'),
        right: document.getElementById('right'),
    };

    async function fetchHeatmap() {
        try {
            // Since the C++ binary runs on the host and saves to a file,
            // the JS frontend reads the resulting JSON file served by the local server.
            const response = await fetch('../latest_heatmap.json');
            if (!response.ok) throw new Error('Data file not found. Run the simulation first.');
            return await response.json();
        } catch (e) {
            throw e;
        }
    }

    function getColorForTemp(temp) {
        // Normalize temp between 0 and 100
        const t = Math.max(0, Math.min(1, temp / 100));
        
        // Simple HSL mapping: 240 (blue) to 0 (red)
        const hue = (1 - t) * 240;
        return `hsl(${hue}, 100%, 50%)`;
    }

    function drawHeatmap(data) {
        const { rows, cols, data: grid } = data;
        
        // Set canvas resolution to match grid
        canvas.width = cols;
        canvas.height = rows;

        const imageData = ctx.createImageData(cols, rows);

        for (let i = 0; i < rows; i++) {
            for (let j = 0; j < cols; j++) {
                const temp = grid[i][j];
                
                // Use a custom color mapping for imageData (RGBA)
                // For simplicity, we'll use a basic blue -> red gradient
                const t = Math.max(0, Math.min(1, temp / 100));
                const r = Math.floor(t * 255);
                const b = Math.floor((1 - t) * 255);
                const g = Math.floor((1 - Math.abs(t - 0.5) * 2) * 100);

                const index = (i * cols + j) * 4;
                imageData.data[index] = r;     // R
                imageData.data[index + 1] = g; // G
                imageData.data[index + 2] = b; // B
                imageData.data[index + 3] = 255; // A
            }
        }
        ctx.putImageData(imageData, 0, 0);

        // Upscale the canvas for visibility
        canvas.style.width = `${cols * 20}px`;
        canvas.style.height = `${rows * 20}px`;
    }

    async function updateView() {
        statusText.textContent = 'Fetching data...';
        try {
            const data = await fetchHeatmap();
            drawHeatmap(data);
            statusText.textContent = `Loaded step ${data.step} (${data.rows}x${data.cols})`;
        } catch (e) {
            statusText.textContent = `Error: ${e.message}`;
        }
    }

    runBtn.addEventListener('click', async () => {
        statusText.textContent = 'Triggering simulation...';
        
        /**
         * INTEGRATION NOTE:
         * In a production environment, this button would call a backend API (Node/Python) 
         * that executes the C++ binary with the provided parameters.
         * Since this is a local academic tool, the user is expected to run the binary via CLI
         * or the project owner can add a small Python wrapper.
         * 
         * For this version, we simulate the trigger and tell the user to run the binary.
         */
        
        const cmd = `./heat_sim ${inputs.rows.value} ${inputs.cols.value} ${inputs.top.value} ${inputs.bottom.value} ${inputs.left.value} ${inputs.right.value}`;
        
        alert(`To run this simulation, please execute the following in your terminal:\n\n${cmd}`);
        
        // Attempt to load the updated file after a short delay
        setTimeout(updateView, 1000);
    });

    // Initial load
    updateView();
});
