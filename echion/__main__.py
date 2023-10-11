# This file is part of "echion" which is released under MIT.
#
# Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.


def main() -> None:
    from threading import Thread

    import echion.core as ec

    Thread(target=ec.start, name="echion.core.sampler", daemon=True).start()
