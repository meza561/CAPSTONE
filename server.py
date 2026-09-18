import os
import subprocess
import threading
import json
from flask import Flask, Response, request, jsonify, render_template, send_from_directory
from flask_cors import CORS
from flask_limiter import Limiter
from flask_limiter.util import get_remote_address
from flask_talisman import Talisman
from werkzeug.middleware.proxy_fix import ProxyFix
import sqlite3

# web/ holds both the static assets and the two pages rendered as templates
# (index.html needs SITE_URL in its Open Graph tags, 404.html is served by the
# error handler), so it serves as the template folder too.
app = Flask(__name__, template_folder='web')

# Deployment settings. Everything has a local-dev default, so ./start.sh needs
# no environment at all.
IS_PROD = os.environ.get('FLASK_ENV', 'development') == 'production'
ALLOWED_ORIGIN = os.environ.get('ALLOWED_ORIGIN', 'http://localhost:5000')
# Public base URL for absolute links crawlers and social scrapers need.
SITE_URL = os.environ.get('SITE_URL', 'http://localhost:5000').rstrip('/')

# The frontend is served from this same origin, so nothing needs cross-origin
# access in normal use; this exists for the case where the UI is hosted
# elsewhere. Scoped to the API rather than the whole app.
CORS(app, resources={r"/(run|frames|study)": {"origins": [ALLOWED_ORIGIN]}})

# Every POST /run spawns a solver process, so it is the one route worth
# capping. SIM_LOCK already serialises the work itself; this stops a flood of
# requests queueing up behind it. In-memory storage is the right fit because
# the app is single-process by design (see the note on SIM_LOCK below).
RUN_RATE_LIMIT = os.environ.get('RUN_RATE_LIMIT', '60 per minute')
limiter = Limiter(get_remote_address, app=app, storage_uri='memory://')

# Security headers. Everything here is served from this origin: the stylesheet,
# the two scripts, the preview image. The one exception is the favicon, an
# inline SVG data: URI, which is why img-src has to allow data:.
Talisman(
    app,
    force_https=IS_PROD,          # off locally, so http://localhost still works
    strict_transport_security=IS_PROD,
    strict_transport_security_max_age=31536000,
    session_cookie_secure=IS_PROD,
    content_security_policy={
        'default-src': "'self'",
        'img-src': ["'self'", 'data:'],
        'style-src': "'self'",
        'script-src': "'self'",
        'base-uri': "'none'",
        'frame-ancestors': "'none'",
        'form-action': "'self'",
    },
    referrer_policy='strict-origin-when-cross-origin',
)

if IS_PROD:
    # Behind a TLS-terminating proxy the app only ever sees http, so without
    # this force_https redirects forever. One proxy hop.
    app.wsgi_app = ProxyFix(app.wsgi_app, x_for=1, x_proto=1, x_host=1)


@app.errorhandler(429)
def rate_limited(e):
    """JSON, not Flask's HTML error page: every caller here speaks JSON."""
    return jsonify({
        "status": "error",
        "message": f"Rate limit exceeded ({e.description}). Try again shortly.",
    }), 429

# The simulation writes to one fixed database and one fixed JSON file, so two
# overlapping requests would interleave and corrupt each other's results.
SIM_LOCK = threading.Lock()

# Path to the compiled C++ binary
SIM_BINARY = "./heat_sim"
MAX_SOURCES = 32
WEB_DIR = "./web"
DB_NAME = "heat_sim.db"

def _clamp(value, lo, hi, default):
    """Coerce a client-supplied dimension into a sane range."""
    try:
        v = int(value)
    except (TypeError, ValueError):
        return default
    return max(lo, min(hi, v))


def _clamp_float(value, lo, hi, default):
    """Coerce a client-supplied float into a sane range."""
    try:
        v = float(value)
    except (TypeError, ValueError):
        return default
    if v != v:  # NaN
        return default
    return max(lo, min(hi, v))


def get_grid_from_db(step):
    """Return the stored frame at or before `step`.

    Timesteps are persisted at an interval (SAVE_INTERVAL in main.cpp), so an
    arbitrary requested step is snapped down to the nearest stored frame,
    falling back to the earliest frame available.
    """
    if not os.path.exists(DB_NAME):
        return None

    conn = sqlite3.connect(DB_NAME)
    try:
        cursor = conn.cursor()

        # Index-only probe against the (step, x, y) primary key.
        cursor.execute(
            "SELECT step FROM HeatMap WHERE step <= ? ORDER BY step DESC LIMIT 1",
            (step,),
        )
        row = cursor.fetchone()
        if row is None:
            cursor.execute("SELECT step FROM HeatMap ORDER BY step ASC LIMIT 1")
            row = cursor.fetchone()
        if row is None:
            return None

        actual_step = row[0]
        cursor.execute(
            "SELECT x, y, temp FROM HeatMap WHERE step = ? ORDER BY x, y",
            (actual_step,),
        )
        rows_data = cursor.fetchall()
    except sqlite3.OperationalError:
        # No simulation has been run yet, so the table does not exist.
        return None
    finally:
        conn.close()

    if not rows_data:
        return None

    max_x = max(r[0] for r in rows_data) + 1
    max_y = max(r[1] for r in rows_data) + 1

    grid = [[0.0] * max_y for _ in range(max_x)]
    for x, y, temp in rows_data:
        grid[x][y] = temp

    return {"step": actual_step, "rows": max_x, "cols": max_y, "data": grid}


@app.route('/')
def index():
    # Rendered rather than served flat, so the Open Graph and Twitter tags can
    # carry an absolute SITE_URL without the domain being baked into the file.
    return render_template('index.html', site_url=SITE_URL)

@app.route('/robots.txt')
def robots():
    # The API routes hold no crawlable content; keeping them out saves crawl
    # budget and avoids bots POSTing nothing useful at the solver.
    body = ('User-agent: *\n'
            'Allow: /\n'
            'Disallow: /run\n'
            'Disallow: /frames\n'
            'Disallow: /study\n'
            f'Sitemap: {SITE_URL}/sitemap.xml\n')
    return Response(body, mimetype='text/plain')

@app.route('/sitemap.xml')
def sitemap():
    # One page: the simulator itself. Everything else is an API route or an
    # asset, neither of which belongs in a sitemap.
    xml = ('<?xml version="1.0" encoding="UTF-8"?>\n'
           '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n'
           f'  <url><loc>{SITE_URL}/</loc><changefreq>monthly</changefreq></url>\n'
           '</urlset>\n')
    return Response(xml, mimetype='application/xml')

@app.route('/<path:path>')
def static_files(path):
    return send_from_directory(WEB_DIR, path)


@app.errorhandler(404)
def not_found(e):
    # The API's own 404s are returned directly by their routes; this catches
    # everything else, which means a browser at a bad URL - so answer with the
    # page, and keep JSON for anything under the API paths.
    if request.path.startswith(('/run', '/frames', '/study')):
        return jsonify({"status": "error", "message": "Not found"}), 404
    return render_template('404.html'), 404

@app.route('/study')
def study():
    """Serve the convergence/stability results produced by ./run_study.sh."""
    path = 'study_results.json'
    if not os.path.exists(path):
        # 200, not 404: "not generated yet" is the normal state of a fresh
        # checkout (the file is a gitignored ./run_study.sh output), and the
        # browser logs every non-2xx fetch as a console error.
        return jsonify({
            "status": "missing",
            "message": "No study results found."
        })
    try:
        with open(path) as f:
            return jsonify(json.load(f))
    except (OSError, ValueError) as e:
        return jsonify({"status": "error",
                        "message": f"Could not read study results: {e}"}), 500


@app.route('/frames')
def frames():
    """The step numbers stored for the current run, in order.

    The UI slider indexes this list. Without it the client had to guess which
    steps exist from the save interval, round to whole seconds, and let the
    server snap to whatever was nearest - so the time it displayed was not
    quite the time of the frame it was showing.
    """
    if not os.path.exists(DB_NAME):
        return jsonify({"steps": []})

    conn = sqlite3.connect(DB_NAME)
    try:
        cursor = conn.cursor()
        # Index-only scan of the (step, x, y) primary key.
        cursor.execute("SELECT DISTINCT step FROM HeatMap ORDER BY step")
        steps = [r[0] for r in cursor.fetchall()]
    except sqlite3.OperationalError:
        steps = []          # nothing has been run yet
    finally:
        conn.close()

    return jsonify({"steps": steps})


@app.route('/run', methods=['POST'])
@limiter.limit(lambda: RUN_RATE_LIMIT)
def run_simulation():
    data = request.json
    try:
        # Bound grid size: an unbounded grid is what let the database grow
        # to multiple gigabytes.
        n_rows = _clamp(data.get('rows', 20), 2, 300, 20)
        n_cols = _clamp(data.get('cols', 20), 2, 300, 20)
        rows, cols = str(n_rows), str(n_cols)
        top = str(_clamp_float(data.get('top', 100), -1e6, 1e6, 100.0))
        bottom = str(_clamp_float(data.get('bottom', 0), -1e6, 1e6, 0.0))
        left = str(_clamp_float(data.get('left', 0), -1e6, 1e6, 0.0))
        right = str(_clamp_float(data.get('right', 0), -1e6, 1e6, 0.0))
        # 'fdm' is kept as an alias for the explicit scheme so older saved
        # runs and the previous API shape still work.
        mode = str(data.get('mode', 'fdm'))
        if mode == 'explicit':
            mode = 'fdm'
        if mode not in ('fdm', 'be', 'cn', 'pde', 'fisher', 'gray-scott'):
            mode = 'fdm'

        # Diffusion number. The explicit scheme is only stable to 0.25, so it
        # is held there; the implicit schemes are unconditionally stable and
        # take whatever the user asks for.
        if mode == 'fdm':
            diffusion = 0.2
        else:
            diffusion = _clamp_float(data.get('F', 5.0), 1e-4, 1000.0, 5.0)
        # alpha sets the timestep (dt = 0.2*dx^2/alpha), so a zero or negative
        # value would be a division by zero in the solver.
        alpha = str(_clamp_float(data.get('alpha', 0.01), 1e-4, 1e3, 0.01))
        
        # Point source parameters
        # Heat sources arrive as [{x, y, temp}, ...] in UI coordinates:
        # origin bottom-left, x to the right, y upward. The solver indexes
        # [row][col] with row 0 at the top, so the flip happens here and
        # nowhere else.
        raw_sources = data.get('sources')
        if raw_sources is None:
            # Backward compatibility with the old single-source payload.
            if data.get('hasPointSource'):
                raw_sources = [{
                    'x': data.get('psC', 0),
                    'y': (n_rows - 1) - _clamp(data.get('psR', 0), 0, n_rows - 1, 0),
                    'temp': data.get('psTemp', 0),
                }]
            else:
                raw_sources = []
        if not isinstance(raw_sources, list):
            raw_sources = []

        source_args = []
        for s in raw_sources[:MAX_SOURCES]:
            if not isinstance(s, dict):
                continue
            # Clamping rather than rejecting: the solver silently ignores an
            # out-of-range coordinate, which looks like the feature doing
            # nothing at all.
            x = _clamp(s.get('x', 0), 0, n_cols - 1, 0)
            y = _clamp(s.get('y', 0), 0, n_rows - 1, 0)
            t = _clamp_float(s.get('temp', 0), -1e6, 1e6, 0.0)
            row = (n_rows - 1) - y      # flip: y counts up from the bottom
            source_args.extend([str(row), str(x), str(t)])

        # Execute the C++ binary
        cmd = [SIM_BINARY, rows, cols, top, bottom, left, right, mode, alpha,
               str(diffusion)]

        if mode in ('fisher', 'gray-scott'):
            # Reaction-diffusion takes its own parameters instead of heat
            # sources; boundary temperatures do not apply (edges are zero-flux).
            steps = _clamp(data.get('rdSteps', 5000), 1, 200000, 5000)
            if mode == 'gray-scott':
                cmd.extend([
                    str(_clamp_float(data.get('Du', 0.16), 1e-4, 0.25, 0.16)),
                    str(_clamp_float(data.get('Dv', 0.08), 1e-4, 0.25, 0.08)),
                    str(_clamp_float(data.get('feed', 0.035), 0.0, 0.2, 0.035)),
                    str(_clamp_float(data.get('kill', 0.065), 0.0, 0.2, 0.065)),
                    str(steps)])
            else:
                cmd.extend([
                    str(_clamp_float(data.get('rdD', 0.2), 1e-4, 100.0, 0.2)),
                    str(_clamp_float(data.get('rdR', 1.0), 1e-4, 100.0, 1.0)),
                    str(steps)])
        else:
            cmd.extend(source_args)

            # Insulated (zero-flux) edges.
            ins = data.get('insulated') or {}
            flags = ''.join(c for c, key in
                            (('t', 'top'), ('b', 'bottom'), ('l', 'left'), ('r', 'right'))
                            if bool(ins.get(key)))
            if flags:
                cmd.append('--insulate=' + flags)

            # Material regions, given as (x, y) rectangles in UI coordinates
            # with the origin bottom-left; converted to row/col here, the same
            # place the heat-source flip happens.
            for mat in (data.get('materials') or [])[:MAX_SOURCES]:
                if not isinstance(mat, dict):
                    continue
                x0 = _clamp(mat.get('x0', 0), 0, n_cols - 1, 0)
                x1 = _clamp(mat.get('x1', 0), 0, n_cols - 1, 0)
                y0 = _clamp(mat.get('y0', 0), 0, n_rows - 1, 0)
                y1 = _clamp(mat.get('y1', 0), 0, n_rows - 1, 0)
                av = _clamp_float(mat.get('alpha', 0.01), 1e-6, 1e3, 0.01)
                r0 = (n_rows - 1) - max(y0, y1)
                r1 = (n_rows - 1) - min(y0, y1)
                cmd.append('--material=%d,%d,%d,%d,%g' % (r0, min(x0, x1), r1, max(x0, x1), av))
            
        try:
            with SIM_LOCK:
                result = subprocess.run(cmd, capture_output=True, text=True,
                                        check=True, timeout=300)
                # Read inside the lock: the binary rewrites this same file on
                # every run, so releasing first would let a concurrent request
                # swap it out between the run and the read.
                with open('latest_heatmap.json', 'r') as f:
                    heatmap_data = json.load(f)
        except subprocess.TimeoutExpired:
            msg = "Simulation timed out after 300s"
            print("ERROR:", msg, flush=True)
            return jsonify({"status": "error", "message": msg}), 500
        except FileNotFoundError:
            msg = (f"Simulation binary not found at {SIM_BINARY}. "
                   f"Build it first by running ./start.sh")
            print("ERROR:", msg, flush=True)
            return jsonify({"status": "error", "message": msg}), 500
        except subprocess.CalledProcessError as e:
            detail = (e.stderr or e.stdout or "").strip() or f"exit status {e.returncode}"
            msg = f"Simulation failed: {detail}"
            print("ERROR:", msg, flush=True)
            return jsonify({"status": "error", "message": msg}), 500

        return jsonify({
            "status": "success",
            "output": result.stdout,
            "data": heatmap_data
        })
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/run', methods=['GET'])
def get_timestep():
    try:
        step = request.args.get('time', type=int)
        if step is None:
            return jsonify({"status": "error", "message": "Time parameter required"}), 400
        
        heatmap_data = get_grid_from_db(step)
        if not heatmap_data:
            return jsonify({"status": "error", "message": "Timestep not found"}), 404
            
        return jsonify(heatmap_data)
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

if __name__ == '__main__':
    if not os.path.exists(SIM_BINARY):
        print(f"Error: {SIM_BINARY} not found. Please build the project first.")
        exit(1)
    
    port = int(os.environ.get('PORT', 5000))
    print(f"Starting Heat Simulation Server on http://127.0.0.1:{port}")
    app.run(host='0.0.0.0', port=port, debug=False)
