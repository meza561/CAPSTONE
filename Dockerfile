# Two stages: the C++ solver needs a compiler and SQLite headers, the runtime
# needs neither. Cloudflare Pages/Workers cannot run this image - it spawns a
# process per simulation and writes files - so it wants a container host
# (Fly.io, Render, Railway, a VPS) with Cloudflare in front for TLS/CDN.

FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
# Only what the binary needs, so a web/ or test edit does not rebuild it.
COPY main.cpp database.cpp database.hpp simulation.hpp reaction.hpp wave.hpp ./
RUN g++ -std=c++17 -O2 main.cpp database.cpp -lsqlite3 -o heat_sim

FROM python:3.12-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        libsqlite3-0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

COPY --from=build /src/heat_sim ./heat_sim
COPY server.py wsgi.py ./
COPY web/ ./web/

# server.py resolves ./heat_sim and runs/ relative to the working directory.
RUN mkdir -p runs && useradd --create-home app && chown -R app:app /app
USER app

ENV FLASK_ENV=production \
    PORT=8080 \
    PYTHONUNBUFFERED=1
EXPOSE 8080

# One worker: the rate limiter counts in process memory and SIM_LOCK caps
# concurrent solvers per process. See wsgi.py before raising it. Threads keep
# frame reads responsive while a simulation holds the lock.
CMD gunicorn -w 1 --threads 4 -b 0.0.0.0:${PORT} --access-logfile - wsgi:app
