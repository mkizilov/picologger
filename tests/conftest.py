import pytest


def pytest_addoption(parser):
    parser.addoption("--hil-port", default=None, help="explicit USB CDC port for hardware tests")


@pytest.fixture(scope="session")
def hil_port(request):
    port = request.config.getoption("--hil-port")
    if not port:
        pytest.skip("hardware tests require an explicit --hil-port")
    return port
