"""WSGI entrypoint for production.

    gunicorn -w 1 --threads 4 -b 0.0.0.0:5000 wsgi:app

STILL ONE worker for now, though the main reason is gone: every run writes its
own runs/<run_id>.{db,json}, so two processes no longer share output files.

What remains before raising it:
  - The rate limiter counts in process memory, so N workers would allow N
    times the configured limit. It needs shared storage (redis://) first.
  - SIM_LOCK now only caps concurrent solver processes within one worker.
    N workers means up to N simultaneous solvers, so the box needs the cores
    to match.
  - runs/ is swept per process; several workers sweeping the same directory is
    harmless (each tolerates files vanishing underneath it) but untested at
    more than one.

Threads are fine: SIM_LOCK serialises the simulations themselves, while frame
reads (/frames, /run?time=) stay responsive.
"""
from server import app

__all__ = ['app']
