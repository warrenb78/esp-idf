import argparse
import sys
from construct import *
import serial
import itertools

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
        GET_NODES = 5,
        GET_NODES_REPLY = 6,
        GET_STATISTICS = 7,
        GET_STATISTICS_REPLY = 8,)

MessageHeader = Struct(
    "cmd" / MessageType,
    "len" / Int16ul)

Message = Struct(
    "header" / MessageHeader,
    "buf" / Array(this.header.len, Byte)
)

Mac = Array(6, Byte)

MAX_NODES = 15

GetNodesReply = Struct(
    "num_nodes" / Int8ul,
    "nodes" / Array(MAX_NODES, Mac)
)

StatisticsNodeInfo = Struct(
    "first_message_ms" / Int64ul,
    "last_keep_alive_ms" / Int64ul,
    "last_keep_alive_far_ms" / Int64ul,
    "total_bytes_sent" / Int64ul,
    "count_of_messages" / Int64ul,
    "mac" / Mac,
    "parent_mac" / Mac,
    "layer" / Int8ul,
    "missed_messages" / Int64ul,
    "last_rssi" / Int64sl,
)

StatisticsTreeInfo = Struct(
    "num_nodes" / Int8ul,
    "nodes" / Array(MAX_NODES, StatisticsNodeInfo)
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

    def _send_empty_command(self, cmd):
        b = self.format.build(dict(header = dict(cmd=cmd, len=0), buf=b""))
        print(b)
        self.s.write(b)

    def send_become_root(self):
        self._send_empty_command(MessageType.BECOME_ROOT)

    def send_start_keep_alive(self):
        self._send_empty_command(MessageType.START_KEEP_ALIVE)

    def _recv_message(self):
        arr = self.s.read(MessageHeader.sizeof())
        header = MessageHeader.parse(arr)
        arr = self.s.read(header.len)
        print(f"got {MessageHeader.sizeof()} bytes to read {header.len}")
        return arr

    def send_get_nodes(self):
        self._send_empty_command(MessageType.GET_NODES)
        arr = self._recv_message()
        res = GetNodesReply.parse(arr)
        print(f"number of nodes {res.num_nodes}")
        for i, mac in enumerate(res.nodes):
            if i >= res.num_nodes:
                break
            mac = Mac.build(mac)
            if mac not in self.mac_to_id.keys():
                self.mac_to_id[mac] = self.count
                self.id_to_mac[self.count] = mac
                self.count += 1

    def send_get_statistics(self):
        self._send_empty_command(MessageType.GET_STATISTICS)
        arr = self._recv_message()
        res = StatisticsTreeInfo.parse(arr)
        print(f"number of nodes {res.num_nodes}")
        if res.num_nodes == 0:
            return
        tree = dict()
        childs = set()
        for node_info in itertools.islice(res.nodes, res.num_nodes):
            tree.setdefault(tuple(node_info.parent_mac), []).append(node_info)
            childs.add(tuple(node_info.mac))

        root_set = list(set(tree.keys()) - childs)
        assert len(root_set) == 1

        print(f"root mac {Mac.build(root_set[0])}")
