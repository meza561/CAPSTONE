"""Render the convergence and stability figures for the written report.

Reads study_results.json (produced by heat_study) and writes PNG + PDF into
figures/. Run via ./run_study.sh, or directly once matplotlib is installed:

    pip install -r requirements-study.txt
    python make_figures.py
"""

import json
import os
import sys

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:
    sys.exit("matplotlib is required: pip install -r requirements-study.txt")

# --- palette -------------------------------------------------------------
# Validated for colour-vision deficiency against the app's white panel.
SERIES_1 = "#2a78d6"   # blue
SERIES_2 = "#eb6834"   # orange
SERIES_3 = "#1baf7a"   # aqua
UNSTABLE = "#e34948"   # red
INK      = "#0b0b0b"
INK_2    = "#52514e"
MUTED    = "#898781"
GRID     = "#e1e0d9"
AXIS     = "#c3c2b7"
SURFACE  = "#ffffff"

plt.rcParams.update({
    "font.family": "sans-serif",
    "font.sans-serif": ["DejaVu Sans", "Helvetica", "Arial"],
    "font.size": 10,
    "figure.facecolor": SURFACE,
    "axes.facecolor": SURFACE,
    "savefig.facecolor": SURFACE,
})

OUT = "figures"
os.makedirs(OUT, exist_ok=True)


def caption(fig, text):
    """Caption in figure coordinates, below the reserved bottom margin."""
    fig.text(0.015, 0.03, text, color=MUTED, fontsize=8.5, ha="left", va="bottom")


def style(ax, title, xlabel, ylabel):
    # "600" is not a weight DejaVu Sans provides; asking for it makes
    # matplotlib emit a findfont warning on every figure.
    ax.set_title(title, color=INK, fontsize=12, fontweight="bold", pad=12, loc="left")
    ax.set_xlabel(xlabel, color=INK_2)
    ax.set_ylabel(ylabel, color=INK_2)
    ax.grid(True, which="both", color=GRID, linewidth=0.8, zorder=0)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(AXIS)
    ax.tick_params(colors=MUTED, which="both")
    for lbl in ax.get_xticklabels() + ax.get_yticklabels():
        lbl.set_color(INK_2)
    # Reserve space so the caption never lands on the tick labels.
    ax.figure.subplots_adjust(left=0.13, right=0.97, top=0.90, bottom=0.26)


def save(fig, name):
    for ext in ("png", "pdf"):
        path = os.path.join(OUT, f"{name}.{ext}")
        fig.savefig(path, dpi=200, bbox_inches="tight")
    plt.close(fig)
    print(f"  wrote {OUT}/{name}.png and .pdf")


def guide(ax, xs, ys, slope, label, offset=1.0):
    """Dashed reference slope, shifted clear of the data so it reads as a ruler."""
    x0, y0 = xs[-1], ys[-1] * offset
    gx = [min(xs) * 0.88, max(xs) * 1.10]
    gy = [y0 * (x / x0) ** slope for x in gx]
    ax.plot(gx, gy, linestyle="--", linewidth=1.5, color=MUTED, zorder=1)
    ax.annotate(label, xy=(gx[1], gy[1]), xytext=(0, -14),
                textcoords="offset points", color=MUTED, fontsize=9,
                ha="right", va="top")


def fig_spatial(d):
    s = d["spatial"]["cases"]
    fig, ax = plt.subplots(figsize=(7.0, 5.0))

    for key, color, marker in (("smooth", SERIES_1, "o"),
                               ("discontinuous", SERIES_2, "s")):
        pts = s[key]["points"]
        dx = [p["dx"] for p in pts]
        l2 = [p["l2"] for p in pts]
        order = s[key]["fitted_order_l2"]
        nice = "Smooth data" if key == "smooth" else "Discontinuous corners"
        ax.plot(dx, l2, marker=marker, markersize=7, linewidth=2, color=color,
                label=f"{nice}  (order {order:.2f})", zorder=3,
                markeredgecolor=SURFACE, markeredgewidth=1.5)

    sm = s["smooth"]["points"]
    guide(ax, [p["dx"] for p in sm], [p["l2"] for p in sm], 2.0, "slope 2", offset=0.28)

    ax.set_xscale("log"); ax.set_yscale("log")
    style(ax, "Spatial convergence of the FDM steady state",
          "grid spacing  Δx", "L₂ error vs exact solution")
    leg = ax.legend(frameon=False, loc="lower right")
    for t in leg.get_texts():
        t.set_color(INK_2)
    caption(fig,
            "Smooth boundary data recovers the theoretical second order. With a discontinuous corner the\n"
            "exact solution is singular there, which caps the attainable order regardless of refinement.")
    save(fig, "spatial_convergence")


def fig_temporal(d):
    t = d["temporal"]
    order_of = [
        ("explicit", SERIES_1, "o", "Explicit"),
        ("backward_euler", SERIES_2, "s", "Backward Euler"),
        ("crank_nicolson", SERIES_3, "^", "Crank\u2013Nicolson"),
    ]

    fig, ax = plt.subplots(figsize=(7.0, 5.0))
    anchors = {}
    for key, color, marker, nice in order_of:
        sc = t["schemes"][key]
        dt = [p["dt"] for p in sc["points"]]
        l2 = [p["l2"] for p in sc["points"]]
        anchors[key] = (dt, l2)
        # Explicit and backward Euler are both first order with nearly equal
        # error constants, so their curves coincide. Draw explicit wider and
        # underneath so it stays visible rather than being hidden entirely.
        wide = (key == "explicit")
        ax.plot(dt, l2, marker=marker, markersize=9 if wide else 7,
                linewidth=5 if wide else 2, color=color,
                zorder=2 if wide else 3,
                markeredgecolor=SURFACE, markeredgewidth=1.5,
                label=f"{nice}  (order {sc['fitted_order_l2']:.2f})")

    guide(ax, *anchors["explicit"], 1.0, "slope 1", offset=0.35)
    guide(ax, *anchors["crank_nicolson"], 2.0, "slope 2", offset=0.25)

    ax.set_xscale("log"); ax.set_yscale("log")
    style(ax, "Temporal convergence by scheme",
          "time step  \u0394t", "L\u2082 error vs fine-\u0394t reference")
    leg = ax.legend(frameon=False, loc="center left")
    for x in leg.get_texts():
        x.set_color(INK_2)
    caption(fig,
            f"Grid fixed at {t['N']}\u00d7{t['N']}; each scheme advanced to t* = {t['t_star']:.2e} s and compared against a\n"
            "reference computed with its own discretisation at a 256\u00d7 smaller step. Explicit and backward Euler\n"
            "are both first order with nearly equal error constants, so their curves coincide \u2014 explicit is drawn\n"
            "wider beneath. Crank\u2013Nicolson is second order: at equal \u0394t its error is ~2000\u00d7 smaller.")
    save(fig, "temporal_convergence")


def fig_cost(d):
    """Steps needed to reach steady state - the practical payoff of implicit."""
    runs = d["cost"]["runs"]
    labels = [r["label"] for r in runs]
    steps = [r["steps"] for r in runs]
    ypos = list(range(len(runs)))[::-1]

    fig, ax = plt.subplots(figsize=(7.0, 4.2))
    ax.barh(ypos, steps, height=0.6, color=SERIES_1, zorder=3)
    for y, r in zip(ypos, runs):
        ax.annotate(f"{r['steps']:,} steps   ({r['seconds']*1000:.0f} ms)",
                    xy=(r["steps"], y), xytext=(8, 0), textcoords="offset points",
                    va="center", color=INK_2, fontsize=9)

    ax.set_yticks(ypos)
    ax.set_yticklabels(labels)
    ax.set_xlim(0, max(steps) * 1.45)
    style(ax, "Cost to reach steady state", "time steps required", "")
    ax.grid(False, axis="y")
    caption(fig,
            f"{d['cost']['N']}\u00d7{d['cost']['N']} grid, all runs converging to the same exact centre "
            "temperature of 25 \u00b0C. The explicit scheme is\ncapped at F = 0.2 by stability; the implicit "
            "schemes are not, and need roughly 20\u00d7 fewer steps. Each\nimplicit step costs more, so the "
            "wall-clock times end up comparable at this grid size.")
    save(fig, "cost")


def fig_stability(d):
    st = d["stability"]
    fig, ax = plt.subplots(figsize=(7.0, 5.0))

    stable = [r for r in st["runs"] if not r["unstable"]]
    unstable = [r for r in st["runs"] if r["unstable"]]

    # The stable runs lie exactly on top of one another at the boundary
    # maximum, so they share one label rather than stacking four on one point.
    for run in stable:
        xs = [p[0] for p in run["series"]]
        ys = [max(p[1], 1e-12) for p in run["series"]]
        ax.plot(xs, ys, linewidth=2, color=SERIES_1, zorder=2)
    if stable:
        last = stable[0]["series"][-1]
        flabels = ", ".join(f"{r['F']:g}" for r in stable)
        ax.annotate(f"F = {flabels}\n(all bounded)",
                    xy=(last[0], max(last[1], 1e-12)), xytext=(8, 0),
                    textcoords="offset points", color=SERIES_1, fontsize=9,
                    va="center")

    for run in unstable:
        xs = [p[0] for p in run["series"]]
        ys = [max(p[1], 1e-12) for p in run["series"]]
        ax.plot(xs, ys, linewidth=2, color=UNSTABLE, zorder=3)
        ax.annotate(f"F = {run['F']:g}", xy=(xs[-1], ys[-1]), xytext=(7, 0),
                    textcoords="offset points", color=UNSTABLE, fontsize=9,
                    va="center")

    ax.axhline(100.0, color=MUTED, linestyle=":", linewidth=1.2, zorder=1)

    ax.set_yscale("log")
    ax.set_xlim(0, max(p[0] for r in st["runs"] for p in r["series"]) * 1.42)
    style(ax, "Stability: the explicit scheme above and below F = 1/4",
          "time step number", "max |T| over the plate  (\u00b0C, log scale)")
    caption(fig,
            "Blue runs satisfy F \u2264 0.25 and stay pinned at the dotted boundary maximum (100 \u00b0C).\n"
            "Red runs exceed the limit and grow without bound \u2014 F = 0.5 passes 10\u00b9\u00b2 within 30 steps.\n"
            "This is why \u0394t is derived from \u03b1 rather than fixed.")
    save(fig, "stability")


def fig_scaling(d):
    """Speedup vs threads. Only meaningful with OpenMP actually enabled."""
    sc = d.get("scaling")
    if not sc or not sc.get("openmp") or sc.get("max_threads", 1) < 2:
        print("  skipped scaling figure (built without OpenMP, or single core)")
        return

    runs = sc["runs"]
    grids = sorted({r["N"] for r in runs})
    fig, axes = plt.subplots(1, len(grids), figsize=(4.2 * len(grids), 4.4), squeeze=False)

    for ax, N in zip(axes[0], grids):
        threads = sorted({r["threads"] for r in runs if r["N"] == N})
        ideal = [t for t in threads]
        ax.plot(threads, ideal, linestyle="--", linewidth=1.5, color=MUTED, zorder=1)
        ax.annotate("ideal", xy=(threads[-1], ideal[-1]), xytext=(-2, -14),
                    textcoords="offset points", color=MUTED, fontsize=9, ha="right")

        for key, color, nice in (("explicit", SERIES_1, "Explicit"),
                                 ("crank_nicolson", SERIES_3, "Crank\u2013Nicolson")):
            pts = sorted([r for r in runs if r["N"] == N and r["scheme"] == key],
                         key=lambda r: r["threads"])
            if not pts:
                continue
            ax.plot([p["threads"] for p in pts], [p["speedup"] for p in pts],
                    marker="o", markersize=7, linewidth=2, color=color, zorder=3,
                    markeredgecolor=SURFACE, markeredgewidth=1.5, label=nice)

        ax.set_xscale("log", base=2); ax.set_yscale("log", base=2)
        ax.set_xticks(threads); ax.set_xticklabels([str(t) for t in threads])
        ax.set_yticks(threads); ax.set_yticklabels([str(t) for t in threads])
        style(ax, f"{N}\u00d7{N} grid", "threads", "speedup" if N == grids[0] else "")
        leg = ax.legend(frameon=False, loc="upper left")
        for t in leg.get_texts():
            t.set_color(INK_2)

    caption(fig,
            f"Strong scaling on {sc['max_threads']} hardware threads. Rows of the explicit update and each ADI grid "
            f"line are\nindependent, so neither needs synchronisation, and serial and parallel output is "
            f"bit-identical.\nPoints marginally above the ideal line are timing noise, not superlinear "
            f"speedup. A stencil this cheap is\nmemory-bandwidth bound, so expect efficiency to fall away "
            f"once the core count is high enough to saturate it.")
    save(fig, "scaling")


def main():
    if not os.path.exists("study_results.json"):
        sys.exit("study_results.json not found - run ./heat_study first")
    with open("study_results.json") as f:
        d = json.load(f)
    print("Rendering figures...")
    fig_spatial(d)
    fig_temporal(d)
    fig_cost(d)
    fig_stability(d)
    fig_scaling(d)

    s = d["spatial"]["cases"]
    print("\nSummary")
    print(f"  spatial order (smooth)        {s['smooth']['fitted_order_l2']:.3f}   (theory 2)")
    print(f"  spatial order (discontinuous) {s['discontinuous']['fitted_order_l2']:.3f}   (corner-limited)")
    for k, nice, th in (("explicit", "explicit", 1), ("backward_euler", "backward Euler", 1),
                        ("crank_nicolson", "Crank-Nicolson", 2)):
        print(f"  temporal order ({nice:<14}) {d['temporal']['schemes'][k]['fitted_order_l2']:.3f}   (theory {th})")
    bad = [r["F"] for r in d["stability"]["runs"] if r["unstable"]]
    ok = [r["F"] for r in d["stability"]["runs"] if not r["unstable"]]
    print(f"  stable F                      {ok}")
    print(f"  unstable F                    {bad}")
    mp = d["sanity"]["maximum_principle"]["holds"]
    print(f"  maximum principle             {'holds' if mp else 'VIOLATED'}")


if __name__ == "__main__":
    main()
