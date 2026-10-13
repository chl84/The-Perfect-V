#!/usr/bin/env python3
"""Bound one explicitly requested graphics test and persist diagnostics locally."""
import argparse
import datetime
import glob
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import threading
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--timeout", type=int, default=45)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command or not 1 <= args.timeout <= 660:
        parser.error("Provide a command and a timeout of 1..660 seconds")
    args.output.mkdir(parents=True, exist_ok=False)
    alarm = threading.Event()
    finished = threading.Event()
    cancelled = threading.Event()
    for signum in (signal.SIGINT, signal.SIGTERM):
        signal.signal(signum, lambda *_: cancelled.set())
    failure = re.compile(r"ring .*timeout|GPU reset begin|GPU Recovery Failed|VM_L2.*fault", re.I)
    meta = {"started": datetime.datetime.now().astimezone().isoformat(), "command": command,
            "timeout": args.timeout,
            "environment": {key: os.environ.get(key) for key in
                            ("AMD_DEBUG", "SDL_VIDEODRIVER", "LIBGL_ALWAYS_SOFTWARE", "GALLIUM_DRIVER",
                             "VK_DRIVER_FILES", "VK_ICD_FILENAMES", "SDL_GPU_DRIVER")}}
    (args.output / "metadata.json").write_text(json.dumps(meta, indent=2) + "\n")
    journal_log = (args.output / "kernel.log").open("w", buffering=1)
    journal = subprocess.Popen(["journalctl", "-k", "-f", "--since=now", "--no-pager", "-o", "short-iso"],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    def read_journal():
        for line in journal.stdout:
            journal_log.write(line)
            journal_log.flush()
            os.fsync(journal_log.fileno())
            if failure.search(line):
                alarm.set()

    reader = threading.Thread(target=read_journal, daemon=True)
    reader.start()
    metrics = (args.output / "metrics.jsonl").open("w", buffering=1)
    dumped = set()

    def collect():
        while not finished.is_set():
            record = {"time": datetime.datetime.now().astimezone().isoformat()}
            for field in ("gpu_busy_percent", "mem_info_vram_used", "mem_info_gtt_used"):
                path = Path("/sys/class/drm/card1/device") / field
                try:
                    record[field] = path.read_text().strip()
                except OSError as e:
                    record[field] = str(e)
            metrics.write(json.dumps(record) + "\n")
            metrics.flush()
            os.fsync(metrics.fileno())
            for source in glob.glob("/sys/class/devcoredump/devcd*/data"):
                if source in dumped:
                    continue
                dumped.add(source)
                target = args.output / (Path(source).parent.name + ".dump")
                try:
                    with target.open("wb") as output:
                        result = subprocess.run(["cat", source], stdout=output, stderr=subprocess.PIPE, timeout=8)
                    (args.output / "devcoredump-status.txt").write_text(
                        f"{source}: exit {result.returncode}\n{result.stderr.decode(errors='replace')}")
                    if result.returncode:
                        target.unlink(missing_ok=True)
                except (OSError, subprocess.TimeoutExpired) as e:
                    (args.output / "devcoredump-status.txt").write_text(str(e) + "\n")
            finished.wait(.5)

    collector = threading.Thread(target=collect, daemon=True)
    collector.start()
    reason = "completed"
    result = 1
    try:
        with (args.output / "probe.log").open("wb", buffering=0) as probe_log:
            test = subprocess.Popen(command, stdout=probe_log, stderr=subprocess.STDOUT, start_new_session=True)
            deadline = time.monotonic() + args.timeout
            while test.poll() is None:
                if alarm.is_set() or cancelled.is_set() or time.monotonic() >= deadline:
                    reason = ("GPU failure" if alarm.is_set() else
                              "interrupted" if cancelled.is_set() else "timeout")
                    try:
                        os.killpg(test.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                    try:
                        test.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        try:
                            os.killpg(test.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                        test.wait(timeout=3)
                    break
                time.sleep(.1)
            result = test.wait()
            os.fsync(probe_log.fileno())
    except OSError as e:
        reason = f"launch/logging error: {e}"
    except subprocess.TimeoutExpired:
        reason = "process did not stop after SIGKILL"
        result = 124
    finally:
        finished.set()
        collector.join(timeout=2)
        journal.terminate()
        try:
            journal.wait(timeout=2)
        except subprocess.TimeoutExpired:
            journal.kill()
            journal.wait()
        reader.join(timeout=2)
        journal_log.close()
        if not collector.is_alive():
            metrics.close()
    meta.update(exit_code=result, reason=reason, ended=datetime.datetime.now().astimezone().isoformat())
    (args.output / "metadata.json").write_text(json.dumps(meta, indent=2) + "\n")
    print(f"Graphics test: {reason}, exit={result}, logs={args.output}", flush=True)
    return result if result >= 0 else 128 - result


if __name__ == "__main__":
    raise SystemExit(main())
