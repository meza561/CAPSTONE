"""Render web/og-image.png, the 1200x630 social preview card.

Committed as an asset because it is referenced by the Open Graph tags, but
generated here so it cannot drift from the app's palette: the background is the
same thermal ramp (blue -> green -> red) that script.js paints the plate with,
and the panel colours come from styles.css.

    venv/bin/python make_og_image.py
"""
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

EMERALD = '#064E3B'
CHAMPAGNE = '#F8E7C9'
TEXT_MAIN = '#1a2e26'
W, H, DPI = 1200, 630, 100


def thermal(t):
    """The COLOR_MAPS.thermal ramp from web/script.js, vectorised."""
    return np.stack([
        np.where(t > 0.5, (t - 0.5) * 2, 0.0),
        1 - np.abs(t - 0.5) * 2,
        np.where(t < 0.5, (0.5 - t) * 2, 0.0),
    ], axis=-1)


def main():
    fig = plt.figure(figsize=(W / DPI, H / DPI), dpi=DPI)
    fig.patch.set_facecolor(CHAMPAGNE)

    # The app's canonical run: one hot edge along the top, cooling downward and
    # toward the cold sides. Held at low opacity so it tints the champagne
    # rather than replacing it - this is a backdrop, not the plate itself.
    ax = fig.add_axes([0, 0, 1, 1])
    yn, xn = np.mgrid[0:1:256j, 0:1:256j]
    field = np.clip((1 - yn) ** 1.7 * np.sin(np.pi * xn) ** 0.6, 0, 1)
    # Composited on intensity rather than a flat alpha: cool cells stay
    # champagne instead of blending toward the cold end of the ramp, which over
    # a cream background comes out lavender.
    champagne = np.array([0xF8, 0xE7, 0xC9]) / 255
    weight = (field ** 0.9)[..., None] * 0.9
    ax.imshow(champagne * (1 - weight) + thermal(field) * weight,
              aspect='auto', interpolation='bilinear')
    ax.set_axis_off()

    # Panel, matching the app's cards.
    panel = fig.add_axes([0.06, 0.16, 0.88, 0.68])
    panel.set_facecolor('white')
    panel.set_xticks([]); panel.set_yticks([])
    for side in panel.spines.values():
        side.set_edgecolor('#d1c7b3')

    panel.text(0.5, 0.74, 'Heat Simulation Visualizer', transform=panel.transAxes,
               ha='center', va='center', fontsize=40, fontweight='bold', color=EMERALD)
    panel.text(0.5, 0.50, '2D heat equation, solved three ways',
               transform=panel.transAxes, ha='center', va='center',
               fontsize=21, color=TEXT_MAIN)
    panel.text(0.5, 0.31,
               'Explicit  ·  Backward Euler  ·  Crank–Nicolson  ·  Reaction–diffusion',
               transform=panel.transAxes, ha='center', va='center',
               fontsize=15, color='#52645c')

    # The legend ramp, as it appears under the plate.
    bar = fig.add_axes([0.30, 0.235, 0.40, 0.028])
    bar.imshow(thermal(np.linspace(0, 1, 256))[None, :, :], aspect='auto')
    bar.set_xticks([]); bar.set_yticks([])
    for side in bar.spines.values():
        side.set_edgecolor('#d1c7b3')

    fig.savefig('web/og-image.png', dpi=DPI, facecolor=CHAMPAGNE)
    print(f'wrote web/og-image.png ({W}x{H})')


if __name__ == '__main__':
    main()
