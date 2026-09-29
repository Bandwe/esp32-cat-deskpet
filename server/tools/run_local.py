"""Run the owner site on 127.0.0.1:8088 using a private local .env file."""

from __future__ import annotations

import os
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
ALLOWED = {
    "ESP_SESSION_SECRET", "ESP_OWNER_PASSWORD_HASH", "ESP_PUBLIC_ORIGIN",
    "ESP_DB_PATH", "ESP_COOKIE_SECURE", "ESP_FONT_PATH",
}


def main() -> None:
    private_file = BASE / ".env"
    if not private_file.is_file():
        raise SystemExit(f"Missing private config: {private_file}")
    for line in private_file.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        key, sep, quoted = line.partition("=")
        if not sep or key not in ALLOWED or len(quoted) < 2 or quoted[0] != "'" or quoted[-1] != "'":
            raise SystemExit("Invalid private config format")
        os.environ[key] = quoted[1:-1]
    sys.path.insert(0, str(BASE))
    from app import create_app
    create_app().run(host="127.0.0.1", port=8088, debug=False)


if __name__ == "__main__":
    main()
