import time

from tests import target_more_than_one_module_dep


def main():
    time.sleep(0.25)
    target_more_than_one_module_dep.helper_function(1, 2.0)


if __name__ == "__main__":
    main()
