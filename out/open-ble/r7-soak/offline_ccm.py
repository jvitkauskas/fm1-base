# SPDX-License-Identifier: MIT OR MPL-2.0
# Copyright (c) 2026 Julius Vitkauskas
"""Offline check of an R7 frozen RX record against the central's LTK.

usage: btmon -T -r btmon.btsnoop > /tmp/btmon.txt
       python3 offline_ccm.py /tmp/btmon.txt SKDM SKDS IV_USED COUNTER_HEX CIPHERTEXT_TAG [HEADER0]
Values are the r7_skdm, r7_skds, r7_iv_used, r7_pdu counter and r7_ciphertext_tag lines, as printed.
The LTK is read from the central's LE Start Encryption command in the btmon text and never printed.
Prints the expected ping (LL_PING_REQ, plaintext 12) at nearby counters in both directions, the decoded
first byte, the MIC bit difference, AAD values that would reproduce the record, and whether the received
tag equals the tag of one of the 40 previous pings or the ciphertext of a recent ATT write.
"""
import re, subprocess, sys

def aes(k, b):
    return subprocess.run(["openssl", "enc", "-aes-128-ecb", "-nopad", "-K", k.hex()],
                          input=b, capture_output=True, check=True).stdout
def xor(a, b): return bytes(x ^ y for x, y in zip(a, b))

text = open(sys.argv[1]).read()
blk = text[text.find("(0x08|0x0019) plen 28"):][:600]
m = re.search(r"Long term key\[16\]:\s*\n\s*((?:[0-9a-f]{2} ){15}[0-9a-f]{2})", blk)
if not m: sys.exit("no LE Start Encryption with an LTK in the btmon text")
ltk_hci = bytes.fromhex(m.group(1).replace(" ", ""))          # HCI order, least significant octet first
skdm, skds, iv = (bytes.fromhex(a) for a in sys.argv[2:5])
C = int(sys.argv[5], 16); ct = bytes.fromhex(sys.argv[6])
hdr0 = int(sys.argv[7], 16) if len(sys.argv) > 7 else 0x0F
sk = aes(ltk_hci[::-1], (skdm + skds)[::-1])                  # session key, most significant octet first

def nonce(c, d):
    n = c.to_bytes(5, "little"); return n[:4] + bytes([n[4] | (0x80 if d else 0)]) + iv
def ccm(c, d, pl, aad):
    n = nonce(c, d)
    x = aes(sk, bytes([0x49]) + n + len(pl).to_bytes(2, "big"))
    x = aes(sk, xor(x, bytes([0, 1, aad]) + bytes(13)))
    if pl: x = aes(sk, xor(x, pl + bytes(16 - len(pl))))
    s0 = aes(sk, bytes([1]) + n + b"\0\0"); s1 = aes(sk, bytes([1]) + n + b"\0\1")
    return xor(pl, s1) + xor(x[:4], s0[:4]), s1

tail = ct[1:]
for d in (1, 0):
    for c in range(max(C - 3, 0), C + 4):
        exp, s1 = ccm(c, d, b"\x12", hdr0 & 3)
        diff = bin(int.from_bytes(xor(exp[1:], tail), "big")).count("1")
        print("dir %d ctr %X expected %s recv %s decoded_byte %02X mic_bitdiff %d"
              % (d, c, exp.hex().upper(), ct.hex().upper(), ct[0] ^ s1[0], diff))
print("aad values reproducing the record:", [hex(a) for a in range(256) if ccm(C, 1, b"\x12", a)[0] == ct])
print("previous-ping tag matches:", ["%X" % c for c in range(max(C - 40, 0), C) if ccm(c, 1, b"\x12", 3)[0][1:] == tail])
att = (bytes([0, 4, 0, 0x52]), bytes([0, 4, 0, 0x12]))     # L2CAP length high, CID 4, ATT write cmd/req
print("ATT write ciphertext matches:",
      ["%X" % c for c in range(max(C - 0x300, 0), C + 1) if xor(ccm(c, 1, b"", 3)[1][1:5], tail) in att])
