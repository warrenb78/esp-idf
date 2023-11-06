import argparse
import sys
from construct import *
import serial

def get_parser() -> argparse.ArgumentParser:
#{
    parser = argparse.ArgumentParser()
    parser.add_argument('port', )
    return parser
#}

class Commander:
#{
    def __init__(self, shit, baudrate):
    #{
        self.s = serial.Serial(port=shit, baudrate=baudrate)
        self.format = Struct(
            "cmd" / Enum(Int32ul, 
                KEEP_ALIVE = 0,
                START_KEEP_ALIVE = 1,
                STOP_KEEP_ALIVE = 2,
                BECOME_ROOT = 3,
                GO_TO_SLEEP = 4),
            "len" / Short,
            "buf" / Array(this.len, Byte)
        )
    #}

    def send_become_root(self):
    #{
        b = self.format.build(dict(cmd=self.format.cmd.BECOME_ROOT, len=0, buf=b""))
        print(b)
        self.s.write(b)
    #}

    def send_start_keep_alive(self):
    #{
        b = self.format.build(dict(cmd=self.format.cmd.START_KEEP_ALIVE, len=0, buf=b""))
        print(b)
        self.s.write(b)
    #}
#}


# if __name__ == '__main__':
#     sys.exit(main())