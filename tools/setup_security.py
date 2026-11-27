#!/usr/bin/env python3
"""Tạo bí mật cho Smart Shelf (OWASP IoT I1-I3). Cần Docker để băm mật khẩu.

Tạo hoặc cập nhật:
  .env                              SHELF_HMAC_KEY, hash mật khẩu Node-RED, khóa mã hóa credentials
  include/secrets.h                 (nếu chưa có) cấu hình firmware kèm SHELF_HMAC_KEY
  docker/nginx/auth/.htpasswd       tài khoản đăng nhập web dashboard
  docker/mosquitto/config/passwd    tài khoản broker nội bộ: shelf_device, shelf_nodered, shelf_dashboard

Mật khẩu sinh ngẫu nhiên chỉ in ra màn hình một lần; hãy lưu vào trình quản lý mật khẩu.
Chạy lại với --rotate để sinh khóa và mật khẩu mới.

  python tools/setup_security.py [--rotate]
"""
import argparse
import re
import secrets
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENV_FILE = ROOT / ".env"
SECRETS_H = ROOT / "include" / "secrets.h"
SECRETS_EXAMPLE = ROOT / "include" / "secrets.example.h"
HTPASSWD = ROOT / "docker" / "nginx" / "auth" / ".htpasswd"
MOSQ_PASSWD = ROOT / "docker" / "mosquitto" / "config" / "passwd"
NODERED_IMAGE = "nodered/node-red:4.1"
MOSQUITTO_IMAGE = "eclipse-mosquitto:2"
MQTT_USERS = ["shelf_device", "shelf_nodered", "shelf_dashboard"]


def run(cmd, stdin=None):
    res = subprocess.run(cmd, input=stdin, capture_output=True, text=True)
    if res.returncode != 0:
        sys.exit(f"Lỗi khi chạy {' '.join(cmd[:4])}...:\n{res.stderr.strip()}")
    return res.stdout.strip()


def bcrypt_hash(password):
    # bcryptjs có sẵn trong image Node-RED; mật khẩu đi qua stdin, không nằm trên dòng lệnh.
    js = ("let d='';process.stdin.on('data',c=>d+=c).on('end',()=>"
          "console.log(require('/usr/src/node-red/node_modules/bcryptjs').hashSync(d,10)))")
    out = run(["docker", "run", "-i", "--rm", "--entrypoint", "node", NODERED_IMAGE, "-e", js], stdin=password)
    if not re.match(r"^\$2[aby]\$\d{2}\$", out):
        sys.exit("Không tạo được bcrypt hash: " + out)
    return out


def mosquitto_passwd_file(creds):
    with tempfile.TemporaryDirectory() as tmp:
        plain = Path(tmp) / "passwd"
        plain.write_text("".join(f"{u}:{p}\n" for u, p in creds.items()), encoding="utf-8", newline="\n")
        run(["docker", "run", "--rm", "-v", f"{tmp}:/work", "--entrypoint", "mosquitto_passwd",
             MOSQUITTO_IMAGE, "-U", "/work/passwd"])
        return plain.read_text(encoding="utf-8")


def read_env():
    env = {}
    if ENV_FILE.exists():
        for line in ENV_FILE.read_text(encoding="utf-8").splitlines():
            m = re.match(r"^\s*([A-Z0-9_]+)\s*=\s*(.*)$", line)
            if m:
                env[m.group(1)] = m.group(2).strip().strip("'\"")
    return env


def write_env(env):
    order = ["TELEGRAM_BOT_TOKEN", "TELEGRAM_CHAT_IDS", "MQTT_HOST", "MQTT_PORT", "SHELF_HMAC_KEY",
             "NODERED_ADMIN_USER", "NODERED_ADMIN_PASSWORD_HASH", "NODERED_CREDENTIAL_SECRET"]
    keys = order + [k for k in env if k not in order]
    lines = ["# Tạo bởi tools/setup_security.py. KHÔNG commit tệp này."]
    # Nháy đơn để docker compose không hiểu ký tự $ trong bcrypt hash là biến.
    lines += [f"{k}='{env.get(k, '')}'" for k in keys]
    ENV_FILE.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rotate", action="store_true", help="sinh khóa HMAC và mọi mật khẩu mới")
    args = ap.parse_args()

    env = read_env()
    shown = {}

    if args.rotate or len(env.get("SHELF_HMAC_KEY", "")) < 16:
        env["SHELF_HMAC_KEY"] = secrets.token_urlsafe(32)
        shown["Khóa ký lệnh SHELF_HMAC_KEY (nhập vào nút Bảo mật trên dashboard)"] = env["SHELF_HMAC_KEY"]
    env.setdefault("MQTT_HOST", "broker.hivemq.com")
    env.setdefault("MQTT_PORT", "8883")
    env.setdefault("NODERED_ADMIN_USER", "admin")
    if args.rotate or not env.get("NODERED_CREDENTIAL_SECRET"):
        env["NODERED_CREDENTIAL_SECRET"] = secrets.token_urlsafe(32)

    if args.rotate or not env.get("NODERED_ADMIN_PASSWORD_HASH"):
        pw = secrets.token_urlsafe(16)
        env["NODERED_ADMIN_PASSWORD_HASH"] = bcrypt_hash(pw)
        shown[f"Node-RED (http://localhost:1880) tài khoản {env['NODERED_ADMIN_USER']}"] = pw

    if args.rotate or not HTPASSWD.exists():
        pw = secrets.token_urlsafe(16)
        HTPASSWD.parent.mkdir(parents=True, exist_ok=True)
        HTPASSWD.write_text(f"viewer:{bcrypt_hash(pw)}\n", encoding="utf-8", newline="\n")
        shown["Web dashboard (http://localhost:8088) tài khoản viewer"] = pw

    if args.rotate or not MOSQ_PASSWD.exists():
        creds = {u: secrets.token_urlsafe(16) for u in MQTT_USERS}
        MOSQ_PASSWD.write_text(mosquitto_passwd_file(creds), encoding="utf-8", newline="\n")
        for u, p in creds.items():
            shown[f"Mosquitto nội bộ tài khoản {u}"] = p

    write_env(env)

    if not SECRETS_H.exists():
        text = SECRETS_EXAMPLE.read_text(encoding="utf-8")
        text = text.replace('#define SHELF_HMAC_KEY ""', f'#define SHELF_HMAC_KEY "{env["SHELF_HMAC_KEY"]}"')
        SECRETS_H.write_text(text, encoding="utf-8")
        print(f"Đã tạo {SECRETS_H.relative_to(ROOT)} (điền thêm token Telegram nếu dùng).")
    elif env["SHELF_HMAC_KEY"] not in SECRETS_H.read_text(encoding="utf-8"):
        print("LƯU Ý: include/secrets.h đã có sẵn; hãy đặt SHELF_HMAC_KEY trong đó trùng với .env.")

    print("Đã ghi .env, docker/nginx/auth/.htpasswd, docker/mosquitto/config/passwd.\n")
    if shown:
        print("=== Mật khẩu mới (chỉ hiện một lần) ===")
        for k, v in shown.items():
            print(f"- {k}: {v}")


if __name__ == "__main__":
    main()
