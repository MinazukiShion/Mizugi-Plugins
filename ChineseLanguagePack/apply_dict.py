# Apply zh_dict.tsv (ID -> Chinese) to command.xml / resource.xml.
import re
import sys
import xml.sax.saxutils as sx

def load_dict(path):
    d = {}
    for line in open(path, encoding='utf-8'):
        line = line.rstrip('\n')
        if not line or '\t' not in line:
            continue
        k, v = line.split('\t', 1)
        d[k] = v.replace('\\n', '\n')
    return d

def process(xml_path, d):
    raw = open(xml_path, encoding='utf-8').read()
    n = [0]
    def repl(m):
        key = m.group(2)
        if key in d:
            n[0] += 1
            return m.group(1) + sx.escape(d[key]) + m.group(4)
        return m.group(0)
    raw = re.sub(r'(<string id="([^"]*)">)([^<]*)(</string>)', repl, raw)
    open(xml_path, 'w', encoding='utf-8').write(raw)
    return n[0]

if __name__ == '__main__':
    d = load_dict(r'D:\Mizuho\zh_dict.tsv')
    print('dict entries:', len(d))
    for p in sys.argv[1:]:
        print(p, '->', process(p, d), 'strings translated')
