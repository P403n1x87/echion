from tests.utils import DataSummary
from tests.utils import PY
from tests.utils import run_target
from tests.utils import retry_on_valueerror


@retry_on_valueerror()
def test_more_than_one_module():
    result, data = run_target("target_more_than_one_module")
    assert result.returncode == 0 and data, result.stderr.decode()

    md = data.metadata
    assert md["mode"] == "wall"
    assert md["interval"] == "1000"

    summary = DataSummary(data)

    summary_json = {}
    for thread in summary.threads:
        summary_json[thread] = [
            {
                "stack": key,
                "metric": value,
            }
            for key, value in summary.threads[thread].items()
            if key and isinstance(next(iter(key)), str)
        ]

    with open("summary_more_than_one_module.json", "w") as f:
        import json

        json.dump(summary_json, f, indent=2)

    # Test stacks and expected values
    summary.assert_stack(
        "0:MainThread",
        (
            "_run_module_as_main",
            "_run_code",
            "<module>",
            "main",
            "helper_function",
        ),
        lambda v: v >= 0.0,
    )

    qual_names = PY >= (3, 11)

    summary.assert_substack(
        "0:MainThread",
        (
            "<module>",  # top-level code from main module
            "_handle_fromlist",  # !!! import machinery
            "_call_with_frames_removed",  # !!! import machinery
            "_find_and_load",  # !!! import machinery
            "_find_and_load_unlocked",  # !!! import machinery
            "_load_unlocked",  # !!! import machinery
            (  # !!! import machinery
                ("_ImportHookChainedLoader." if qual_names else "") + "_exec_module"
            ),
            (  # !!! import machinery
                ("_LoaderBasics." if qual_names else "") + "exec_module"
            ),
            "_call_with_frames_removed",  # !!! import machinery
            "<module>",  # top-level code from dependency module
            "wait_500_ms_on_import",  # function in dependency module
        ),
        lambda v: v >= 0.0,
    )
