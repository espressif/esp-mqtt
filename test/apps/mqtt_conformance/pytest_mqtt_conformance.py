# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Unlicense OR CC0-1.0
from __future__ import annotations

import base64
import contextlib
import copy
import enum
import importlib
import json
import logging
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
DUT_READY_TIMEOUT = 20
DUT_CONNECT_TIMEOUT = 20
DUT_SUBSCRIBE_TIMEOUT = 15
DUT_CMD_TIMEOUT = 10
DUT_EVENT_TIMEOUT = 20
PAHO_BROKER_LOG_LEVEL = os.getenv("MQTT_CONFORMANCE_PAHO_BROKER_LOG_LEVEL", "WARNING").upper()
CONNECT_RETRIES = int(os.getenv("MQTT_CONFORMANCE_CONNECT_RETRIES", "3"))
RETRY_BACKOFF_SEC = float(os.getenv("MQTT_CONFORMANCE_RETRY_BACKOFF_SEC", "2"))
DEFAULT_BROKER_RECEIVE_MAXIMUM = 2
TEST_TIMEOUT_MARGIN_SEC = 2

QUOTA_REJECTION_FORBIDDEN = (
    b"MQTT5 publish check fail",
    b"Publish failed, msg_id=-1",
    b"MQTT_EVENT_ERROR",
    b"MQTT_EVENT_DISCONNECTED",
)

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


# Raw esp_mqtt_protocol_ver_t ordinals (see mqtt_client.h): the wire format
# mirrors the real enum value.
MQTT_PROTOCOL_V_3_1_1 = 2
MQTT_PROTOCOL_V_5 = 3


def esp_mqtt_config(
    *,
    uri: str,
    client_id: str | None = None,
    protocol_ver: int | None = None,
    disable_auto_reconnect: bool = True,
) -> str:
    """Encode an ``mqtt_config`` blob (base64 JSON) for the DUT `init <b64>` command.

    The JSON shape mirrors esp_mqtt_client_config_t's real field nesting
    (broker.address.uri, credentials.client_id, session.*, network.*).
    A random ``client_id`` is injected unless the caller supplies one.
    """
    client_id = client_id or "esp-" + "".join(random.choices(string.digits + "abcdef", k=8))
    session: dict[str, object] = {}

    if protocol_ver is not None:
        session["protocol_ver"] = protocol_ver

    mqtt_config: dict[str, object] = {
        "broker": {"address": {"uri": uri}},
        "credentials": {"client_id": client_id},
        "network": {"disable_auto_reconnect": disable_auto_reconnect},
    }

    if session:
        mqtt_config["session"] = session

    return base64.b64encode(json.dumps({"mqtt_config": mqtt_config}).encode()).decode()


def configure_paho_broker_logging() -> None:
    logger = logging.getLogger("MQTT broker")
    level = getattr(logging, PAHO_BROKER_LOG_LEVEL, logging.WARNING)
    logger.setLevel(level)


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


class MqttPacketType(enum.IntEnum):
    CONNECT = 1
    CONNACK = 2
    PUBLISH = 3
    PUBACK = 4
    PUBREC = 5
    PUBREL = 6
    PUBCOMP = 7
    SUBSCRIBE = 8
    SUBACK = 9
    UNSUBSCRIBE = 10
    UNSUBACK = 11
    PINGREQ = 12
    PINGRESP = 13
    DISCONNECT = 14
    AUTH = 15


class BrokerInterface(Protocol):
    uri: str

    def wait_for_held_packets(self, packet_type: MqttPacketType, count: int, timeout: float) -> None: ...

    def held_packet_count(self, packet_type: MqttPacketType) -> int: ...

    def release_held_packets(
        self,
        packet_type: MqttPacketType,
        count: int | None = None,
        *,
        keep_holding: bool = False,
    ) -> None: ...

    def release_all_held_packets(self) -> None: ...

    def held_packet_identifiers(self, packet_type: MqttPacketType) -> list[int]: ...

    def inject_ack(self, packet_type: MqttPacketType, packet_identifier: int) -> None: ...

    def discard_held_packets(self) -> None: ...

    def set_receive_maximum(self, receive_maximum: int) -> None: ...

    def disconnect_clients(self) -> None: ...

    def shutdown(self) -> None: ...


def _start_paho_broker(
    host_ip: str,
    port: int = 0,
    receive_maximum: int = DEFAULT_BROKER_RECEIVE_MAXIMUM,
    hold_packet_types: tuple[MqttPacketType, ...] = (),
) -> BrokerInterface:
    """Start paho V311+V5 broker in-process and return its control handle.

    ``port=0`` (the default) binds an OS-assigned ephemeral port.

    Imports deferred so idf-ci collection-time mocking does not replace paho.
    """
    configure_paho_broker_logging()

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
        "receiveMaximum": receive_maximum,
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
    bound_port = server.socket.getsockname()[1]

    original_respond = None
    v5_brokers_mod = None
    held_packets: dict[MqttPacketType, list[tuple[object, object, int]]] = {}
    held_packets_condition = threading.Condition()
    active_hold_packet_types = set(hold_packet_types)
    if hold_packet_types:
        v5_brokers_mod = importlib.import_module("mqtt.brokers.V5.MQTTBrokers")
        original_respond = v5_brokers_mod.respond

        def controlled_respond(sock, packet, maximumPacketSize=500):
            packet_type = MqttPacketType(packet.fh.PacketType)
            with held_packets_condition:
                if packet_type in active_hold_packet_types:
                    # paho mutates some packet objects after respond() returns
                    # (notably setting DUP on PUBLISH), so retain the wire-state
                    # snapshot that was presented to this interception point.
                    held_packet = (sock, copy.deepcopy(packet), maximumPacketSize)
                    held_packets.setdefault(packet_type, []).append(held_packet)
                    held_packets_condition.notify_all()
                    return
            original_respond(sock, packet, maximumPacketSize)

        v5_brokers_mod.respond = controlled_respond  # type: ignore[attr-defined]

    class _Broker:
        def __init__(self) -> None:
            self.uri = f"mqtt://{host_ip}:{bound_port}"
            self._broker3 = broker3
            self._broker5 = broker5
            self._server = server
            self._v5_brokers_mod = v5_brokers_mod
            self._original_respond = original_respond

        def wait_for_held_packets(self, packet_type: MqttPacketType, count: int, timeout: float) -> None:
            deadline = time.monotonic() + timeout
            with held_packets_condition:
                while len(held_packets.get(packet_type, ())) < count:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        held_count = len(held_packets.get(packet_type, ()))
                        raise TimeoutError(
                            f"Timed out waiting for {count} held {packet_type.name} packets; got {held_count}"
                        )
                    held_packets_condition.wait(remaining)

        def held_packet_count(self, packet_type: MqttPacketType) -> int:
            with held_packets_condition:
                return len(held_packets.get(packet_type, ()))

        def held_packet_identifiers(self, packet_type: MqttPacketType) -> list[int]:
            with held_packets_condition:
                return [int(getattr(packet, "packetIdentifier")) for _, packet, _ in held_packets.get(packet_type, ())]

        def inject_ack(self, packet_type: MqttPacketType, packet_identifier: int) -> None:
            if packet_type not in (MqttPacketType.PUBACK, MqttPacketType.PUBCOMP):
                raise ValueError(f"Cannot inject {packet_type.name}; expected PUBACK or PUBCOMP")
            respond = self._original_respond
            if respond is None:
                raise RuntimeError("Cannot inject acknowledgements unless packet holding is enabled")

            from mqtt.formats import MQTTV5

            packet_class = MQTTV5.Pubacks if packet_type == MqttPacketType.PUBACK else MQTTV5.Pubcomps
            packet = packet_class()
            packet.packetIdentifier = packet_identifier
            with lock:
                sockets = list(self._broker5.clients)
                if len(sockets) != 1:
                    raise RuntimeError(f"Expected one connected MQTT5 client, got {len(sockets)}")
                respond(sockets[0], packet)

        def discard_held_packets(self) -> None:
            with held_packets_condition:
                held_packets.clear()

        def set_receive_maximum(self, receive_maximum: int) -> None:
            if not 1 <= receive_maximum <= 0xFFFF:
                raise ValueError("Receive Maximum must be in the range 1..65535")
            with lock:
                self._broker5.options["receiveMaximum"] = receive_maximum

        def disconnect_clients(self) -> None:
            with lock:
                self._broker5.disconnectAll()

        def _send_held_packets(self, pending_packets: list[tuple[object, object, int]]) -> None:
            respond = self._original_respond
            if pending_packets and respond is None:
                raise RuntimeError("Cannot release held packets without the broker response callback")
            if respond is None:
                return
            for sock, packet, maximum_packet_size in pending_packets:
                respond(sock, packet, maximum_packet_size)

        def release_held_packets(
            self,
            packet_type: MqttPacketType,
            count: int | None = None,
            *,
            keep_holding: bool = False,
        ) -> None:
            with held_packets_condition:
                if not keep_holding:
                    active_hold_packet_types.discard(packet_type)
                packet_queue = held_packets.get(packet_type, [])
                release_count = len(packet_queue) if count is None else count
                if release_count < 0 or release_count > len(packet_queue):
                    raise ValueError(
                        f"Cannot release {release_count} held {packet_type.name} packets; {len(packet_queue)} available"
                    )
                pending_packets = packet_queue[:release_count]
                del packet_queue[:release_count]
                if not packet_queue:
                    held_packets.pop(packet_type, None)
            self._send_held_packets(pending_packets)

        def release_all_held_packets(self) -> None:
            with held_packets_condition:
                active_hold_packet_types.clear()
                pending_packets = [packet for packets in held_packets.values() for packet in packets]
                held_packets.clear()
            self._send_held_packets(pending_packets)

        def shutdown(self) -> None:
            self.release_all_held_packets()
            if self._original_respond is not None and self._v5_brokers_mod is not None:
                self._v5_brokers_mod.respond = self._original_respond  # type: ignore[attr-defined]
            self._broker3.shutdown()
            self._broker5.shutdown()
            if self._server:
                self._server.shutdown()

    return _Broker()


@contextlib.contextmanager
def broker_started(
    port: int = 0,
    *,
    receive_maximum: int = DEFAULT_BROKER_RECEIVE_MAXIMUM,
    hold_packet_types: tuple[MqttPacketType, ...] = (),
) -> Generator[BrokerInterface, None, None]:
    """Start an in-process paho broker and guarantee shutdown on exit."""
    require_paho_testing_checked_out()
    host_ip = os.getenv("MQTT_CONFORMANCE_HOST_IP", "").strip() or get_host_ip4_by_dest_ip()
    paho_broker = _start_paho_broker(
        host_ip=host_ip,
        port=port,
        receive_maximum=receive_maximum,
        hold_packet_types=hold_packet_types,
    )
    try:
        yield paho_broker
    finally:
        paho_broker.shutdown()


@contextlib.contextmanager
def initialized_mqtt_client(dut: Dut, uri: str, *, protocol_ver: int = MQTT_PROTOCOL_V_5) -> Generator[Dut, None, None]:
    """Init the MQTT client against ``uri`` and guarantee ``destroy`` on exit."""
    require_paho_testing_checked_out()
    dut.expect(re.compile(rb"mqtt>"), timeout=DUT_READY_TIMEOUT)
    dut.write(f"init {esp_mqtt_config(protocol_ver=protocol_ver, uri=uri)}")
    try:
        yield dut
    finally:
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


@contextlib.contextmanager
def started_client(dut: Dut) -> Generator[Dut, None, None]:
    """Start the MQTT client and guarantee ``stop`` is issued on exit, even on failure."""
    try:
        start_client(dut)
        yield dut
    finally:
        stop_client(dut)


def case_timeout(
    *,
    connect_operations: int = 0,
    subscribe_operations: int = 0,
    publish_operations: int = 0,
    event_wait_operations: int = 0,
    timeout_margin: int = TEST_TIMEOUT_MARGIN_SEC,
) -> int:
    """Compound a timeout from the operations performed by a test or test phase."""
    timeout = (
        connect_operations * DUT_CONNECT_TIMEOUT
        + subscribe_operations * DUT_SUBSCRIBE_TIMEOUT
        + event_wait_operations * DUT_EVENT_TIMEOUT
    )
    if publish_operations:
        timeout += max(6, int(publish_operations * 1.5 + timeout_margin))
    elif timeout:
        timeout += timeout_margin
    return timeout


def subscribed_to(dut: Dut, topic: str, qos: int, timeout: int = DUT_SUBSCRIBE_TIMEOUT) -> None:
    """Issue ``subscribe`` and wait for the resulting MQTT_EVENT_SUBSCRIBED."""
    dut.write(f"subscribe {topic} {qos}")
    dut.expect(re.compile(rb"MQTT_EVENT_SUBSCRIBED"), timeout=timeout)


def publish_from_dut(
    dut: Dut,
    topic: str,
    qos: int,
    *,
    payload_prefix: str,
    message_count: int,
    enqueue: int = 0,
    retain: int = 0,
    pattern_repetitions: int = 1,
) -> None:
    """Write ``message_count`` individual ``publish`` commands (default: publish path)."""
    for i in range(message_count):
        publish_payload = f"{payload_prefix}{i}" if message_count > 1 else payload_prefix
        dut.write(f"publish {topic} {publish_payload} {pattern_repetitions} {qos} {retain} {enqueue}")


def data_payload_patterns(prefix: str, n_messages: int) -> dict[bytes, int]:
    """Return one expected DATA payload marker for each uniquely suffixed message."""
    return {f"MQTT_EVENT_DATA_PAYLOAD {prefix}{i}".encode(): 1 for i in range(n_messages)}


def _check_forbidden_log_line(line: bytes, forbidden: tuple[bytes, ...]) -> None:
    for bad in forbidden:
        if bad in line:
            pytest.fail(f"Forbidden log line containing {bad!r}: {line!r}")


def expect_n(
    dut: Dut,
    patterns: "dict[bytes, int]",
    timeout: int | None = None,
    forbidden: tuple[bytes, ...] = QUOTA_REJECTION_FORBIDDEN,
) -> "dict[bytes, int]":
    """Wait until each pattern appears at least the required number of times.

    Searches a combined regex against DUT output so all patterns are matched
    in a single sequential pass — patterns that appear early in the stream
    (e.g. log lines emitted during write() calls) are not missed.
    """
    keys = list(patterns.keys())
    combined = re.compile(b"|".join(re.escape(k) for k in keys + list(forbidden)))
    seen: dict[bytes, int] = {k: 0 for k in keys}
    if timeout is None:
        timeout = max(patterns.values()) * 3 + TEST_TIMEOUT_MARGIN_SEC
    deadline = time.monotonic() + timeout
    while any(seen[k] < patterns[k] for k in keys):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise pexpect.TIMEOUT(f"Timed out. seen={seen}, expected={patterns}")
        m = dut.expect(combined, timeout=remaining)
        line: bytes = m.group(0)
        _check_forbidden_log_line(line, forbidden)
        for key in keys:
            if key in line:
                seen[key] += 1
                break
    return seen


@pytest.mark.eth_ip101
@pytest.mark.timeout(
    case_timeout(
        connect_operations=1,
        subscribe_operations=1,
        publish_operations=4,
    )
)
@idf_parametrize("target", ["esp32"], indirect=["target"])
@pytest.mark.parametrize("enqueue", [0, 1], ids=["publish", "enqueue"])
@pytest.mark.parametrize(
    "qos,completion_packet",
    [(1, MqttPacketType.PUBACK), (2, MqttPacketType.PUBCOMP)],
    ids=["qos1", "qos2"],
)
def test_mqtt5_receive_maximum_defers_publish(
    dut: Dut,
    enqueue: int,
    qos: int,
    completion_packet: MqttPacketType,
) -> None:
    """Receive Maximum defers, rather than drops, QoS 1/2 messages from both APIs."""
    topic = build_topic()
    publish_quota = DEFAULT_BROKER_RECEIVE_MAXIMUM
    initial_prefix = "initial"
    deferred_payload = "deferred"
    sentinel_payload = "qos0sentinel"

    with (
        broker_started(hold_packet_types=(completion_packet,)) as broker,
        initialized_mqtt_client(dut, broker.uri) as client,
        started_client(client),
    ):
        subscribed_to(client, topic, qos)
        try:
            publish_from_dut(
                client,
                topic,
                qos,
                payload_prefix=initial_prefix,
                message_count=publish_quota,
                enqueue=enqueue,
            )
            expect_n(
                client,
                {b"Publish requested, msg_id=": publish_quota} | data_payload_patterns(initial_prefix, publish_quota),
                timeout=DUT_EVENT_TIMEOUT,
            )
            broker.wait_for_held_packets(completion_packet, publish_quota, timeout=DUT_EVENT_TIMEOUT)

            publish_from_dut(client, topic, qos, payload_prefix=deferred_payload, message_count=1, enqueue=enqueue)
            deferred_patterns = {b"Publish requested, msg_id=": 1}
            if enqueue == 0:
                deferred_patterns[b"Unable to publish now: maximum inflight messages reached"] = 1
            expect_n(client, deferred_patterns, timeout=DUT_CMD_TIMEOUT)

            # A direct QoS 0 round trip provides a broker/DUT synchronization
            # point while the publish completion packets remain held.
            publish_from_dut(client, topic, 0, payload_prefix=sentinel_payload, message_count=1)
            expect_n(
                client,
                {
                    b"Publish requested, msg_id=": 1,
                    f"MQTT_EVENT_DATA_PAYLOAD {sentinel_payload}".encode(): 1,
                },
                timeout=DUT_EVENT_TIMEOUT,
            )
            # Retransmissions may produce duplicate completion packets while
            # the original quota remains held.
            assert broker.held_packet_count(completion_packet) >= publish_quota

            broker.release_held_packets(completion_packet, count=1, keep_holding=True)
            broker.wait_for_held_packets(completion_packet, publish_quota, timeout=DUT_EVENT_TIMEOUT)
            expect_n(
                client,
                {
                    b"MQTT_EVENT_PUBLISHED": 1,
                    f"MQTT_EVENT_DATA_PAYLOAD {deferred_payload}".encode(): 1,
                },
                timeout=DUT_EVENT_TIMEOUT,
            )
        finally:
            broker.release_held_packets(completion_packet)

        expect_n(client, {b"MQTT_EVENT_PUBLISHED": publish_quota}, timeout=DUT_EVENT_TIMEOUT)


@pytest.mark.eth_ip101
@pytest.mark.timeout(
    case_timeout(
        connect_operations=1,
        subscribe_operations=1,
        publish_operations=4,
    )
)
@idf_parametrize("target", ["esp32"], indirect=["target"])
def test_mqtt5_server_receive_maximum_mixed_qos(dut: Dut) -> None:
    """QoS 1 and QoS 2 PUBLISH packets consume one shared inflight quota."""
    topic = build_topic()
    deferred_payload = "mixed_deferred"
    sentinel_payload = "mixed_sentinel"

    with (
        broker_started(hold_packet_types=(MqttPacketType.PUBACK, MqttPacketType.PUBCOMP)) as broker,
        initialized_mqtt_client(dut, broker.uri) as client,
        started_client(client),
    ):
        subscribed_to(client, topic, 2)
        try:
            publish_from_dut(client, topic, 1, payload_prefix="mixed_q1", message_count=1)
            publish_from_dut(client, topic, 2, payload_prefix="mixed_q2", message_count=1)
            expect_n(
                client,
                {
                    b"Publish requested, msg_id=": 2,
                    b"MQTT_EVENT_DATA_PAYLOAD mixed_q1": 1,
                    b"MQTT_EVENT_DATA_PAYLOAD mixed_q2": 1,
                },
                timeout=DUT_EVENT_TIMEOUT,
            )
            broker.wait_for_held_packets(MqttPacketType.PUBACK, 1, timeout=DUT_EVENT_TIMEOUT)
            broker.wait_for_held_packets(MqttPacketType.PUBCOMP, 1, timeout=DUT_EVENT_TIMEOUT)

            publish_from_dut(client, topic, 1, payload_prefix=deferred_payload, message_count=1)
            expect_n(
                client,
                {
                    b"Publish requested, msg_id=": 1,
                    b"Unable to publish now: maximum inflight messages reached": 1,
                },
                timeout=DUT_CMD_TIMEOUT,
            )
            publish_from_dut(client, topic, 0, payload_prefix=sentinel_payload, message_count=1)
            expect_n(
                client,
                {
                    b"Publish requested, msg_id=": 1,
                    f"MQTT_EVENT_DATA_PAYLOAD {sentinel_payload}".encode(): 1,
                },
                timeout=DUT_EVENT_TIMEOUT,
            )
            # Retransmissions may add duplicate completion packets while both
            # original inflight messages remain held.
            assert broker.held_packet_count(MqttPacketType.PUBACK) >= 1
            assert broker.held_packet_count(MqttPacketType.PUBCOMP) >= 1

            broker.release_held_packets(MqttPacketType.PUBCOMP, count=1, keep_holding=True)
            broker.wait_for_held_packets(MqttPacketType.PUBACK, 2, timeout=DUT_EVENT_TIMEOUT)
            expect_n(
                client,
                {
                    b"MQTT_EVENT_PUBLISHED": 1,
                    f"MQTT_EVENT_DATA_PAYLOAD {deferred_payload}".encode(): 1,
                },
                timeout=DUT_EVENT_TIMEOUT,
            )
        finally:
            broker.release_held_packets(MqttPacketType.PUBACK)
            broker.release_held_packets(MqttPacketType.PUBCOMP)

        expect_n(client, {b"MQTT_EVENT_PUBLISHED": 2}, timeout=DUT_EVENT_TIMEOUT)


@pytest.mark.eth_ip101
@pytest.mark.timeout(
    case_timeout(
        connect_operations=1,
        subscribe_operations=1,
        publish_operations=4,
        event_wait_operations=1,
    )
)
@idf_parametrize("target", ["esp32"], indirect=["target"])
@pytest.mark.parametrize(
    "qos,completion_packet",
    [(1, MqttPacketType.PUBACK), (2, MqttPacketType.PUBCOMP)],
    ids=["qos1", "qos2"],
)
@pytest.mark.parametrize("ack_kind", ["unsolicited", "duplicate"])
def test_mqtt5_unmatched_completion_does_not_release_receive_maximum(
    dut: Dut,
    qos: int,
    completion_packet: MqttPacketType,
    ack_kind: str,
) -> None:
    """Only an acknowledgement matching a live inflight PUBLISH releases quota."""
    topic = build_topic()
    blocked_payload = f"{ack_kind}_blocked"
    probe_payload = f"{ack_kind}_probe"
    sentinel_payload = f"{ack_kind}_sentinel"

    with (
        broker_started(receive_maximum=1, hold_packet_types=(completion_packet,)) as broker,
        initialized_mqtt_client(dut, broker.uri) as client,
        started_client(client),
    ):
        subscribed_to(client, topic, qos)
        try:
            publish_from_dut(client, topic, qos, payload_prefix="active", message_count=1)
            expect_n(
                client,
                {b"Publish requested, msg_id=": 1, b"MQTT_EVENT_DATA_PAYLOAD active": 1},
                timeout=DUT_EVENT_TIMEOUT,
            )
            broker.wait_for_held_packets(completion_packet, 1, timeout=DUT_EVENT_TIMEOUT)
            active_id = broker.held_packet_identifiers(completion_packet)[0]

            publish_from_dut(client, topic, qos, payload_prefix=blocked_payload, message_count=1, enqueue=1)
            expect_n(client, {b"Publish requested, msg_id=": 1}, timeout=DUT_CMD_TIMEOUT)

            if ack_kind == "unsolicited":
                unused_id = 0xFFFF if active_id != 0xFFFF else 0xFFFE
                broker.inject_ack(completion_packet, unused_id)
                forbidden_payload = blocked_payload
            else:
                broker.release_held_packets(completion_packet, count=1, keep_holding=True)
                broker.wait_for_held_packets(completion_packet, 1, timeout=DUT_EVENT_TIMEOUT)
                expect_n(
                    client,
                    {
                        b"MQTT_EVENT_PUBLISHED": 1,
                        f"MQTT_EVENT_DATA_PAYLOAD {blocked_payload}".encode(): 1,
                    },
                    timeout=DUT_EVENT_TIMEOUT,
                )
                broker.inject_ack(completion_packet, active_id)
                publish_from_dut(client, topic, qos, payload_prefix=probe_payload, message_count=1, enqueue=1)
                expect_n(client, {b"Publish requested, msg_id=": 1}, timeout=DUT_CMD_TIMEOUT)
                forbidden_payload = probe_payload

            publish_from_dut(client, topic, 0, payload_prefix=sentinel_payload, message_count=1)
            expect_n(
                client,
                {
                    b"Publish requested, msg_id=": 1,
                    f"MQTT_EVENT_DATA_PAYLOAD {sentinel_payload}".encode(): 1,
                },
                timeout=DUT_EVENT_TIMEOUT,
                forbidden=QUOTA_REJECTION_FORBIDDEN + (f"MQTT_EVENT_DATA_PAYLOAD {forbidden_payload}".encode(),),
            )
        finally:
            broker.release_all_held_packets()


@pytest.mark.eth_ip101
@pytest.mark.timeout(
    case_timeout(
        connect_operations=2,
        subscribe_operations=2,
        publish_operations=5,
        event_wait_operations=2,
    )
)
@idf_parametrize("target", ["esp32"], indirect=["target"])
def test_mqtt5_reconnect_applies_receive_maximum_to_retransmits(dut: Dut) -> None:
    """Retransmitted PUBLISH packets consume the newly negotiated connection quota."""
    topic = build_topic()
    probe_payload = "reconnect_probe"
    sentinel_payload = "reconnect_sentinel"

    with (
        broker_started(receive_maximum=2, hold_packet_types=(MqttPacketType.PUBACK,)) as broker,
        initialized_mqtt_client(dut, broker.uri) as client,
        started_client(client),
    ):
        subscribed_to(client, topic, 1)
        try:
            publish_from_dut(client, topic, 1, payload_prefix="reconnect_active", message_count=2)
            expect_n(
                client,
                {b"Publish requested, msg_id=": 2} | data_payload_patterns("reconnect_active", 2),
                timeout=DUT_EVENT_TIMEOUT,
            )
            broker.wait_for_held_packets(MqttPacketType.PUBACK, 2, timeout=DUT_EVENT_TIMEOUT)

            broker.discard_held_packets()
            broker.set_receive_maximum(1)
            broker.disconnect_clients()
            client.expect(re.compile(rb"MQTT_EVENT_DISCONNECTED"), timeout=DUT_EVENT_TIMEOUT)
            client.write("reconnect")
            client.expect(re.compile(rb"MQTT_EVENT_CONNECTED"), timeout=DUT_CONNECT_TIMEOUT)
            subscribed_to(client, topic, 1)

            broker.wait_for_held_packets(MqttPacketType.PUBACK, 1, timeout=DUT_EVENT_TIMEOUT)
            publish_from_dut(client, topic, 1, payload_prefix=probe_payload, message_count=1, enqueue=1)
            expect_n(client, {b"Publish requested, msg_id=": 1}, timeout=DUT_CMD_TIMEOUT)

            publish_from_dut(client, topic, 0, payload_prefix=sentinel_payload, message_count=1)
            expect_n(
                client,
                {
                    b"Publish requested, msg_id=": 1,
                    f"MQTT_EVENT_DATA_PAYLOAD {sentinel_payload}".encode(): 1,
                },
                timeout=DUT_EVENT_TIMEOUT,
                forbidden=QUOTA_REJECTION_FORBIDDEN + (f"MQTT_EVENT_DATA_PAYLOAD {probe_payload}".encode(),),
            )
            assert broker.held_packet_count(MqttPacketType.PUBACK) == 1
        finally:
            broker.release_all_held_packets()


@pytest.mark.eth_ip101
@pytest.mark.timeout(
    case_timeout(
        connect_operations=2,
        subscribe_operations=1,
        publish_operations=2,
        event_wait_operations=2,
    )
)
@idf_parametrize("target", ["esp32"], indirect=["target"])
def test_mqtt5_reconnect_resends_requeued_packets_once(dut: Dut) -> None:
    """A packet requeued on reconnect is sent once and does not block the outbox.

    This pins current behavior, which knowingly resends more than [MQTT-4.4.0-1]
    allows; see test_mqtt5_reconnect_resends_only_inflight_publishes__sec_4_4 for
    the conformant end state. What must hold either way is that requeuing does
    not break the send path:

    - the requeued SUBSCRIBE leaves QUEUED after being sent, instead of being
      redelivered on every task loop pass and starving the packets behind it,
    - its outbox tick follows the send, so the retransmit path does not
      immediately send it a second time,
    - the requeued QoS 1 PUBLISH behind it still reaches the broker.

    The broker answers every SUBSCRIBE it receives, so held SUBACKs count the
    resends. One retransmit timeout (1s by default) after the reconnect the
    client legitimately retransmits again, which is why the count is checked as
    soon as the PUBLISH arrives.
    """
    topic = build_topic()

    with (
        broker_started(hold_packet_types=(MqttPacketType.SUBACK, MqttPacketType.PUBACK)) as broker,
        initialized_mqtt_client(dut, broker.uri) as client,
        started_client(client),
    ):
        try:
            client.write(f"subscribe {topic} 1")
            expect_n(client, {b"Subscribe requested, msg_id=": 1}, timeout=DUT_CMD_TIMEOUT)
            broker.wait_for_held_packets(MqttPacketType.SUBACK, 1, timeout=DUT_EVENT_TIMEOUT)

            publish_from_dut(client, topic, 1, payload_prefix="requeued", message_count=1)
            expect_n(client, {b"Publish requested, msg_id=": 1}, timeout=DUT_CMD_TIMEOUT)
            broker.wait_for_held_packets(MqttPacketType.PUBACK, 1, timeout=DUT_EVENT_TIMEOUT)

            # packets held for the old socket are useless once it is gone
            broker.discard_held_packets()
            broker.disconnect_clients()
            client.expect(re.compile(rb"MQTT_EVENT_DISCONNECTED"), timeout=DUT_EVENT_TIMEOUT)
            client.write("reconnect")
            client.expect(re.compile(rb"MQTT_EVENT_CONNECTED"), timeout=DUT_CONNECT_TIMEOUT)

            # The SUBSCRIBE was enqueued first, so it is requeued and sent first.
            # Reaching the PUBLISH at all proves it was not starved behind it.
            broker.wait_for_held_packets(MqttPacketType.PUBACK, 1, timeout=DUT_EVENT_TIMEOUT)
            assert broker.held_packet_count(MqttPacketType.SUBACK) == 1
        finally:
            broker.release_all_held_packets()


@pytest.mark.eth_ip101
@pytest.mark.xfail(
    reason="Known failure: the client requeues every unacknowledged packet on reconnect, "
    "not just QoS>0 PUBLISH, and the periodic retransmit resends any TRANSMITTED packet "
    "regardless of type. Fixing it changes long-standing behavior, so it is handled separately.",
    strict=True,
)
@pytest.mark.timeout(
    case_timeout(
        connect_operations=2,
        subscribe_operations=1,
        publish_operations=2,
        event_wait_operations=2,
    )
)
@idf_parametrize("target", ["esp32"], indirect=["target"])
def test_mqtt5_reconnect_resends_only_inflight_publishes__sec_4_4(dut: Dut) -> None:
    """Only unacknowledged QoS>0 PUBLISH packets are resent on a new connection.

    MQTT5 4.4: resending unacknowledged PUBLISH (QoS > 0) and PUBREL packets is
    "the only circumstance where a Client or Server is REQUIRED to resend
    messages. Clients and Servers MUST NOT resend messages at any other time"
    [MQTT-4.4.0-1].

    Holding the SUBACK leaves a SUBSCRIBE unacknowledged across the reconnect.
    The broker answers every SUBSCRIBE it receives, so a resent one would show
    up as a second held SUBACK.
    """
    topic = build_topic()

    with (
        broker_started(hold_packet_types=(MqttPacketType.SUBACK, MqttPacketType.PUBACK)) as broker,
        initialized_mqtt_client(dut, broker.uri) as client,
        started_client(client),
    ):
        try:
            client.write(f"subscribe {topic} 1")
            expect_n(client, {b"Subscribe requested, msg_id=": 1}, timeout=DUT_CMD_TIMEOUT)
            broker.wait_for_held_packets(MqttPacketType.SUBACK, 1, timeout=DUT_EVENT_TIMEOUT)

            publish_from_dut(client, topic, 1, payload_prefix="inflight", message_count=1)
            expect_n(client, {b"Publish requested, msg_id=": 1}, timeout=DUT_CMD_TIMEOUT)
            broker.wait_for_held_packets(MqttPacketType.PUBACK, 1, timeout=DUT_EVENT_TIMEOUT)

            # packets held for the old socket are useless once it is gone
            broker.discard_held_packets()
            broker.disconnect_clients()
            client.expect(re.compile(rb"MQTT_EVENT_DISCONNECTED"), timeout=DUT_EVENT_TIMEOUT)
            client.write("reconnect")
            client.expect(re.compile(rb"MQTT_EVENT_CONNECTED"), timeout=DUT_CONNECT_TIMEOUT)

            # The SUBSCRIBE precedes the PUBLISH in the outbox, so once the
            # retransmitted PUBLISH arrives any resent SUBSCRIBE would already
            # have been answered.
            broker.wait_for_held_packets(MqttPacketType.PUBACK, 1, timeout=DUT_EVENT_TIMEOUT)
            assert broker.held_packet_count(MqttPacketType.SUBACK) == 0
        finally:
            broker.release_all_held_packets()


@pytest.mark.eth_ip101
@pytest.mark.timeout(
    case_timeout(
        connect_operations=1,
        subscribe_operations=2,
        publish_operations=DEFAULT_BROKER_RECEIVE_MAXIMUM,
    )
)
@idf_parametrize("target", ["esp32"], indirect=["target"])
def test_mqtt5_subscribe_not_delayed_by_receive_maximum(dut: Dut) -> None:
    """SUBSCRIBE must not be delayed by inflight publish quota.

    MQTT5 §4.9: "The Client MUST NOT delay the sending of any packets
    other than PUBLISH packets due to having sent Receive Maximum publish
    packets without receiving acknowledgements for them."

    The SUBACK must arrive while the broker is still holding all QoS 1 PUBACKs.
    """
    topic_pub = build_topic()
    topic_sub = build_topic()
    publish_quota = DEFAULT_BROKER_RECEIVE_MAXIMUM

    with (
        broker_started(hold_packet_types=(MqttPacketType.PUBACK,)) as broker,
        initialized_mqtt_client(dut, broker.uri) as client,
        started_client(client),
    ):
        subscribed_to(client, topic_pub, 1)
        try:
            publish_from_dut(client, topic_pub, 1, payload_prefix="subscribe_sat", message_count=publish_quota)
            expect_n(
                client,
                {b"Publish requested, msg_id=": publish_quota} | data_payload_patterns("subscribe_sat", publish_quota),
                timeout=DUT_EVENT_TIMEOUT,
            )
            broker.wait_for_held_packets(MqttPacketType.PUBACK, publish_quota, timeout=DUT_EVENT_TIMEOUT)

            subscribed_to(client, topic_sub, 1)
            # Retransmissions can produce additional held PUBACKs for the same
            # inflight packets; none of the original quota has been released.
            assert broker.held_packet_count(MqttPacketType.PUBACK) >= publish_quota
        finally:
            broker.release_held_packets(MqttPacketType.PUBACK)

        expect_n(client, {b"MQTT_EVENT_PUBLISHED": publish_quota}, timeout=DUT_EVENT_TIMEOUT)


@pytest.mark.eth_ip101
@pytest.mark.timeout(
    case_timeout(
        connect_operations=1,
        subscribe_operations=2,
        publish_operations=DEFAULT_BROKER_RECEIVE_MAXIMUM + 2,
    )
)
@idf_parametrize("target", ["esp32"], indirect=["target"])
@pytest.mark.parametrize(
    "qos0_enqueue",
    [
        pytest.param(False, id="publish"),
        pytest.param(True, id="enqueue"),
    ],
)
def test_mqtt5_qos0_not_blocked_by_quota(dut: Dut, qos0_enqueue: bool) -> None:
    """QoS 0 must bypass a quota-blocked QoS 1 outbox head through either API."""
    topic_q1 = build_topic()
    topic_q0 = build_topic()
    publish_quota = DEFAULT_BROKER_RECEIVE_MAXIMUM  # exactly fills the broker quota
    blocked_payload = "blocked_qos1"
    qos0_payload = "qos0_bypass"

    with (
        broker_started(hold_packet_types=(MqttPacketType.PUBACK,)) as broker,
        initialized_mqtt_client(dut, broker.uri) as client,
        started_client(client),
    ):
        subscribed_to(client, topic_q1, 1)
        subscribed_to(client, topic_q0, 0)

        try:
            publish_from_dut(
                client,
                topic_q1,
                1,
                payload_prefix="qos0_sat",
                message_count=publish_quota,
                enqueue=1,
            )
            expect_n(
                client,
                {b"Publish requested, msg_id=": publish_quota} | data_payload_patterns("qos0_sat", publish_quota),
                timeout=DUT_EVENT_TIMEOUT,
            )
            broker.wait_for_held_packets(MqttPacketType.PUBACK, publish_quota, timeout=DUT_EVENT_TIMEOUT)

            publish_from_dut(client, topic_q1, 1, payload_prefix=blocked_payload, message_count=1, enqueue=1)
            expect_n(client, {b"Publish requested, msg_id=": 1}, timeout=DUT_CMD_TIMEOUT)

            publish_from_dut(
                client,
                topic_q0,
                0,
                payload_prefix=qos0_payload,
                message_count=1,
                enqueue=int(qos0_enqueue),
            )
            expect_n(
                client,
                {
                    b"Publish requested, msg_id=": 1,
                    f"MQTT_EVENT_DATA_PAYLOAD {qos0_payload}".encode(): 1,
                },
                timeout=DUT_CMD_TIMEOUT,
            )
            # Retransmissions can produce additional held PUBACKs for the same
            # inflight packets; none of the original quota has been released.
            assert broker.held_packet_count(MqttPacketType.PUBACK) >= publish_quota
        finally:
            broker.release_held_packets(MqttPacketType.PUBACK)

        expect_n(client, {b"MQTT_EVENT_PUBLISHED": publish_quota + 1}, timeout=DUT_EVENT_TIMEOUT)


@pytest.mark.eth_ip101
@pytest.mark.timeout(
    case_timeout(
        connect_operations=1,
        subscribe_operations=1,
        event_wait_operations=2,
    )
)
@idf_parametrize("target", ["esp32"], indirect=["target"])
@pytest.mark.parametrize(
    "protocol_ver",
    [MQTT_PROTOCOL_V_3_1_1, MQTT_PROTOCOL_V_5],
    ids=["v311", "v5"],
)
def test_subscribe_and_qos1_publish__sec_3_8_4_and_4_3(dut: Dut, protocol_ver: int) -> None:
    """
    Base subscribe/QoS 1 publish conformance case, run against both protocol versions.
    Section numbers are identical in both specs:
    - section 3.8.4: SUBSCRIBE Actions (SUBACK interaction)
    - section 4.3: Quality of Service levels and protocol flows (QoS 1: at least once semantics)

    """
    topic = build_topic()

    with (
        broker_started() as broker,
        initialized_mqtt_client(dut, broker.uri, protocol_ver=protocol_ver) as client,
        started_client(client),
    ):
        subscribed_to(client, topic, 1)

        publish_from_dut(
            client,
            topic,
            1,
            payload_prefix="qos1",
            message_count=1,
            enqueue=1,
            pattern_repetitions=4,
        )
        expect_n(
            client,
            {
                b"MQTT_EVENT_PUBLISHED": 1,
                b"MQTT_EVENT_DATA_COMPLETE": 1,
            },
            timeout=DUT_EVENT_TIMEOUT,
        )
