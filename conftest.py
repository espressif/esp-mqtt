import pytest
import typing as t
import os
import logging as logger
from pytest_embedded.plugin import multi_dut_argument
from pytest_embedded.plugin import multi_dut_fixture
from _pytest.fixtures import FixtureRequest

def pytest_configure(config):
    for plugin in config.pluginmanager.list_name_plugin():
        print("Loaded plugins", plugin)

@pytest.fixture
@multi_dut_argument
def config(request: FixtureRequest) -> str:
    return getattr(request, 'param', None) or 'default'  # type: ignore

# @pytest.fixture
# @multi_dut_fixture
# def target(request: FixtureRequest, dut_total: int, dut_index: int) -> str:
#     plugin = request.config.stash[pytest.StashKey['IdfPytestEmbedded']]
#
#     if dut_total == 1:
#         return plugin.target[0]  # type: ignore
#
#     return plugin.target[dut_index]  # type: ignore

@pytest.fixture
@multi_dut_fixture
def build_dir(
    request: FixtureRequest,
    app_path: str,
    target: t.Optional[str],
    config: t.Optional[str],
) -> str:
    """Find a valid build directory based on priority rules.

    Checks local build directories in the following order:

    1. build_<target>_<config>
    2. build_<target>
    3. build_<config>
    4. build

    :param request: Pytest fixture request
    :param app_path: Path to the application
    :param target: Target being used
    :param config: Configuration being used

    :returns: Valid build directory name

    :raises ValueError: If no valid build directory is found
    """
    check_dirs = []
    build_dir_arg = request.config.getoption('build_dir')

    if build_dir_arg:
        check_dirs.append(build_dir_arg)
    if target is not None and config is not None:
        check_dirs.append(f'build_{target}_{config}')
    if target is not None:
        check_dirs.append(f'build_{target}')
    if config is not None:
        check_dirs.append(f'build_{config}')
    check_dirs.append('build')

    for check_dir in check_dirs:
        binary_path = os.path.join(app_path, check_dir)
        if os.path.isdir(binary_path):
            logger.info(f'Found valid binary path: {binary_path}')
            return check_dir

        logger.warning('Checking binary path: %s... missing... trying another location', binary_path)

    raise ValueError(
        'No valid build directory found. '
        f'Please build the binary via "idf.py -B {check_dirs[0]} build" and run pytest again'
    )

@pytest.fixture
def log_performance(record_property: t.Callable[[str, object], None]) -> t.Callable[[str, str], None]:
    """
    log performance item with pre-defined format to the console
    and record it under the ``properties`` tag in the junit report if available.
    """

    def real_func(item: str, value: str) -> None:
        """
        :param item: performance item name
        :param value: performance value
        """
        logger.info('[Performance][%s]: %s', item, value)
        record_property(item, value)

    return real_func
