# PUBLIC TEST-ONLY credentials for a DISPOSABLE integration test.
# Reused after VM restart: NO production security or cross-run nonce guarantee.
GUEST_TX = (0x1501, bytes(range(16)), b'\x11\x22\x33\x44')
PEER_TX = (0x1502, bytes(range(16, 32)), b'\x55\x66\x77\x88')
LOCAL = b'\xfe\x80'+bytes(12)+b'\x01\x51'
PEER = b'\xfe\x80'+bytes(12)+b'\x01\x52'
