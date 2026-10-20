"""Exercise early UI/backend ordering without a display, mounts or a phone."""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent


@unittest.skipUnless(sys.platform.startswith("linux") and shutil.which("g++"),
                     "Requires a native Linux C++ compiler")
class EarlyGraphicsStartupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        source = cls.root / "source"
        shutil.copytree(ROOT / "fixtures/early-ui", source)
        cls.actual = (ROOT.parent / "core/runner.cpp").read_text()
        scaffold = (source / "aeraui/core/runner.cpp").read_text()
        prefix = scaffold[:scaffold.index("std::atomic<bool> gBackendReady")]
        start = cls.actual.index("std::atomic<bool> gBackendReady")
        end = cls.actual.index("DisplayMetrics gDisplayMetrics{};") + len("DisplayMetrics gDisplayMetrics{};")
        helpers_start = cls.actual.index("bool IsTerminalResult(")
        helpers_end = cls.actual.index("RunResult ToRunResult(", helpers_start)
        helpers = cls.actual[helpers_start:helpers_end]
        # Permit running this regression against the asynchronous implementation
        # before the fix: simulated display initialization then has no barrier.
        if "void NotifyEarlyDisplayReady()" not in helpers:
            helpers += "\nvoid NotifyEarlyDisplayReady() {}\n"
        loop = """
}  // namespace
RunResult RunLoop(bool fastboot, const DisplayMetrics& metrics, bool resume) {
    return TestRunLoop(fastboot, metrics, resume);
}
void TestDisplayInitialized() { NotifyEarlyDisplayReady(); }
"""
        functions = cls.actual[cls.actual.index("static RunResult RunInlineUi("):]
        (source / "aeraui/core/runner.cpp").write_text(
            prefix + cls.actual[start:end] + "\n" + helpers + loop + functions)
        shutil.copyfile(ROOT.parent / "include/aeraui/runner.hpp",
                        source / "aeraui/include/aeraui/runner.hpp")
        cls.binary = cls.root / "early-graphics"
        result = subprocess.run([
            "g++", "-std=c++17", "-pthread", "-Wall", "-Wextra", "-Werror",
            "-I", str(source), str(ROOT / "early_graphics_startup_driver.cpp"),
            "-o", str(cls.binary)], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_case(self, scenario):
        result = subprocess.run([str(self.binary), scenario], timeout=5,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_backend_cannot_overlay_vendor_during_graphics_initialization(self):
        self.run_case("success")

    def test_initialization_failure_releases_startup_and_remains_joinable(self):
        self.run_case("failure")

    def test_early_terminal_result_releases_startup_and_remains_latched(self):
        self.run_case("terminal")

    def test_readiness_is_published_after_engine_initialization(self):
        loop = self.actual[self.actual.index("RunResult RunLoop("):
                           self.actual.index("// Startup can enter the UI")]
        self.assertLess(loop.index("engine.Initialize("),
                        loop.index("return RunResult::kEngineFailure;"))
        self.assertLess(loop.index("return RunResult::kEngineFailure;"),
                        loop.index("NotifyEarlyDisplayReady();"))
        self.assertLess(loop.index("NotifyEarlyDisplayReady();"),
                        loop.index("aeraScreenTimer.Wake();"))


if __name__ == "__main__":
    unittest.main()
