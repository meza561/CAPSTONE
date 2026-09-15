import os
import subprocess
import json
from flask import Flask, request, jsonify, send_from_directory
from flask_cors import CORS

app = Flask(__name__)
CORS(app)

# Path to the compiled C++ binary
SIM_BINARY = "./heat_sim"
WEB_DIR = "./web"

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

        # Execute the C++ binary
        cmd = [SIM_BINARY, rows, cols, top, bottom, left, right]
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)

        # The binary writes to latest_heatmap.json
        with open('latest_heatmap.json', 'r') as f:
            heatmap_data = json.load(f)

        return jsonify({
            "status": "success",
            "output": result.stdout,
            "data": heatmap_data
        })
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

if __name__ == '__main__':
    # Ensure the binary exists
    if not os.path.exists(SIM_BINARY):
        print(f"Error: {SIM_BINARY} not found. Please build the project first.")
        exit(1)
    
    print("Starting Heat Simulation Server on http://127.0.0.1:5000")
    app.run(host='0.0.0.0', port=5000, debug=True)
