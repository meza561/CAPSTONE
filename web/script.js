document.addEventListener('DOMContentLoaded', () => {
    const canvas = document.getElementById('heatCanvas');
    const ctx = canvas.getContext('2d');
    const runBtn = document.getElementById('runBtn');
    const statusText = document.getElementById('status');
    
    const tabBtns = document.querySelectorAll('.tab-btn');
    const tabs = {
        'sim-tab': document.getElementById('sim-tab'),
        'history-tab': document.getElementById('history-tab'),
        'validation-tab': document.getElementById('validation-tab')
    };

    const inputs = {
        mode: document.getElementById('simMode'),
        F: document.getElementById('diffusionF'),
        alpha: document.getElementById('alpha'),
        rows: document.getElementById('rows'),
        cols: document.getElementById('cols'),
        top: document.getElementById('top'),
        bottom: document.getElementById('bottom'),
        left: document.getElementById('left'),
        right: document.getElementById('right'),
        hasPS: document.getElementById('hasPointSource'),
    };

    // Element handles. These were previously relied on as implicit globals,
    // which only works when an id is also a valid JS identifier - so the
    // hyphenated ones (time-evolution-ctrl, alpha-params) threw ReferenceError.
    const psParamsDiv  = document.getElementById('ps-params');
    const psGroup      = document.getElementById('ps-group');
    const boundaryGroup = document.getElementById('boundary-group');
    const alphaParamsDiv = document.getElementById('alpha-params');
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
    // ---- Heat sources -------------------------------------------------
    // Coordinates are (x, y) with the origin at the BOTTOM-LEFT: x runs right,
    // y runs up. The server flips y into a row index; nothing here needs to
    // know about array layout.
    const srcList = document.getElementById('src-list');
    const srcQuick = document.getElementById('srcQuick');
    const srcQuickMsg = document.getElementById('srcQuickMsg');
    const MAX_SOURCES = 32;

    let sources = [{ x: 10, y: 10, temp: 100 }];

    function gridLimits() {
        const cols = parseInt(inputs.cols.value) || 20;
        const rows = parseInt(inputs.rows.value) || 20;
        return { maxX: Math.max(0, cols - 1), maxY: Math.max(0, rows - 1) };
    }

    function renderSources() {
        const { maxX, maxY } = gridLimits();
        srcList.innerHTML = '';
        sources.forEach((s, i) => {
            const row = document.createElement('div');
            row.className = 'src-row';
            row.innerHTML =
                `<input type="number" class="src-x" min="0" max="${maxX}" value="${s.x}"
                        aria-label="Source ${i + 1} x position">` +
                `<input type="number" class="src-y" min="0" max="${maxY}" value="${s.y}"
                        aria-label="Source ${i + 1} y position">` +
                `<input type="number" class="src-t" value="${s.temp}"
                        aria-label="Source ${i + 1} temperature in degrees Celsius">` +
                `<button type="button" class="src-del" title="Remove this heat spot"
                         aria-label="Remove heat spot ${i + 1}">&times;</button>`;

            row.querySelector('.src-x').addEventListener('input', e => {
                sources[i].x = parseInt(e.target.value);
            });
            row.querySelector('.src-y').addEventListener('input', e => {
                sources[i].y = parseInt(e.target.value);
            });
            row.querySelector('.src-t').addEventListener('input', e => {
                sources[i].temp = parseFloat(e.target.value);
            });
            row.querySelector('.src-del').addEventListener('click', () => {
                sources.splice(i, 1);
                renderSources();
            });
            srcList.appendChild(row);
        });

        if (!sources.length) {
            const empty = document.createElement('p');
            empty.className = 'src-empty';
            empty.textContent = 'No heat spots yet \u2014 add one below.';
            srcList.appendChild(empty);
        }
    }

    document.getElementById('addSrcBtn').addEventListener('click', () => {
        if (sources.length >= MAX_SOURCES) return;
        const { maxX, maxY } = gridLimits();
        sources.push({ x: Math.round(maxX / 2), y: Math.round(maxY / 2), temp: 100 });
        renderSources();
    });

    // Re-render so the min/max hints track the current grid size.
    inputs.rows.addEventListener('change', renderSources);
    inputs.cols.addEventListener('change', renderSources);

    /**
     * Parse "(x, y, C)" entries. Tolerates missing parentheses, and accepts
     * several at once separated by semicolons or newlines.
     */
    function parseQuickAdd(text) {
        const out = [];
        const bad = [];
        const chunks = String(text)
            .split(/[;\n]+/)
            .map(s => s.trim())
            .filter(Boolean);

        chunks.forEach(chunk => {
            const nums = chunk.replace(/[()\[\]]/g, '')
                              .split(',')
                              .map(s => s.trim())
                              .filter(s => s !== '');
            if (nums.length !== 3 || nums.some(n => !isFinite(Number(n)))) {
                bad.push(chunk);
                return;
            }
            out.push({
                x: Math.round(Number(nums[0])),
                y: Math.round(Number(nums[1])),
                temp: Number(nums[2])
            });
        });
        return { parsed: out, bad: bad };
    }

    document.getElementById('srcQuickBtn').addEventListener('click', () => {
        const { parsed, bad } = parseQuickAdd(srcQuick.value);
        if (parsed.length) {
            sources = sources.concat(parsed).slice(0, MAX_SOURCES);
            renderSources();
            srcQuick.value = '';
        }
        if (bad.length) {
            srcQuickMsg.textContent =
                `Could not read: ${bad.join(' | ')}. Expected (x, y, C).`;
            srcQuickMsg.classList.add('is-error');
        } else if (parsed.length) {
            srcQuickMsg.textContent =
                `Added ${parsed.length} heat spot${parsed.length > 1 ? 's' : ''}.`;
            srcQuickMsg.classList.remove('is-error');
        } else {
            srcQuickMsg.textContent = 'Nothing to add. Expected (x, y, C).';
            srcQuickMsg.classList.add('is-error');
        }
    });

    srcQuick.addEventListener('keydown', e => {
        if (e.key === 'Enter') {
            e.preventDefault();
            document.getElementById('srcQuickBtn').click();
        }
    });

    // Drop anything out of range or incomplete before sending.
    function validSources() {
        if (!inputs.hasPS.checked) return [];
        const { maxX, maxY } = gridLimits();
        return sources
            .filter(s => isFinite(s.x) && isFinite(s.y) && isFinite(s.temp))
            .map(s => ({
                x: Math.min(maxX, Math.max(0, Math.round(s.x))),
                y: Math.min(maxY, Math.max(0, Math.round(s.y))),
                temp: s.temp
            }));
    }

    renderSources();

    // Alpha drives the time-dependent FDM solve only; the analytical steady
    // state is independent of diffusivity. Without this listener the control
    // was never shown at all, so alpha could not be changed from the UI.
    const fParams = document.getElementById('f-params');
    const fHint = document.getElementById('f-hint');
    const rdParams = document.getElementById('rd-params');
    const rdIntro = document.getElementById('rd-intro');
    const gsFields = document.getElementById('gs-fields');
    const fkFields = document.getElementById('fk-fields');
    const gsPreset = document.getElementById('gsPreset');
    const fkSpeed = document.getElementById('fk-speed');
    const rd = {
        steps: document.getElementById('rdSteps'),
        feed: document.getElementById('gsFeed'), kill: document.getElementById('gsKill'),
        Du: document.getElementById('gsDu'), Dv: document.getElementById('gsDv'),
        D: document.getElementById('fkD'), r: document.getElementById('fkR')
    };

    // Presets fill in feed/kill; editing either one switches to Custom so the
    // dropdown never claims a regime the numbers no longer match.
    gsPreset.addEventListener('change', () => {
        if (gsPreset.value === 'custom') return;
        const [f, k] = gsPreset.value.split(',');
        rd.feed.value = f;
        rd.kill.value = k;
    });
    [rd.feed, rd.kill].forEach(el => el.addEventListener('input', () => {
        const match = `${rd.feed.value},${rd.kill.value}`;
        const found = Array.from(gsPreset.options).some(o => o.value === match);
        gsPreset.value = found ? match : 'custom';
    }));

    function updateFisherSpeed() {
        const D = parseFloat(rd.D.value), r = parseFloat(rd.r.value);
        fkSpeed.textContent = (isFinite(D) && isFinite(r) && D > 0 && r > 0)
            ? `Predicted front speed c* = 2\u221a(Dr) = ${(2 * Math.sqrt(D * r)).toFixed(4)}`
            : '';
    }
    [rd.D, rd.r].forEach(el => el.addEventListener('input', updateFisherSpeed));

    function syncModeUI() {
        const mode = inputs.mode.value;
        const isPde = mode === 'pde';
        const isImplicit = (mode === 'be' || mode === 'cn');
        const isRD = (mode === 'fisher' || mode === 'gray-scott');

        // Reaction-diffusion has no boundary temperatures (edges are zero-flux)
        // and no heat sources; it seeds itself.
        alphaParamsDiv.classList.toggle('hidden', isPde || isRD);
        if (psGroup) psGroup.classList.toggle('hidden', isPde || isRD);
        if (boundaryGroup) boundaryGroup.classList.toggle('hidden', isRD);
        if (isPde) timeCtrl.classList.add('hidden');

        // F is only a free parameter for the implicit schemes. The explicit
        // one is pinned at 0.2 because it diverges above 0.25.
        if (fParams) fParams.classList.toggle('hidden', !isImplicit);
        if (fHint) {
            fHint.textContent = isImplicit
                ? 'Unconditionally stable \u2014 try 50 or 500. The explicit scheme '
                  + 'diverges above 0.25, which is why its step size is fixed.'
                : '';
        }

        if (rdParams) rdParams.classList.toggle('hidden', !isRD);
        gsFields.classList.toggle('hidden', mode !== 'gray-scott');
        fkFields.classList.toggle('hidden', mode !== 'fisher');
        if (isRD) {
            rdIntro.innerHTML = (mode === 'gray-scott')
                ? 'Two species with unequal diffusivities \u2014 the Turing mechanism. '
                  + 'Edges are zero-flux and the field seeds itself; the display shows '
                  + 'the activator <em>v</em>.'
                : 'One species: diffusion plus logistic growth, which produces a '
                  + 'travelling population front.';
            updateFisherSpeed();
        }
    }
    inputs.mode.addEventListener('change', syncModeUI);
    syncModeUI();

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
            } else if (btn.dataset.tab === 'validation-tab') {
                // Loaded on first view; the study JSON is static between runs.
                if (typeof renderValidation === 'function') renderValidation();
            }
        });
    });

    // --- hover readout ---------------------------------------------------
    // Whatever is currently on the canvas, so a hover can look up the value
    // under the cursor without re-fetching.
    let currentField = null;

    const canvasTip = document.createElement('div');
    canvasTip.className = 'chart-tip hidden';
    document.body.appendChild(canvasTip);

    canvas.addEventListener('mousemove', e => {
        if (!currentField) return;
        const rect = canvas.getBoundingClientRect();
        const { rows, cols, data: g } = currentField;
        // The canvas is one pixel per cell, stretched by CSS, so map the
        // pointer through the displayed size rather than the backing size.
        const j = Math.floor((e.clientX - rect.left) / rect.width * cols);
        const i = Math.floor((e.clientY - rect.top) / rect.height * rows);
        if (i < 0 || i >= rows || j < 0 || j >= cols || !g[i]) {
            canvasTip.classList.add('hidden');
            return;
        }
        // Same bottom-left convention the heat sources use.
        const y = (rows - 1) - i;
        canvasTip.innerHTML =
            `<strong>${g[i][j].toFixed(2)} \u00b0C</strong><br>x = ${j}, y = ${y}`;
        canvasTip.classList.remove('hidden');
        canvasTip.style.left = (e.pageX + 14) + 'px';
        canvasTip.style.top = (e.pageY - 8) + 'px';
    });

    canvas.addEventListener('mouseleave', () => canvasTip.classList.add('hidden'));

    function drawHeatmap(data) {
        currentField = data;
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
        const uniform = (max - min) < 1e-9;
        document.getElementById('legendMin').textContent = `${min.toFixed(1)}°C`;
        document.getElementById('legendMax').textContent =
            uniform ? `${max.toFixed(1)}°C (uniform)` : `${max.toFixed(1)}°C`;

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
            sources: validSources(),
            F: parseFloat(inputs.F.value),
            rdSteps: parseInt(rd.steps.value),
            Du: parseFloat(rd.Du.value), Dv: parseFloat(rd.Dv.value),
            feed: parseFloat(rd.feed.value), kill: parseFloat(rd.kill.value),
            rdD: parseFloat(rd.D.value), rdR: parseFloat(rd.r.value),
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
            const isPde = inputs.mode.value === 'pde';
            const isRD = (inputs.mode.value === 'fisher' || inputs.mode.value === 'gray-scott');
            const flat = heatmapData.data.reduce((a, r) => a.concat(r), []);
            const lo = Math.min.apply(null, flat), hi = Math.max.apply(null, flat);
            const isUniform = (hi - lo) < 1e-9;

            // A steady state is a single frame, so "max step" is meaningless
            // for the analytical solver and reads like a failure.
            const scheme = heatmapData.scheme || '';
            let msg = isPde
                ? `Analytical steady state (${heatmapData.rows}x${heatmapData.cols})`
                : `${scheme ? scheme.charAt(0).toUpperCase() + scheme.slice(1) : 'Complete'}`
                  + ` \u2014 ${heatmapData.step + 1} steps`
                  // F is the heat-diffusion number; it means nothing for the
                  // reaction-diffusion models, which set their own step size.
                  + ((heatmapData.F && !isRD) ? `, F = ${heatmapData.F}` : '')
                  + ` (${heatmapData.rows}x${heatmapData.cols})`;

            if (isUniform) {
                msg += ` \u2014 plate is uniformly ${hi.toFixed(1)}\u00b0C`;
                const noHeat = [params.top, params.bottom, params.left, params.right]
                    .every(v => Math.abs(v) < 1e-9);
                if (isPde && params.sources.length) {
                    msg += '. Analytical mode solves from the boundary temperatures only, '
                         + 'so the point source is not used \u2014 switch to FDM for that.';
                } else if (noHeat) {
                    msg += '. Set a boundary temperature, or enable a point source in FDM mode.';
                }
            }
            statusText.textContent = msg;
            
            if (inputs.mode.value !== 'pde') {
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
            const srcs = p.sources || (p.hasPointSource
                ? [{ x: p.psC, y: p.psR, temp: p.psTemp }] : []);
            const originType = srcs.length ? 'Sources' : 'Edge';
            row.innerHTML = `
                <td>${item.date}</td>
                <td>${p.rows}x${p.cols}</td>
                <td title="T:${p.top}, B:${p.bottom}, L:${p.left}, R:${p.right}">
                    ${originType} ${srcs.length
                        ? srcs.map(s => `(${s.x},${s.y},${s.temp})`).join(' ')
                        : 'Boundaries'}
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
                if (item.params.F) inputs.F.value = item.params.F;
                inputs.alpha.value = item.params.alpha;
                inputs.rows.value = item.params.rows;
                inputs.cols.value = item.params.cols;
                inputs.top.value = item.params.top;
                inputs.bottom.value = item.params.bottom;
                inputs.left.value = item.params.left;
                inputs.right.value = item.params.right;
                inputs.hasPS.checked = item.params.hasPointSource || false;
                // Entries saved before multi-source support carry a single
                // psR/psC/psTemp triple in row/col terms; convert it.
                if (Array.isArray(item.params.sources)) {
                    sources = item.params.sources.map(s => ({ ...s }));
                } else if (item.params.hasPointSource) {
                    const rowCount = parseInt(item.params.rows) || 20;
                    sources = [{
                        x: item.params.psC || 0,
                        y: (rowCount - 1) - (item.params.psR || 0),
                        temp: item.params.psTemp || 0
                    }];
                } else {
                    sources = [];
                }
                renderSources();

                alphaParamsDiv.classList.toggle('hidden', item.params.mode === 'pde');
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
