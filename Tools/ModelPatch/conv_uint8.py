"""Make the Omnilingual model load on the ONNX Runtime of UE 5.5 and 5.6.

model.int8.onnx stores the weights of its 8 ConvInteger nodes (the feature
extractor) as int8. ONNX Runtime 1.20, which UE 5.6 ships, only has the uint8
kernel ("Could not find an implementation for ConvInteger(10)"); 1.24 (UE 5.8)
has both. Adding 128 to the weights and to their zero point leaves
(w - w_zero_point) unchanged, so the outputs are bit-identical.

Every edit keeps the file's size and layout, so Get-Models.ps1 applies them
from a table without Python:
  type  one byte, the tensor's data_type: 3 (INT8) -> 2 (UINT8)
  xor   the weights' raw bytes, each XOR 0x80 (int8 v -> uint8 v + 128)
  zp    the zero point, int32_data [0] -> raw_data [128]: 2a 01 00 -> 4a 01 80

usage: python -I conv_uint8.py <model.int8.onnx> [<patched output>]
Prints the table for Get-Models.ps1 and the SHA-256 before and after. With an
output path, also writes the patched model there.
"""
import hashlib
import sys

import onnx


def varint(buf, i):
    value = shift = 0
    while True:
        b = buf[i]
        i += 1
        value |= (b & 0x7F) << shift
        shift += 7
        if b < 0x80:
            return value, i


def fields(buf):
    """Top-level fields of one serialized message: (field number, wire type, offset of the tag, offset of the payload, payload length)."""
    i = 0
    while i < len(buf):
        tag_at = i
        key, i = varint(buf, i)
        number, wire = key >> 3, key & 7
        if wire == 0:
            start = i
            _, i = varint(buf, i)
            yield number, wire, tag_at, start, i - start
        elif wire == 2:
            length, i = varint(buf, i)
            yield number, wire, tag_at, i, length
            i += length
        else:
            raise ValueError('unexpected wire type %d' % wire)


DATA_TYPE, INT32_DATA, RAW_DATA = 2, 5, 9


def edits_for(data, model):
    init = {t.name: t for t in model.graph.initializer}
    edits = []
    for node in model.graph.node:
        if node.op_type != 'ConvInteger':
            continue
        for name in (node.input[1], node.input[3]):
            serialized = init[name].SerializeToString()
            base = data.find(serialized)
            assert base >= 0 and data.find(serialized, base + 1) < 0, name
            found = {number: (tag_at, start, length) for number, _, tag_at, start, length in fields(serialized)}
            tag_at, start, length = found[DATA_TYPE]
            assert serialized[start:start + length] == b'\x03', name
            edits.append(('type', base + start, 1))
            if RAW_DATA in found:
                tag_at, start, length = found[RAW_DATA]
                edits.append(('xor', base + start, length))
            else:
                tag_at, start, length = found[INT32_DATA]
                assert serialized[tag_at:start + length] == b'\x2a\x01\x00', name
                edits.append(('zp', base + tag_at, 3))
    return sorted(edits, key=lambda e: e[1])


def apply(data, edits):
    out = bytearray(data)
    for kind, offset, length in edits:
        if kind == 'type':
            assert out[offset] == 3
            out[offset] = 2
        elif kind == 'xor':
            out[offset:offset + length] = bytes(b ^ 0x80 for b in out[offset:offset + length])
        else:
            assert out[offset:offset + 3] == b'\x2a\x01\x00'
            out[offset:offset + 3] = b'\x4a\x01\x80'
    return bytes(out)


def main():
    source = sys.argv[1]
    data = open(source, 'rb').read()
    edits = edits_for(data, onnx.load_from_string(data))
    patched = apply(data, edits)
    onnx.checker.check_model(onnx.load_from_string(patched))
    print('# %d edits' % len(edits))
    for kind, offset, length in edits:
        print("    @('%s', %d, %d)," % (kind, offset, length))
    print('before', hashlib.sha256(data).hexdigest())
    print('after ', hashlib.sha256(patched).hexdigest())
    if len(sys.argv) > 2:
        open(sys.argv[2], 'wb').write(patched)


if __name__ == '__main__':
    main()
