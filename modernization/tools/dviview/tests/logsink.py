#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# A stand-in for the collector of str, for the tests of the Viewer: it makes
# the Unix datagram socket whose name is its first argument, and writes
# every message that arrives to the file that is its second argument, one
# line for each message, without the priority.  The tests of the Viewer read
# that file where they used to read what the Viewer wrote to the console.
# It ends on SIGTERM.
#
#     python3 -I -B tests/logsink.py SOCKET FILE

import os
import re
import signal
import socket
import sys


def main():
    path, out = sys.argv[1], sys.argv[2]
    signal.signal(signal.SIGTERM, lambda n, f: sys.exit(0))
    if os.path.exists(path):
        os.unlink(path)
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    sock.bind(path)
    with open(out, 'w', buffering=1) as f:
        while True:
            data = sock.recv(4096).decode('latin-1')
            data = re.sub(r'^<\d+>', '', data)
            f.write(re.sub(r'[\x00-\x1f]', ' ', data).rstrip() + '\n')


main()
