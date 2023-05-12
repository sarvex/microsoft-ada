# Copyright (c) Microsoft Corporation.
# Licensed under the MIT license.
import os
import sys
import subprocess
from threading import Thread, Lock

class BuildTools:

    def logstream(self, stream):
        try:
            while True:
                if not (out := stream.readline()):
                    break
                msg = out.decode('utf-8')
                print(msg.strip('\r\n'))
        except:
            errorType, value, traceback = sys.exc_info()
            msg = f"### Exception: {str(errorType)}: {str(value)}"
            print(msg)

    def run(self, command, print_output=True, shell=False):
        cmdstr = command if isinstance(command, str) else " ".join(command)
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0) as proc:
            stdout_thread = Thread(target=self.logstream, args=(proc.stdout,))
            stderr_thread = Thread(target=self.logstream, args=(proc.stderr,))

            stdout_thread.start()
            stderr_thread.start()

            while stdout_thread.isAlive() or stderr_thread.isAlive():
                pass

            proc.wait()

            return proc.returncode