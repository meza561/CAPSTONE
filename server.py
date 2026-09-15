import os
import subprocess
import json
from flask import Flask, request, jsonify, send_from_directory
from flask_cors import CORS
import sqlite3

app = Flask(__name__)
CORS(app)

# Path to the compiled C++ binary
SIM_BINARY = "./heat_sim"
WEB_DIR = "./web"
DB_NAME = "heat_sim.db"

def _clamp(value, lo, hi, default):
    """Coerce a client-supplied dimension into a sane range."""
    try:
        v = int(value)
    except (TypeError, ValueError):
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
    return send_from_directory(WEB_DIR, 'index.html')

@app.route('/<path:path>')
def static_files(path):
    return send_from_directory(WEB_DIR, path)

@app.route('/run', methods=['POST'])
def run_simulation():
    data = request.json
    try:
        # Bound grid size: an unbounded grid is what let the database grow
        # to multiple gigabytes.
        rows = str(_clamp(data.get('rows', 20), 2, 300, 20))
        cols = str(_clamp(data.get('cols', 20), 2, 300, 20))
        top = str(data.get('top', 100))
        bottom = str(data.get('bottom', 0))
        left = str(data.get('left', 0))
        right = str(data.get('right', 0))
        mode = str(data.get('mode', 'fdm'))
        alpha = str(data.get('alpha', 0.01))
        
        # Point source parameters
        has_ps = data.get('hasPointSource', False)
        ps_r = str(data.get('psR', 0))
        ps_c = str(data.get('psC', 0))
        ps_temp = str(data.get('psTemp', 0))

        # Execute the C++ binary
        cmd = [SIM_BINARY, rows, cols, top, bottom, left, right, mode, alpha]
        if has_ps:
            cmd.extend([ps_r, ps_c, ps_temp])
            
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)

        # Return the latest state by default
        with open('latest_heatmap.json', 'r') as f:
            heatmap_data = json.load(f)

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
    
    print("Starting Heat Simulation Server on http://127.0.0.1:5000")
    app.run(host='0.0.0.0', port=5000, debug=False)
