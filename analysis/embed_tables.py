"""Copies results/tables/<name>.md into README.md between  <!-- begin:name -->  and  <!-- end:name -->  so the README never holds hand-typed numbers."""
import os, re, sys
ROOT = os.path.join(os.path.dirname(__file__), '..')
readme = open(os.path.join(ROOT, 'README.md')).read()
def sub(m):
    name = m.group(1); body = open(os.path.join(ROOT, 'results', 'tables', name + '.md')).read().strip()
    return '<!-- begin:%s -->\n%s\n<!-- end:%s -->' % (name, body, name)
new = re.sub(r'<!-- begin:(\w+) -->.*?<!-- end:\1 -->', sub, readme, flags=re.S)
open(os.path.join(ROOT, 'README.md'), 'w').write(new)
print('README tables refreshed')
