# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Unlicense OR CC0-1.0
from __future__ import annotations

import contextlib
import os
import random
import re
import socket
import string
import sys
import threading
import time
from pathlib import Path
from typing import Generator, Protocol

import pexpect
import pytest
from pytest_embedded import Dut
from pytest_embedded_idf.utils import idf_parametrize

TOPIC_SIZE = 16
DUT_READY_TIMEOUT = 30
DUT_CONNECT_TIMEOUT = 30
DUT_SUBSCRIBE_TIMEOUT = 60
DUT_TEST_TIMEOUT = 60
PAHO_BROKER_PORT = int(os.getenv("MQTT_CONFORMANCE_PAHO_BROKER_PORT", "18883"))
CONNECT_RETRIES = int(os.getenv("MQTT_CONFORMANCE_CONNECT_RETRIES", "3"))
RETRY_BACKOFF_SEC = float(os.getenv("MQTT_CONFORMANCE_RETRY_BACKOFF_SEC", "2"))

PAHO_SPEC_FILE = (
    Path(__file__).resolve().parents[3]
    / "test"
    / "tools"
    / "paho.mqtt.testing"
    / "interoperability"
    / "specifications"
    / "MQTTV311.py"
)
PAHO_INTEROP_DIR = PAHO_SPEC_FILE.parent.parent

# Add paho interoperability directory so we can import the broker at fixture time.
if str(PAHO_INTEROP_DIR) not in sys.path:
    sys.path.insert(0, str(PAHO_INTEROP_DIR))


def build_topic() -> str:
    suffix = "".join(random.choice(string.ascii_letters) for _ in range(TOPIC_SIZE))
    return f"test/conformance/{suffix}"


def require_paho_testing_checked_out() -> None:
    """Hard requirement: fail the test if the paho.mqtt.testing submodule is not available."""
    if not PAHO_SPEC_FILE.exists():
        pytest.fail(
            "paho.mqtt.testing submodule is not available (required for mqtt conformance tests). "
            "Run: git submodule update --init --recursive test/tools/paho.mqtt.testing"
        )


def get_host_ip4_by_dest_ip(dest_ip: str = "8.8.8.8") -> str:
    """Return the primary host IPv4 used to reach dest_ip (e.g. for DUT to reach host broker)."""
    with contextlib.closing(socket.socket(socket.AF_INET, socket.SOCK_DGRAM)) as sock:
        sock.connect((dest_ip, 80))
        return sock.getsockname()[0]


class _BrokerHandle(Protocol):
    uri: str

    def shutdown(self) -> None: ...


def _start_paho_broker(port: int, host_ip: str) -> _BrokerHandle:
    """Start paho V311+V5 broker in-process; return object with .uri and .shutdown().
    Imports deferred so idf-ci collection-time mocking does not replace paho.
    """
    from mqtt.brokers.V311 import MQTTBrokers as MQTTV3Brokers
    from mqtt.brokers.V5 import MQTTBrokers as MQTTV5Brokers
    from mqtt.brokers.listeners import TCPListeners

    lock = threading.RLock()
    shared_data: dict = {}
    options = {
        "visual": False,
        "persistence": False,
        "overlapping_single": True,
        "dropQoS0": True,
        "zero_length_clientids": True,
        "publish_on_pubrel": False,
        "topicAliasMaximum": 2,
        "maximumPacketSize": 16384,
        "receiveMaximum": 2,
        "serverKeepAlive": 60,
        "maximum_qos": 2,
        "retain_available": True,
        "subscription_identifier_available": True,
        "shared_subscription_available": True,
        "server_keep_alive": None,
    }
    broker3 = MQTTV3Brokers(options=options.copy(), lock=lock, sharedData=shared_data)
    broker5 = MQTTV5Brokers(options=options.copy(), lock=lock, sharedData=shared_data)
    broker3.setBroker5(broker5)
    broker5.setBroker3(broker3)
    TCPListeners.setBrokers(broker3, broker5)
    server = TCPListeners.create(port=port, host="", serve_forever=False)

    class _Broker:
        def __init__(self) -> None:
            self.uri = f"mqtt://{host_ip}:{port}"
            self._broker3 = broker3
            self._broker5 = broker5
            self._server = server

        def shutdown(self) -> None:
            self._broker3.shutdown()
            self._broker5.shutdown()
            if self._server:
                self._server.shutdown()

    return _Broker()


@pytest.fixture(scope="module")
def broker() -> Generator[_BrokerHandle, None, None]:
    """Start paho MQTT broker in-process for the smoke test. No subclass, just V311+V5 + TCP listener."""
    require_paho_testing_checked_out()
    host_ip = os.getenv("MQTT_CONFORMANCE_HOST_IP", "").strip() or get_host_ip4_by_dest_ip()
    b = _start_paho_broker(port=PAHO_BROKER_PORT, host_ip=host_ip)
    yield b
    b.shutdown()


@pytest.fixture(scope="module")
def broker_uri(broker: _BrokerHandle) -> str:
    return broker.uri


@pytest.fixture
def mqtt_client(dut: Dut, broker_uri: str):
    require_paho_testing_checked_out()
    dut.expect(re.compile(rb"mqtt>"), timeout=DUT_READY_TIMEOUT)
    dut.write("init")
    dut.write(f"set_uri {broker_uri}")
    yield dut
    dut.write("destroy")


def start_client(dut: Dut) -> None:
    for attempt in range(1, CONNECT_RETRIES + 1):
        dut.write("start")
        try:
            dut.expect(re.compile(rb"MQTT_EVENT_CONNECTED"), timeout=DUT_CONNECT_TIMEOUT)
            return
        except pexpect.TIMEOUT:
            dut.write("stop")
            if attempt == CONNECT_RETRIES:
                raise
            time.sleep(RETRY_BACKOFF_SEC)


def stop_client(dut: Dut) -> None:
    dut.write("stop")


@pytest.mark.eth_ip101
@idf_parametrize("target", ["esp32"], indirect=["target"])
def test_mqtt_v311_subscribe_and_qos1_publish__sec_3_8_4_and_4_3(mqtt_client: Dut) -> None:
    """
    MQTT v3.1.1 conformance smoke case:
    - section 3.8.4: SUBSCRIBE/SUBACK interaction
    - section 4.3: QoS 1 publish flow (at least once semantics)

    Reference suite integrated from:
    test/tools/paho.mqtt.testing/interoperability/specifications/MQTTV311.py
    """
    topic = build_topic()

    start_client(mqtt_client)
    mqtt_client.write(f"subscribe {topic} 1")
    mqtt_client.expect(re.compile(rb"MQTT_EVENT_SUBSCRIBED"), timeout=DUT_SUBSCRIBE_TIMEOUT)

    mqtt_client.write(f"publish {topic} qos1 4 1 0 1")
    # DUT may emit DATA_COMPLETE (incoming) before PUBLISHED (outgoing ack); accept either order.
    mqtt_client.expect(
        [re.compile(rb"MQTT_EVENT_PUBLISHED"), re.compile(rb"MQTT_EVENT_DATA_COMPLETE")],
        timeout=DUT_TEST_TIMEOUT,
    )
    mqtt_client.expect(
        [re.compile(rb"MQTT_EVENT_PUBLISHED"), re.compile(rb"MQTT_EVENT_DATA_COMPLETE")],
        timeout=DUT_TEST_TIMEOUT,
    )

    stop_client(mqtt_client)
