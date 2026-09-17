"""Unit tests for the Flask layer in server.py.

These cover the API in isolation - clamping, the frame lookup, and the
subprocess error paths - with Flask's test client rather than a live server
and a real browser, so nothing here needs the compiled binary. The Playwright
suite in tests/ui exercises the same endpoints end to end.
"""
import json
import subprocess
import types

import pytest

import server
from conftest import seed_db


# ---- clamping ---------------------------------------------------------

@pytest.mark.parametrize('value, expected', [
    (20, 20),          # in range, passed through
    (1, 2),            # below the floor
    (999, 300),        # above the ceiling
    ('40', 40),        # numeric string, as query/JSON values arrive
    ('abc', 20),       # not a number at all
    (None, 20),        # missing
    (float('nan'), 20),  # int(nan) raises, so this lands on the default too
])
def test_clamp(value, expected):
    assert server._clamp(value, 2, 300, 20) == expected


@pytest.mark.parametrize('value, expected', [
    (0.5, 0.5),
    (-1e9, -1e6),
    (1e9, 1e6),
    ('2.5', 2.5),
    ('abc', 100.0),
    (None, 100.0),
    (float('nan'), 100.0),
])
def test_clamp_float(value, expected):
    assert server._clamp_float(value, -1e6, 1e6, 100.0) == expected


def test_clamp_float_keeps_infinity_distinct_from_nan():
    # inf is a real float, so it clamps rather than falling back.
    assert server._clamp_float(float('inf'), 0.0, 10.0, 5.0) == 10.0


# ---- GET /frames ------------------------------------------------------

def test_frames_without_a_database(client):
    assert client.get('/frames').get_json() == {'steps': []}


def test_frames_lists_sorted_distinct_steps(client):
    seed_db({20: [[1.0]], 0: [[2.0]], 10: [[3.0]]})
    assert client.get('/frames').get_json() == {'steps': [0, 10, 20]}


def test_frames_survives_a_database_with_no_table(client):
    # A file exists but the run never got as far as creating HeatMap.
    open('heat_sim.db', 'wb').close()
    assert client.get('/frames').get_json() == {'steps': []}


# ---- GET /run?time= ---------------------------------------------------

def test_get_run_requires_a_time(client):
    res = client.get('/run')
    assert res.status_code == 400
    assert res.get_json()['message'] == 'Time parameter required'


def test_get_run_rejects_a_non_integer_time(client):
    # Flask's type=int yields None for junk, same as a missing parameter.
    assert client.get('/run?time=abc').status_code == 400


def test_get_run_without_stored_data(client):
    assert client.get('/run?time=0').status_code == 404


def test_get_run_snaps_down_to_the_nearest_stored_frame(client):
    seed_db({0: [[0.0, 1.0]], 10: [[10.0, 11.0]], 20: [[20.0, 21.0]]})

    body = client.get('/run?time=15').get_json()
    assert body['step'] == 10
    assert body['data'] == [[10.0, 11.0]]
    assert (body['rows'], body['cols']) == (1, 2)

    # An exact hit returns that frame, not the one before it.
    assert client.get('/run?time=20').get_json()['step'] == 20


def test_get_run_falls_back_to_the_earliest_frame(client):
    # Nothing is stored at or before the requested step.
    seed_db({10: [[10.0]], 20: [[20.0]]})
    assert client.get('/run?time=5').get_json()['step'] == 10


def test_get_run_rebuilds_the_grid_shape(client):
    seed_db({0: [[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]]})
    body = client.get('/run?time=0').get_json()
    assert (body['rows'], body['cols']) == (2, 3)
    assert body['data'] == [[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]]


# ---- POST /run --------------------------------------------------------

def fake_run(recorder, stdout='done\n'):
    """A subprocess.run stand-in that records argv instead of executing."""
    def run(cmd, **kwargs):
        recorder.append(cmd)
        return types.SimpleNamespace(stdout=stdout, stderr='', returncode=0)
    return run


def write_heatmap(payload=None):
    """The output file the binary would have written, which the route reads."""
    with open('latest_heatmap.json', 'w') as f:
        json.dump(payload or {'step': 5, 'rows': 1, 'cols': 1, 'dt': 20.0,
                              'saveInterval': 10, 'data': [[1.0]]}, f)


def test_post_run_success(client, monkeypatch):
    calls = []
    monkeypatch.setattr(server.subprocess, 'run', fake_run(calls))
    write_heatmap()

    res = client.post('/run', json={'rows': 20, 'cols': 20, 'mode': 'fdm'})
    assert res.status_code == 200
    body = res.get_json()
    assert body['status'] == 'success'
    assert body['output'] == 'done\n'
    assert body['data']['step'] == 5
    assert calls[0][0] == server.SIM_BINARY


def test_post_run_reports_a_timeout(client, monkeypatch):
    def boom(cmd, **kwargs):
        raise subprocess.TimeoutExpired(cmd, 300)
    monkeypatch.setattr(server.subprocess, 'run', boom)

    res = client.post('/run', json={})
    assert res.status_code == 500
    assert res.get_json()['message'] == 'Simulation timed out after 300s'


def test_post_run_reports_a_missing_binary(client, monkeypatch):
    def boom(cmd, **kwargs):
        raise FileNotFoundError()
    monkeypatch.setattr(server.subprocess, 'run', boom)

    res = client.post('/run', json={})
    assert res.status_code == 500
    message = res.get_json()['message']
    assert server.SIM_BINARY in message and './start.sh' in message


def test_post_run_surfaces_solver_stderr(client, monkeypatch):
    def boom(cmd, **kwargs):
        raise subprocess.CalledProcessError(1, cmd, output='', stderr='grid too large\n')
    monkeypatch.setattr(server.subprocess, 'run', boom)

    res = client.post('/run', json={})
    assert res.status_code == 500
    assert res.get_json()['message'] == 'Simulation failed: grid too large'


def test_post_run_falls_back_to_the_exit_status(client, monkeypatch):
    # Nothing on either stream: report something more useful than an empty string.
    def boom(cmd, **kwargs):
        raise subprocess.CalledProcessError(3, cmd, output='', stderr='')
    monkeypatch.setattr(server.subprocess, 'run', boom)

    assert 'exit status 3' in client.post('/run', json={}).get_json()['message']


# ---- POST /run argv construction --------------------------------------

def test_post_run_flips_heat_sources_into_row_col(client, monkeypatch):
    calls = []
    monkeypatch.setattr(server.subprocess, 'run', fake_run(calls))
    write_heatmap()

    client.post('/run', json={
        'rows': 10, 'cols': 10, 'mode': 'fdm',
        'hasPointSource': True, 'sources': [{'x': 3, 'y': 2, 'temp': 500}],
        'insulated': {'top': True, 'left': True},
    })

    cmd = calls[0]
    assert cmd[7] == 'fdm'
    # y counts up from the bottom, so row = (rows - 1) - y.
    assert cmd[10:13] == ['7', '3', '500.0']
    assert '--insulate=tl' in cmd


def test_post_run_clamps_an_out_of_range_source(client, monkeypatch):
    calls = []
    monkeypatch.setattr(server.subprocess, 'run', fake_run(calls))
    write_heatmap()

    client.post('/run', json={
        'rows': 10, 'cols': 10, 'mode': 'fdm',
        'hasPointSource': True, 'sources': [{'x': 999, 'y': 999, 'temp': 50}],
    })

    # Clamped to the last column and the top row, not dropped.
    assert calls[0][10:13] == ['0', '9', '50.0']


def test_post_run_builds_fisher_arguments(client, monkeypatch):
    calls = []
    monkeypatch.setattr(server.subprocess, 'run', fake_run(calls))
    write_heatmap()

    client.post('/run', json={
        'rows': 20, 'cols': 20, 'mode': 'fisher',
        'rdD': 0.3, 'rdR': 2.0, 'rdSteps': 123,
        # Reaction-diffusion ignores these: edges are zero-flux and it seeds itself.
        'hasPointSource': True, 'sources': [{'x': 1, 'y': 1, 'temp': 100}],
    })

    cmd = calls[0]
    assert cmd[7] == 'fisher'
    assert cmd[10:] == ['0.3', '2.0', '123']
    assert not any(arg.startswith('--insulate=') for arg in cmd)


def test_post_run_builds_gray_scott_arguments(client, monkeypatch):
    calls = []
    monkeypatch.setattr(server.subprocess, 'run', fake_run(calls))
    write_heatmap()

    client.post('/run', json={
        'rows': 128, 'cols': 128, 'mode': 'gray-scott',
        'Du': 0.16, 'Dv': 0.08, 'feed': 0.035, 'kill': 0.065, 'rdSteps': 900,
    })

    assert calls[0][10:] == ['0.16', '0.08', '0.035', '0.065', '900']


def test_post_run_pins_the_explicit_scheme_to_a_stable_diffusion_number(client, monkeypatch):
    calls = []
    monkeypatch.setattr(server.subprocess, 'run', fake_run(calls))
    write_heatmap()

    # F is a free parameter for the implicit schemes only; the explicit one
    # diverges above 0.25, so whatever the client asks for is ignored.
    client.post('/run', json={'mode': 'fdm', 'F': 500})
    assert calls[0][9] == '0.2'

    client.post('/run', json={'mode': 'cn', 'F': 500})
    assert calls[1][9] == '500.0'


def test_post_run_rejects_an_unknown_mode(client, monkeypatch):
    calls = []
    monkeypatch.setattr(server.subprocess, 'run', fake_run(calls))
    write_heatmap()

    client.post('/run', json={'mode': 'nonsense'})
    assert calls[0][7] == 'fdm'


def test_post_run_accepts_the_legacy_single_source_payload(client, monkeypatch):
    calls = []
    monkeypatch.setattr(server.subprocess, 'run', fake_run(calls))
    write_heatmap()

    # Older clients sent psR/psC/psTemp in row/col terms instead of sources[].
    client.post('/run', json={
        'rows': 10, 'cols': 10, 'mode': 'fdm',
        'hasPointSource': True, 'psR': 2, 'psC': 3, 'psTemp': 250,
    })

    assert calls[0][10:13] == ['2', '3', '250.0']
