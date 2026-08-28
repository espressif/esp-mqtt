# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0

import logging
import os
import shlex
import shutil
import socket
import subprocess
import time
import typing as t
import warnings
from pathlib import Path

with warnings.catch_warnings():
    warnings.simplefilter("ignore", DeprecationWarning)
    from telnetlib import Telnet

import pytest
from pytest_embedded_idf import IdfDut


_MQTT_CONFORMANCE_DIR = Path(__file__).parent / "test" / "apps" / "mqtt_conformance"
_coverage_prepared_builds: set[Path] = set()

collect_ignore = ["test/apps/mqtt_conformance"] if os.getenv("MQTT_CONFORMANCE_ENABLED") == "0" else []


def _is_mqtt_conformance_test(request: pytest.FixtureRequest) -> bool:
    try:
        request.node.path.relative_to(_MQTT_CONFORMANCE_DIR)
    except ValueError:
        return False
    return True


def _gcov_enabled(dut: IdfDut) -> bool:
    return dut.app.sdkconfig.get("ESP_GCOV_ENABLE") is True


def _flatten_gcov_data(binary_path: Path) -> None:
    for gcda in binary_path.rglob("*.gcda"):
        if gcda.parent == binary_path:
            continue
        flattened = binary_path / "#".join(gcda.relative_to(binary_path).parts)
        shutil.copy2(gcda, flattened)


def _restore_gcov_data(binary_path: Path) -> None:
    for gcda in binary_path.glob("*.gcda"):
        destination = binary_path.joinpath(*gcda.name.split("#"))
        destination.parent.mkdir(parents=True, exist_ok=True)
        gcda.replace(destination)


class OpenOCD:
    def __init__(self, dut: IdfDut) -> None:
        self.binary_path = dut.app.binary_path
        self.log_file = os.path.join(dut.logdir, "ocd.txt")
        self.log_stream: t.Optional[t.TextIO] = None
        self.proc: t.Optional[subprocess.Popen] = None
        self.telnet: t.Optional[Telnet] = None

    def run(self) -> None:
        scripts = os.getenv("OPENOCD_SCRIPTS")
        if not scripts:
            raise RuntimeError("OPENOCD_SCRIPTS is not set; export the ESP-IDF environment")
        args = shlex.split(
            os.getenv(
                "MQTT_CONFORMANCE_OPENOCD_ARGS",
                "-f board/esp32-ethernet-kit-3.3v.cfg",
            )
        )
        self.log_stream = open(self.log_file, "w", encoding="utf-8")
        self.proc = subprocess.Popen(
            ["openocd", "-s", scripts, *args],
            stdout=self.log_stream,
            stderr=subprocess.STDOUT,
            cwd=self.binary_path,
        )
        for _ in range(20):
            if self.proc.poll() is not None:
                raise RuntimeError(f"OpenOCD exited; see {self.log_file}")
            try:
                self.telnet = Telnet("127.0.0.1", 4444, 1)
                greeting = self.telnet.read_until(b">", timeout=5)
                if greeting.endswith(b">"):
                    return
            except (ConnectionRefusedError, TimeoutError):
                pass
            time.sleep(0.25)
        raise RuntimeError(f"OpenOCD did not start; see {self.log_file}")

    def write(self, command: str, timeout: int = 120) -> str:
        if self.telnet is None:
            raise RuntimeError("OpenOCD console is not connected")
        self.telnet.write((command + "\n").encode())
        response = self.telnet.read_until(b">", timeout=timeout)
        if not response.endswith(b">"):
            raise TimeoutError(f"OpenOCD command timed out: {command}")
        return response.decode(errors="replace")

    def gcov_dump(self) -> str:
        output = self.write("esp gcov dump")
        if "Targets connected." not in output or "Targets disconnected." not in output:
            raise RuntimeError(f"Incomplete gcov dump: {output}")
        return output

    def kill(self) -> None:
        if self.telnet is not None:
            self.telnet.close()
            self.telnet = None
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=5)
        if self.log_stream is not None:
            self.log_stream.close()


@pytest.fixture(autouse=True)
def dump_mqtt_conformance_gcov(request: pytest.FixtureRequest):
    if not _is_mqtt_conformance_test(request):
        yield
        return

    dut = request.getfixturevalue("dut")
    if not _gcov_enabled(dut):
        yield
        return

    binary_path = Path(dut.app.binary_path)
    if binary_path not in _coverage_prepared_builds:
        logging.info("Removing stale gcov data from %s", binary_path)
        for gcda in binary_path.rglob("*.gcda"):
            gcda.unlink()
        _coverage_prepared_builds.add(binary_path)

    yield

    relocatable_gcov = os.getenv("CI") is not None
    if relocatable_gcov:
        _flatten_gcov_data(binary_path)
    ocd = OpenOCD(dut)
    try:
        ocd.run()
        dut.write("gcov")
        dut.expect("GCOV dump waiting for host", timeout=10)
        logging.info("gcov dump:\n%s", ocd.gcov_dump())
        if relocatable_gcov:
            _restore_gcov_data(binary_path)
        dut.expect("GCOV dump complete", timeout=10)
    finally:
        ocd.kill()


def get_host_ip4_by_dest_ip(dest_ip: str = "") -> str:
    """
    Get the local IP address that would be used to reach a destination IP.

    Args:
        dest_ip: Destination IP address. Defaults to 8.8.8.8 if not provided.

    Returns:
        The local IP address as a string.
    """
    if not dest_ip:
        dest_ip = "8.8.8.8"
    s1 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s1.connect((dest_ip, 80))
    host_ip = s1.getsockname()[0]
    s1.close()
    assert isinstance(host_ip, str)
    print(f"Using host ip: {host_ip}")
    return host_ip


@pytest.fixture
def log_performance(
    record_property: t.Callable[[str, object], None],
) -> t.Callable[[str, str], None]:
    """
    log performance item with pre-defined format to the console
    and record it under the ``properties`` tag in the junit report if available.
    """

    def real_func(item: str, value: str) -> None:
        """
            :param item: performance item name
        :param value: performance value
        """
        logging.info("[Performance][%s]: %s", item, value)
        record_property(item, value)

    return real_func
