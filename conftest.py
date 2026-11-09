"""Keep ordinary repository pytest sessions free of Cubit and Qt."""
import pytest

from tools import cubit_pytest_guard as guard


def pytest_configure(config):
    guard.configure()
    libraries = guard.loaded_vendor_libraries()
    if libraries:
        raise pytest.UsageError("Cubit/Qt DLLs already loaded before collection: " + ", ".join(libraries))


def pytest_collectreport(report):
    libraries = guard.loaded_vendor_libraries()
    if libraries:
        report.outcome = "failed"
        report.longrepr = f"{report.nodeid}: Cubit/Qt DLLs loaded during collection: " + ", ".join(libraries)


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    result = yield
    report = result.get_result()
    if item.session.shouldfail:
        return
    libraries = guard.loaded_vendor_libraries()
    if libraries:
        detail = f"{item.nodeid} ({report.when}): loaded Cubit/Qt DLLs in pytest: " + ", ".join(libraries)
        report.outcome = "failed"
        report.longrepr = detail
        item.session.shouldfail = detail
