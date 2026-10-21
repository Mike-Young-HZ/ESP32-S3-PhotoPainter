#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""把 InkTime 选片、800×480 Baseline JPEG、本机 HTTP 下载和设备接受规则串成一次模拟。

用 InkTime 的 venv Python 单独启动。默认 InkTime 根目录是设备仓库同级的 InkTime-main，
可用环境变量 INKTIME_PROJECT_DIR 覆盖。导入 render_daily_photo 和 server 之前，把假 config
放进 sys.modules。本脚本是单独进程，不会 reload 已经导入的模块。

不访问外网，不扫描真实相册。睡眠计划不在这里重写，HTTP 链路之后调用
run_daily_host_tests.py。
"""

from __future__ import annotations

import os
import random
import subprocess
import sys
import tempfile
import threading
import time
import types
import urllib.error
import urllib.parse
import urllib.request
from datetime import date, timedelta
from pathlib import Path

from PIL import Image

DEVICE_REPO = Path(__file__).resolve().parents[4]
DEFAULT_INKTIME = DEVICE_REPO.parent / "InkTime-main"
HOST_TEST = Path(__file__).resolve().parent / "run_daily_host_tests.py"
DOWNLOAD_KEY = "test-key"
SEED = 20261021
MAX_JPEG_BYTES = 2 * 1024 * 1024
HTTP_TIMEOUT_SEC = 5
PHOTO_AREA_Y = 8


class SimFailure(Exception):
    """联调检查失败。由 main 转成非零退出。"""


def inktime_root() -> Path:
    override = os.environ.get("INKTIME_PROJECT_DIR", "").strip()
    if override:
        return Path(override).expanduser().resolve()
    return DEFAULT_INKTIME.resolve()


def install_fake_config(output_dir: Path, album_dir: Path) -> None:
    """必须在导入 InkTime 模块之前调用。密钥只使用占位符。"""
    if "render_daily_photo" in sys.modules or "server" in sys.modules:
        raise SimFailure(
            "render_daily_photo 或 server 已经导入。请单独运行本脚本，不要 reload。"
        )
    config = types.ModuleType("config")
    config.BIN_OUTPUT_DIR = str(output_dir.resolve())
    config.DOWNLOAD_KEY = DOWNLOAD_KEY
    config.MEMORY_THRESHOLD = 70
    config.IMAGE_DIR = str(album_dir.resolve())
    config.DB_PATH = str((output_dir / "unused-photos.db").resolve())
    config.FONT_PATH = ""
    config.ENABLE_REVIEW_WEBUI = False
    sys.modules["config"] = config


def import_inktime(root: Path):
    if not (root / "render_daily_photo.py").is_file() or not (root / "server.py").is_file():
        raise SimFailure(f"InkTime 根目录里没有 render_daily_photo.py 或 server.py：{root}")
    sys.path.insert(0, str(root))
    import render_daily_photo
    import server

    config = sys.modules.get("config")
    if config is None or getattr(config, "__file__", None):
        raise SimFailure("假 config 没有生效，拒绝读取磁盘上的 config.py。")
    if config.DOWNLOAD_KEY != DOWNLOAD_KEY or config.MEMORY_THRESHOLD != 70:
        raise SimFailure("假 config 的 DOWNLOAD_KEY 或 MEMORY_THRESHOLD 不正确。")
    output = Path(render_daily_photo.BIN_OUTPUT_DIR).resolve()
    if output != Path(server.BIN_OUTPUT_DIR).resolve():
        raise SimFailure("render_daily_photo 和 server 的输出目录不一致。")
    for repo in (DEVICE_REPO, root):
        if output == repo or repo in output.parents:
            raise SimFailure(f"输出目录落在仓库里：{output}")
    if server.DOWNLOAD_KEY != DOWNLOAD_KEY:
        raise SimFailure("server.DOWNLOAD_KEY 不是 test-key。")
    if float(render_daily_photo.MEMORY_THRESHOLD) != 70.0:
        raise SimFailure("render_daily_photo.MEMORY_THRESHOLD 不是 70。")
    return render_daily_photo, server


def make_photo(path: Path, color: tuple[int, int, int]) -> None:
    Image.new("RGB", (960, 640), color).save(path, format="PNG")


def candidate(
    path: Path,
    month_day: str,
    taken_on: str,
    memory: float,
    caption: str,
) -> dict:
    return {
        "path": str(path),
        "md": month_day,
        "date": taken_on,
        "side": caption,
        "memory": memory,
        "lat": 31.2304,
        "lon": 121.4737,
        "city": "上海",
    }


def build_candidates(album: Path, today: date) -> tuple[list[dict], dict[str, Path]]:
    """只使用临时 PNG。同一月日的高分照片才是「历史上的今天」候选。"""
    month_day = f"{today.month:02d}-{today.day:02d}"
    other = today - timedelta(days=1)
    other_month_day = f"{other.month:02d}-{other.day:02d}"
    year = today.year - 6
    paths = {
        "high_red": album / "history-red.png",
        "high_blue": album / "history-blue.png",
        "low": album / "history-low.png",
        "other_day": album / "other-day.png",
    }
    colors = {
        "high_red": (210, 40, 36),
        "high_blue": (24, 52, 180),
        "low": (12, 12, 12),
        "other_day": (16, 180, 48),
    }
    for name, path in paths.items():
        make_photo(path, colors[name])
    items = [
        candidate(paths["high_red"], month_day, f"{year}-" + month_day, 96.0, "红日"),
        candidate(paths["high_blue"], month_day, f"{year + 1}-" + month_day, 88.0, "蓝日"),
        candidate(paths["low"], month_day, f"{year + 2}-" + month_day, 40.0, "低于阈值"),
        candidate(
            paths["other_day"],
            other_month_day,
            other.replace(year=year).isoformat(),
            100.0,
            "另一天",
        ),
    ]
    return items, paths


def choose_one(render_daily_photo, items: list[dict], today: date) -> tuple[dict, dict]:
    random.seed(SEED)
    chosen, info = render_daily_photo.choose_photos_for_today(items, today, count=1)
    if len(chosen) != 1:
        raise SimFailure(f"count=1 却选出了 {len(chosen)} 张。")
    month_day = f"{today.month:02d}-{today.day:02d}"
    if info.get("used_md") != month_day or info.get("day_offset") != 0:
        raise SimFailure(f"没有命中历史上的今天：{info}")
    if info.get("fallback_global_max"):
        raise SimFailure("选片走了全局兜底。")
    if float(info.get("threshold", -1)) != 70.0:
        raise SimFailure(f"选片阈值不是 70：{info.get('threshold')}")
    return chosen[0], info


def jpeg_sof_frames(data: bytes) -> list[tuple[int, int, int]]:
    """返回 (marker, height, width)。SOF2（0xC2）是渐进式。"""
    if len(data) < 2 or data[:2] != b"\xff\xd8":
        raise SimFailure("JPEG 不以 FFD8 开头。")
    sof_markers = {
        0xC0, 0xC1, 0xC2, 0xC3,
        0xC5, 0xC6, 0xC7,
        0xC9, 0xCA, 0xCB,
        0xCD, 0xCE, 0xCF,
    }
    frames: list[tuple[int, int, int]] = []
    index = 2
    while index < len(data) - 1:
        if data[index] != 0xFF:
            raise SimFailure(f"JPEG 标记错位：{index}")
        while index < len(data) and data[index] == 0xFF:
            index += 1
        if index >= len(data):
            break
        marker = data[index]
        index += 1
        if marker in (0xD8, 0x01) or 0xD0 <= marker <= 0xD7:
            continue
        if marker == 0xD9:
            break
        if index + 2 > len(data):
            break
        length = int.from_bytes(data[index:index + 2], "big")
        if length < 2 or index + length > len(data):
            raise SimFailure(f"JPEG 段长度非法：{length}")
        payload = data[index + 2:index + length]
        if marker in sof_markers:
            if len(payload) < 5:
                raise SimFailure("SOF 段过短。")
            height = int.from_bytes(payload[1:3], "big")
            width = int.from_bytes(payload[3:5], "big")
            frames.append((marker, height, width))
        if marker == 0xDA:
            index += length
            while index < len(data) - 1:
                if data[index] == 0xFF:
                    nxt = data[index + 1]
                    if nxt == 0x00 or 0xD0 <= nxt <= 0xD7:
                        index += 2
                        continue
                    break
                index += 1
            continue
        index += length
    return frames


def assert_baseline_jpeg(data: bytes) -> None:
    if data[:2] != b"\xff\xd8":
        raise SimFailure("响应体不以 FFD8 开头。")
    if b"\xff\xc0" not in data:
        raise SimFailure("JPEG 不含 SOF0（FFC0）。")
    if b"\xff\xc2" in data:
        raise SimFailure("JPEG 含有 SOF2（FFC2）。")
    if len(data) > MAX_JPEG_BYTES:
        raise SimFailure(f"JPEG 有 {len(data)} 字节，超过 2MB。")
    frames = jpeg_sof_frames(data)
    sof0 = [frame for frame in frames if frame[0] == 0xC0]
    sof2 = [frame for frame in frames if frame[0] == 0xC2]
    if len(sof0) != 1 or sof2:
        raise SimFailure(f"SOF 标记不符合 Baseline：{frames}")
    _marker, height, width = sof0[0]
    if height != 480 or width != 800:
        raise SimFailure(f"SOF 尺寸是高 {height}、宽 {width}，需要高 480、宽 800。")


def assert_jpeg_uses_selected_photo(jpeg_path: Path, selected_path: Path) -> None:
    with Image.open(selected_path) as source:
        expected = source.convert("RGB").getpixel((0, 0))
    with Image.open(jpeg_path) as rendered:
        if rendered.size != (800, 480):
            raise SimFailure(f"写出的 JPEG 尺寸是 {rendered.size}。")
        actual = rendered.convert("RGB").getpixel((8, PHOTO_AREA_Y))
    if any(abs(left - right) > 20 for left, right in zip(expected, actual)):
        raise SimFailure(
            f"JPEG 上方颜色 {actual} 与选中照片 {expected} 不一致，写文件没用这张图。"
        )


def device_accepts(status: int, body: bytes) -> bool:
    """与 Daily_mode.cpp 一致：只收 HTTP 200、FFD8、不超过 2MB。"""
    if status != 200:
        return False
    if len(body) < 2 or len(body) > MAX_JPEG_BYTES:
        return False
    return body[:2] == b"\xff\xd8"


def offer_to_cache(cache: Path, status: int, body: bytes) -> bool:
    before = cache.read_bytes()
    if device_accepts(status, body):
        cache.write_bytes(body)
        if cache.read_bytes() != body:
            raise SimFailure("成功响应没有替换缓存。")
        return True
    if cache.read_bytes() != before:
        raise SimFailure("404 或非法内容替换了已有缓存。")
    return False


class _LocalRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        host = urllib.parse.urlparse(newurl).hostname
        if host != "127.0.0.1":
            raise SimFailure(f"拒绝访问非本机地址：{newurl}")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def http_get(url: str) -> tuple[int, str, bytes]:
    request = urllib.request.Request(url, method="GET")
    opener = urllib.request.build_opener(_LocalRedirect)
    try:
        with opener.open(request, timeout=HTTP_TIMEOUT_SEC) as response:
            content_type = response.headers.get("Content-Type", "")
            return response.status, content_type, response.read()
    except urllib.error.HTTPError as error:
        content_type = error.headers.get("Content-Type", "") if error.headers else ""
        return error.code, content_type, error.read()


def start_server(app):
    from werkzeug.serving import make_server

    httpd = make_server("127.0.0.1", 0, app, threaded=True)
    port = int(httpd.server_address[1])
    thread = threading.Thread(target=httpd.serve_forever, name="photopainter-http", daemon=True)
    thread.start()
    deadline = time.monotonic() + 5
    last_error = "连接超时"
    probe = f"http://127.0.0.1:{port}/static/inktime/not-ready/photopainter.jpg"
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(probe, timeout=0.2) as response:
                response.read()
            return httpd, thread, port
        except urllib.error.HTTPError as error:
            error.read()
            error.close()
            return httpd, thread, port
        except Exception as exc:
            last_error = str(exc)
            time.sleep(0.05)
    raise SimFailure(f"127.0.0.1 上的临时服务没有起来：{last_error}")


def stop_server(httpd, thread) -> None:
    if httpd is not None:
        httpd.shutdown()
        httpd.server_close()
    if thread is not None:
        thread.join(timeout=5)


def run_http_chain(server_module, jpeg_path: Path, cache: Path) -> str:
    good = jpeg_path.read_bytes()
    assert_baseline_jpeg(good)
    cache.write_bytes(b"cached-before-download")
    httpd = None
    thread = None
    try:
        httpd, thread, port = start_server(server_module.app)
        good_url = f"http://127.0.0.1:{port}/static/inktime/{DOWNLOAD_KEY}/photopainter.jpg"
        wrong_url = f"http://127.0.0.1:{port}/static/inktime/wrong-key/photopainter.jpg"

        status, _content_type, body = http_get(wrong_url)
        if status != 404:
            raise SimFailure(f"错误密钥应返回 404，实际 {status}。")
        if offer_to_cache(cache, status, body):
            raise SimFailure("错误密钥不应替换缓存。")

        jpeg_path.unlink()
        status, _content_type, body = http_get(good_url)
        if status != 404:
            raise SimFailure(f"文件缺失应返回 404，实际 {status}。")
        if offer_to_cache(cache, status, body):
            raise SimFailure("文件缺失不应替换缓存。")
        jpeg_path.write_bytes(good)

        jpeg_path.write_bytes(b"this-is-not-a-jpeg")
        status, content_type, body = http_get(good_url)
        if status != 200:
            raise SimFailure(f"非法内容仍是已有文件，应返回 200，实际 {status}。")
        if "image/jpeg" not in content_type:
            raise SimFailure(f"jpg 路由的 Content-Type 不含 image/jpeg：{content_type}")
        if device_accepts(status, body) or offer_to_cache(cache, status, body):
            raise SimFailure("非 FFD8 内容被设备接受了。")

        jpeg_path.write_bytes(b"\xff\xd8" + b"\x00" * MAX_JPEG_BYTES)
        status, _content_type, body = http_get(good_url)
        if status != 200:
            raise SimFailure(f"超大文件应仍返回 200，实际 {status}。")
        if len(body) <= MAX_JPEG_BYTES or offer_to_cache(cache, status, body):
            raise SimFailure("超过 2MB 的内容被设备接受了。")

        jpeg_path.write_bytes(good)
        status, content_type, body = http_get(good_url)
        if status != 200:
            raise SimFailure(f"正确密钥应返回 200，实际 {status}。")
        if "image/jpeg" not in content_type:
            raise SimFailure(f"Content-Type 不含 image/jpeg：{content_type}")
        if body != good:
            raise SimFailure("下载内容与 photopainter.jpg 不一致。")
        assert_baseline_jpeg(body)
        if not offer_to_cache(cache, status, body):
            raise SimFailure("正确 JPEG 没有替换缓存。")
        if cache.read_bytes() != good:
            raise SimFailure("缓存文件不是刚下载的 JPEG。")

        status, _content_type, body = http_get(wrong_url)
        if status != 404 or offer_to_cache(cache, status, body):
            raise SimFailure("成功之后的 404 改写了缓存。")
        if cache.read_bytes() != good:
            raise SimFailure("404 之后缓存不再是成功下载的 JPEG。")
        return good_url
    finally:
        stop_server(httpd, thread)


def run_host_tests() -> None:
    if not HOST_TEST.is_file():
        raise SimFailure(f"找不到主机测试：{HOST_TEST}")
    completed = subprocess.run(["python3", str(HOST_TEST)], check=False)
    if completed.returncode != 0:
        raise SimFailure(f"主机测试未通过，退出码 {completed.returncode}。")


def run(root: Path) -> None:
    output_dir = root / "output"
    album = root / "album"
    output_dir.mkdir()
    album.mkdir()
    install_fake_config(output_dir, album)
    inktime = inktime_root()
    render_daily_photo, server_module = import_inktime(inktime)

    today = date.today()
    items, _paths = build_candidates(album, today)
    selected, _info = choose_one(render_daily_photo, items, today)
    random.seed(SEED)
    jpeg_path = render_daily_photo.write_photopainter_jpg(items, today, output_dir)
    if jpeg_path != output_dir / "photopainter.jpg" or not jpeg_path.is_file():
        raise SimFailure(f"没有写出 photopainter.jpg：{jpeg_path}")
    assert_jpeg_uses_selected_photo(jpeg_path, Path(selected["path"]))

    cache = root / "device" / "daily.jpg"
    cache.parent.mkdir()
    url = run_http_chain(server_module, jpeg_path, cache)
    run_host_tests()
    jpeg_size = jpeg_path.stat().st_size
    print(f"选中路径：{selected['path']}")
    print(f"JPEG 字节数：{jpeg_size}")
    print(f"URL：{url}")
    print("缓存是否替换：成功已替换，404 与非法内容未替换")
    print("主机测试：通过")


def main() -> int:
    try:
        with tempfile.TemporaryDirectory(prefix="photopainter-daily-sim-") as temp:
            run(Path(temp))
    except SimFailure as exc:
        print(str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
