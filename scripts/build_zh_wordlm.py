"""Build resources/charlm/zh_wordlm.bin from the jieba dictionary.

Usage: python -I scripts/build_zh_wordlm.py path/to/jieba/dict.txt [min_frequency]

The output is a word model used by lossy GBK recovery
(tools/encoding/mojibake/LossyGbkRecovery.cpp): a prefix tree of every jieba
word made only of GBK encodable CJK characters whose frequency reaches
min_frequency (default 3). Each node stores the jieba frequency of the word
ending there, or 0. No other corpus is used.

Binary layout (little endian):
  b"LTWT", u32 version (1), u32 node count,
  node count x (u16 code unit, u32 word frequency, u16 child count),
  in breadth-first order with the root (code unit 0) first and the children
  of each node sorted by code unit, so the children of all nodes at one depth
  follow each other in the order of their parents.
"""
import struct
import sys
from collections import deque


def gbk_cjk(word):
    if not all('一' <= c <= '鿿' for c in word):
        return False
    try:
        word.encode('gbk')
    except UnicodeEncodeError:
        return False
    return True


def main(path, output, min_frequency):
    root = [{}, 0]  # children by character, word frequency
    words = 0
    with open(path, encoding='utf-8') as f:
        for line in f:
            fields = line.split()
            if len(fields) < 2 or not gbk_cjk(fields[0]) or int(fields[1]) < min_frequency:
                continue
            node = root
            for c in fields[0]:
                node = node[0].setdefault(c, [{}, 0])
            node[1] += int(fields[1])
            words += 1
    records = []
    queue = deque([('\0', root)])
    while queue:
        c, node = queue.popleft()
        assert node[1] < 2**32 and len(node[0]) < 2**16
        records.append(struct.pack('<HIH', ord(c), node[1], len(node[0])))
        for child in sorted(node[0]):
            queue.append((child, node[0][child]))
    with open(output, 'wb') as f:
        f.write(b'LTWT' + struct.pack('<II', 1, len(records)))
        f.write(b''.join(records))
    print(f'{words} words, {len(records)} nodes -> {output}')


if __name__ == '__main__':
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    main(sys.argv[1], 'resources/charlm/zh_wordlm.bin', int(sys.argv[2]) if len(sys.argv) == 3 else 3)
