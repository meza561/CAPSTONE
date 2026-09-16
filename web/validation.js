/* Numerical validation charts.
 *
 * Dependency-free inline SVG. Palette and mark specs match make_figures.py so
 * the on-screen charts and the report figures read as one system. Colours were
 * validated for colour-vision deficiency against this app's white panel.
 */
(function () {
    'use strict';

    const C = {
        s1: '#2a78d6',   // blue   - smooth / stable / primary series
        s2: '#eb6834',   // orange - discontinuous
        s3: '#1baf7a',   // aqua   - Crank-Nicolson
        bad: '#e34948',  // red    - unstable
        ink: '#0b0b0b',
        ink2: '#52514e',
        muted: '#898781',
        grid: '#e1e0d9',
        axis: '#c3c2b7',
        surface: '#ffffff'
    };

    const SVG_NS = 'http://www.w3.org/2000/svg';
    const el = (n, attrs, text) => {
        const e = document.createElementNS(SVG_NS, n);
        for (const k in attrs) e.setAttribute(k, attrs[k]);
        if (text != null) e.textContent = text;
        return e;
    };

    // Shared tooltip, created once.
    let tip = null;
    function tooltip() {
        if (!tip) {
            tip = document.createElement('div');
            tip.className = 'chart-tip hidden';
            document.body.appendChild(tip);
        }
        return tip;
    }
    function showTip(evt, html) {
        const t = tooltip();
        t.innerHTML = html;
        t.classList.remove('hidden');
        t.style.left = (evt.pageX + 12) + 'px';
        t.style.top = (evt.pageY - 10) + 'px';
    }
    function hideTip() { if (tip) tip.classList.add('hidden'); }

    function decadeTicks(lo, hi) {
        const ticks = [];
        for (let k = Math.floor(Math.log10(lo)); k <= Math.ceil(Math.log10(hi)); k++) {
            const v = Math.pow(10, k);
            if (v >= lo * 0.999 && v <= hi * 1.001) ticks.push(v);
        }
        return ticks;
    }
    const sup = { '-': '⁻', '0': '⁰', '1': '¹', '2': '²', '3': '³',
                  '4': '⁴', '5': '⁵', '6': '⁶', '7': '⁷',
                  '8': '⁸', '9': '⁹' };
    const pow10 = v => '10' + String(Math.round(Math.log10(v))).split('').map(c => sup[c] || c).join('');

    /**
     * cfg = { xLabel, yLabel, xLog, yLog, series:[{name,color,points,marker,label}],
     *         guides:[{slope,label,from}], hRule:{y,label}, fmt:{x,y} }
     */
    function chart(mount, cfg) {
        const W = 660, H = 360;
        const M = { top: 18, right: cfg.labelGutter || 132, bottom: 48, left: 74 };
        const iw = W - M.left - M.right, ih = H - M.top - M.bottom;

        const all = cfg.series.reduce((a, s) => a.concat(s.points), []);
        let xs = all.map(p => p[0]), ys = all.map(p => p[1]);
        let x0 = Math.min.apply(null, xs), x1 = Math.max.apply(null, xs);
        let y0 = Math.min.apply(null, ys), y1 = Math.max.apply(null, ys);

        if (cfg.xLog) { x0 *= 0.75; x1 *= 1.35; } else { const p = (x1 - x0) * 0.04 || 1; x0 -= p; x1 += p * 4; }
        if (cfg.yLog) { y0 *= 0.45; y1 *= 2.2; } else { const p = (y1 - y0) * 0.08 || 1; y0 -= p; y1 += p; }

        const sx = v => cfg.xLog
            ? M.left + (Math.log10(v) - Math.log10(x0)) / (Math.log10(x1) - Math.log10(x0)) * iw
            : M.left + (v - x0) / (x1 - x0) * iw;
        const sy = v => cfg.yLog
            ? M.top + ih - (Math.log10(v) - Math.log10(y0)) / (Math.log10(y1) - Math.log10(y0)) * ih
            : M.top + ih - (v - y0) / (y1 - y0) * ih;

        const svg = el('svg', {
            viewBox: `0 0 ${W} ${H}`, width: '100%', role: 'img',
            'aria-label': cfg.ariaLabel || cfg.yLabel + ' against ' + cfg.xLabel
        });

        // gridlines + ticks
        const xt = cfg.xLog ? decadeTicks(x0, x1) : (cfg.xTicks || []);
        const yt = cfg.yLog ? decadeTicks(y0, y1) : (cfg.yTicks || []);
        xt.forEach(v => {
            svg.appendChild(el('line', { x1: sx(v), y1: M.top, x2: sx(v), y2: M.top + ih,
                                         stroke: C.grid, 'stroke-width': 1 }));
            svg.appendChild(el('text', { x: sx(v), y: M.top + ih + 20, fill: C.ink2,
                                         'font-size': 12, 'text-anchor': 'middle' },
                                cfg.xLog ? pow10(v) : String(v)));
        });
        yt.forEach(v => {
            svg.appendChild(el('line', { x1: M.left, y1: sy(v), x2: M.left + iw, y2: sy(v),
                                         stroke: C.grid, 'stroke-width': 1 }));
            svg.appendChild(el('text', { x: M.left - 10, y: sy(v) + 4, fill: C.ink2,
                                         'font-size': 12, 'text-anchor': 'end' },
                                cfg.yLog ? pow10(v) : String(v)));
        });

        // axes
        svg.appendChild(el('line', { x1: M.left, y1: M.top + ih, x2: M.left + iw, y2: M.top + ih,
                                     stroke: C.axis, 'stroke-width': 1 }));
        svg.appendChild(el('line', { x1: M.left, y1: M.top, x2: M.left, y2: M.top + ih,
                                     stroke: C.axis, 'stroke-width': 1 }));
        svg.appendChild(el('text', { x: M.left + iw / 2, y: H - 8, fill: C.ink2,
                                     'font-size': 13, 'text-anchor': 'middle' }, cfg.xLabel));
        const yl = el('text', { x: 16, y: M.top + ih / 2, fill: C.ink2, 'font-size': 13,
                                'text-anchor': 'middle',
                                transform: `rotate(-90 16 ${M.top + ih / 2})` }, cfg.yLabel);
        svg.appendChild(yl);

        // horizontal reference rule
        if (cfg.hRule) {
            // Stop at the last data point so the rule never runs under the
            // direct labels sitting just beyond it.
            const ruleEnd = sx(Math.max.apply(null, xs));
            svg.appendChild(el('line', { x1: M.left, y1: sy(cfg.hRule.y), x2: ruleEnd,
                                         y2: sy(cfg.hRule.y), stroke: C.muted,
                                         'stroke-width': 1.2, 'stroke-dasharray': '2 4' }));
        }

        // reference slope guides
        const guideLabels = [];
        (cfg.guides || []).forEach(g => {
            const src = cfg.series[g.from].points;
            const ax = src[src.length - 1][0];
            const ay = src[src.length - 1][1] * (g.offset || 1);
            const gx = [x0 * 1.05, x1 * 0.95];
            const gy = gx.map(x => ay * Math.pow(x / ax, g.slope));
            svg.appendChild(el('line', { x1: sx(gx[0]), y1: sy(gy[0]), x2: sx(gx[1]), y2: sy(gy[1]),
                                         stroke: C.muted, 'stroke-width': 1.5,
                                         'stroke-dasharray': '6 4' }));
            guideLabels.push({ x: sx(gx[1]) - 4, y: sy(gy[1]) + 24,
                               lines: [g.label], color: C.muted, anchorEnd: true });
        });

        // series
        const labels = [];
        cfg.series.forEach(s => {
            const d = s.points.map((p, i) => (i ? 'L' : 'M') + sx(p[0]) + ' ' + sy(p[1])).join(' ');
            svg.appendChild(el('path', { d: d, fill: 'none', stroke: s.color,
                                         'stroke-width': 2, 'stroke-linejoin': 'round' }));
            if (s.marker !== false) {
                s.points.forEach(p => {
                    const m = el('circle', { cx: sx(p[0]), cy: sy(p[1]), r: 5, fill: s.color,
                                             stroke: C.surface, 'stroke-width': 1.5 });
                    const hit = el('circle', { cx: sx(p[0]), cy: sy(p[1]), r: 12,
                                               fill: 'transparent', style: 'cursor:pointer' });
                    const fx = cfg.fmt && cfg.fmt.x ? cfg.fmt.x : (v => v);
                    const fy = cfg.fmt && cfg.fmt.y ? cfg.fmt.y : (v => v);
                    hit.addEventListener('mousemove', e => showTip(e,
                        `<strong>${s.name}</strong><br>${cfg.xLabel}: ${fx(p[0])}<br>${cfg.yLabel}: ${fy(p[1])}`));
                    hit.addEventListener('mouseleave', hideTip);
                    svg.appendChild(m);
                    svg.appendChild(hit);
                });
            }
            // Direct label: identity never rests on colour alone. Anchor to the
            // rightmost point so the label lands in the reserved gutter rather
            // than on top of the plot (convergence series run right-to-left).
            if (s.label) {
                const anchor = s.points.reduce((a, p) => (p[0] > a[0] ? p : a), s.points[0]);
                labels.push({
                    x: sx(anchor[0]) + 10,
                    y: sy(anchor[1]) + 4,
                    lines: String(s.label).split('\n'),
                    color: s.color
                });
            }
        });

        // Push overlapping direct labels apart so two series whose end points sit
        // close together do not stack on the same line.
        const LH = 14;
        guideLabels.forEach(g => labels.push(g));
        labels.sort((a, b) => a.y - b.y);
        for (let i = 1; i < labels.length; i++) {
            const need = labels[i - 1].y + labels[i - 1].lines.length * LH + 4;
            if (labels[i].y < need) labels[i].y = need;
        }
        labels.forEach(L => {
            L.lines.forEach((line, i) => {
                const attrs = {
                    x: L.x, y: L.y + i * LH, fill: L.color, 'font-size': 12,
                    // Surface halo so gridlines and reference rules cannot
                    // strike through the text.
                    stroke: C.surface, 'stroke-width': 3, 'paint-order': 'stroke',
                    'stroke-linejoin': 'round'
                };
                if (L.anchorEnd) attrs['text-anchor'] = 'end';
                svg.appendChild(el('text', attrs, line));
            });
        });

        mount.innerHTML = '';
        mount.appendChild(svg);
    }

    function statTile(label, value, note, tone) {
        return `<div class="stat-tile">
            <div class="stat-value ${tone || ''}">${value}</div>
            <div class="stat-label">${label}</div>
            <div class="stat-note">${note}</div>
        </div>`;
    }

    function table(headers, rows) {
        return '<table class="data-table"><thead><tr>' +
            headers.map(h => `<th>${h}</th>`).join('') + '</tr></thead><tbody>' +
            rows.map(r => '<tr>' + r.map(c => `<td>${c}</td>`).join('') + '</tr>').join('') +
            '</tbody></table>';
    }

    const e2 = v => Number(v).toExponential(2);
    const f3 = v => Number(v).toFixed(3);

    window.renderValidation = async function renderValidation() {
        const status = document.getElementById('study-status');
        const content = document.getElementById('study-content');
        if (content.dataset.loaded === '1') return;

        let d;
        try {
            const res = await fetch('/study');
            if (!res.ok) {
                const err = await res.json().catch(() => ({}));
                throw new Error(err.message || 'Could not load study results');
            }
            d = await res.json();
        } catch (e) {
            status.innerHTML = `${e.message}<br><span class="muted">Run <code>./run_study.sh</code> to generate them.</span>`;
            return;
        }

        const sm = d.spatial.cases.smooth, dc = d.spatial.cases.discontinuous;
        const unstable = d.stability.runs.filter(r => r.unstable).map(r => r.F);
        const stable = d.stability.runs.filter(r => !r.unstable).map(r => r.F);

        document.getElementById('study-stats').innerHTML =
            statTile('Spatial order (smooth data)', f3(sm.fitted_order_l2), 'theory: 2', 'good') +
            statTile('Temporal order (Crank\u2013Nicolson)',
                     f3(d.temporal.schemes.crank_nicolson.fitted_order_l2),
                     'theory: 2 \u2014 Euler schemes give 1', 'good') +
            statTile('Stability threshold', 'F = 0.25', `${stable.length} stable / ${unstable.length} divergent`, '') +
            statTile('Maximum principle', d.sanity.maximum_principle.holds ? 'Holds' : 'Violated',
                     'no interior extremum', d.sanity.maximum_principle.holds ? 'good' : 'bad') +
            (d.cost ? statTile('Steps to steady state',
                     `${d.cost.runs[0].steps.toLocaleString()} \u2192 ${d.cost.runs[2].steps.toLocaleString()}`,
                     'explicit \u2192 Crank\u2013Nicolson', 'good') : '');

        // --- spatial ---
        document.getElementById('cap-spatial').textContent =
            'Spatial convergence of the FDM steady state against the exact solution. Smooth ' +
            'boundary data recovers second order; a discontinuous corner makes the exact ' +
            'solution singular there, capping the attainable order however fine the grid.';
        chart(document.getElementById('chart-spatial'), {
            xLabel: 'Δx', yLabel: 'L₂ error', xLog: true, yLog: true,
            fmt: { x: e2, y: e2 },
            ariaLabel: 'Log-log plot of L2 error against grid spacing for smooth and discontinuous boundary data',
            series: [
                { name: 'Smooth data', color: C.s1, points: sm.points.map(p => [p.dx, p.l2]),
                  label: `Smooth\norder ${f3(sm.fitted_order_l2)}` },
                { name: 'Discontinuous corners', color: C.s2, points: dc.points.map(p => [p.dx, p.l2]),
                  label: `Discontinuous\norder ${f3(dc.fitted_order_l2)}` }
            ],
            guides: [{ slope: 2, label: 'slope 2', from: 0, offset: 0.3 }]
        });

        // --- temporal ---
        const SCHEMES = [
            ['explicit', C.s1, 'Explicit'],
            ['backward_euler', C.s2, 'Backward Euler'],
            ['crank_nicolson', C.s3, 'Crank\u2013Nicolson']
        ];
        document.getElementById('cap-temporal').textContent =
            `Temporal convergence at fixed t* = ${e2(d.temporal.t_star)} s on a ` +
            `${d.temporal.N}\u00d7${d.temporal.N} grid, each scheme against a reference at a 256\u00d7 smaller ` +
            `step. Explicit and backward Euler are both first order with nearly identical error ` +
            `constants, so their curves overlap; Crank\u2013Nicolson is second order and its error at ` +
            `the same \u0394t is roughly 2000\u00d7 smaller.`;
        chart(document.getElementById('chart-temporal'), {
            xLabel: '\u0394t', yLabel: 'L\u2082 error', xLog: true, yLog: true,
            fmt: { x: e2, y: e2 },
            ariaLabel: 'Log-log plot of L2 error against time step for three time-integration schemes',
            series: SCHEMES.map(([key, color, nice]) => ({
                name: nice, color: color,
                points: d.temporal.schemes[key].points.map(p => [p.dt, p.l2]),
                label: `${nice}\norder ${f3(d.temporal.schemes[key].fitted_order_l2)}`
            })),
            guides: [
                { slope: 1, label: 'slope 1', from: 0, offset: 0.35 },
                { slope: 2, label: 'slope 2', from: 2, offset: 0.3 }
            ]
        });

        // --- cost ---
        if (d.cost) {
            document.getElementById('cap-cost').textContent =
                `Steps needed to reach steady state on a ${d.cost.N}\u00d7${d.cost.N} grid, every run ` +
                `landing on the same exact centre temperature of ${d.cost.exact_centre} \u00b0C. The explicit ` +
                `scheme is capped at F = 0.2 by stability; the implicit schemes are not. Each implicit ` +
                `step costs more, so wall-clock times end up comparable at this size \u2014 the win is in ` +
                `step count, and it grows with the grid.`;
            const maxSteps = Math.max.apply(null, d.cost.runs.map(r => r.steps));
            document.getElementById('cost-bars').innerHTML = d.cost.runs.map(r => `
                <div class="cost-row">
                    <div class="cost-label">${r.label}</div>
                    <div class="cost-track">
                        <div class="cost-fill" style="width:${(r.steps / maxSteps * 100).toFixed(1)}%"></div>
                    </div>
                    <div class="cost-value">${r.steps.toLocaleString()} steps
                        <span class="cost-ms">${(r.seconds * 1000).toFixed(0)} ms</span></div>
                </div>`).join('');
        }

        // --- stability ---
        document.getElementById('cap-stability').textContent =
            'Stability sweep. Runs satisfying F ≤ 0.25 stay pinned at the boundary maximum ' +
            '(dotted). Runs above the limit grow without bound — this is why Δt is derived ' +
            'from α rather than fixed.';
        const stableRuns = d.stability.runs.filter(r => !r.unstable);
        const series = [];
        if (stableRuns.length) {
            series.push({
                name: 'F ≤ 0.25 (stable)', color: C.s1, marker: false,
                points: stableRuns[0].series.map(p => [p[0], Math.max(p[1], 1e-12)]),
                label: `F = ${stable.join(', ')}\n(all bounded)`
            });
        }
        d.stability.runs.filter(r => r.unstable).forEach(r => {
            series.push({
                name: `F = ${r.F}`, color: C.bad, marker: false,
                points: r.series.map(p => [p[0], Math.max(p[1], 1e-12)]),
                label: `F = ${r.F}`
            });
        });
        chart(document.getElementById('chart-stability'), {
            xLabel: 'time step', yLabel: 'max |T| (°C)', xLog: false, yLog: true,
            labelGutter: 150,
            xTicks: [0, 100, 200, 300, 400],
            fmt: { x: v => v, y: e2 },
            ariaLabel: 'Semi-log plot of maximum temperature against step number for several diffusion numbers',
            hRule: { y: 100 },
            series: series
        });

        // --- table view (identity never depends on the chart alone) ---
        document.getElementById('study-tables').innerHTML =
            '<h4>Spatial convergence</h4>' +
            table(['case', 'N', 'Δx', 'steps', 'L₂', 'L∞'],
                [].concat(
                    sm.points.map(p => ['smooth', p.N, e2(p.dx), p.steps, e2(p.l2), e2(p.linf)]),
                    dc.points.map(p => ['discontinuous', p.N, e2(p.dx), p.steps, e2(p.l2), e2(p.linf)]))) +
            '<h4>Temporal convergence</h4>' +
            table(['scheme', 'F', 'Δt', 'steps', 'L₂', 'L∞'],
                SCHEMES.reduce((acc, s) => acc.concat(
                    d.temporal.schemes[s[0]].points.map(
                        p => [s[2], p.F, e2(p.dt), p.steps, e2(p.l2), e2(p.linf)])), [])) +
            (d.cost ? '<h4>Cost to steady state</h4>' +
                table(['scheme', 'F', 'steps', 'seconds', 'centre', 'error vs exact'],
                    d.cost.runs.map(r => [r.label, r.F, r.steps, r.seconds.toFixed(3),
                                          r.centre.toFixed(4), e2(r.error)])) : '') +
            '<h4>Stability</h4>' +
            table(['F', 'verdict', 'final max |T|'],
                d.stability.runs.map(r => [r.F, r.unstable ? 'diverges' : 'bounded', e2(r.final_max)]));

        status.classList.add('hidden');
        content.classList.remove('hidden');
        content.dataset.loaded = '1';
    };
})();
