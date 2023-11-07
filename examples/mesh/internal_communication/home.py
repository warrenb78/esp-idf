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

class tree_represent_node(object):
    def __init__(self, value, children = []):
        self.value = value
        self.children = children

    def __str__(self, level=0):
        ret = "\t"*level+repr(self.value)+"\n"
        for child in self.children:
            ret += child.__str__(level+1)
        return ret

    def __repr__(self):
        return '<tree node representation>'


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

    @staticmethod
    def _format_mac(mac):
        return f'{mac[0]:02X}:{mac[1]:02X}:{mac[2]:02X}:{mac[3]:02X}:{mac[4]:02X}:{mac[5]:02X}'

    def print_macs(self):
        for dev_id, mac in self.id_to_mac.items():
            mac = Mac.parse(mac)
            print(f"{dev_id:2} - {self._format_mac(mac)}")

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
        nodes = dict()
        for node_info in itertools.islice(res.nodes, res.num_nodes):
            # add to parent
            tree.setdefault(tuple(node_info.parent_mac), []).append(tuple(node_info.mac))
            # init empty for leafs
            tree.setdefault(tuple(node_info.mac), [])
            nodes[tuple(node_info.mac)] = node_info

        root_set = list(set(tree.keys()) - set(nodes.keys()))
        assert len(root_set) == 1

        print(f"root mac {Mac.build(root_set[0])}")
        print('\n' + self._format_sub_tree(root_set[0], tree, nodes))
        
    @staticmethod
    def _format_info_for_tree(mac, info_dict) -> str:
        val = info_dict.get(mac, None)
        if val is None:
            return ''
        if val.count_of_messages < 2:
            kbps = ''
            msg_p_s = ''
        else:
            time_s = (val.last_keep_alive_ms - val.first_message_ms) / 1000.
            kbps_val = val.total_bytes_sent / 1000. / time_s;
            kbps = f' KB/s[{kbps_val:4.3f}]'
            msg_p_s = f' Pkt/s[{val.count_of_messages/time_s:3.2f}]'

        return f' : rssi[{val.last_rssi:03}]{kbps}{msg_p_s}'

    def _format_sub_tree(self, current_id, tree, info, level=0) -> str:
        id_str = self._format_mac(current_id)
        node_info = self._format_info_for_tree(current_id, info)
        prefix = '----' * level + (' ' if level else '')
        res = f'{prefix}{id_str}{node_info}\n'
        for child in tree[current_id]:
            res += self._format_sub_tree(child, tree, info, level + 1)
        return res
        