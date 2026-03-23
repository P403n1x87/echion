import time


def leaf():
    time.sleep(2)   # line 5


def middle():
    leaf()          # line 9


def top():
    middle()        # line 13


top()
