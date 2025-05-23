#!/usr/bin/env python3

# This file is part of "austin" which is released under GPL.
#
# See file LICENCE or go to http://www.gnu.org/licenses/ for full license
# details.
#
# Austin is a Python frame stack sampler for CPython.
#
# Copyright (c) 2019 Gabriele N. Tornetta <phoenix1987@gmail.com>.
# All rights reserved.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import threading
from time import monotonic as time
from time import sleep


def idle():
    sleep(1)


def keep_cpu_busy():
    a = []
    i = 0
    idle()
    end = time() + 1
    while time() <= end:
        i += 1
        a.append(i)
        if i % 1000000 == 0:
            print("Unwanted output " + str(i))


if __name__ == "__main__":
    threading.Thread(target=keep_cpu_busy, name="SecondaryThread").start()
    keep_cpu_busy()
