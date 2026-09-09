# Pre-buildfs hook: compress data/web/index.html -> index.html.gz for LittleFS.
# The board serves the .gz variant (gzip content-encoding), a ~4x smaller transfer
# that stays reliable under low free heap. Regenerated automatically on every buildfs.
Import("env")
import gzip, shutil, os
def _gz(*a, **k):
    src = os.path.join(env.get("PROJECT_DIR", "."), "data", "web", "index.html")
    if os.path.exists(src):
        with open(src, "rb") as fi, gzip.open(src + ".gz", "wb", compresslevel=9) as fo:
            shutil.copyfileobj(fi, fo)
        print("[gzip_web] index.html -> index.html.gz (%d bytes)" % os.path.getsize(src + ".gz"))
env.AddPreAction("$BUILD_DIR/littlefs.bin", _gz)
