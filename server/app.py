"""Owner-only message composer and a narrow, token-protected ESP device API."""

from __future__ import annotations

import hashlib
import hmac
import re
import secrets
import sqlite3
import unicodedata
import zlib
from datetime import datetime, timezone
from functools import wraps
from pathlib import Path
from urllib.parse import urlsplit

from flask import Flask, Response, abort, g, jsonify, redirect, render_template, request, session, url_for
from werkzeug.middleware.proxy_fix import ProxyFix
from werkzeug.security import check_password_hash

from card_renderer import DEFAULT_FONT, PACKED_BYTES, packed_to_png, render_card

BASE = Path(__file__).resolve().parent
TOKEN_PATTERN = re.compile(r"^Bearer ([A-Za-z0-9_-]{32,256})$")
PAIR_CODE_PATTERN = re.compile(r"^[0-9]{8}$")
MAX_REVISION = 0xFFFFFFFF
PAIR_CODE_TTL_SECONDS = 600
PAIR_MAX_INVALID_ATTEMPTS = 5


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def normalize_message(raw: object) -> str:
    if not isinstance(raw, str):
        raise ValueError("请输入文字")
    text = unicodedata.normalize("NFC", raw.replace("\r\n", "\n"))
    if not text.strip():
        raise ValueError("留言不能为空")
    if len(text) > 80:
        raise ValueError("最多 80 个字符")
    for char in text:
        category = unicodedata.category(char)
        if char != "\n" and (category.startswith("C") or category in ("Zl", "Zp")):
            raise ValueError("不支持控制字符或隐藏字符")
    return text


def create_app(config: dict | None = None) -> Flask:
    import os

    app = Flask(__name__, static_folder="static", template_folder="templates")
    # Deployment binds the app to host loopback; exactly one trusted Nginx hop sits in front.
    app.wsgi_app = ProxyFix(app.wsgi_app, x_for=1, x_proto=1, x_host=1)
    app.config.update(
        SECRET_KEY=os.environ.get("ESP_SESSION_SECRET", ""),
        OWNER_PASSWORD_HASH=os.environ.get("ESP_OWNER_PASSWORD_HASH", ""),
        DB_PATH=os.environ.get("ESP_DB_PATH", str(BASE / "data" / "esp.sqlite3")),
        FONT_PATH=os.environ.get("ESP_FONT_PATH", str(DEFAULT_FONT)),
        PUBLIC_ORIGIN=os.environ.get("ESP_PUBLIC_ORIGIN", ""),
        SESSION_COOKIE_SECURE=os.environ.get("ESP_COOKIE_SECURE", "1") != "0",
        SESSION_COOKIE_HTTPONLY=True,
        SESSION_COOKIE_SAMESITE="Strict",
        MAX_CONTENT_LENGTH=4096,
        JSON_SORT_KEYS=False,
    )
    if config:
        app.config.update(config)
    if len(app.config["SECRET_KEY"]) < 32 or not app.config["OWNER_PASSWORD_HASH"] or not app.config["PUBLIC_ORIGIN"]:
        raise RuntimeError("ESP_SESSION_SECRET (32+ chars), ESP_OWNER_PASSWORD_HASH and ESP_PUBLIC_ORIGIN are required")
    origin_parts = urlsplit(app.config["PUBLIC_ORIGIN"])
    if origin_parts.scheme not in ("http", "https") or not origin_parts.netloc or origin_parts.path not in ("", "/"):
        raise RuntimeError("ESP_PUBLIC_ORIGIN must be a bare http(s) origin")
    if not Path(app.config["FONT_PATH"]).is_file():
        raise RuntimeError(f"CJK font not found: {app.config['FONT_PATH']}")

    db_path = Path(app.config["DB_PATH"])
    db_path.parent.mkdir(parents=True, exist_ok=True)
    with sqlite3.connect(db_path) as db:
        db.executescript(
            """
            CREATE TABLE IF NOT EXISTS message (
                id INTEGER PRIMARY KEY CHECK (id = 1),
                revision INTEGER NOT NULL CHECK (revision BETWEEN 0 AND 4294967295),
                text TEXT NOT NULL,
                card BLOB,
                card_crc32 INTEGER NOT NULL,
                saved_at TEXT
            );
            CREATE TABLE IF NOT EXISTS device (
                id INTEGER PRIMARY KEY CHECK (id = 1),
                token_sha256 TEXT,
                last_seen TEXT,
                ack_revision INTEGER NOT NULL,
                ack_at TEXT
            );
            CREATE TABLE IF NOT EXISTS login_attempts (
                client TEXT PRIMARY KEY,
                attempts INTEGER NOT NULL,
                window_start INTEGER NOT NULL,
                locked_until INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS pairing (
                id INTEGER PRIMARY KEY CHECK (id = 1),
                code_hmac TEXT,
                expires_at INTEGER NOT NULL,
                invalid_attempts INTEGER NOT NULL CHECK (invalid_attempts BETWEEN 0 AND 5)
            );
            INSERT OR IGNORE INTO message VALUES (1, 0, '', NULL, 0, NULL);
            INSERT OR IGNORE INTO device VALUES (1, NULL, NULL, 0, NULL);
            INSERT OR IGNORE INTO pairing VALUES (1, NULL, 0, 0);
            """
        )

    def db() -> sqlite3.Connection:
        if "db" not in g:
            g.db = sqlite3.connect(db_path, timeout=5)
            g.db.row_factory = sqlite3.Row
        return g.db

    @app.teardown_appcontext
    def close_db(_error: Exception | None) -> None:
        connection = g.pop("db", None)
        if connection is not None:
            connection.close()

    @app.after_request
    def security_headers(response: Response) -> Response:
        response.headers["Cache-Control"] = "no-store"
        response.headers["X-Content-Type-Options"] = "nosniff"
        response.headers["Referrer-Policy"] = "same-origin"
        response.headers["X-Frame-Options"] = "DENY"
        response.headers["Content-Security-Policy"] = (
            "default-src 'none'; script-src 'self'; style-src 'self'; "
            "img-src 'self' blob:; connect-src 'self'; form-action 'self'; base-uri 'none'"
        )
        return response

    def csrf_token() -> str:
        if "csrf" not in session:
            session["csrf"] = secrets.token_urlsafe(32)
        return session["csrf"]

    def require_csrf() -> None:
        supplied_origin = request.headers.get("Origin")
        if not supplied_origin:
            referer = request.headers.get("Referer", "")
            parts = urlsplit(referer)
            supplied_origin = f"{parts.scheme}://{parts.netloc}" if parts.scheme and parts.netloc else ""
        if supplied_origin.rstrip("/") != app.config["PUBLIC_ORIGIN"].rstrip("/"):
            abort(403)
        submitted = request.headers.get("X-CSRF-Token") or request.form.get("csrf", "")
        expected = session.get("csrf", "")
        if not expected or not secrets.compare_digest(submitted, expected):
            abort(403)

    def owner_required(api: bool = False):
        def decorate(view):
            @wraps(view)
            def wrapped(*args, **kwargs):
                if session.get("owner") is not True:
                    if api:
                        return jsonify(error="请先登录"), 401
                    return redirect(url_for("login"))
                return view(*args, **kwargs)
            return wrapped
        return decorate

    def request_json() -> dict:
        payload = request.get_json(silent=True)
        if not isinstance(payload, dict):
            abort(400)
        return payload

    def is_login_locked(client: str) -> bool:
        row = db().execute("SELECT locked_until FROM login_attempts WHERE client = ?", (client,)).fetchone()
        return bool(row and row["locked_until"] > int(datetime.now(timezone.utc).timestamp()))

    def failed_login(client: str) -> None:
        now = int(datetime.now(timezone.utc).timestamp())
        row = db().execute("SELECT attempts, window_start FROM login_attempts WHERE client = ?", (client,)).fetchone()
        attempts = row["attempts"] + 1 if row and now - row["window_start"] <= 900 else 1
        start = row["window_start"] if row and now - row["window_start"] <= 900 else now
        until = now + 900 if attempts >= 5 else 0
        db().execute(
            "INSERT INTO login_attempts VALUES (?, ?, ?, ?) "
            "ON CONFLICT(client) DO UPDATE SET attempts=excluded.attempts, "
            "window_start=excluded.window_start, locked_until=excluded.locked_until",
            (client, attempts, start, until),
        )
        db().commit()

    def read_status() -> dict:
        message = db().execute("SELECT revision, text, saved_at FROM message WHERE id = 1").fetchone()
        device = db().execute(
            "SELECT token_sha256, last_seen, ack_revision, ack_at FROM device WHERE id = 1"
        ).fetchone()
        seen = datetime.fromisoformat(device["last_seen"]) if device["last_seen"] else None
        online = bool(seen and (datetime.now(timezone.utc) - seen).total_seconds() <= 15)
        pair = db().execute("SELECT code_hmac, expires_at, invalid_attempts FROM pairing WHERE id = 1").fetchone()
        pair_active = bool(pair["code_hmac"] and pair["expires_at"] > int(datetime.now(timezone.utc).timestamp())
                           and pair["invalid_attempts"] < PAIR_MAX_INVALID_ATTEMPTS)
        return {
            "revision": message["revision"],
            "message": message["text"],
            "savedAt": message["saved_at"],
            "deviceOnline": online,
            "lastSeen": device["last_seen"],
            "lastReceivedRevision": device["ack_revision"],
            "lastReceivedAt": device["ack_at"],
            "tokenConfigured": bool(device["token_sha256"]),
            "pairCodeActive": pair_active,
            "pairExpiresAt": pair["expires_at"] if pair_active else None,
        }

    def pair_code_hmac(code: str) -> str:
        secret = app.config["SECRET_KEY"].encode("utf-8")
        return hmac.new(secret, b"esp-pair-v1:" + code.encode("ascii"), hashlib.sha256).hexdigest()

    def device_authorized(view):
        @wraps(view)
        def wrapped(*args, **kwargs):
            match = TOKEN_PATTERN.fullmatch(request.headers.get("Authorization", ""))
            candidate = hashlib.sha256(match.group(1).encode("ascii")).hexdigest() if match else ""
            db().execute("BEGIN IMMEDIATE")
            expected = db().execute("SELECT token_sha256 FROM device WHERE id = 1").fetchone()[0]
            if not expected or not secrets.compare_digest(candidate, expected):
                db().rollback()
                return jsonify(error="unauthorized"), 401
            g.device_token_sha256 = candidate
            db().execute("UPDATE device SET last_seen = ? WHERE id = 1", (now_iso(),))
            db().commit()
            return view(*args, **kwargs)
        return wrapped

    @app.route("/login", methods=["GET", "POST"])
    def login():
        if request.method == "GET":
            if session.get("owner") is True:
                return redirect(url_for("home"))
            return render_template("login.html", csrf=csrf_token(), error=None)
        require_csrf()
        client = request.remote_addr or "unknown"
        if is_login_locked(client):
            return render_template("login.html", csrf=csrf_token(), error="尝试过多，请 15 分钟后再试"), 429
        password = request.form.get("password", "")
        if not check_password_hash(app.config["OWNER_PASSWORD_HASH"], password):
            failed_login(client)
            return render_template("login.html", csrf=csrf_token(), error="密码不正确"), 401
        db().execute("DELETE FROM login_attempts WHERE client = ?", (client,))
        db().commit()
        session.clear()
        session["owner"] = True
        csrf_token()
        return redirect(url_for("home"))

    @app.post("/logout")
    @owner_required()
    def logout():
        require_csrf()
        session.clear()
        return redirect(url_for("login"))

    @app.get("/")
    @owner_required()
    def home():
        return render_template("home.html", csrf=csrf_token(), status=read_status())

    @app.get("/api/owner/status")
    @owner_required(api=True)
    def owner_status():
        return jsonify(read_status())

    @app.get("/api/owner/preview/current.png")
    @owner_required(api=True)
    def current_preview():
        row = db().execute("SELECT card FROM message WHERE id = 1").fetchone()
        packed = row["card"] if row["card"] is not None else render_card("", Path(app.config["FONT_PATH"]))
        return Response(packed_to_png(packed), mimetype="image/png")

    @app.post("/api/owner/preview")
    @owner_required(api=True)
    def preview():
        require_csrf()
        try:
            text = normalize_message(request_json().get("text"))
            packed = render_card(text, Path(app.config["FONT_PATH"]))
        except ValueError as error:
            return jsonify(error=str(error)), 400
        return Response(packed_to_png(packed), mimetype="image/png")

    @app.post("/api/owner/send")
    @owner_required(api=True)
    def send():
        require_csrf()
        try:
            text = normalize_message(request_json().get("text"))
            packed = render_card(text, Path(app.config["FONT_PATH"]))
        except ValueError as error:
            return jsonify(error=str(error)), 400
        checksum = zlib.crc32(packed) & 0xFFFFFFFF
        db().execute("BEGIN IMMEDIATE")
        current_revision = db().execute("SELECT revision FROM message WHERE id = 1").fetchone()[0]
        if current_revision >= MAX_REVISION:
            db().rollback()
            return jsonify(error="设备版本号已达到上限，请联系维护者"), 409
        db().execute(
            "UPDATE message SET revision = revision + 1, text = ?, card = ?, "
            "card_crc32 = ?, saved_at = ? WHERE id = 1",
            (text, packed, checksum, now_iso()),
        )
        row = db().execute("SELECT revision FROM message WHERE id = 1").fetchone()
        db().execute("UPDATE device SET ack_revision = 0, ack_at = NULL WHERE id = 1")
        db().commit()
        return jsonify(revision=row["revision"], card_bytes=PACKED_BYTES, card_crc32=checksum)

    @app.post("/api/owner/device-key")
    @owner_required(api=True)
    def rotate_device_key():
        require_csrf()
        token = secrets.token_urlsafe(32)
        token_hash = hashlib.sha256(token.encode("ascii")).hexdigest()
        db().execute("BEGIN IMMEDIATE")
        db().execute(
            "UPDATE device SET token_sha256 = ?, last_seen = NULL, ack_revision = 0, ack_at = NULL WHERE id = 1",
            (token_hash,),
        )
        db().execute("UPDATE pairing SET code_hmac = NULL, expires_at = 0, invalid_attempts = 0 WHERE id = 1")
        db().commit()
        return jsonify(deviceToken=token, warning="请立即复制；关闭后不会再显示。重新生成会使旧设备密钥失效。")

    @app.post("/api/owner/pair-code")
    @owner_required(api=True)
    def owner_pair_code():
        require_csrf()
        db().execute("BEGIN IMMEDIATE")
        previous = db().execute("SELECT code_hmac FROM pairing WHERE id = 1").fetchone()[0]
        for _ in range(10):
            code = f"{secrets.randbelow(100_000_000):08d}"
            digest = pair_code_hmac(code)
            if not previous or not secrets.compare_digest(digest, previous):
                break
        else:
            db().rollback()
            abort(500)
        expiry = int(datetime.now(timezone.utc).timestamp()) + PAIR_CODE_TTL_SECONDS
        db().execute(
            "UPDATE pairing SET code_hmac = ?, expires_at = ?, invalid_attempts = 0 WHERE id = 1",
            (digest, expiry),
        )
        db().commit()
        return jsonify(code=code, expiresInSeconds=PAIR_CODE_TTL_SECONDS)

    @app.post("/api/device/pair")
    def device_pair():
        if len(request.get_data(cache=True)) > 128:
            return jsonify(error="invalid request"), 413
        payload = request.get_json(silent=True)
        if not isinstance(payload, dict):
            return jsonify(error="invalid request"), 400
        code = payload.get("code")
        if set(payload) != {"code"} or not isinstance(code, str) or not PAIR_CODE_PATTERN.fullmatch(code):
            return jsonify(error="invalid request"), 400

        db().execute("BEGIN IMMEDIATE")
        row = db().execute(
            "SELECT code_hmac, expires_at, invalid_attempts FROM pairing WHERE id = 1"
        ).fetchone()
        if not row["code_hmac"] or row["expires_at"] <= int(datetime.now(timezone.utc).timestamp()):
            db().rollback()
            return jsonify(error="pairing unavailable"), 403
        if row["invalid_attempts"] >= PAIR_MAX_INVALID_ATTEMPTS:
            db().rollback()
            return jsonify(error="pairing unavailable"), 429
        if not secrets.compare_digest(pair_code_hmac(code), row["code_hmac"]):
            failures = row["invalid_attempts"] + 1
            db().execute("UPDATE pairing SET invalid_attempts = ? WHERE id = 1", (failures,))
            db().commit()
            return jsonify(error="pairing unavailable"), 429 if failures >= PAIR_MAX_INVALID_ATTEMPTS else 403

        token = secrets.token_urlsafe(32)
        token_hash = hashlib.sha256(token.encode("ascii")).hexdigest()
        db().execute(
            "UPDATE device SET token_sha256 = ?, last_seen = NULL, ack_revision = 0, ack_at = NULL WHERE id = 1",
            (token_hash,),
        )
        db().execute("UPDATE pairing SET code_hmac = NULL, expires_at = 0, invalid_attempts = 0 WHERE id = 1")
        db().commit()
        response = jsonify(deviceToken=token)
        response.headers["Content-Length"] = str(len(response.get_data()))
        return response

    @app.get("/api/device/latest")
    @device_authorized
    def device_latest():
        row = db().execute("SELECT revision, card_crc32 FROM message WHERE id = 1").fetchone()
        response = jsonify(
            revision=row["revision"],
            card_bytes=PACKED_BYTES if row["revision"] else 0,
            card_crc32=row["card_crc32"] if row["revision"] else 0,
        )
        response.headers["Content-Length"] = str(len(response.get_data()))
        return response

    @app.get("/healthz")
    def healthz():
        db().execute("SELECT 1").fetchone()
        return jsonify(ok=True)

    @app.get("/api/device/card/<int:revision>")
    @device_authorized
    def device_card(revision: int):
        row = db().execute("SELECT revision, card FROM message WHERE id = 1").fetchone()
        if revision <= 0 or revision != row["revision"] or row["card"] is None:
            abort(404)
        if len(row["card"]) != PACKED_BYTES:
            abort(500)
        response = Response(row["card"], mimetype="application/octet-stream")
        response.headers["Content-Length"] = str(PACKED_BYTES)
        return response

    @app.post("/api/device/ack")
    @device_authorized
    def device_ack():
        payload = request_json()
        revision = payload.get("revision")
        if type(revision) is not int or revision <= 0 or revision > MAX_REVISION:
            return jsonify(error="invalid revision"), 400
        db().execute("BEGIN IMMEDIATE")
        current_token = db().execute("SELECT token_sha256 FROM device WHERE id = 1").fetchone()[0]
        if not current_token or not secrets.compare_digest(g.device_token_sha256, current_token):
            db().rollback()
            return jsonify(error="unauthorized"), 401
        current = db().execute("SELECT revision FROM message WHERE id = 1").fetchone()[0]
        if revision != current:
            db().rollback()
            return jsonify(error="stale revision"), 409
        db().execute("UPDATE device SET ack_revision = ?, ack_at = ? WHERE id = 1", (revision, now_iso()))
        db().commit()
        return jsonify(ok=True)

    @app.errorhandler(413)
    def too_large(_error):
        return jsonify(error="请求内容过大"), 413

    return app


if __name__ == "__main__":
    create_app().run(host="127.0.0.1", port=8088, debug=False)
