# -*- coding: utf-8 -*-
"""
智能叫号系统 · 患者端服务

- 单文件 Flask 应用 + SQLite，Windows 与树莓派通用，无需改动即可移植
- 启动：python app.py      （树莓派：python3 app.py）
- 手机访问：http://<电脑局域网IP>:5000
- 数据接口同时供患者网页与后续微信小程序使用

接口：
    POST /api/device/call       终端上报叫号 / 过号 / 暂停
    POST /api/device/heartbeat  终端心跳（建议 5 秒一次）
    GET  /api/queue?room=01     查询诊室当前状态
    GET  /api/history?room=01   历史叫号记录
"""

from pathlib import Path
from datetime import datetime
import json
import sqlite3
import time

from flask import Flask, jsonify, request, send_from_directory

BASE_DIR = Path(__file__).resolve().parent
DB_PATH = BASE_DIR / "queue.db"
STATIC_DIR = BASE_DIR / "static"

ONLINE_TIMEOUT = 60.0          # 超过该秒数没有心跳即判定终端离线
                               # 需容忍 WiFi 断开后重连时间（扫描+连接最坏约 45 秒）
DEFAULT_ROOM = "01"
LOCAL_DEMO_COUNT = 12          # STM32 本地预置的演示号数量（A001~A012），
                               # 服务端取号从该值之后递增，避免与终端本地号冲突

app = Flask(__name__, static_folder=str(STATIC_DIR), static_url_path="")


# --------------------------------------------------------------------------
# 数据库
# --------------------------------------------------------------------------
def db():
    conn = sqlite3.connect(str(DB_PATH))
    conn.row_factory = sqlite3.Row
    return conn


def now_str():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def init_db():
    conn = db()
    conn.executescript(
        """
        CREATE TABLE IF NOT EXISTS room_state (
            room           TEXT PRIMARY KEY,
            current_ticket TEXT,
            waiting        INTEGER NOT NULL DEFAULT 0,
            passed         INTEGER NOT NULL DEFAULT 0,
            paused         INTEGER NOT NULL DEFAULT 0,
            last_seen      REAL    NOT NULL DEFAULT 0,
            updated_at     TEXT
        );
        CREATE TABLE IF NOT EXISTS history (
            id     INTEGER PRIMARY KEY AUTOINCREMENT,
            room   TEXT NOT NULL,
            ticket TEXT,
            event  TEXT NOT NULL,
            ts     TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS tickets (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            room       TEXT NOT NULL,
            ticket     TEXT NOT NULL,
            seq        INTEGER NOT NULL,
            status     TEXT NOT NULL DEFAULT 'waiting',
            created_at TEXT NOT NULL
        );
        """
    )

    # 每次启动清空取号记录与历史，号码从 A001 重新开始
    conn.execute("DELETE FROM tickets")
    conn.execute("DELETE FROM history")
    conn.execute("DELETE FROM room_state")
    conn.execute(
        "INSERT INTO room_state"
        "(room,current_ticket,waiting,passed,paused,last_seen,updated_at)"
        " VALUES(?,?,?,?,?,?,?)",
        (DEFAULT_ROOM, "", 0, 0, 0, time.time(), now_str()),
    )
    conn.commit()
    conn.close()


def add_history(room, ticket, event):
    conn = db()
    conn.execute(
        "INSERT INTO history(room,ticket,event,ts) VALUES(?,?,?,?)",
        (room, ticket, event, now_str()),
    )
    conn.commit()
    conn.close()


def upsert_state(room, ticket=None, waiting=None, passed=None, paused=None):
    """写入诊室状态；传 None 的字段保持原值，last_seen 统一刷新。"""
    conn = db()
    row = conn.execute(
        "SELECT * FROM room_state WHERE room=?", (room,)
    ).fetchone()

    if row is None:
        conn.execute(
            "INSERT INTO room_state"
            "(room,current_ticket,waiting,passed,paused,last_seen,updated_at)"
            " VALUES(?,?,?,?,?,?,?)",
            (
                room,
                ticket if ticket is not None else "",
                waiting if waiting is not None else 0,
                passed if passed is not None else 0,
                paused if paused is not None else 0,
                time.time(),
                now_str(),
            ),
        )
    else:
        conn.execute(
            "UPDATE room_state SET current_ticket=?,waiting=?,passed=?,"
            "paused=?,last_seen=?,updated_at=? WHERE room=?",
            (
                ticket if ticket is not None else row["current_ticket"],
                waiting if waiting is not None else row["waiting"],
                passed if passed is not None else row["passed"],
                paused if paused is not None else row["paused"],
                time.time(),
                now_str(),
                room,
            ),
        )

    conn.commit()
    conn.close()


# --------------------------------------------------------------------------
# 设备接口
# --------------------------------------------------------------------------
def _as_int(value, default=0):
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def _as_bool_int(value):
    if isinstance(value, str):
        return 1 if value.strip().lower() in ("1", "true", "yes", "on") else 0
    return 1 if value else 0


@app.post("/api/device/call")
def device_call():
    """终端叫号 / 过号 / 暂停时上报。"""
    data = request.get_json(silent=True) or {}
    room = str(data.get("room", DEFAULT_ROOM))

    ticket = data.get("ticket")
    ticket = str(ticket) if ticket not in (None, "") else None

    # 叫号时，把 tickets 表中已叫过的号标记为 done，
    # 这样 waiting 数（status='waiting' 的数量）会自然减少。
    # 注意：只更新 seq <= 当前叫号序号的记录，避免跳过中间号。
    if ticket is not None:
        cur_seq = parse_seq(ticket)
        if cur_seq > 0:
            conn = db()
            conn.execute(
                "UPDATE tickets SET status='done'"
                " WHERE room=? AND seq<=? AND status='waiting'",
                (room, cur_seq),
            )
            conn.commit()
            conn.close()

    # waiting 不再采信终端上报值，改由 get_queue() 从 tickets 表实时计算；
    # passed（过号数）服务端无数据源，仍以终端上报为准。
    upsert_state(
        room,
        ticket=ticket,
        passed=_as_int(data.get("passed"), None),
        paused=_as_bool_int(data.get("paused")) if "paused" in data else None,
    )
    add_history(room, ticket, str(data.get("state", "call")))
    return jsonify({"ok": True})


@app.post("/api/device/heartbeat")
def device_heartbeat():
    """终端心跳，只刷新在线时间。waiting 由 tickets 表实时计算。"""
    data = request.get_json(silent=True) or {}
    room = str(data.get("room", DEFAULT_ROOM))

    ticket = data.get("ticket")
    ticket = str(ticket) if ticket not in (None, "") else None

    upsert_state(
        room,
        ticket=ticket,
        paused=_as_bool_int(data.get("paused")) if "paused" in data else None,
    )
    return jsonify({"ok": True})


# --------------------------------------------------------------------------
# 调试接口（按键状态上报，用于排查同步问题）
# --------------------------------------------------------------------------
DEBUG_LOG = BASE_DIR / "debug.log"


@app.post("/api/device/debug")
def device_debug():
    """接收终端按键时的状态快照，追加写入 debug.log。"""
    data = request.get_json(silent=True) or {}
    data["_ts"] = now_str()
    data["_ip"] = request.remote_addr

    # 任何来自终端的有效请求都证明设备在线，不能只依赖心跳 POST。
    upsert_state(str(data.get("room", DEFAULT_ROOM)))

    with open(DEBUG_LOG, "a", encoding="utf-8") as f:
        f.write(json.dumps(data, ensure_ascii=False) + "\n")

    return jsonify({"ok": True})


@app.get("/api/device/debug")
def get_debug_log():
    """返回最近的调试日志（默认 50 条，最多 200 条）。"""
    limit = max(1, min(_as_int(request.args.get("limit"), 50), 200))
    lines = []

    if DEBUG_LOG.exists():
        with open(DEBUG_LOG, "r", encoding="utf-8") as f:
            lines = f.readlines()

    # 取最后 limit 条，倒序（最新在前）
    entries = []
    for line in reversed(lines[-limit:]):
        line = line.strip()
        if line:
            try:
                entries.append(json.loads(line))
            except json.JSONDecodeError:
                pass

    return jsonify(entries)


# --------------------------------------------------------------------------
# 查询接口
# --------------------------------------------------------------------------
@app.get("/api/queue")
def get_queue():
    room = request.args.get("room", DEFAULT_ROOM)
    conn = db()
    row = conn.execute("SELECT * FROM room_state WHERE room=?", (room,)).fetchone()

    if row is None:
        conn.close()
        return jsonify({"ok": False, "error": "room not found"}), 404

    # 等候数从 tickets 表实时计算：status='waiting' 的数量。
    # 不采信 STM32 上报的 waiting（终端本地队列不含网页端取的号）。
    waiting = conn.execute(
        "SELECT COUNT(*) FROM tickets WHERE room=? AND status='waiting'",
        (room,),
    ).fetchone()[0]

    conn.close()

    online = (time.time() - row["last_seen"]) <= ONLINE_TIMEOUT
    return jsonify(
        {
            "room": row["room"],
            "current": row["current_ticket"],
            "waiting": waiting,
            "passed": row["passed"],
            "paused": bool(row["paused"]),
            "online": online,
            "updated_at": row["updated_at"],
        }
    )


@app.get("/api/device/pending")
def device_pending():
    """终端拉取网页端新取的等候号。

    返回纯文本，逗号分隔，例如 "A013,A014,A015"；没有则返回空字符串。
    since 参数为终端当前队列中最大的序号，只返回序号更大的号，
    避免把终端已有的本地演示号重复拉回来。
    """
    room = request.args.get("room", DEFAULT_ROOM)
    since = _as_int(request.args.get("since"), 0)

    # 拉取新号同样是终端在线证据，避免少量心跳丢失造成误报离线。
    upsert_state(room)

    conn = db()
    rows = conn.execute(
        "SELECT ticket FROM tickets"
        " WHERE room=? AND seq>? AND status='waiting'"
        " ORDER BY seq ASC",
        (room, since),
    ).fetchall()
    conn.close()

    # 末尾加换行，确保 ESP8266 能按行解析（否则 body 会与 CLOSED 合并）
    return ",".join(r["ticket"] for r in rows) + "\n"


@app.get("/api/history")
def get_history():
    room = request.args.get("room", DEFAULT_ROOM)
    limit = max(1, min(_as_int(request.args.get("limit"), 20), 100))

    conn = db()
    rows = conn.execute(
        "SELECT room,ticket,event,ts FROM history"
        " WHERE room=? ORDER BY id DESC LIMIT ?",
        (room, limit),
    ).fetchall()
    conn.close()

    return jsonify([dict(r) for r in rows])


# --------------------------------------------------------------------------
# 取号接口（患者端）
# --------------------------------------------------------------------------
def parse_seq(ticket):
    """从 A012 这类号码中取出数字部分，非法输入返回 0。"""
    if not ticket:
        return 0
    digits = "".join(ch for ch in str(ticket) if ch.isdigit())
    return int(digits) if digits else 0


@app.post("/api/ticket")
def create_ticket():
    """患者取号：号码接在当前叫号与已发号码之后递增。"""
    data = request.get_json(silent=True) or {}
    room = str(data.get("room", DEFAULT_ROOM))

    conn = db()
    row = conn.execute(
        "SELECT current_ticket FROM room_state WHERE room=?", (room,)
    ).fetchone()
    cur_seq = parse_seq(row["current_ticket"]) if row else 0

    last_seq = conn.execute(
        "SELECT MAX(seq) FROM tickets WHERE room=?", (room,)
    ).fetchone()[0] or 0

    # 起始序号不能低于终端本地预置号数量，避免与终端本地 A001~A012 冲突
    seq = max(cur_seq, last_seq, LOCAL_DEMO_COUNT) + 1
    ticket = "A%03d" % seq

    conn.execute(
        "INSERT INTO tickets(room,ticket,seq,status,created_at) VALUES(?,?,?,?,?)",
        (room, ticket, seq, "waiting", now_str()),
    )
    conn.commit()
    conn.close()

    add_history(room, ticket, "ticket")
    return jsonify(
        {"ok": True, "ticket": ticket, "ahead": max(0, seq - cur_seq - 1)}
    )


@app.get("/api/ticket")
def get_ticket():
    """查询某个号码的排队进度。"""
    room = request.args.get("room", DEFAULT_ROOM)
    ticket = request.args.get("ticket", "")

    conn = db()
    row = conn.execute(
        "SELECT current_ticket FROM room_state WHERE room=?", (room,)
    ).fetchone()
    conn.close()

    if row is None:
        return jsonify({"ok": False, "error": "room not found"}), 404

    cur_seq = parse_seq(row["current_ticket"])
    my_seq = parse_seq(ticket)
    called = my_seq > 0 and my_seq <= cur_seq

    return jsonify(
        {
            "ok": True,
            "room": room,
            "ticket": ticket,
            "current": row["current_ticket"],
            "ahead": 0 if called else max(0, my_seq - cur_seq - 1),
            "called": called,
        }
    )


# --------------------------------------------------------------------------
# 页面
# --------------------------------------------------------------------------
@app.get("/")
def index():
    return send_from_directory(str(STATIC_DIR), "index.html")


@app.get("/debug")
def debug_page():
    return send_from_directory(str(STATIC_DIR), "debug.html")


@app.post("/api/device/debug/clear")
def clear_debug_log():
    """清空调试日志文件。"""
    if DEBUG_LOG.exists():
        DEBUG_LOG.unlink()
    return jsonify({"ok": True})


init_db()


if __name__ == "__main__":
    # 必须监听 0.0.0.0，否则手机无法访问
    app.run(host="0.0.0.0", port=5000, threaded=True)
