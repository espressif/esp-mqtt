import pytest
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

@pytest.fixture
@multi_dut_fixture
def target(request: FixtureRequest, dut_total: int, dut_index: int) -> str:
    plugin = request.config.stash[pytest.StashKey['IdfPytestEmbedded']]

    if dut_total == 1:
        return plugin.target[0]  # type: ignore

    return plugin.target[dut_index]  # type: ignore
