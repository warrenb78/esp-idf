import argparse
import sys
from construct import *
import serial

def get_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    parser.add_argument('port', )
    return parser

class Commander:
    def __init__(self, shit, baudrate):
        self.s = serial.Serial(port=shit, baudrate=baudrate)
        self.format = Struct(
            "cmd" / Enum(Int32ul, 
                KEEP_ALIVE = 0,
                START_KEEP_ALIVE = 1,
                STOP_KEEP_ALIVE = 2,
                BECOME_ROOT = 3,
                GO_TO_SLEEP = 4,
                GET_NODES = 5),
            "len" / Short,
            "buf" / Array(this.len, Byte)
        )
        self.id_to_mac = {}
        self.mac_to_id = {}
        self.count = 0

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.s.close()

    def open(self):
        self.s.open()

    def close(self):
        self.s.close()

    def send_become_root(self):
        b = self.format.build(dict(cmd=self.format.cmd.BECOME_ROOT, len=0, buf=b""))
        print(b)
        self.s.write(b)

    def send_start_keep_alive(self):
        b = self.format.build(dict(cmd=self.format.cmd.START_KEEP_ALIVE, len=0, buf=b""))
        print(b)
        self.s.write(b)

    def send_get_nodes(self):
        b = self.format.build(dict(cmd=self.format.cmd.GET_NODES, len=0, buf=b""))
        print(b)
        self.s.write(b)
        arr = self.s.read(4)
        size = int.from_bytes(arr, byteorder='little', signed=False)
        print(f"found {size} nodes")
        arr = self.s.read(size*6)
        macs = [tuple(arr[i: i+6]) for i in range(0, len(arr), 6)]
        for mac in macs:
            if mac not in self.mac_to_id.keys():
                self.mac_to_id[mac] = self.count
                self.id_to_mac[self.count] = mac
                self.count += 1
