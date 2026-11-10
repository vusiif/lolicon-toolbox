"""Build resources/charlm/zh_charlm.bin from the jieba dictionary.

Usage: python -I scripts/build_zh_charlm.py path/to/jieba/dict.txt

The output is a character model used by lossy GBK recovery
(tools/encoding/mojibake/LossyGbkRecovery.cpp). It stores, for every GBK
encodable CJK character found in the dictionary, the frequency-weighted count
of its occurrences, of words ending with it and of words starting with it, plus
the within-word adjacent character pairs whose weighted count reaches
PAIR_THRESHOLD. Every number is a sum of jieba word frequencies; no other
corpus is used.

Binary layout (little endian):
  b"LTCL", u32 version (1), u32 total occurrences, u32 total word starts,
  u32 n, n x (u16 code unit, u32 occurrences, u32 word ends, u32 word starts),
  sorted by code unit,
  u32 m, m x (u16 first index, u16 second index, u32 count), sorted.
"""
import struct
import sys

PAIR_THRESHOLD = 20


def gbk_cjk(word):
    for c in word:
        if not ('㐀' <= c <= '鿿'):
            return False
    try:
        word.encode('gbk')
    except UnicodeEncodeError:
        return False
    return True


def main(path, output):
    occurrences, ends, starts, pairs = {}, {}, {}, {}
    with open(path, encoding='utf-8') as f:
        for line in f:
            fields = line.split()
            if len(fields) < 2 or not gbk_cjk(fields[0]):
                continue
            word, frequency = fields[0], int(fields[1])
            for c in word:
                occurrences[c] = occurrences.get(c, 0) + frequency
            for a, b in zip(word, word[1:]):
                pairs[(a, b)] = pairs.get((a, b), 0) + frequency
            ends[word[-1]] = ends.get(word[-1], 0) + frequency
            starts[word[0]] = starts.get(word[0], 0) + frequency
    chars = sorted(occurrences)
    index = {c: i for i, c in enumerate(chars)}
    kept = sorted((index[a], index[b], n) for (a, b), n in pairs.items() if n >= PAIR_THRESHOLD)
    total, total_starts = sum(occurrences.values()), sum(starts.values())
    assert total < 2**32 and len(chars) < 2**16
    with open(output, 'wb') as f:
        f.write(b'LTCL' + struct.pack('<III', 1, total, total_starts))
        f.write(struct.pack('<I', len(chars)))
        for c in chars:
            f.write(struct.pack('<HIII', ord(c), occurrences[c], ends.get(c, 0), starts.get(c, 0)))
        f.write(struct.pack('<I', len(kept)))
        for a, b, n in kept:
            f.write(struct.pack('<HHI', a, b, n))
    print(f'{len(chars)} characters, {len(kept)} pairs -> {output}')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1], 'resources/charlm/zh_charlm.bin')
