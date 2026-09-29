"""Interactively create a Compose env file without printing or storing a password.

Example:
  python tools/init_env.py --output .env --origin https://esp.example.com
"""

from __future__ import annotations

import argparse
import getpass
import os
import secrets
from pathlib import Path
from urllib.parse import urlsplit

from werkzeug.security import generate_password_hash


def main() -> None:
    parser = argparse.ArgumentParser(description="Create private ESP site runtime configuration")
    parser.add_argument("--output", required=True, type=Path, help="New .env path; refuses to overwrite")
    parser.add_argument("--origin", required=True, help="Exact public origin, e.g. https://esp.example.com")
    parser.add_argument("--db-path", default="/data/esp.sqlite3")
    parser.add_argument("--local-http", action="store_true", help="Only for localhost testing; disables secure cookie")
    parser.add_argument("--generate-admin-password", action="store_true",
                        help="Generate a high-entropy owner password and show it once after writing the env file")
    args = parser.parse_args()

    origin = urlsplit(args.origin)
    if origin.scheme not in ("http", "https") or not origin.netloc or origin.path not in ("", "/"):
        parser.error("--origin must be a bare http(s) origin without a path")
    if not args.local_http and origin.scheme != "https":
        parser.error("production origin must use HTTPS")
    if args.local_http and origin.hostname not in ("localhost", "127.0.0.1", "::1"):
        parser.error("--local-http is allowed only for localhost")
    if any("'" in value or "\n" in value or "\r" in value for value in (args.origin, args.db_path)):
        parser.error("origin and database path cannot contain quotes or line breaks")

    if args.generate_admin_password:
        password = secrets.token_urlsafe(24)
    else:
        password = getpass.getpass("设置管理密码（至少 16 个字符，不会回显）：")
        second = getpass.getpass("再次输入管理密码：")
        if password != second or len(password) < 16:
            parser.error("两次密码不一致，或长度不足 16 个字符")

    password_hash = generate_password_hash(password, method="scrypt:32768:8:1")
    secret = secrets.token_urlsafe(48)
    values = {
        "ESP_SESSION_SECRET": secret,
        "ESP_OWNER_PASSWORD_HASH": password_hash,
        "ESP_PUBLIC_ORIGIN": args.origin.rstrip("/"),
        "ESP_DB_PATH": args.db_path,
        "ESP_COOKIE_SECURE": "0" if args.local_http else "1",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as file:
        for key, value in values.items():
            file.write(f"{key}='{value}'\n")
    try:
        os.chmod(args.output, 0o600)
    except OSError:
        pass
    print(f"已生成私密配置文件：{args.output.resolve()}")
    print("配置文件不含明文管理密码或设备密钥。请勿提交、截图或分享该文件。")
    if args.generate_admin_password:
        print("一次性初始管理密码（立即安全保存，此后不会再显示）：")
        print(password)


if __name__ == "__main__":
    main()
