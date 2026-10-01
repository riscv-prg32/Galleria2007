#!/usr/bin/env python3
"""Run a Galleria 2007 cartridge in PRG32 QEMU with scripted input.

Saves framebuffer screenshots (PNG), the firmware audio stream (WAV) and,
with --video, a 30-second MP4 preview of actual QEMU gameplay and audio:

  scripts/qemu_preview.py --script smoke --out build/qemu-smoke
  scripts/qemu_preview.py --script preview --video --out build/qemu-preview

Requirements: the PRG32 QEMU firmware built in $PRG32_REPO/build-qemu
(`python3 -m prg32 qemu build`), Espressif's qemu-riscv32, Pillow, and ffmpeg
for --video. QEMU uses the SDL display with SDL's offscreen driver (the
firmware waits for display refreshes) so no window opens. Espressif QEMU has
no `screendump`, so frames are read from the firmware framebuffer (`g_fb`,
320x240 RGB565) through QMP `pmemsave`.

Adapted from scripts/qemu_preview.py of Cockroaches_Cathisteria (PRG32
project family, MIT). The flash image is copied to a temporary directory, so
the PRG32 checkout is never modified.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import wave
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
PRG32 = Path(os.environ.get("PRG32_REPO", ROOT.parent / "PRG32")).resolve()
CONSOLE_PORT, AUDIO_PORT, QMP_PORT = 4522, 4521, 4523
RATE = 22050

# PRG32 QEMU UART keyboard: w/s/a/d directions, j = A, k = B, Enter = START.
KEYS = {"left": "a", "right": "d", "up": "w", "down": "s", "a": "j", "b": "k", "start": "\r"}

# (delay seconds, action, argument). Actions: key, hold, chord, shot.
SCRIPTS = {
    "smoke": [
        (7.0, "shot", "title"),
        (0.3, "key", "a"),
        (1.5, "shot", "intro"),
        (1.5, "key", "a"),
        (1.5, "shot", "start"),
        (0.2, "hold", ("up", 2.5)),
        (0.2, "shot", "walk"),
        (0.2, "key", "b"),
        (0.6, "shot", "torch_off"),
        (0.2, "key", "b"),
        (0.3, "chord", None),
        (0.8, "shot", "backpack"),
        (0.2, "key", "b"),
        (0.5, "hold", ("left", 0.8)),
        (0.4, "shot", "turn"),
    ],
    "preview": [
        (6.0, "shot", None),
        (0.3, "key", "a"),
        (2.5, "key", "a"),
        (1.0, "hold", ("up", 3.0)),
        (0.2, "hold", ("left", 0.5)),
        (0.2, "hold", ("right", 1.0)),
        (0.2, "hold", ("up", 3.5)),
        (0.3, "key", "b"),
        (1.5, "key", "b"),
        (0.5, "hold", ("right", 0.6)),
        (0.2, "hold", ("up", 3.0)),
        (0.3, "chord", None),
        (2.0, "key", "b"),
        (0.4, "hold", ("left", 1.2)),
        (0.2, "hold", ("up", 3.0)),
        (0.3, "hold", ("right", 0.7)),
        (0.2, "hold", ("up", 2.0)),
        (2.5, "shot", None),
    ],
}


def find_qemu() -> str:
    env = os.environ.get("QEMU_BIN")
    if env:
        return env
    found = sorted(Path.home().glob(".espressif/tools/qemu-riscv32/*/qemu/bin/qemu-system-riscv32"))
    return str(found[-1]) if found else "qemu-system-riscv32"


def connect(port: int, proc: subprocess.Popen, timeout: float = 25) -> socket.socket:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(f"QEMU exited early ({proc.returncode})")
        s = socket.socket()
        try:
            s.connect(("127.0.0.1", port))
            s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            return s
        except ConnectionRefusedError:
            s.close()
            time.sleep(0.1)
    raise TimeoutError(f"port {port}")


class Qmp:
    def __init__(self, sock: socket.socket):
        self.f = sock.makefile("rw")
        self.f.readline()
        self.execute("qmp_capabilities")

    def execute(self, name: str, **arguments):
        msg = {"execute": name}
        if arguments:
            msg["arguments"] = arguments
        self.f.write(json.dumps(msg) + "\n")
        self.f.flush()
        while True:
            reply = json.loads(self.f.readline())
            if "return" in reply:
                return reply["return"]
            if "error" in reply:
                raise RuntimeError(reply["error"])


def framebuffer_address() -> int:
    elf = PRG32 / "build-qemu/PRG32.elf"
    nm = shutil.which("riscv32-esp-elf-nm")
    if not nm:
        found = sorted(Path.home().glob(".espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin/riscv32-esp-elf-nm"))
        nm = str(found[-1]) if found else "riscv32-esp-elf-nm"
    out = subprocess.run([nm, str(elf)], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == "g_fb":
            return int(parts[0], 16)
    raise SystemExit("g_fb not found in the QEMU firmware ELF")


def rgb565_image(raw: bytes) -> Image.Image:
    img = Image.new("RGB", (320, 240))
    px = img.load()
    for i in range(320 * 240):
        v = raw[2 * i] | (raw[2 * i + 1] << 8)
        r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
        px[i % 320, i // 320] = ((r * 527 + 23) >> 6, (g * 259 + 33) >> 6, (b * 527 + 23) >> 6)
    return img


def audio_reader(sock: socket.socket, stop: threading.Event, pcm: bytearray):
    """Grant one 20 ms audio credit ('K') every 20 ms and collect the PCM."""
    sock.settimeout(0.005)
    next_credit = time.monotonic()
    while not stop.is_set():
        now = time.monotonic()
        if now >= next_credit:
            try:
                sock.sendall(b"K")
            except OSError:
                return
            next_credit += 0.02
            if next_credit < now - 0.2:
                next_credit = now
        try:
            data = sock.recv(8192)
        except (TimeoutError, socket.timeout):
            continue
        except OSError:
            return
        if not data:
            return
        pcm.extend(data)


def console_reader(sock: socket.socket, stop: threading.Event, log: bytearray):
    sock.settimeout(0.3)
    while not stop.is_set():
        try:
            data = sock.recv(4096)
        except (TimeoutError, socket.timeout):
            continue
        except OSError:
            return
        if not data:
            return
        log.extend(data)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cart", type=Path, default=ROOT / "dist/galleria2007-it-qemu.prg32")
    ap.add_argument("--script", choices=sorted(SCRIPTS), default="smoke")
    ap.add_argument("--out", type=Path, default=ROOT / "build/qemu-smoke")
    ap.add_argument("--video", action="store_true", help="record frames for a 30 s MP4")
    ap.add_argument("--fps", type=int, default=8)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    flash_src, efuse_src = PRG32 / "build-qemu/qemu_flash.bin", PRG32 / "build-qemu/qemu_efuse.bin"
    for f in (flash_src, efuse_src, args.cart):
        if not f.exists():
            raise SystemExit(f"missing {f}")

    with tempfile.TemporaryDirectory(prefix="g2007-qemu-") as tmp_s:
        tmp = Path(tmp_s)
        flash, efuse = tmp / "flash.bin", tmp / "efuse.bin"
        shutil.copy2(flash_src, flash)
        shutil.copy2(efuse_src, efuse)
        subprocess.run([sys.executable, str(ROOT / "tools/prg32_cli.py"), "qemu", "upload",
                        str(args.cart.resolve()), "--flash", str(flash)], cwd=PRG32, check=True,
                       stdout=subprocess.DEVNULL, env=dict(os.environ, PRG32_REPO=str(PRG32)))
        cmd = [find_qemu(), "-M", "esp32c3", "-m", "4M",
               "-drive", f"file={flash},if=mtd,format=raw",
               "-drive", f"file={efuse},if=none,format=raw,id=efuse",
               "-global", "driver=nvram.esp32c3.efuse,property=drive,value=efuse",
               "-global", "driver=timer.esp32c3.timg,property=wdt_disable,value=true",
               "-nic", "user,model=open_eth", "-display", "sdl",
               "-qmp", f"tcp:127.0.0.1:{QMP_PORT},server=on,wait=off",
               "-serial", f"tcp::{CONSOLE_PORT},server=on,wait=off,nodelay=on",
               "-serial", f"tcp::{AUDIO_PORT},server=on,wait=on,nodelay=on"]
        log = open(tmp / "qemu.log", "wb")
        env = dict(os.environ, SDL_VIDEODRIVER=os.environ.get("SDL_VIDEODRIVER", "offscreen"))
        proc = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, env=env)
        stop = threading.Event()
        pcm, transcript = bytearray(), bytearray()
        frames: list[Path] = []
        try:
            audio = connect(AUDIO_PORT, proc)
            threading.Thread(target=audio_reader, args=(audio, stop, pcm), daemon=True).start()
            console = connect(CONSOLE_PORT, proc)
            threading.Thread(target=console_reader, args=(console, stop, transcript), daemon=True).start()
            qmp = Qmp(connect(QMP_PORT, proc))
            fb_addr = framebuffer_address()
            qmp_lock = threading.Lock()

            def grab(path: Path) -> None:
                dump = tmp / "fb.raw"
                with qmp_lock:
                    qmp.execute("stop")
                    qmp.execute("pmemsave", val=fb_addr, size=320 * 240 * 2, filename=str(dump))
                    qmp.execute("cont")
                rgb565_image(dump.read_bytes()).save(path)

            video_stop = threading.Event()
            audio_mark = [0]

            def recorder():
                n = 0
                period = 1.0 / args.fps
                nxt = time.monotonic()
                while not video_stop.is_set():
                    p = tmp / f"frame{n:05d}.png"
                    grab(p)
                    frames.append(p)
                    n += 1
                    nxt += period
                    time.sleep(max(0.0, nxt - time.monotonic()))

            def tap(name: str, seconds: float = 0.12):
                end = time.monotonic() + seconds
                while True:
                    console.sendall(KEYS[name].encode())
                    time.sleep(0.03)
                    if time.monotonic() >= end:
                        break

            rec = None
            for i, (delay, action, arg) in enumerate(SCRIPTS[args.script]):
                time.sleep(delay)
                if args.video and rec is None and i == 1:
                    audio_mark[0] = len(pcm)
                    rec = threading.Thread(target=recorder, daemon=True)
                    rec.start()
                if action == "key":
                    tap(arg)
                elif action == "hold":
                    tap(arg[0], arg[1])
                elif action == "chord":
                    console.sendall(b"jk")      # both inside the 110 ms chord window
                    time.sleep(0.15)
                elif action == "shot" and arg:
                    path = args.out / f"{arg}.png"
                    grab(path)
                    print(f"shot {path}")
            if rec is not None:
                video_stop.set()
                rec.join(timeout=5)
        finally:
            stop.set()
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
            log.close()
            (args.out / "console.log").write_bytes(transcript)
            shutil.copy2(tmp / "qemu.log", args.out / "qemu.log")
        if pcm:
            start = audio_mark[0] if args.video else 0
            data = bytes(pcm[start:])
            data = data[:len(data) - len(data) % 2]
            with wave.open(str(args.out / "audio.wav"), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(RATE)
                w.writeframes(data)
            print(f"audio {args.out / 'audio.wav'} ({len(data) / 2 / RATE:.1f} s)")
        if args.video and frames:
            ffmpeg = shutil.which("ffmpeg") or "ffmpeg"
            for k, f in enumerate(frames):        # crop the firmware status bands
                Image.open(f).crop((0, 20, 320, 220)).resize((640, 400), Image.NEAREST).save(tmp / f"v{k:05d}.png")
            seconds = len(frames) / args.fps
            mp4 = args.out / "preview.mp4"
            subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-framerate", str(args.fps),
                            "-i", str(tmp / "v%05d.png"), "-i", str(args.out / "audio.wav"),
                            "-t", f"{min(30.0, seconds):.2f}", "-c:v", "libx264", "-pix_fmt", "yuv420p",
                            "-c:a", "aac", "-b:a", "96k", "-shortest", str(mp4)], check=True)
            print(f"video {mp4} ({min(30.0, seconds):.1f} s, {len(frames)} frames)")
        if b"loaded cartridge" not in transcript.lower() and b"cartridge" not in transcript.lower():
            print("warning: no cartridge load message in console.log", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
