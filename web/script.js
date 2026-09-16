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

    // Element handles. These were previously relied on as implicit globals,
    // which only works when an id is also a valid JS identifier - so the
    // hyphenated ones (time-evolution-ctrl, pde-params) threw ReferenceError.
    const psParamsDiv  = document.getElementById('ps-params');
    const pdeParamsDiv = document.getElementById('pde-params');
    const timeCtrl     = document.getElementById('time-evolution-ctrl');
    const timeSlider   = document.getElementById('timeSlider');
    const timeVal      = document.getElementById('timeVal');
    const sliderMinLabel = document.getElementById('sliderMinLabel');
    const sliderMaxLabel = document.getElementById('sliderMaxLabel');

    // Time mapping for the current run. dt is chosen by the solver from the
    // stability limit for the requested alpha, so it is reported by the
    // backend rather than assumed here.
    let simDt = 0.1;
    let simSaveInterval = 1;
    inputs.hasPS.addEventListener('change', () => {
        psParamsDiv.classList.toggle('hidden', !inputs.hasPS.checked);
    });

    timeSlider.addEventListener('input', async () => {
        const realTime = parseFloat(timeSlider.value);
        timeVal.textContent = Math.round(realTime);

        // Simulation time t = step * dt, so step = t / dt. dt now comes from
        // the solver (it depends on alpha) instead of being hardcoded.
        const step = Math.round(realTime / simDt);

        try {
            const response = await fetch(`/run?time=${step}`);
            if (!response.ok) throw new Error('Failed to fetch timestep');
            const data = await response.json();
            drawHeatmap(data);
            // The server snaps to the nearest stored frame, so report the
            // time actually being displayed rather than the one requested.
            if (typeof data.step === 'number') {
                timeVal.textContent = Math.round(data.step * simDt);
            }
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

            if (!response.ok) {
                // Surface what the server actually said instead of a generic string.
                let detail = 'Server error during simulation';
                try {
                    const err = await response.json();
                    if (err && err.message) detail = err.message;
                } catch (_) { /* non-JSON error body */ }
                throw new Error(detail);
            }
            
            const result = await response.json();
            const heatmapData = result.data;

            // Adopt the solver's time mapping for this run.
            simDt = (typeof heatmapData.dt === 'number' && heatmapData.dt > 0)
                ? heatmapData.dt : 0.1;
            simSaveInterval = heatmapData.saveInterval || 1;

            drawHeatmap(heatmapData);
            statusText.textContent = `Complete! Max Step ${heatmapData.step} (${heatmapData.rows}x${heatmapData.cols})`;
            
            if (inputs.mode.value === 'fdm') {
                timeCtrl.classList.remove('hidden');

                // The slider spans real simulated seconds, from t = 0 to how
                // long this run actually took to settle. That span depends on
                // alpha, so it is recomputed per run rather than fixed at
                // 1000s - which used to truncate the run to its first 10%.
                const maxSeconds = heatmapData.step * simDt;
                const stepSeconds = Math.max(1, Math.round(simSaveInterval * simDt));
                timeSlider.min = 0;
                timeSlider.step = stepSeconds;
                timeSlider.max = Math.max(stepSeconds, Math.round(maxSeconds));
                timeSlider.value = timeSlider.max;
                timeVal.textContent = timeSlider.value;
                if (sliderMinLabel) sliderMinLabel.textContent = '0s';
                if (sliderMaxLabel) sliderMaxLabel.textContent = timeSlider.max + 's';
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
