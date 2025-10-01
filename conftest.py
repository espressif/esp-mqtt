# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0

import socket
import pytest
import logging
import typing as t


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
