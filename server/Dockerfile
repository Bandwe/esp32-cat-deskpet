FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_DISABLE_PIP_VERSION_CHECK=1

RUN groupadd --gid 10001 esp \
    && useradd --uid 10001 --gid esp --create-home --shell /usr/sbin/nologin esp \
    && mkdir /data \
    && chown esp:esp /data

WORKDIR /app
COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

COPY app.py card_renderer.py ./
COPY templates ./templates
COPY static ./static
COPY fonts ./fonts

USER 10001:10001
EXPOSE 8000
CMD ["gunicorn", "--bind", "0.0.0.0:8000", "--workers", "1", "--threads", "4", "--timeout", "30", "app:create_app()"]
