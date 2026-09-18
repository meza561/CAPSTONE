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
    const frameVal     = document.getElementById('frameVal');
    const playBtn      = document.getElementById('playBtn');
    const sliderMinLabel = document.getElementById('sliderMinLabel');
    const sliderMaxLabel = document.getElementById('sliderMaxLabel');
    const gradientBar  = document.getElementById('gradientBar');
    const fieldSummary = document.getElementById('fieldSummary');
    const fieldTable   = document.getElementById('field-table');
    const colorMapSel  = document.getElementById('colorMap');
    const autoScaleBox = document.getElementById('autoScale');

    // Time mapping for the current run. dt is chosen by the solver from the
    // stability limit for the requested alpha, so it is reported by the
    // backend rather than assumed here.
    let simDt = 0.1;
    let simSaveInterval = 1;

    // Which steps this run actually stored. The slider indexes this list
    // rather than a rounded number of seconds: whole seconds made the slider
    // useless for fast runs (a whole run of 0.5 s collapsed to two positions)
    // and meant the displayed time never quite matched the frame shown.
    let frameSteps = [];
    // Which run's files the timeline reads. The server keeps one
    // database per run, so every frame request has to name it.
    let currentRunId = null;
    // Field units for the current run. Reaction-diffusion returns species
    // concentrations, not temperatures, so labelling them degrees Celsius is
    // simply wrong.
    let fieldUnit = '°C';
    let fieldLabel = 'Temperature';
    // Fixed colour range for the run. Rescaling every frame to its own
    // min/max made a cooling plate look identical at every instant, which is
    // the opposite of what the timeline is for.
    let scaleRange = null;
    let timeSuffix = ' s';

    // ---- Colour maps ---------------------------------------------------
    // The plate, the key and the tooltip all read colour from here, so the
    // key cannot drift from what is painted. It previously did: the CSS
    // gradient ran blue-cyan-green-yellow-red while the canvas painted
    // blue-teal-green-olive-red, so the key mis-stated the middle of its own
    // scale.
    const COLOR_MAPS = {
        // The original map: blue (cold) through green to red (hot).
        thermal: t => [
            t > 0.5 ? (t - 0.5) * 2 * 255 : 0,
            (1 - Math.abs(t - 0.5) * 2) * 255,
            t < 0.5 ? (0.5 - t) * 2 * 255 : 0
        ],
        // Incandescence: black through red and orange to white. Brightness
        // increases with the value, so it reads correctly in greyscale and
        // for colour-blind viewers, and it has no bright band in the middle
        // to be mistaken for a feature of the solution.
        blackbody: t => [
            Math.min(1, t * 3) * 255,
            Math.min(1, Math.max(0, t * 3 - 1)) * 255,
            Math.min(1, Math.max(0, t * 3 - 2)) * 255
        ],
        gray: t => [t * 255, t * 255, t * 255]
    };
    let colorMap = COLOR_MAPS.thermal;

    function heatColor(v) {
        const t = v < 0 ? 0 : (v > 1 ? 1 : v);
        const c = colorMap(t);
        return [Math.round(c[0]), Math.round(c[1]), Math.round(c[2])];
    }

    function renderLegendBar() {
        const stops = [];
        for (let i = 0; i <= 20; i++) {
            const [r, g, b] = heatColor(i / 20);
            stops.push(`rgb(${r},${g},${b}) ${i * 5}%`);
        }
        gradientBar.style.background =
            `linear-gradient(to right, ${stops.join(', ')})`;
    }

    colorMapSel.addEventListener('change', () => {
        colorMap = COLOR_MAPS[colorMapSel.value] || COLOR_MAPS.thermal;
        renderLegendBar();
        if (currentField) drawHeatmap(currentField);
    });
    autoScaleBox.addEventListener('change', () => {
        if (currentField) drawHeatmap(currentField);
    });
    renderLegendBar();

    // Precision follows the span of the current scale, not the magnitude of
    // each value: temperatures run to hundreds, reaction-diffusion
    // concentrations to fractions of one. Taking it per value printed the two
    // ends of one scale at different precisions ("0.000" beside "100.0").
    let valueDigits = 1;

    function fmtValue(v, extra) {
        if (!isFinite(v)) return '--';
        return v.toFixed(valueDigits + (extra || 0)) + fieldUnit;
    }

    function setValuePrecision(span) {
        valueDigits = span >= 10 ? 1 : (span >= 1 ? 2 : 3);
    }

    function fmtTime(t) {
        if (!isFinite(t)) return '0';
        const a = Math.abs(t);
        if (a === 0) return '0' + timeSuffix;
        if (a < 0.01) return t.toExponential(2) + timeSuffix;
        if (a < 1) return t.toFixed(4) + timeSuffix;
        if (a < 1000) return t.toFixed(2) + timeSuffix;
        return Math.round(t).toLocaleString() + timeSuffix;
    }
    function gridLimits() {
        const cols = parseInt(inputs.cols.value) || 20;
        const rows = parseInt(inputs.rows.value) || 20;
        return { maxX: Math.max(0, cols - 1), maxY: Math.max(0, rows - 1) };
    }

    // ---- Material regions ----------------------------------------------
    // Same (x, y) convention as heat sources: origin bottom-left. The server
    // converts to row/col.
    const matList = document.getElementById('mat-list');
    const insBoxes = {
        top: document.getElementById('insTop'),
        bottom: document.getElementById('insBottom'),
        left: document.getElementById('insLeft'),
        right: document.getElementById('insRight')
    };
    let materials = [];

    function renderMaterials() {
        matList.innerHTML = '';
        materials.forEach((m, i) => {
            const row = document.createElement('div');
            row.className = 'mat-row';
            row.innerHTML =
                ['x0','y0','x1','y1'].map(k =>
                    `<input type="number" class="mat-${k}" min="0" value="${m[k]}"
                            aria-label="Region ${i+1} ${k}">`).join('') +
                `<input type="number" class="mat-a" step="0.01" min="0.000001" value="${m.alpha}"
                        aria-label="Region ${i+1} diffusivity">` +
                `<button type="button" class="src-del" title="Remove this region"
                         aria-label="Remove region ${i+1}">&times;</button>`;
            ['x0','y0','x1','y1'].forEach(k =>
                row.querySelector('.mat-'+k).addEventListener('input', e => {
                    materials[i][k] = parseInt(e.target.value);
                }));
            row.querySelector('.mat-a').addEventListener('input', e => {
                materials[i].alpha = parseFloat(e.target.value);
            });
            row.querySelector('.src-del').addEventListener('click', () => {
                materials.splice(i, 1); renderMaterials();
            });
            matList.appendChild(row);
        });
        if (!materials.length) {
            const p = document.createElement('p');
            p.className = 'src-empty';
            p.textContent = 'Uniform \u03b1 everywhere \u2014 add a region to vary it.';
            matList.appendChild(p);
        }
    }

    document.getElementById('addMatBtn').addEventListener('click', () => {
        const { maxX, maxY } = gridLimits();
        materials.push({
            x0: Math.round(maxX * 0.35), y0: Math.round(maxY * 0.35),
            x1: Math.round(maxX * 0.65), y1: Math.round(maxY * 0.65),
            alpha: 0.001
        });
        renderMaterials();
    });

    function validMaterials() {
        const { maxX, maxY } = gridLimits();
        const cl = (v, hi) => Math.min(hi, Math.max(0, Math.round(v) || 0));
        return materials
            .filter(m => isFinite(m.alpha) && m.alpha > 0)
            .map(m => ({ x0: cl(m.x0,maxX), y0: cl(m.y0,maxY),
                         x1: cl(m.x1,maxX), y1: cl(m.y1,maxY), alpha: m.alpha }));
    }

    // ---- Heat sources -------------------------------------------------
    // Coordinates are (x, y) with the origin at the BOTTOM-LEFT: x runs right,
    // y runs up. The server flips y into a row index; nothing here needs to
    // know about array layout.
    const srcList = document.getElementById('src-list');
    const srcQuick = document.getElementById('srcQuick');
    const srcQuickMsg = document.getElementById('srcQuickMsg');
    const MAX_SOURCES = 32;

    let sources = [{ x: 10, y: 10, temp: 100 }];

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
    renderMaterials();

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
        // The analytical solution assumes uniform alpha and Dirichlet edges;
        // reaction-diffusion has its own diffusivities and zero-flux edges.
        const matGroup = document.getElementById('mat-group');
        if (matGroup) matGroup.classList.toggle('hidden', isPde || isRD);
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

    // ---- Timeline -------------------------------------------------------
    // Frames are fetched one at a time and kept, so replaying a run costs
    // nothing after the first pass. The cache is capped by total cells rather
    // than by frame count: 100 frames of a 300x300 grid is nine million
    // numbers, which is not something to hold on to.
    const frameCache = new Map();
    let frameSeq = 0;

    function cacheLimit() {
        const cells = currentField
            ? Math.max(1, currentField.rows * currentField.cols) : 400;
        return Math.max(8, Math.floor(3e6 / cells));
    }

    async function fetchFrame(step) {
        if (frameCache.has(step)) return frameCache.get(step);
        const response = await fetch(`/run?time=${step}&run_id=${currentRunId}`);
        if (!response.ok) throw new Error('Could not load that frame');
        const data = await response.json();
        while (frameCache.size >= cacheLimit()) {
            frameCache.delete(frameCache.keys().next().value);
        }
        frameCache.set(step, data);
        return data;
    }

    function setFrameLabel(idx) {
        const step = frameSteps[idx];
        if (step === undefined) return;
        timeVal.textContent = 't = ' + fmtTime(step * simDt);
        frameVal.textContent =
            `frame ${idx + 1} of ${frameSteps.length} — step ${step}`;
        // Without this the slider announces its raw index ("23"), which says
        // nothing about the frame it is showing.
        timeSlider.setAttribute('aria-valuetext',
            `${timeVal.textContent}, frame ${idx + 1} of ${frameSteps.length}`);
    }

    /**
     * Show a frame by its index in frameSteps. Dragging fires far faster than
     * the server answers, so responses are sequence-checked: an older reply
     * that lands after a newer one is dropped instead of painting a frame the
     * slider has already moved past.
     */
    async function showFrame(idx) {
        const step = frameSteps[idx];
        if (step === undefined) return;
        const seq = ++frameSeq;
        try {
            const data = await fetchFrame(step);
            if (seq !== frameSeq) return;      // a later request won
            drawHeatmap(data);
            setFrameLabel(idx);
        } catch (e) {
            if (seq === frameSeq) console.error('Frame load failed:', e);
        }
    }

    timeSlider.addEventListener('input', () => {
        stopPlay();
        const idx = parseInt(timeSlider.value, 10) || 0;
        setFrameLabel(idx);                    // label tracks the thumb at once
        showFrame(idx);
    });

    // ---- Playback -------------------------------------------------------
    let playing = false;

    function stopPlay() {
        playing = false;
        playBtn.innerHTML = '&#9654;';
        playBtn.setAttribute('aria-label', 'Play the simulation');
    }

    async function play() {
        if (frameSteps.length < 2) return;
        playing = true;
        playBtn.innerHTML = '&#10073;&#10073;';
        playBtn.setAttribute('aria-label', 'Pause');

        // Restart from the beginning if the timeline is already at the end.
        let idx = parseInt(timeSlider.value, 10) || 0;
        if (idx >= frameSteps.length - 1) idx = 0;

        while (playing && idx < frameSteps.length) {
            timeSlider.value = idx;
            // Awaiting each frame paces playback to whatever the server can
            // actually deliver, so it never queues up work it cannot finish.
            await showFrame(idx);
            if (!playing) return;
            await new Promise(r => setTimeout(r, 45));
            idx++;
        }
        stopPlay();
    }

    playBtn.addEventListener('click', () => (playing ? stopPlay() : play()));

    /** Fetch the list of steps this run stored. */
    async function loadFrameList(finalStep) {
        frameSteps = [];
        try {
            const response = await fetch(`/frames?run_id=${currentRunId}`);
            if (response.ok) {
                const d = await response.json();
                if (Array.isArray(d.steps)) frameSteps = d.steps;
            }
        } catch (e) { /* fall through to the reconstruction below */ }

        // Older servers have no /frames endpoint; the save interval is enough
        // to reconstruct which steps exist.
        if (!frameSteps.length && finalStep >= 0) {
            const every = Math.max(1, simSaveInterval);
            for (let s = 0; s <= finalStep; s += every) frameSteps.push(s);
            if (frameSteps[frameSteps.length - 1] !== finalStep) {
                frameSteps.push(finalStep);
            }
        }
        return frameSteps;
    }

    tabBtns.forEach(btn => {
        btn.addEventListener('click', () => {
            // Playback keeps fetching frames it can no longer show.
            if (btn.dataset.tab !== 'sim-tab') stopPlay();
            tabBtns.forEach(b => {
                b.classList.remove('active');
                b.setAttribute('aria-selected', 'false');
            });
            btn.classList.add('active');
            btn.setAttribute('aria-selected', 'true');
            
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

    /**
     * Where the grid is actually painted inside the canvas element. A
     * non-square grid is letterboxed by `object-fit: contain`, so the drawn
     * area is smaller than the element and the hover has to account for it -
     * otherwise every reading on a 20x300 plate points at the wrong cell.
     */
    function paintedRect() {
        const r = canvas.getBoundingClientRect();
        if (!currentField) return r;
        const aspect = currentField.cols / currentField.rows;
        let w = r.width, h = r.height;
        if (w / h > aspect) w = h * aspect; else h = w / aspect;
        return { left: r.left + (r.width - w) / 2,
                 top: r.top + (r.height - h) / 2, width: w, height: h };
    }

    canvas.addEventListener('mousemove', e => {
        if (!currentField) return;
        const rect = paintedRect();
        const { rows, cols, data: g } = currentField;
        const j = Math.floor((e.clientX - rect.left) / rect.width * cols);
        const i = Math.floor((e.clientY - rect.top) / rect.height * rows);
        if (i < 0 || i >= rows || j < 0 || j >= cols || !g[i]) {
            canvasTip.classList.add('hidden');
            return;
        }
        // Same bottom-left convention the heat sources use.
        const y = (rows - 1) - i;
        canvasTip.innerHTML =
            `<strong>${fmtValue(g[i][j], 1)}</strong><br>x = ${j}, y = ${y}`;
        canvasTip.classList.remove('hidden');

        // Keep the tip inside the window: near the right edge it used to run
        // off the page and get clipped.
        const tipW = canvasTip.offsetWidth || 120;
        const overflowsRight = e.clientX + 14 + tipW > window.innerWidth - 8;
        canvasTip.style.left =
            (overflowsRight ? e.pageX - tipW - 14 : e.pageX + 14) + 'px';
        canvasTip.style.top = (e.pageY - 8) + 'px';
    });

    canvas.addEventListener('mouseleave', () => canvasTip.classList.add('hidden'));

    function rangeOf(field) {
        let min = Infinity, max = -Infinity;
        for (let i = 0; i < field.rows; i++) {
            const row = field.data[i];
            for (let j = 0; j < field.cols; j++) {
                if (row[j] < min) min = row[j];
                if (row[j] > max) max = row[j];
            }
        }
        return { min: min, max: max };
    }

    /**
     * Text alternative to the canvas, in the same (x, y) bottom-left terms the
     * hover readout uses. Large grids are sampled rather than dumped: 300x300
     * is 90,000 cells, which is not a table anyone can read.
     */
    const TABLE_SAMPLES = 12;

    function renderFieldTable(field) {
        const { rows, cols, data: g } = field;
        const stepR = Math.ceil(rows / TABLE_SAMPLES);
        const stepC = Math.ceil(cols / TABLE_SAMPLES);
        const rIdx = [], cIdx = [];
        for (let i = 0; i < rows; i += stepR) rIdx.push(i);
        for (let j = 0; j < cols; j += stepC) cIdx.push(j);

        const head = '<tr><th scope="col">y \\ x</th>'
            + cIdx.map(j => `<th scope="col">${j}</th>`).join('') + '</tr>';
        const body = rIdx.map(i =>
            `<tr><th scope="row">${(rows - 1) - i}</th>`
            + cIdx.map(j => `<td>${fmtValue(g[i][j])}</td>`).join('') + '</tr>').join('');
        const sampled = (stepR > 1 || stepC > 1)
            ? ` Sampled every ${stepR} rows and ${stepC} columns of ${rows}x${cols}.` : '';

        fieldTable.innerHTML =
            `<table class="data-table"><caption>${fieldLabel} by (x, y), `
            + `origin bottom-left.${sampled}</caption>`
            + `<thead>${head}</thead><tbody>${body}</tbody></table>`;
    }

    function drawHeatmap(data) {
        currentField = data;
        const { rows, cols, data: grid } = data;
        canvas.width = cols;
        canvas.height = rows;
        const imageData = ctx.createImageData(cols, rows);

        const frame = rangeOf(data);

        // Hold the colour scale fixed across the run unless asked not to.
        // Per-frame rescaling made the timeline nearly useless: a plate whose
        // edges are pinned looks the same at every instant, and once a run
        // converged to a uniform field it divided by a range of ~1e-12 and
        // painted round-off noise as a full-scale rainbow.
        let lo = frame.min, hi = frame.max;
        if (scaleRange && !autoScaleBox.checked) {
            lo = Math.min(scaleRange.min, frame.min);
            hi = Math.max(scaleRange.max, frame.max);
        }

        const flat = (hi - lo) < 1e-9;
        const range = flat ? 1 : (hi - lo);
        setValuePrecision(hi - lo);

        document.getElementById('legendMin').textContent = fmtValue(lo);
        document.getElementById('legendMax').textContent = fmtValue(hi);

        for (let i = 0; i < rows; i++) {
            for (let j = 0; j < cols; j++) {
                // A field with no spread carries no information, so paint it
                // one flat mid-scale colour rather than amplifying noise.
                const [r, g, b] = heatColor(flat ? 0.5 : (grid[i][j] - lo) / range);
                const index = (i * cols + j) * 4;
                imageData.data[index] = r;
                imageData.data[index + 1] = g;
                imageData.data[index + 2] = b;
                imageData.data[index + 3] = 255;
            }
        }
        ctx.putImageData(imageData, 0, 0);
        renderFieldTable(data);
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
            materials: validMaterials(),
            insulated: {
                top: insBoxes.top.checked, bottom: insBoxes.bottom.checked,
                left: insBoxes.left.checked, right: insBoxes.right.checked
            },
        };

        setStatus('Running simulation on server…');
        runBtn.disabled = true;
        stopPlay();
        frameCache.clear();

        // Units for this run. Reaction-diffusion returns a species
        // concentration in [0, 1] and advances in model time, not seconds.
        const rdMode = inputs.mode.value === 'fisher' ? 'u'
                     : (inputs.mode.value === 'gray-scott' ? 'v' : null);
        fieldUnit = rdMode ? ' ' + rdMode : '°C';
        fieldLabel = rdMode ? 'Concentration ' + rdMode : 'Temperature';
        timeSuffix = rdMode ? '' : ' s';

        try {
            const response = await fetch('/run', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(params)
            });

            if (!response.ok) {
                // Surface what the server actually said instead of a generic string.
                let detail = `Server error during simulation (HTTP ${response.status})`;
                try {
                    const err = await response.json();
                    if (err && err.message) detail = err.message;
                } catch (_) {
                    // A non-JSON body means this did not reach the API at all -
                    // typically static hosting answering with its own 404 page,
                    // i.e. the backend is not deployed behind this domain.
                    if (response.status === 404) {
                        detail = 'The simulation API is not reachable at /run '
                               + '(HTTP 404). This page is being served without '
                               + 'its backend.';
                    }
                }
                throw new Error(detail);
            }
            
            const result = await response.json();
            const heatmapData = result.data;
            currentRunId = result.run_id || null;

            // Adopt the solver's time mapping for this run.
            simDt = (typeof heatmapData.dt === 'number' && heatmapData.dt > 0)
                ? heatmapData.dt : 0.1;
            simSaveInterval = heatmapData.saveInterval || 1;

            // The run's colour range. The discrete maximum principle puts the
            // whole evolution between the extremes present at the start, so
            // the first and last frames bracket every frame in between.
            scaleRange = rangeOf(heatmapData);

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
                msg += ` \u2014 field is uniformly ${fmtValue(hi)}`;
                const noHeat = [params.top, params.bottom, params.left, params.right]
                    .every(v => Math.abs(v) < 1e-9);
                if (isPde && params.sources.length) {
                    msg += '. Analytical mode solves from the boundary temperatures only, '
                         + 'so the point source is not used \u2014 switch to FDM for that.';
                } else if (noHeat) {
                    msg += '. Set a boundary temperature, or enable a point source in FDM mode.';
                }
            }
            setStatus(msg);

            // Announced to screen readers, which get nothing from the canvas.
            // Only on a completed run, not per frame: scrubbing the timeline
            // would otherwise fire this on every frame.
            fieldSummary.textContent =
                `${fieldLabel} field, `
                + `${heatmapData.rows} by ${heatmapData.cols} cells, `
                + `ranging from ${fmtValue(scaleRange.min)} to ${fmtValue(scaleRange.max)}. `
                + msg;

            if (inputs.mode.value !== 'pde') {
                timeCtrl.classList.remove('hidden');

                // The slider indexes the frames that were actually stored, so
                // every position is a real frame and the reported time is the
                // time of the frame on screen. Whole-second positions used to
                // collapse to two stops for a run lasting under a second.
                await loadFrameList(heatmapData.step);
                const last = Math.max(0, frameSteps.length - 1);
                timeSlider.min = 0;
                timeSlider.step = 1;
                timeSlider.max = last;
                timeSlider.value = last;
                setFrameLabel(last);
                playBtn.disabled = frameSteps.length < 2;
                if (sliderMinLabel) {
                    sliderMinLabel.textContent = fmtTime(frameSteps[0] * simDt);
                }
                if (sliderMaxLabel) {
                    sliderMaxLabel.textContent = fmtTime(frameSteps[last] * simDt);
                }

                // Widen the locked range with the first frame: a Gray-Scott
                // run starts as near-uniform seed noise, and scaling that to
                // full range makes the seed look like a finished pattern.
                if (frameSteps.length > 1) {
                    try {
                        const first = await fetchFrame(frameSteps[0]);
                        const r0 = rangeOf(first);
                        scaleRange = { min: Math.min(scaleRange.min, r0.min),
                                       max: Math.max(scaleRange.max, r0.max) };
                        drawHeatmap(heatmapData);
                    } catch (_) { /* keep the final frame's range */ }
                }
            } else {
                timeCtrl.classList.add('hidden');
            }

            saveToHistory({
                date: new Date().toLocaleString(),
                params: params,
                runId: currentRunId,
                steps: heatmapData.step,
                data: heatmapData
            });

        } catch (e) {
            setStatus(`Error: ${e.message}`, true);
        } finally {
            runBtn.disabled = false;
        }
    }

    function setStatus(text, isError) {
        statusText.textContent = text;
        statusText.classList.toggle('is-error', !!isError);
    }

    /**
     * History lives in localStorage, which holds only a few megabytes. A
     * 300x300 grid is 90,000 numbers, so storing the field of every run
     * filled the quota and threw - and because the write happened inside the
     * run's try block, a successful simulation then reported itself as failed.
     */
    const HISTORY_KEY = 'heat_sim_history';
    const MAX_STORED_CELLS = 10000;   // 100x100

    function readHistory() {
        try {
            return JSON.parse(localStorage.getItem(HISTORY_KEY) || '[]');
        } catch (e) {
            return [];
        }
    }

    function saveToHistory(entry) {
        const item = Object.assign({}, entry);
        if (item.data && item.data.rows * item.data.cols > MAX_STORED_CELLS) {
            // Keep the settings, drop the field: the run can be reproduced
            // from its parameters, and the browser cannot hold the grid.
            item.data = null;
            item.dataOmitted = true;
        }

        let history = readHistory();
        history.unshift(item);
        history = history.slice(0, 50);

        // Shed the oldest entries until it fits rather than losing the lot.
        while (history.length) {
            try {
                localStorage.setItem(HISTORY_KEY, JSON.stringify(history));
                return;
            } catch (e) {
                if (history.length === 1) {
                    console.warn('History could not be saved:', e);
                    return;
                }
                history = history.slice(0, Math.max(1, history.length - 5));
            }
        }
    }

    function renderHistory() {
        const history = readHistory();
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

                // Restoring only some of the settings meant that pressing Run
                // after View reproduced a different simulation from the one on
                // screen. Insulated edges, material regions and the
                // reaction-diffusion parameters are part of the run too.
                materials = Array.isArray(item.params.materials)
                    ? item.params.materials.map(m => ({ ...m })) : [];
                renderMaterials();

                const ins = item.params.insulated || {};
                Object.keys(insBoxes).forEach(k => {
                    insBoxes[k].checked = !!ins[k];
                });

                const rdFields = { rdSteps: rd.steps, Du: rd.Du, Dv: rd.Dv,
                                   feed: rd.feed, kill: rd.kill,
                                   rdD: rd.D, rdR: rd.r };
                Object.keys(rdFields).forEach(k => {
                    if (typeof item.params[k] === 'number' && isFinite(item.params[k])) {
                        rdFields[k].value = item.params[k];
                    }
                });

                // Entries saved before per-run files have no runId; either way
                // the timeline stays hidden until Run recomputes, since the
                // stored run may have aged out of the server's retention window.
                currentRunId = item.runId || null;

                stopPlay();
                syncModeUI();
                psParamsDiv.classList.toggle('hidden', !inputs.hasPS.checked);
                timeCtrl.classList.add('hidden');   // the frames on disk are
                                                    // from the latest run, not
                                                    // from this stored one
                document.querySelector('[data-tab="sim-tab"]').click();

                if (item.data) {
                    scaleRange = rangeOf(item.data);
                    drawHeatmap(item.data);
                    setStatus(`Restored from history — ${item.steps} steps. `
                        + 'Press Run to recompute and re-enable the timeline.');
                } else {
                    setStatus('Settings restored. The field itself was too large '
                        + 'to keep in the browser, so press Run to recompute it.');
                }
            });
        });
    }

    document.getElementById('clearHistoryBtn').addEventListener('click', () => {
        localStorage.removeItem(HISTORY_KEY);
        renderHistory();
    });

    // A scroll over a focused number input silently changes its value in most
    // browsers, so scrolling past the parameter column could quietly alter the
    // simulation. Drop focus instead.
    document.addEventListener('wheel', e => {
        const el = e.target;
        if (el instanceof HTMLInputElement && el.type === 'number'
            && document.activeElement === el) {
            el.blur();
        }
    }, { passive: true });

    runBtn.addEventListener('click', runSimulation);
});
