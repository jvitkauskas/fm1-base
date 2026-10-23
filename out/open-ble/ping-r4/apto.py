# Set the LE Authenticated Payload Timeout on an ACL handle via a raw HCI socket (needs root).
import socket, struct, sys, time
handle = int(sys.argv[1], 0); tmo_10ms = int(sys.argv[2], 0)
s = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_RAW, socket.BTPROTO_HCI)
s.bind((0,))
# filter: packet type event (4), events Command Complete (0x0E)
flt = struct.pack("<IIIHxx", 1 << 4, 1 << 0x0E, 0, 0)
s.setsockopt(socket.SOL_HCI, socket.HCI_FILTER, flt)
op = (0x03 << 10) | 0x07C
s.send(struct.pack("<BHBHH", 1, op, 4, handle, tmo_10ms))
s.settimeout(2)
while True:
    p = s.recv(260)
    if p[0] == 4 and p[1] == 0x0E and struct.unpack_from("<H", p, 4)[0] == op:
        print("status", hex(p[6])); break
