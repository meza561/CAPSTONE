document.addEventListener('DOMContentLoaded', () => {
    const canvas = document.getElementById('heatCanvas');
    const ctx = canvas.getContext('2d');
    const runBtn = document.getElementById('runBtn');
    const statusText = document.getElementById('status');
    
    const tabBtns = document.querySelectorAll('.tab-btn');
    const tabs = {
        'sim-tab': document.getElementById('sim-tab'),
        'history-tab': document.getElementById('history-tab')
    };

    const inputs = {
        mode: document.getElementById('simMode'),
        alpha: document.getElementById('alpha'),
        rows: document.getElementById('rows'),
        cols: document.getElementById('cols'),
        top: document.getElementById('top'),
        bottom: document.getElementById('bottom'),
        left: document.getElementById('left'),
        right: document.getElementById('right'),
    };

    const pdeParamsDiv = document.getElementById('pde-params');

    inputs.mode.addEventListener('change', () => {
        pdeParamsDiv.classList.toggle('hidden', inputs.mode.value !== 'pde');
    });

    tabBtns.forEach(btn => {
        btn.addEventListener('click', () => {
            tabBtns.forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            
            Object.keys(tabs).forEach(id => {
                tabs[id].classList.toggle('hidden', id !== btn.dataset.tab);
            });

            if (btn.dataset.tab === 'history-tab') {
                renderHistory();
            }
        });
    });

    function drawHeatmap(data) {
        const { rows, cols, data: grid } = data;
        canvas.width = cols;
        canvas.height = rows;
        const imageData = ctx.createImageData(cols, rows);

        for (let i = 0; i < rows; i++) {
            for (let j = 0; j < cols; j++) {
                const temp = grid[i][j];
                const t = Math.max(0, Math.min(1, temp / 100));
                const r = Math.floor(t > 0.5 ? (t - 0.5) * 2 * 255 : 0);
                const b = Math.floor(t < 0.5 ? (0.5 - t) * 2 * 255 : 0);
                const g = Math.floor((1 - Math.abs(t - 0.5) * 2) * 255);
                const index = (i * cols + j) * 4;
                imageData.data[index] = r;
                imageData.data[index + 1] = g;
                imageData.data[index + 2] = b;
                imageData.data[index + 3] = 255;
            }
        }
        ctx.putImageData(imageData, 0, 0);
    }

    async function runSimulation() {
        const params = {
            mode: inputs.mode.value,
            alpha: parseFloat(inputs.alpha.value),
            rows: parseInt(inputs.rows.value),
            cols: parseInt(inputs.cols.value),
            top: parseFloat(inputs.top.value),
            bottom: parseFloat(inputs.bottom.value),
            left: parseFloat(inputs.left.value),
            right: parseFloat(inputs.right.value),
        };

        statusText.textContent = 'Running simulation on server...';
        runBtn.disabled = true;

        try {
            const response = await fetch('/run', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(params)
            });

            if (!response.ok) throw new Error('Server error during simulation');
            
            const result = await response.json();
            const heatmapData = result.data;

            drawHeatmap(heatmapData);
            statusText.textContent = `Complete! Step ${heatmapData.step} (${heatmapData.rows}x${heatmapData.cols})`;

            saveToHistory({
                date: new Date().toLocaleString(),
                params: params,
                steps: heatmapData.step,
                data: heatmapData
            });

        } catch (e) {
            statusText.textContent = `Error: ${e.message}`;
        } finally {
            runBtn.disabled = false;
        }
    }

    function saveToHistory(entry) {
        const history = JSON.parse(localStorage.getItem('heat_sim_history') || '[]');
        history.unshift(entry);
        localStorage.setItem('heat_sim_history', JSON.stringify(history.slice(0, 50)));
    }

    function renderHistory() {
        const history = JSON.parse(localStorage.getItem('heat_sim_history') || '[]');
        const tbody = document.querySelector('#historyTable tbody');
        tbody.innerHTML = '';

        history.forEach((item, index) => {
            const row = document.createElement('tr');
            const p = item.params;
            row.innerHTML = `
                <td>${item.date}</td>
                <td>${p.rows}x${p.cols}</td>
                <td>${p.top},${p.bottom},${p.left},${p.right}</td>
                <td>${item.steps}</td>
                <td><button class="view-btn" data-index="${index}">View</button></td>
            `;
            tbody.appendChild(row);
        });

        document.querySelectorAll('.view-btn').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const index = e.target.dataset.index;
                const item = history[index];
                
                inputs.mode.value = item.params.mode;
                inputs.alpha.value = item.params.alpha;
                inputs.rows.value = item.params.rows;
                inputs.cols.value = item.params.cols;
                inputs.top.value = item.params.top;
                inputs.bottom.value = item.params.bottom;
                inputs.left.value = item.params.left;
                inputs.right.value = item.params.right;

                pdeParamsDiv.classList.toggle('hidden', item.params.mode !== 'pde');

                drawHeatmap(item.data);
                document.querySelector('[data-tab="sim-tab"]').click();
                statusText.textContent = `Restored from history: Step ${item.steps}`;
            });
        });
    }

    document.getElementById('clearHistoryBtn').addEventListener('click', () => {
        localStorage.removeItem('heat_sim_history');
        renderHistory();
    });

    runBtn.addEventListener('click', runSimulation);
});
