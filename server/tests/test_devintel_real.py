"""devintel на РЕАЛЬНОМУ хості: глибокий скан (порти/banner/http/nbns/ssdp) -> classify -> summary,
і наскрізь у профіль. Ціль — жива плата (192.168.100.41, реальний HTTP-девайс). Fault: недосяжний IP."""
import os
import pytest

BOARD_IP = os.environ.get("ESPOS_BOARD_IP", "192.168.100.41")


def _reachable(ip, port=80, t=1.0):
    import socket
    try:
        s = socket.socket(); s.settimeout(t); s.connect((ip, port)); s.close(); return True
    except Exception:
        return False


def test_oui_and_random_mac_real():
    import devintel
    # реальні MAC із захоплення: глобальний вендор vs локально-адміністрований
    assert devintel.is_random_mac("F8:FE:5E:8A:3C:DE") is False   # глобальний
    assert devintel.is_random_mac("02:AA:BB:CC:DD:EE") is True    # locally-administered біт
    v = devintel.oui_vendor("F8:FE:5E:00:00:01")
    assert isinstance(v, str)                                     # OUI-лукап не падає


def test_scan_ports_real_board():
    if not _reachable(BOARD_IP):
        pytest.skip(f"плата {BOARD_IP} недосяжна по :80")
    import devintel
    ports = devintel.scan_ports(BOARD_IP, timeout=0.8)
    assert 80 in ports, f"плата має відкритий :80, знайдено {ports}"


def test_intel_real_board_classifies():
    if not _reachable(BOARD_IP):
        pytest.skip(f"плата {BOARD_IP} недосяжна")
    import devintel
    info = devintel.intel(BOARD_IP, mac="F8:FE:5E:8A:3C:DE", vendor="", budget=8.0)
    assert isinstance(info, dict)
    assert 80 in (info.get("ports") or [])
    assert info.get("category")                                   # якась класифікація присвоєна
    line = devintel.summary_line(info)
    assert isinstance(line, str) and line                         # summary будується без винятку


def test_intel_feeds_profile(fresh_db):
    """Реальний intel -> upsert_device -> профіль зберігає category/ports/intel_json."""
    if not _reachable(BOARD_IP):
        pytest.skip(f"плата {BOARD_IP} недосяжна")
    db = fresh_db
    import devintel, profiles
    info = devintel.intel(BOARD_IP, mac="F8:FE:5E:8A:3C:DE", budget=8.0)
    did = profiles.upsert_device("F8:FE:5E:8A:3C:DE", BOARD_IP, category=info.get("category"),
                                 intel=info, event="scan")
    prof = profiles.device_profile(did)
    assert prof["device"]["category"] == info.get("category")
    assert prof["device"]["open_ports"] and "80" in prof["device"]["open_ports"]
    assert prof["device"].get("intel")                           # intel_json розпарсився назад


def test_intel_unreachable_ip_graceful():
    """Недосяжний IP (fault) -> intel повертає dict без винятку, з порожніми портами."""
    import devintel
    info = devintel.intel("192.0.2.123", mac="AA:BB:CC:DD:EE:FF", budget=3.0)  # TEST-NET-1, нема відповіді
    assert isinstance(info, dict)
    assert (info.get("ports") or []) == []                       # нічого не відкрито/не впало
