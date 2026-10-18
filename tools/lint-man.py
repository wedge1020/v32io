#!/usr/bin/env python3
"""A small checker for the manual pages of both components (man/v32io/man3
and man/v32dev/man3), for systems with no
mandoc or groff to run on them:

  * every page is either an alias (a single line ".so man3/<page>") that
    points to a page that exists, or has a .TH line with the title, the
    section 3v32, a date, "<component> <version>" (v32io in the v32io
    manual, v32dev in the v32dev one) and the manual name, followed
    by .SH NAME with a "name \\- description" line, and .SH SYNOPSIS,
    DESCRIPTION and SEE ALSO;
  * .nf/.fi, .RS/.RE pairs are balanced, and no font escape is left open;
  * the pages in SEE ALSO exist in the same manual (each component can be
    installed without the other) or are known Vircon32 pages;
  * no text line starts with a dot or an apostrophe by accident, and no
    line has trailing space-less control sequences left over.
  * with --compile DEVTOOLS: the example programs (the code blocks that
    start with #include) in the pages compile with the real compiler, with
    the headers of the component's library (lib/v32io or lib/v32dev, all
    of them in one folder as they are installed) and the Vircon32 headers
    of DEVTOOLS.

    tools/lint-man.py [--compile DEVTOOLS_DIR] [v32io | v32dev | all]
"""
import os, re, subprocess, sys, tempfile

args = sys.argv[1:]
devtools = None
if args[:1] == ['--compile']:
    devtools, args = args[1], args[2:]
which = args[0] if args else 'all'
if which not in ('v32io', 'v32dev', 'all'):
    sys.exit('usage: lint-man.py [--compile DEVTOOLS_DIR] [v32io | v32dev | all]')
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')

# pages of the Vircon32 manual that SEE ALSO may name
external = {'vircon32', 'input.h', 'video.h', 'time.h', 'misc.h', 'memcard.h',
            'string.h', 'compile', 'Vircon32'}

errors = []
total = 0
def err(page, msg, line=None):
    errors.append('%s%s: %s' % (page, ':%d' % line if line else '', msg))

def lint(comp):
    global total
    mandir = os.path.join(root, 'man', comp, 'man3')
    libdir = os.path.join(root, 'lib', comp)
    pages = sorted(f for f in os.listdir(mandir) if f.endswith('.3v32'))
    names = {f[:-5] for f in pages}
    requests = re.compile(r'^[.\'][A-Za-z]{1,3}( |$)')
    known = set('TH SH SS TP PP IP B I BR BI IR RI RB BR SB SM nf fi RS RE br sp so'.split())

    for page in pages:
        path = os.path.join(mandir, page)
        lines = open(path).read().split('\n')
        if lines and lines[-1] == '':
            lines.pop()

        # aliases
        if lines[0].startswith('.so '):
            if len(lines) != 1:
                err(page, 'an alias page must be just the .so line')
            m = re.match(r'^\.so man3/(.+)\.3v32$', lines[0])
            if not m or m.group(1) not in names:
                err(page, 'alias target does not exist: ' + lines[0])
            continue

        # the header of the page
        m = re.match(r'^\.TH (\S+) 3v32 "([A-Z][a-z]+ \d{1,2}, \d{4})" "' + comp + r' ([^"]+)" "Vircon32 C Library"$', lines[0])
        if not m:
            err(page, 'bad .TH line', 1)
        elif m.group(1).lower() != page[:-5].lower():
            err(page, 'the .TH title %s does not match the page name' % m.group(1), 1)

        text = '\n'.join(lines)
        for sec in ('NAME', 'SYNOPSIS', 'DESCRIPTION', 'SEE ALSO'):
            if '.SH %s' % sec not in text.replace('.SH "%s"' % sec, '.SH %s' % sec):
                err(page, 'missing section ' + sec)
        i = [k for k, l in enumerate(lines) if l == '.SH NAME']
        if i and not re.match(r'^\S+ \\- \S', lines[i[0] + 1] if i[0] + 1 < len(lines) else ''):
            err(page, 'NAME line should be "name \\- description"', i[0] + 2)

        # pairs
        nf = rs = 0
        for n, l in enumerate(lines, 1):
            if l == '.nf': nf += 1
            elif l == '.fi': nf -= 1
            elif l == '.RS': rs += 1
            elif l == '.RE': rs -= 1
            if nf < 0 or rs < 0:
                err(page, 'unbalanced .nf/.fi or .RS/.RE', n); nf = rs = 0
            if (l.startswith('.') or l.startswith("'")) and not l.startswith('.\\"'):
                m2 = re.match(r'^[.\']([A-Za-z]+)', l)
                if not m2 or m2.group(1) not in known:
                    err(page, 'unknown request: ' + l[:40], n)
            elif nf == 0 and l.strip() == '' :
                err(page, 'blank line outside .nf (use .PP)', n)
            # font escapes
            opened = len(re.findall(r'\\f[BIR]', l)) - len(re.findall(r'\\f[PR]', l))
            if opened > 0 and nf == 0:
                err(page, 'font escape left open', n)
            if re.search(r'(?<!\\)\\(?![-\\e(fc"&`\'.ae%|^ 0-9])', l):
                err(page, 'suspicious backslash escape: ' + l[:50], n)
        if nf != 0 or rs != 0:
            err(page, 'unclosed .nf or .RS at the end')

        # SEE ALSO entries
        m = re.search(r'\.SH SEE ALSO\n(.*?)(?:\n\.SH |\Z)', text, re.S)
        if m:
            for ref in re.findall(r'\.BR (\S+) \((\w+)\)', m.group(1)):
                if ref[1] == '3v32' and ref[0] not in names and ref[0] not in external:
                    err(page, 'SEE ALSO names a page that does not exist: %s(%s)' % ref)

    # the examples
    if devtools:
        compile_ = os.path.join(devtools, 'compile')
        include = os.path.join(devtools, 'include')
        count = 0
        for page in pages:
            if open(os.path.join(mandir, page)).readline().startswith('.so '):
                continue
            text = open(os.path.join(mandir, page)).read()
            for block in re.findall(r'\.RS\n\.nf\n(#include.*?)\n\.fi\n\.RE', text, re.S):
                count += 1
                with tempfile.TemporaryDirectory() as tmp:
                    for h in sorted(f for f in os.listdir(libdir) if f.endswith('.h')):
                        open(os.path.join(tmp, h), 'w').write(open(os.path.join(libdir, h)).read())
                    for h in os.listdir(include):
                        if not os.path.exists(os.path.join(tmp, h)):
                            os.symlink(os.path.join(include, h), os.path.join(tmp, h))
                    src = os.path.join(tmp, 'example.c')
                    code = block.replace('\\e', '\\').replace('\\-', '-')
                    open(src, 'w').write(code + '\n')
                    r = subprocess.run([compile_, src, '-o', os.path.join(tmp, 'example.asm')],
                                       cwd=tmp, capture_output=True, text=True)
                    if r.returncode != 0:
                        err(page, 'example %d does not compile:\n%s' % (count, (r.stdout + r.stderr).strip()))
        print('%s: %d example(s) compiled' % (comp, count))

    total += len(pages)
    print('%s: %d page(s) checked' % (comp, len(pages)))

for comp in (['v32io', 'v32dev'] if which == 'all' else [which]):
    lint(comp)

for e in errors:
    print(e)
print('%d page(s) checked, %d problem(s)' % (total, len(errors)))
sys.exit(1 if errors else 0)
