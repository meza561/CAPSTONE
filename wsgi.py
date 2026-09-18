"""WSGI entrypoint for production.

    gunicorn -w 1 --threads 4 -b 0.0.0.0:5000 wsgi:app

ONE worker, deliberately. The solver writes to a single heat_sim.db and a
single latest_heatmap.json, and the only thing keeping two simultaneous runs
from interleaving is SIM_LOCK - a lock inside one process. A second worker has
its own lock and would corrupt both files. The in-memory rate limiter counts
per process too, so N workers would mean N times the configured limit.

Threads are fine: SIM_LOCK serialises the simulations themselves, while frame
reads (/frames, /run?time=) stay responsive.

To scale past one process the shared state has to move out of the filesystem
first - per-run database files or a real database, plus shared storage for the
limiter.
"""
from server import app

__all__ = ['app']
