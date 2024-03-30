

import pyskynet
from pyskynet import settings


def test_old_env():
    settings["rewrw"] = "rewrewrewrewrwe"
    settings["rew"] = {"rew":321}
    pyskynet.start()
    pyskynet.scriptservice("""
        local pyskynet = require "pyskynet"

        pyskynet.start(function()
            print(pyskynet.settings.rewrw)
            print(pyskynet.settings.rew.rew)
        end)
    """)


test_old_env()

