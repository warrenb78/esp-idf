import argparse
import sys
from construct import *
import serial

def get_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    parser.add_argument('port', )
    return parser

MessageType = Enum(Int32ul, 
        KEEP_ALIVE = 0,
        START_KEEP_ALIVE = 1,
        STOP_KEEP_ALIVE = 2,
        BECOME_ROOT = 3,
        GO_TO_SLEEP = 4,
        GET_NODES = 5)

MessageHeader = Struct(
    "cmd" / MessageType,
    "len" / Int16ul)

Message = Struct(
    "header" / MessageHeader,
    "buf" / Array(this.header.len, Byte)
)

Mac = Array(6, Byte)

GetNodesReply = Struct(
    "num_nodes" / Int8ul,
    "nodes" / Array(15, Mac)
)

class Commander:
    def __init__(self, shit, baudrate):
        self.s = serial.Serial(port=shit, baudrate=baudrate)
        self.format = Message
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

    def print_macs(self):
        for dev_id, mac in self.id_to_mac.items():
            mac = Mac.parse(mac)
            print(f"{dev_id:2} - {mac[0]:02X}:{mac[1]:02X}:{mac[2]:02X}:{mac[3]:02X}:{mac[4]:02X}:{mac[5]:02X}")

    def send_become_root(self):
        b = self.format.build(dict(header = dict(cmd=MessageType.BECOME_ROOT, len=0), buf=b""))
        print(b)
        self.s.write(b)

    def send_start_keep_alive(self):
        b = self.format.build(dict(header = dict(cmd=MessageType.START_KEEP_ALIVE, len=0), buf=b""))
        print(b)
        self.s.write(b)

    def send_get_nodes(self):
        b = self.format.build(dict(header = dict(cmd=MessageType.GET_NODES, len=0), buf=b""))
        print(b)
        self.s.write(b)
        arr = self.s.read(MessageHeader.sizeof())
        header = MessageHeader.parse(arr)
        print(f"got {MessageHeader.sizeof()} bytes to read {header.len}")
        arr = self.s.read(header.len)
        res = GetNodesReply.parse(arr)
        print(f"number of nodes {res.num_nodes}")
        mac_size = 6
        # macs = [tuple(arr[i: i+mac_size]) for i in range(0, len(arr), mac_size)]
        for i, mac in enumerate(res.nodes):
            if i >= res.num_nodes:
                break
            mac = Mac.build(mac)
            if mac not in self.mac_to_id.keys():
                self.mac_to_id[mac] = self.count
                self.id_to_mac[self.count] = mac
                self.count += 1
