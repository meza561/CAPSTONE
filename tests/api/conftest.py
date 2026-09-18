import os
import sqlite3
import sys

import pytest

# server.py lives at the repo root, which is not on sys.path when pytest is
# invoked as `pytest tests/api` rather than `python -m pytest`.
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..')))

import server  # noqa: E402


@pytest.fixture
def client(tmp_path, monkeypatch):
    """Flask test client, with the CWD moved to an empty tmp dir.

    server.py addresses heat_sim.db and latest_heatmap.json relative to the
    working directory, so this isolates every test from the real ones without
    having to monkeypatch the module's constants.
    """
    monkeypatch.chdir(tmp_path)
    # The limiter counts per IP in process memory, so every test shares one
    # counter and would eventually exhaust it. Off unless a test asks for it.
    monkeypatch.setattr(server.limiter, 'enabled', False)
    return server.app.test_client()


def seed_db(frames, path='heat_sim.db'):
    """Write frames as {step: [[temp, ...], ...]} into a HeatMap table.

    Same schema database.cpp creates: (step, x, y) as the whole primary key,
    with x the row index and y the column index.
    """
    conn = sqlite3.connect(path)
    conn.execute('CREATE TABLE HeatMap ('
                 'step INTEGER NOT NULL, x INTEGER NOT NULL, y INTEGER NOT NULL, '
                 'temp REAL NOT NULL, PRIMARY KEY (step, x, y)) WITHOUT ROWID;')
    for step, grid in frames.items():
        for x, row in enumerate(grid):
            for y, temp in enumerate(row):
                conn.execute('INSERT INTO HeatMap VALUES (?, ?, ?, ?)', (step, x, y, temp))
    conn.commit()
    conn.close()
