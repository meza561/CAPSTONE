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

def get_grid_from_db(step):
    conn = sqlite3.connect(DB_NAME)
    cursor = conn.cursor()
    # Assuming the table structure stores points as (step, x, y, temp)
    cursor.execute("SELECT x, y, temp FROM timesteps WHERE step = ? ORDER BY x, y", (step,))
    rows_data = cursor.fetchall()
    conn.close()
    
    if not rows_data:
        return None

    # Determine grid dimensions
    max_x = max(r[0] for r in rows_data) + 1
    max_y = max(r[1] for r in rows_data) + 1
    
    grid = [[0.0 for _ in range(max_y)] for _ in range(max_x)]
    for x, y, temp in rows_data:
        grid[x][y] = temp
        
    return {"step": step, "rows": max_x, "cols": max_y, "data": grid}

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
        rows = str(data.get('rows', 20))
        cols = str(data.get('cols', 20))
        top = str(data.get('top', 100))
        bottom = str(data.get('bottom', 0))
        left = str(data.get('left', 0))
        right = str(data.get('right', 0))
        mode = str(data.get('mode', 'fdm'))
        alpha = str(data.get('alpha', 0.01))

        # Execute the C++ binary
        cmd = [SIM_BINARY, rows, cols, top, bottom, left, right, mode, alpha]
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
    app.run(host='0.0.0.0', port=5000, debug=True)
