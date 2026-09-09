"""Локальні тести honeytokens (без мережі/Telegram). Запуск: python test_honeytokens.py"""
import honeytokens as ht
import alerts

ON = {"honeytokens": {"enabled": True, "token": "espos_bkp_7f3a1c9e", "cooldown_s": 100}}
OFF = {"honeytokens": {"enabled": False, "token": "espos_bkp_7f3a1c9e"}}


def ev(path, args=None, headers=None, cfg=ON):
    return ht.evaluate_request(path, args or {}, headers or {}, cfg)


def test_disabled_never_trips():
    assert ev("/api/backup", cfg=OFF) is None
    assert ev("/x", {"token": "espos_bkp_7f3a1c9e"}, cfg=OFF) is None
    assert ht.evaluate_request("/api/backup", {}, {}, {}) is None  # нема секції взагалі


def test_decoy_path_hit():
    assert ev("/api/backup")[0] == "honey_path"
    assert ev("/.env")[0] == "honey_path"
    assert ev("/wp-login.php")[0] == "honey_path"


def test_decoy_path_normalization():
    assert ev("/API/Backup/")[0] == "honey_path"    # регістр + хвостовий слеш
    assert ev("/api/live") is None                  # реальний роут — не приманка
    assert ev("/") is None


def test_honey_cred_in_args():
    assert ev("/anything", {"token": "espos_bkp_7f3a1c9e"})[0] == "honey_cred"
    assert ev("/anything", {"q": "prefix-espos_bkp_7f3a1c9e-suffix"})[0] == "honey_cred"  # підрядок


def test_honey_cred_in_headers():
    assert ev("/x", {}, {"Authorization": "Bearer espos_bkp_7f3a1c9e"})[0] == "honey_cred"
    assert ev("/x", {}, {"X-Gateway-Token": "espos_bkp_7f3a1c9e"})[0] == "honey_cred"
    assert ev("/x", {}, {"User-Agent": "espos_bkp_7f3a1c9e"}) is None  # не чутливий заголовок


def test_short_token_ignored():
    cfg = {"honeytokens": {"enabled": True, "token": "abc"}}   # < 8 симв. -> ігнор cred
    assert ht.evaluate_request("/x", {"token": "abc"}, {}, cfg) is None


def test_cred_takes_priority_over_path():
    # на decoy-шляху ще й з токеном -> cred важливіший (перше спрацювання)
    assert ev("/api/backup", {"token": "espos_bkp_7f3a1c9e"})[0] == "honey_cred"


# ── Flask-блупринт (інтеграція) ─────────────────────────────────────────────
def _flask_app():
    from flask import Flask
    fired = []
    app = Flask(__name__)
    gate = alerts.AlertGate(cooldown_s=100)
    app.register_blueprint(ht.make_blueprint(ON, notify_fn=lambda t: fired.append(t), gate=gate))

    @app.route("/real")
    def real():
        return "ok"
    return app, fired


def test_bp_decoy_returns_404_and_alerts():
    app, fired = _flask_app()
    c = app.test_client()
    r = c.get("/api/backup")
    assert r.status_code == 404            # блендимось під «нема шляху»
    assert len(fired) == 1 and "HONEYTOKEN" in fired[0]


def test_bp_dedup_within_cooldown():
    app, fired = _flask_app()
    c = app.test_client()
    c.get("/api/backup"); c.get("/api/backup")   # той самий IP+key у межах cooldown
    assert len(fired) == 1                        # друге спрацювання проковтнуто


def test_bp_real_route_untouched():
    app, fired = _flask_app()
    c = app.test_client()
    r = c.get("/real")
    assert r.status_code == 200 and r.get_data(as_text=True) == "ok"
    assert fired == []                            # легіт-запит нічого не тригерить


def test_bp_cred_trips_on_any_request():
    app, fired = _flask_app()
    c = app.test_client()
    r = c.get("/real?token=espos_bkp_7f3a1c9e")   # honey-cred на реальному роуті
    assert r.status_code == 200                   # запит НЕ блокується
    assert len(fired) == 1 and "HONEYTOKEN" in fired[0]


if __name__ == "__main__":
    n = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn(); print(f"  PASS {name}"); n += 1
    print(f"== {n} tests passed ==")
