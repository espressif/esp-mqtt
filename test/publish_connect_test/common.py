import socket
import logging
from typing import Any
from pytest_embedded import Dut

def get_host_ip4_by_dest_ip(dest_ip: str = '') -> str:
    if not dest_ip:
        dest_ip = '8.8.8.8'
    s1 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s1.connect((dest_ip, 80))
    host_ip = s1.getsockname()[0]
    s1.close()
    assert isinstance(host_ip, str)
    print(f'Using host ip: {host_ip}')
    return host_ip

def get_runner_ip(dut: Dut) -> Any:
    dut_ip = dut.expect(r'IPv4 address: (\d+\.\d+\.\d+\.\d+)[^\d]', timeout=30).group(1).decode()
    logging.info('Got IP={}'.format(dut_ip))
    return get_host_ip4_by_dest_ip(dut_ip)
