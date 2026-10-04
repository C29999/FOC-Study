"""Compatibility entry point: compile and test the actual control/parser C."""
from pathlib import Path
import runpy

if __name__ == "__main__":
    runpy.run_path(str(Path(__file__).resolve().parent / "tests/run_host_tests.py"), run_name="__main__")
