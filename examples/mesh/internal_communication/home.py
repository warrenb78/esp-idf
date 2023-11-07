import argparse
import sys
from construct import *
import serial
import itertools

import log

NOT_ALIVE_TIME_MS = 1000

MessageType = Enum(Int32ul, 
        KEEP_ALIVE = 0,
        START_KEEP_ALIVE = 1,
        STOP_KEEP_ALIVE = 2,
        BECOME_ROOT = 3,
        GO_TO_SLEEP = 4,
        GET_NODES = 5,
        GET_NODES_REPLY = 6,
        GET_STATISTICS = 7,
        GET_STATISTICS_REPLY = 8,
        CLEAR_STATISTICS = 9,
        FORWARD = 10,
        ECHO_REQUEST = 11,
        ECHO_REPLY = 12,
        GET_LATENCY = 13,
        GET_LATENCY_REPLY = 14,
)

MessageHeader = Struct(
    "cmd" / MessageType,
    "len" / Int16ul
)

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
    "current_ms" / Int64ul,
    "nodes" / Array(MAX_NODES, StatisticsNodeInfo)
)

GetLatencyReplyData = Struct(
    "start_ms" / Int64ul,
    "end_ms" / Int64ul,
)

_LOGGER = log.logging.getLogger(__name__)


class Commander:
    def __init__(self, port, baudrate):
        self.s = serial.Serial(port=port, baudrate=baudrate)
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
            _LOGGER.info(f"{dev_id:2} - {self._format_mac(mac)}")

    def send_become_root(self):
        self._send_command(MessageType.BECOME_ROOT)

    def clear_statistics(self):
        self._send_command(MessageType.CLEAR_STATISTICS)

    def start_100kbps_transmission(self, dst):
        return self.start_transmission(dst, 10, 1000)

    def start_max_trasmition(self, dst):
        # Max parameters for send
        return self.start_transmission(dst, 1, 1460)

    def start_transmission(self, dst, delay_ms, payload_size):
        return self._send_start_keep_alive(dst, 0, delay_ms, payload_size)

    def stop_transmission(self, dst):
        return self._send_stop_keep_alive(dst)

    def bring_node_down(self, dst, ms):
        return self._send_go_to_sleep(dst, ms)

    def get_nodes(self):
        return self._send_get_nodes()

    def transmission_info(self):
        return self._send_get_statistics()

    def get_latency(self, from_node, to_node):
        return self._send_get_latency(from_node, to_node)

    def _build_command(self, cmd, buf=b''):
        return self.format.build(dict(header = dict(cmd=cmd, len=len(buf)), buf=buf))

    def _send_command(self, cmd, buf=b''):
        b = self._build_command(cmd, buf)
        _LOGGER.debug(f'Sending cmd {cmd}: data {b}')
        self.s.write(b)

    def _recv_message(self):
        header_arr = self.s.read(MessageHeader.sizeof())
        header = MessageHeader.parse(header_arr)
        _LOGGER.debug(f'len {header.len}')
        arr = self.s.read(header.len)
        _LOGGER.debug(f'Receive cmd {header.cmd}: bytes len {header.len} data {header_arr + arr}')
        return arr

    def _send_forward(self, dst, buf):
        forward = Struct(
            "mac" / Mac,
            "to_host" / Int8ul,
            "buf" / Array(len(buf), Byte)
        ).build(dict(mac=self.id_to_mac[dst], buf = buf, to_host = 0))

        self._send_command(MessageType.FORWARD, forward)

    def _send_start_keep_alive(self, dst, reset_index, delay_ms, payload_size, send_to_root=True, target_mac=(0,0,0,0,0,0)):
        start_keep_alive = Struct(
            "reset_index" / Byte,
            "delay_ms" / Int32ul,
            "send_to_root" / Byte,
            "payload_size" / Int16ul,
            "target_mac" / Array(6, Byte)
        ).build(dict(reset_index = reset_index, delay_ms = delay_ms,
                     send_to_root = send_to_root, target_mac=target_mac,
                     payload_size = payload_size))

        b = self._build_command(MessageType.START_KEEP_ALIVE, start_keep_alive)

        self._send_forward(dst, b)
        
    def _send_stop_keep_alive(self, dst):
        b = self.format.build(dict(header = dict(cmd=MessageType.STOP_KEEP_ALIVE, len=0), buf=b""))
        self._send_forward(dst, b)

    def _send_get_latency(self, src_echo, dst_echo):
        get_latency = Struct(
            "dst" / Mac
        ).build(dict(dst = self.id_to_mac[dst_echo]))
        b = self._build_command(MessageType.GET_LATENCY, get_latency)
        self._send_forward(src_echo, b)
        arr = self._recv_message()
        res = GetLatencyReplyData.parse(arr)
        _LOGGER.debug("Latency %d -> %d -> %d start [%d ms] end [%d ms] round trip [%d ms]", 
                      src_echo, dst_echo, src_echo, res.start_ms,
                      res.end_ms, res.end_ms - res.start_ms)
        _LOGGER.info("Latency %d -> %d -> %d round trip [%d ms]", 
                     src_echo, dst_echo, src_echo, res.end_ms - res.start_ms)

    def _send_go_to_sleep(self, dst, ms):
        go_to_sleep = Struct(
            "ms" / Int64ul
        ).build(dict(ms=ms))

        b = self._build_command(MessageType.GO_TO_SLEEP, go_to_sleep)

        self._send_forward(dst, b)

    def _send_get_nodes(self):
        self._send_command(MessageType.GET_NODES)
        arr = self._recv_message()
        res = GetNodesReply.parse(arr)
        _LOGGER.info(f"Got {res.num_nodes} nodes")
        for i, mac in enumerate(res.nodes):
            if i >= res.num_nodes:
                break
            mac = Mac.build(mac)
            if mac not in self.mac_to_id.keys():
                self.mac_to_id[mac] = self.count
                self.id_to_mac[self.count] = mac
                _LOGGER.info(f'New node id {self.count} mac {self._format_mac(mac)}')
                self.count += 1

    def _send_get_statistics(self):
        self._send_command(MessageType.GET_STATISTICS)
        arr = self._recv_message()
        res = StatisticsTreeInfo.parse(arr)
        _LOGGER.info(f"Sees {res.num_nodes} nodes from root")
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

        _LOGGER.info('Tree:\n' + self._format_sub_tree(root_set[0], tree, nodes, res.current_ms))
        
    @staticmethod
    def _format_info_for_tree(mac, info_dict, current_ms) -> str:
        val = info_dict.get(mac, None)
        if val is None:
            return ''
        if val.count_of_messages < 2:
            kbps = ''
            msg_p_s = ''
        else:
            time_s = (val.last_keep_alive_ms - val.first_message_ms) / 1000.
            kbps_val = val.total_bytes_sent / 1000. / time_s;
            kbps = f' - KB/s[{kbps_val:4.3f}]'
            msg_p_s = f' - Pkt/s[{val.count_of_messages/time_s:3.2f}]'

        since_last = current_ms - val.last_keep_alive_ms
        alive = since_last < NOT_ALIVE_TIME_MS
        alive_s = 'Up  ' if alive else 'Down'

        return f' : {alive_s} - Since last pkt ms[{since_last:4}] - rssi[{val.last_rssi:03}]{kbps}{msg_p_s}'

    def _format_sub_tree(self, current_id, tree, info, current_ms, level=0) -> str:
        id_str = self._format_mac(current_id)
        node_info = self._format_info_for_tree(current_id, info, current_ms)
        prefix = '----' * level + (' ' if level else '')
        res = f'{prefix}{id_str}{node_info}\n'
        for child in tree[current_id]:
            res += self._format_sub_tree(child, tree, info, current_ms, level + 1)
        return res
        
def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    parser.add_argument('-l', '--log-path', help='Path to text loger', default='mesh.log')
    return parser

if __name__ == '__main__':
    args = build_parser().parse_args()
    log.config_logging(args.log_path)
