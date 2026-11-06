#!/usr/bin/env python
"""Check that every relative link/image in README.md and docs/index.html resolves in the tree,
that in-page #anchors exist, and that docs/flash/manifest.json has the keys ESP Web Tools needs.
Release artefacts (docs/firmware/*.bin, docs/firmware/regions/) are created by tools/make_site_release.ps1
and are allowed to be missing. Usage: python -I tools/check_site_links.py"""
import json, os, re, sys
from html.parser import HTMLParser
from urllib.parse import unquote

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RELEASE_OK = re.compile(r'^docs/firmware/(cydpod-v[^/]+-w328c\.bin|regions/?.*)$')
errors, checked = [], 0

class P(HTMLParser):
    def __init__(self):
        super().__init__(); self.refs = []; self.ids = set()
    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if 'id' in a: self.ids.add(a['id'])
        for k in ('href', 'src', 'srcset', 'manifest'):
            if k in a and a[k]:
                for part in (a[k].split(',') if k == 'srcset' else [a[k]]):
                    self.refs.append((tag, part.strip().split(' ')[0]))

def check(refs, base_file, ids):
    global checked
    for tag, ref in refs:
        if re.match(r'^(https?:|mailto:|data:|//)', ref) or not ref:
            continue
        if ref.startswith('#'):
            checked += 1
            if ref[1:] not in ids: errors.append(f'{base_file}: missing anchor {ref}')
            continue
        path = unquote(ref.split('#')[0].split('?')[0])
        full = os.path.normpath(os.path.join(os.path.dirname(os.path.join(root, base_file)), path))
        rel = os.path.relpath(full, root).replace(os.sep, '/')
        checked += 1
        if os.path.exists(full): continue
        if RELEASE_OK.match(rel): print(f'  note: {base_file}: {ref} is a release-time artefact'); continue
        errors.append(f'{base_file}: <{tag}> {ref} -> {rel} not found')

def unmark(text):
    # <!--rel:name-->value<!--/rel--> release markers: keep the value
    return re.sub(r'<!--rel:\w+-->(.*?)<!--/rel-->', lambda m: m.group(1), text, flags=re.S)

def html_refs(rel):
    p = P(); p.feed(unmark(open(os.path.join(root, rel), encoding='utf8').read())); return p.refs, p.ids

# index.html
refs, ids = html_refs('docs/index.html'); check(refs, 'docs/index.html', ids)
# README.md: markdown links/images + embedded HTML
md = unmark(open(os.path.join(root, 'README.md'), encoding='utf8').read())
md_nocode = re.sub(r'```.*?```', '', md, flags=re.S)
md_nocode = re.sub(r'`[^`\n]*`', '', md_nocode)
mdrefs = [('md', m) for m in re.findall(r'!?\[[^\]]*\]\(([^)\s]+)', md_nocode)]
p = P(); p.feed(md_nocode)
check(mdrefs + p.refs, 'README.md', set(re.findall(r'^#+\s*(.+)$', md, re.M)))

# manifest
mf = json.load(open(os.path.join(root, 'docs/flash/manifest.json'), encoding='utf8'))
b = mf['builds'][0]; part = b['parts'][0]
for ok, what in ((mf.get('name'), 'name'), (mf.get('version'), 'version'), (b.get('chipFamily') == 'ESP32', 'chipFamily'),
                 (len(b['parts']) == 1 and part.get('offset') == 0, 'single part at offset 0'),
                 (part.get('path', '').startswith('../firmware/'), 'part path')):
    checked += 1
    if not ok: errors.append(f'manifest.json: bad {what}')
print(f'checked {checked} references; manifest {mf["name"]} v{mf["version"]} {b["chipFamily"]} {part["path"]} @ {part["offset"]}')
if errors:
    print('\n'.join('FAIL ' + e for e in errors)); sys.exit(1)
print('PASS site links, anchors and manifest')
