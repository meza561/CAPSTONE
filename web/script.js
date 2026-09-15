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
        hasPS: document.getElementById('hasPointSource'),
        psR: document.getElementById('psR'),
        psC: document.getElementById('psC'),
        psTemp: document.getElementById('psTemp'),
    };

    const psParamsDiv = document.getElementById('ps-params');
    inputs.hasPS.addEventListener('change', () => {
        psParamsDiv.classList.toggle('hidden', !inputs.hasPS.checked);
    });

    timeSlider.addEventListener('input', async () => {
        const realTime = parseInt(timeSlider.value);
        timeVal.textContent = realTime;
        
        // Temporal Scaling Logic:
        // Simulation time t = step * dt
        // Step = t / dt
        // Using a default dt = 0.1 (as in main.cpp), and assuming 1s = 10 steps.
        // To be accurate, the server should probably provide dt, but we'll use the sim default.
        const dt = 0.1;
        const step = Math.floor(realTime / dt);
        
        try {
            const response = await fetch(`/run?time=${step}`);
            if (!response.ok) throw new Error('Failed to fetch timestep');
            const data = await response.json();
            drawHeatmap(data);
        } catch (e) {
            console.error('Slider error:', e);
        }
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

        // Calculate dynamic scale
        let min = Infinity;
        let max = -Infinity;
        for (let i = 0; i < rows; i++) {
            for (let j = 0; j < cols; j++) {
                const val = grid[i][j];
                if (val < min) min = val;
                if (val > max) max = val;
            }
        }

        // Update legend
        document.getElementById('legendMin').textContent = `${min.toFixed(1)}°C`;
        document.getElementById('legendMax').textContent = `${max.toFixed(1)}°C`;

        const range = max - min || 1;

        for (let i = 0; i < rows; i++) {
            for (let j = 0; j < cols; j++) {
                const temp = grid[i][j];
                const t = (temp - min) / range;
                
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
            hasPointSource: inputs.hasPS.checked,
            psR: parseInt(inputs.psR.value),
            psC: parseInt(inputs.psC.value),
            psTemp: parseFloat(inputs.psTemp.value),
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
            statusText.textContent = `Complete! Max Step ${heatmapData.step} (${heatmapData.rows}x${heatmapData.cols})`;
            
            if (inputs.mode.value === 'fdm') {
                timeCtrl.classList.remove('hidden');
                // The slider now represents seconds (1 to 1000).
                // We ensure the max is limited by the simulation's actual convergence/max steps.
                const dt = 0.1;
                const maxSeconds = heatmapData.step * dt;
                timeSlider.max = Math.min(1000, Math.floor(maxSeconds));
                timeSlider.value = Math.min(1000, Math.floor(maxSeconds));
                timeVal.textContent = timeSlider.value;
            } else {
                timeCtrl.classList.add('hidden');
            }

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
            const originType = p.hasPointSource ? 'Point' : 'Edge';
            row.innerHTML = `
                <td>${item.date}</td>
                <td>${p.rows}x${p.cols}</td>
                <td title="T:${p.top}, B:${p.bottom}, L:${p.left}, R:${p.right}">
                    ${originType} ${p.hasPointSource ? `(${p.psR},${p.psC})` : 'Boundaries'}
                </td>
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
                inputs.hasPS.checked = item.params.hasPointSource || false;
                inputs.psR.value = item.params.psR || 0;
                inputs.psC.value = item.params.psC || 0;
                inputs.psTemp.value = item.params.psTemp || 0;

                pdeParamsDiv.classList.toggle('hidden', item.params.mode !== 'pde');
                psParamsDiv.classList.toggle('hidden', !inputs.hasPS.checked);

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
