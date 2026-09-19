FROM python:3.10-slim AS runtime

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1 \
    QIBAN_SERVER_HOST=0.0.0.0 \
    QIBAN_SERVER_PORT=8787 \
    QIBAN_SERVER_DATA_DIR=/srv/qiban-relay/data \
    QIBAN_SERVER_DB_PATH=/srv/qiban-relay/data/qiban_relay.sqlite3

WORKDIR /app

COPY requirements.txt /app/requirements.txt
RUN python -m pip install --upgrade pip && \
    python -m pip install -r /app/requirements.txt

COPY app /app/app

EXPOSE 8787

HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
  CMD python -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8787/healthz', timeout=3).read()"

CMD ["sh", "-c", "uvicorn app.main:app --host ${QIBAN_SERVER_HOST} --port ${QIBAN_SERVER_PORT}"]
