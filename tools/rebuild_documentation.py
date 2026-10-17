#!/usr/bin/env python3
"""Build embedded documentation and linked PDFs from the reviewed UTF-8 sources.

Uses only Python's standard library. Output is deterministic; no fonts, browser,
TeX installation or PDF tools are required to build the game or regenerate docs.
"""
from pathlib import Path
import re
import struct
import textwrap
import zlib

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / 'assets/documentation'
MANUALS = {
    'earth': 'Corncob Redux - Earth Campaign',
    'other_worlds': 'Corncob Redux - Other Worlds Campaign',
    'hints': 'Corncob Redux - Strategy and Flying Hints',
    'mission_builder': 'Corncob Redux - Mission Builder',
}


def write_if_changed(path, data):
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def plain(text):
    # Menu help uses a backslash to highlight the next character. Manuals do not.
    return text.replace('\x18', 'Up').replace('\x19', 'Down')


def literal(text):
    encoded = text.encode('cp1252', errors='replace')
    return b'(' + encoded.replace(b'\\', b'\\\\').replace(b'(', b'\\(').replace(b')', b'\\)') + b')'


class PDF:
    def __init__(self):
        self.objects = [None]

    def reserve(self):
        self.objects.append(None)
        return len(self.objects) - 1

    def add(self, content):
        index = self.reserve()
        self.objects[index] = content
        return index

    def write(self, path, root, info):
        data = bytearray(b'%PDF-1.4\n%\xe2\xe3\xcf\xd3\n')
        offsets = [0]
        for index, content in enumerate(self.objects[1:], 1):
            assert content is not None
            offsets.append(len(data))
            data += f'{index} 0 obj\n'.encode() + content + b'\nendobj\n'
        position = len(data)
        data += f'xref\n0 {len(offsets)}\n0000000000 65535 f \n'.encode()
        for offset in offsets[1:]:
            data += f'{offset:010d} 00000 n \n'.encode()
        data += f'trailer\n<< /Size {len(offsets)} /Root {root} 0 R /Info {info} 0 R >>\nstartxref\n{position}\n%%EOF\n'.encode()
        write_if_changed(path, data)


def make_pdf(name, title, text):
    chapters = [section.strip() for section in text.split('\f')]
    while chapters and not chapters[-1]:
        chapters.pop()
    if name in ('earth', 'other_worlds'):
        chapters = chapters[1:]  # A generated linked contents replaces the original text contents.
    headings = [next((line.strip() for line in chapter.splitlines() if line.strip() and not line.startswith('[Historical')), 'Reference') for chapter in chapters]
    if name == "other_worlds":
        headings[1] = "Title and credits"
        headings[14] = "Defensive Flying and Bringing a Damaged Plane Home"
        headings[21] = "Using the Mission Builder"
    pdf = PDF()
    catalog, pages, outlines = pdf.reserve(), pdf.reserve(), pdf.reserve()
    font = pdf.add(b'<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding >>')
    bold = pdf.add(b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>')
    # Pages contain positioned text lines and optional internal chapter links.
    sheets = []
    contents = []
    def sheet(heading):
        result = {'lines': [(line, 17, 780 - index * 20, True) for index, line in enumerate(textwrap.wrap(heading, 53))], 'links': []}
        sheets.append(result)
        return result
    page = sheet(title)
    intro = [
        'Lost Engines - Corncob Redux',
        'Reviewed edition of the original game documentation.',
        'Controls and changed rules describe Redux defaults.',
        'Original credits, source tables and archival details are retained.',
        '', 'Contents - select a chapter to jump to it:',
    ]
    y = 744
    for line in intro:
        page['lines'].append((line, 9, y, False)); y -= 15
    for index, heading in enumerate(headings):
        if y < 65:
            page = sheet('Contents (continued)'); y = 744
        label = f'{index + 1:2}. {heading}'
        for line in textwrap.wrap(label, 83):
            page['lines'].append((line, 9, y, False)); page['links'].append((y, index)); y -= 15
    for index, chapter in enumerate(chapters):
        contents.append(len(sheets))
        page = sheet(headings[index]); y = 744
        lines = plain(chapter).splitlines()
        if lines and lines[0].strip() == headings[index]:
            lines = lines[1:]
        blank = 0
        for line in lines:
            line = line.expandtabs(4)
            if not line.strip():
                blank += 1
                if blank > 1: continue
            else: blank = 0
            if re.fullmatch(r'[-=]{3,}', line.strip()): continue
            for wrapped in textwrap.wrap(line, 86, replace_whitespace=False, drop_whitespace=False) or ['']:
                if y < 60:
                    page = sheet(headings[index] + ' (continued)'); y = 744
                page['lines'].append((wrapped, 9, y, False)); y -= 12
    page_ids = [pdf.reserve() for _ in sheets]
    for number, (page, page_id) in enumerate(zip(sheets, page_ids), 1):
        stream = bytearray()
        for line, size, y, is_bold in page['lines'] + [(f'Corncob Redux    |    {number} / {len(sheets)}', 8, 32, False)]:
            stream += f'BT /{"F2" if is_bold else "F1"} {size} Tf 40 {y} Td '.encode() + literal(line) + b' Tj ET\n'
        compressed = zlib.compress(bytes(stream), 9)
        stream_id = pdf.add(f'<< /Length {len(compressed)} /Filter /FlateDecode >>\nstream\n'.encode() + compressed + b'\nendstream')
        annotations = []
        for y, index in page['links']:
            target = page_ids[contents[index]]
            annotations.append(pdf.add(f'<< /Type /Annot /Subtype /Link /Rect [38 {y - 3} 555 {y + 12}] /Border [0 0 0] /Dest [{target} 0 R /Fit] >>'.encode()))
        refs = ' '.join(f'{item} 0 R' for item in annotations)
        pdf.objects[page_id] = f'<< /Type /Page /Parent {pages} 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 {font} 0 R /F2 {bold} 0 R >> >> /Contents {stream_id} 0 R /Annots [{refs}] >>'.encode()
    bookmark_ids = [pdf.reserve() for _ in headings]
    for index, (heading, bookmark_id) in enumerate(zip(headings, bookmark_ids)):
        previous = f'/Prev {bookmark_ids[index - 1]} 0 R' if index else ''
        following = f'/Next {bookmark_ids[index + 1]} 0 R' if index + 1 < len(headings) else ''
        pdf.objects[bookmark_id] = b'<< /Title ' + literal(heading) + f' /Parent {outlines} 0 R {previous} {following} /Dest [{page_ids[contents[index]]} 0 R /Fit] >>'.encode()
    pdf.objects[outlines] = f'<< /Type /Outlines /First {bookmark_ids[0]} 0 R /Last {bookmark_ids[-1]} 0 R /Count {len(bookmark_ids)} >>'.encode()
    pdf.objects[pages] = f'<< /Type /Pages /Count {len(page_ids)} /Kids [{" ".join(f"{item} 0 R" for item in page_ids)}] >>'.encode()
    pdf.objects[catalog] = f'<< /Type /Catalog /Pages {pages} 0 R /Outlines {outlines} 0 R /PageMode /UseOutlines >>'.encode()
    info = pdf.add(b'<< /Title ' + literal(title) + b' /Author (Lost Engines; original documentation by Kevin Stokes) /Creator (tools/rebuild_documentation.py) >>')
    pdf.write(DOCS / f'{name}.pdf', catalog, info)


def main():
    entries = []
    for name, title in MANUALS.items():
        text = (DOCS / f'{name}.txt').read_text()
        make_pdf(name, title, text)
        entries.append((f'{name}.pdf', (DOCS / f'{name}.pdf').read_bytes()))
    for name, key in {
        'earth': 'earth/corncob.doc', 'other_worlds': 'other_worlds/corncob.doc',
        'earth_menu_help': 'earth/moag.inf', 'other_worlds_menu_help': 'other_worlds/moag.inf',
        'hints': 'hints.txt', 'mission_builder': 'mbuilder.doc',
    }.items():
        entries.append((key, (DOCS / f'{name}.txt').read_text().encode('cp437', errors='replace')))
    pack = bytearray(b'CORNUI01' + struct.pack('<H', len(entries)))
    for name, data in entries:
        encoded = name.encode('ascii')
        pack += struct.pack('<BI', len(encoded), len(data)) + encoded + data
    write_if_changed(ROOT / 'assets/documentation.dat', pack)


if __name__ == '__main__':
    main()
