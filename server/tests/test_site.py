from __future__ import annotations

import hashlib
import io
import sqlite3
import tempfile
import threading
import time
import unittest
import zlib
from contextlib import closing, redirect_stdout
from concurrent.futures import ThreadPoolExecutor
from io import BytesIO
from pathlib import Path
from unittest.mock import patch

from PIL import Image
from werkzeug.security import generate_password_hash

import app as site_module
from app import create_app, normalize_message
from card_renderer import DEFAULT_FONT, HEIGHT, PACKED_BYTES, WIDTH, packed_to_png, render_card, unpack_image
from tools import init_env


class SiteTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent)
        self.db_path = Path(self.temp.name) / "test.sqlite3"
        self.app = create_app({
            "TESTING": True,
            "SECRET_KEY": "test-secret-strong-enough-for-32-chars",
            "OWNER_PASSWORD_HASH": generate_password_hash("test-password-only"),
            "DB_PATH": str(self.db_path),
            "FONT_PATH": str(DEFAULT_FONT),
            "PUBLIC_ORIGIN": "http://localhost",
            "SESSION_COOKIE_SECURE": False,
        })
        self.client = self.app.test_client()

    def tearDown(self):
        self.temp.cleanup()

    def csrf(self):
        with self.client.session_transaction() as user_session:
            return user_session["csrf"]

    def login(self):
        self.client.get("/login")
        response = self.client.post(
            "/login",
            data={"password": "test-password-only", "csrf": self.csrf()},
            headers={"Origin": "http://localhost"},
        )
        self.assertEqual(response.status_code, 302)

    def owner_post(self, url, data=None, *, json=False, csrf=True, origin="http://localhost"):
        headers = {"Origin": origin}
        if csrf:
            headers["X-CSRF-Token"] = self.csrf()
        if json:
            return self.client.post(url, json=data, headers=headers)
        return self.client.post(url, data=data, headers=headers)

    def create_device_token(self):
        response = self.owner_post("/api/owner/device-key")
        self.assertEqual(response.status_code, 200)
        return response.json["deviceToken"]

    def device_headers(self, token):
        return {"Authorization": "Bearer " + token}

    def test_full_send_preview_device_receive_and_rotation(self):
        self.assertEqual(self.client.get("/").status_code, 302)
        self.login()
        initial = self.client.get("/api/owner/status").json
        self.assertEqual(initial["revision"], 0)
        self.assertFalse(initial["tokenConfigured"])
        self.assertEqual(self.client.get("/api/device/latest").status_code, 401)

        token = self.create_device_token()
        with closing(sqlite3.connect(self.db_path)) as db:
            stored_hash = db.execute("SELECT token_sha256 FROM device WHERE id=1").fetchone()[0]
        self.assertEqual(stored_hash, hashlib.sha256(token.encode()).hexdigest())
        self.assertNotEqual(stored_hash, token)
        headers = self.device_headers(token)
        empty_latest = self.client.get("/api/device/latest", headers=headers)
        self.assertEqual(empty_latest.json, {"revision": 0, "card_bytes": 0, "card_crc32": 0})
        self.assertEqual(int(empty_latest.headers["Content-Length"]), len(empty_latest.data))
        self.assertLessEqual(len(empty_latest.data), 192)

        sample = "今天天气真好，记得休息一下。\n喵！"
        preview = self.owner_post("/api/owner/preview", {"text": sample}, json=True)
        self.assertEqual(preview.status_code, 200)
        preview_image = Image.open(BytesIO(preview.data))
        self.assertEqual(preview_image.size, (WIDTH, HEIGHT))
        self.assertEqual(preview_image.mode, "L")

        result = self.owner_post("/api/owner/send", {"text": sample}, json=True)
        self.assertEqual(result.status_code, 200)
        self.assertEqual(result.json["revision"], 1)
        latest = self.client.get("/api/device/latest", headers=headers)
        metadata = latest.json
        self.assertEqual(int(latest.headers["Content-Length"]), len(latest.data))
        self.assertLessEqual(len(latest.data), 192)
        self.assertEqual(metadata["card_bytes"], PACKED_BYTES)
        self.assertEqual(metadata["revision"], 1)
        card = self.client.get("/api/device/card/1", headers=headers)
        self.assertEqual(card.status_code, 200)
        self.assertEqual(card.mimetype, "application/octet-stream")
        self.assertEqual(len(card.data), PACKED_BYTES)
        self.assertEqual(int(card.headers["Content-Length"]), PACKED_BYTES)
        self.assertEqual(card.data, render_card(sample))
        self.assertEqual(zlib.crc32(card.data) & 0xFFFFFFFF, metadata["card_crc32"])
        self.assertEqual(self.client.get("/api/owner/preview/current.png").data, packed_to_png(card.data))

        ack = self.client.post("/api/device/ack", json={"revision": 1}, headers=headers)
        self.assertEqual(ack.status_code, 200)
        status = self.client.get("/api/owner/status").json
        self.assertEqual(status["lastReceivedRevision"], 1)
        self.assertTrue(status["deviceOnline"])
        self.assertIsNotNone(status["lastReceivedAt"])
        self.assertEqual(self.client.post("/api/device/ack", json={"revision": 0}, headers=headers).status_code, 400)

        new_token = self.create_device_token()
        self.assertNotEqual(token, new_token)
        self.assertEqual(self.client.get("/api/device/latest", headers=headers).status_code, 401)
        self.assertEqual(self.client.get("/api/device/latest", headers=self.device_headers(new_token)).status_code, 200)
        self.assertEqual(self.client.get("/api/owner/status").json["lastReceivedRevision"], 0)

    def test_only_current_revision_ack_and_card(self):
        self.login()
        token = self.create_device_token()
        headers = self.device_headers(token)
        self.owner_post("/api/owner/send", {"text": "第一条"}, json=True)
        self.owner_post("/api/owner/send", {"text": "第二条"}, json=True)
        self.assertEqual(self.client.get("/api/device/card/1", headers=headers).status_code, 404)
        self.assertEqual(self.client.post("/api/device/ack", json={"revision": 1}, headers=headers).status_code, 409)
        self.assertEqual(self.client.post("/api/device/ack", json={"revision": 2}, headers=headers).status_code, 200)

    def test_auth_csrf_origin_and_input_bounds(self):
        self.login()
        self.assertEqual(self.owner_post("/api/owner/send", {"text": "hi"}, json=True, csrf=False).status_code, 403)
        self.assertEqual(self.owner_post("/api/owner/send", {"text": "hi"}, json=True, origin="https://evil.example").status_code, 403)
        self.assertEqual(self.owner_post("/api/owner/send", {"text": "hi"}, json=True, origin="").status_code, 403)
        for invalid in ("", " \n ", "猫" * 81, "隐藏\u200b字", "控制\x00字", "好\n" * 30):
            self.assertEqual(self.owner_post("/api/owner/send", {"text": invalid}, json=True).status_code, 400)
        self.assertEqual(self.owner_post("/api/owner/send", {"text": "x" * 5000}, json=True).status_code, 413)
        self.assertEqual(self.client.get("/api/device/card/1").status_code, 401)
        self.assertEqual(self.client.post("/api/device/ack", json={"revision": 1}).status_code, 401)
        self.assertEqual(normalize_message("a\r\nb"), "a\nb")
        self.assertEqual(self.client.get("/healthz").json, {"ok": True})

    def test_login_rate_limit_and_no_auth_leak(self):
        page = self.client.get("/login")
        self.assertNotIn(b"test-password-only", page.data)
        token = self.csrf()
        for _ in range(5):
            response = self.client.post("/login", data={"password": "bad", "csrf": token},
                                        headers={"Origin": "http://localhost"})
            self.assertEqual(response.status_code, 401)
        response = self.client.post("/login", data={"password": "test-password-only", "csrf": token},
                                    headers={"Origin": "http://localhost"})
        self.assertEqual(response.status_code, 429)

    def test_nibble_order_and_exact_grayscale_preview(self):
        image = unpack_image(bytes([0xF0]) + bytes(PACKED_BYTES - 1))
        self.assertEqual(image.getpixel((0, 0)), 255)
        self.assertEqual(image.getpixel((1, 0)), 0)
        self.assertEqual(Image.open(BytesIO(packed_to_png(bytes([0xF0]) + bytes(PACKED_BYTES - 1)))).getpixel((0, 0)), 255)
        sample = render_card("猫眼睛")
        self.assertEqual(len(sample), PACKED_BYTES)
        self.assertGreater(len(set(sample)), 10)
        self.assertEqual(len(render_card("猫" * 80)), PACKED_BYTES)

    def test_revision_is_unsigned_32_bit_and_never_wraps(self):
        self.login()
        self.owner_post("/api/owner/send", {"text": "原内容"}, json=True)
        with closing(sqlite3.connect(self.db_path)) as db:
            db.execute("UPDATE message SET revision = 4294967295 WHERE id = 1")
            db.commit()
        response = self.owner_post("/api/owner/send", {"text": "不能覆盖"}, json=True)
        self.assertEqual(response.status_code, 409)
        self.assertEqual(self.client.get("/api/owner/status").json["revision"], 4294967295)

    def test_interactive_env_generator_keeps_plain_password_out_of_file_and_output(self):
        output = Path(self.temp.name) / ".env"
        password = "only-for-automated-test-1234"
        arguments = ["init_env.py", "--output", str(output), "--origin", "https://esp.example.com"]
        stream = io.StringIO()
        with patch("sys.argv", arguments), patch("tools.init_env.getpass.getpass", side_effect=[password, password]):
            with redirect_stdout(stream):
                init_env.main()
        contents = output.read_text(encoding="utf-8")
        self.assertNotIn(password, contents)
        self.assertNotIn(password, stream.getvalue())
        self.assertIn("ESP_OWNER_PASSWORD_HASH='scrypt:", contents)
        self.assertIn("ESP_COOKIE_SECURE='1'", contents)
        self.assertIn("ESP_PUBLIC_ORIGIN='https://esp.example.com'", contents)

    def test_generated_initial_password_is_shown_once_and_not_saved_plaintext(self):
        output = Path(self.temp.name) / "generated.env"
        arguments = ["init_env.py", "--output", str(output), "--origin", "https://esp.example.com",
                     "--generate-admin-password"]
        stream = io.StringIO()
        with patch("sys.argv", arguments), redirect_stdout(stream):
            init_env.main()
        password = stream.getvalue().splitlines()[-1]
        self.assertGreaterEqual(len(password), 32)
        self.assertEqual(stream.getvalue().count(password), 1)
        self.assertNotIn(password, output.read_text(encoding="utf-8"))

    def test_pair_code_is_owner_only_csrf_protected_and_shown_once(self):
        self.assertEqual(self.client.post("/api/owner/pair-code").status_code, 401)
        self.login()
        self.assertEqual(self.owner_post("/api/owner/pair-code", csrf=False).status_code, 403)
        self.assertEqual(self.owner_post("/api/owner/pair-code", origin="https://evil.example").status_code, 403)
        before = int(time.time())
        issued = self.owner_post("/api/owner/pair-code")
        self.assertEqual(issued.status_code, 200)
        self.assertRegex(issued.json["code"], r"^[0-9]{8}$")
        self.assertEqual(issued.json["expiresInSeconds"], 600)
        self.assertEqual(issued.headers["Cache-Control"], "no-store")
        owner_status = self.client.get("/api/owner/status").json
        self.assertTrue(owner_status["pairCodeActive"])
        self.assertNotIn("code", owner_status)
        self.assertNotIn(issued.json["code"].encode(), self.client.get("/").data)
        with closing(sqlite3.connect(self.db_path)) as db:
            stored = db.execute("SELECT code_hmac, invalid_attempts, expires_at FROM pairing WHERE id = 1").fetchone()
        self.assertEqual(stored[1], 0)
        self.assertNotEqual(stored[0], issued.json["code"])
        self.assertGreaterEqual(stored[2], before + 600)
        self.assertLessEqual(stored[2], before + 602)

    def test_successful_pair_replaces_old_token_and_cannot_replay(self):
        self.login()
        old_token = self.create_device_token()
        self.owner_post("/api/owner/send", {"text": "已经发出"}, json=True)
        self.client.post("/api/device/ack", json={"revision": 1}, headers=self.device_headers(old_token))
        self.assertEqual(self.client.get("/api/owner/status").json["lastReceivedRevision"], 1)
        code = self.owner_post("/api/owner/pair-code").json["code"]

        paired = self.client.post("/api/device/pair", json={"code": code})
        self.assertEqual(paired.status_code, 200)
        self.assertEqual(set(paired.json), {"deviceToken"})
        new_token = paired.json["deviceToken"]
        self.assertGreaterEqual(len(new_token), 32)
        self.assertEqual(int(paired.headers["Content-Length"]), len(paired.data))
        self.assertLessEqual(len(paired.data), 128)
        self.assertEqual(paired.headers["Cache-Control"], "no-store")
        self.assertNotEqual(new_token, old_token)
        self.assertEqual(self.client.get("/api/device/latest", headers=self.device_headers(old_token)).status_code, 401)
        self.assertEqual(self.client.get("/api/device/latest", headers=self.device_headers(new_token)).status_code, 200)
        self.assertEqual(self.client.post("/api/device/pair", json={"code": code}).status_code, 403)
        status = self.client.get("/api/owner/status").json
        self.assertFalse(status["pairCodeActive"])
        self.assertEqual(status["lastReceivedRevision"], 0)
        with closing(sqlite3.connect(self.db_path)) as db:
            stored_hash = db.execute("SELECT token_sha256 FROM device WHERE id = 1").fetchone()[0]
            stored_code = db.execute("SELECT code_hmac FROM pairing WHERE id = 1").fetchone()[0]
        self.assertEqual(stored_hash, hashlib.sha256(new_token.encode()).hexdigest())
        self.assertIsNone(stored_code)

    def test_old_device_ack_cannot_restore_ack_after_pair_rotates_token(self):
        self.login()
        old_token = self.create_device_token()
        self.owner_post("/api/owner/send", {"text": "等待新设备"}, json=True)
        code = self.owner_post("/api/owner/pair-code").json["code"]

        ready = threading.Event()
        resume = threading.Event()
        outcomes = []
        original_connect = sqlite3.connect
        old_ack_transactions = 0

        class DelayedConnection(sqlite3.Connection):
            def execute(self, sql, parameters=()):
                nonlocal old_ack_transactions
                if sql == "BEGIN IMMEDIATE" and threading.current_thread().name == "old-device-ack":
                    old_ack_transactions += 1
                    if old_ack_transactions == 2:
                        ready.set()
                        if not resume.wait(5):
                            raise TimeoutError("old ACK was not released")
                return super().execute(sql, parameters)

        def delayed_connect(*args, **kwargs):
            kwargs["factory"] = DelayedConnection
            return original_connect(*args, **kwargs)

        def old_ack():
            try:
                response = self.app.test_client().post(
                    "/api/device/ack", json={"revision": 1}, headers=self.device_headers(old_token),
                )
                outcomes.append(response.status_code)
            except Exception as error:
                outcomes.append(error)

        with patch.object(site_module.sqlite3, "connect", side_effect=delayed_connect):
            thread = threading.Thread(target=old_ack, name="old-device-ack")
            thread.start()
            try:
                self.assertTrue(ready.wait(5), "old ACK did not reach its transaction")
                paired = self.client.post("/api/device/pair", json={"code": code})
                self.assertEqual(paired.status_code, 200)
            finally:
                resume.set()
                thread.join(5)

        self.assertFalse(thread.is_alive())
        self.assertEqual(old_ack_transactions, 2)
        self.assertEqual(outcomes, [401])
        self.assertEqual(self.client.get("/api/owner/status").json["lastReceivedRevision"], 0)
        self.assertEqual(self.client.get("/api/device/latest", headers=self.device_headers(old_token)).status_code, 401)
        self.assertEqual(self.client.get("/api/device/latest", headers=self.device_headers(paired.json["deviceToken"])).status_code, 200)

    def test_pair_failures_are_global_per_code_and_expiry_blocks_even_right_code(self):
        self.login()
        code = self.owner_post("/api/owner/pair-code").json["code"]
        wrong = "00000000" if code != "00000000" else "00000001"
        for attempt in range(5):
            response = self.client.post(
                "/api/device/pair", json={"code": wrong},
                environ_overrides={"REMOTE_ADDR": f"198.51.100.{attempt + 1}"},
            )
            self.assertEqual(response.status_code, 429 if attempt == 4 else 403)
            self.assertEqual(response.json, {"error": "pairing unavailable"})
        self.assertEqual(self.client.post("/api/device/pair", json={"code": code}).status_code, 429)
        self.assertFalse(self.client.get("/api/owner/status").json["pairCodeActive"])

        next_code = self.owner_post("/api/owner/pair-code").json["code"]
        with closing(sqlite3.connect(self.db_path)) as db:
            db.execute("UPDATE pairing SET expires_at = 1 WHERE id = 1")
            db.commit()
        self.assertEqual(self.client.post("/api/device/pair", json={"code": next_code}).status_code, 403)
        self.assertFalse(self.client.get("/api/owner/status").json["pairCodeActive"])

    def test_pair_input_validation_and_manual_key_backward_compatibility(self):
        self.login()
        self.assertEqual(self.client.post("/api/device/pair", json={"code": "12345678"}).status_code, 403)
        for payload in ({"code": "1234567"}, {"code": "１２３４５６７８"},
                        {"code": 12345678}, {"code": "12345678", "other": 1}):
            self.assertEqual(self.client.post("/api/device/pair", json=payload).status_code, 400)
        self.assertEqual(self.client.post("/api/device/pair", data="not json").status_code, 400)
        self.assertEqual(self.client.post("/api/device/pair", json={"code": "x" * 200}).status_code, 413)
        code = self.owner_post("/api/owner/pair-code").json["code"]
        token = self.create_device_token()
        self.assertEqual(self.client.get("/api/device/latest", headers=self.device_headers(token)).status_code, 200)
        self.assertEqual(self.client.post("/api/device/pair", json={"code": code}).status_code, 403)

    def test_pair_reissue_invalidates_previous_code_and_is_atomic(self):
        self.login()
        with patch("app.secrets.randbelow", side_effect=[12345678, 87654321]):
            old_code = self.owner_post("/api/owner/pair-code").json["code"]
            new_code = self.owner_post("/api/owner/pair-code").json["code"]
        self.assertNotEqual(old_code, new_code)
        self.assertEqual(self.client.post("/api/device/pair", json={"code": old_code}).status_code, 403)

        def exchange():
            client = self.app.test_client()
            return client.post("/api/device/pair", json={"code": new_code}).status_code

        with ThreadPoolExecutor(max_workers=2) as pool:
            outcomes = sorted(pool.map(lambda _: exchange(), range(2)))
        self.assertEqual(outcomes, [200, 403])

    def test_existing_v01_sqlite_migrates_without_losing_message_or_device(self):
        self.login()
        token = self.create_device_token()
        self.owner_post("/api/owner/send", {"text": "旧版留言"}, json=True)
        with closing(sqlite3.connect(self.db_path)) as db:
            db.execute("DROP TABLE pairing")
            db.commit()
        migrated = create_app({
            "TESTING": True,
            "SECRET_KEY": "test-secret-strong-enough-for-32-chars",
            "OWNER_PASSWORD_HASH": generate_password_hash("test-password-only"),
            "DB_PATH": str(self.db_path),
            "FONT_PATH": str(DEFAULT_FONT),
            "PUBLIC_ORIGIN": "http://localhost",
            "SESSION_COOKIE_SECURE": False,
        })
        client = migrated.test_client()
        latest = client.get("/api/device/latest", headers=self.device_headers(token))
        self.assertEqual(latest.status_code, 200)
        self.assertEqual(latest.json["revision"], 1)
        with closing(sqlite3.connect(self.db_path)) as db:
            self.assertEqual(db.execute("SELECT count(*) FROM pairing").fetchone()[0], 1)


if __name__ == "__main__":
    unittest.main()
