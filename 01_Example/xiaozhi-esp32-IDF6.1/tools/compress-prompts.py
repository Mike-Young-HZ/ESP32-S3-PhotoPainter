"""Lossless PCM packing. Original PCM paths remain the authoritative inputs."""
from pathlib import Path
import struct
import zlib

root=Path(__file__).resolve().parents[1]/'components/port_bsp/pcm_src'
for name in ('mode.pcm','mode_1.pcm','mode_2.pcm','mode_3.pcm'):
    path=root/name
    raw=path.read_bytes()
    packed=struct.pack('<I',len(raw))+zlib.compress(raw,9)
    assert zlib.decompress(packed[4:])==raw
    output=path.with_suffix('.pcm.zlib')
    if not output.exists() or output.read_bytes()!=packed:
        output.write_bytes(packed)
    print(f'{name}: {len(raw)} -> {len(packed)} bytes; exact round-trip verified')
