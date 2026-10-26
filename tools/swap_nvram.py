# Put a 32 KB machine memory image into a DPF CLAP state saved by ostrich_clap_render.
#   swap_nvram.py state_in nvram.bin state_out
import sys
state = open(sys.argv[1], 'rb').read()
nv = open(sys.argv[2], 'rb').read()
parts = state.split(b'\x00')
i = parts.index(b'nvram')
raw = bytes.fromhex(parts[i + 1].decode())
head = len(raw) - len(nv)
assert head >= 0, (len(raw), len(nv))
parts[i + 1] = (raw[:head] + nv).hex().upper().encode()
open(sys.argv[3], 'wb').write(b'\x00'.join(parts))
print('header', head, 'bytes, nvram', len(nv), 'bytes replaced')
