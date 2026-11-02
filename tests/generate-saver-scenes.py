#!/usr/bin/env python3
"""Generate deterministic desktop replay resources, never capture a real screen.

Requires Pillow and an installed monospace font. Assets, masks and the seeded
switch timeline are written only to the explicit output directory.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import random

from PIL import Image, ImageDraw, ImageFilter, ImageFont


def generate(output, width, height, seed):
    output.mkdir(parents=True, exist_ok=False)
    rng = random.Random(seed)
    fonts = [Path('/System/Library/Fonts/Menlo.ttc'),
             Path('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf')]
    font_path = next((p for p in fonts if p.is_file()), None)
    if font_path is None:
        raise SystemExit('Install Menlo or DejaVu Sans Mono before generating resources')
    font = lambda size: ImageFont.truetype(str(font_path), size)
    small, body, title = font(12), font(14), font(20)
    cjk_path = next((p for p in (Path('/System/Library/Fonts/STHeiti Light.ttc'),
                                Path('/System/Library/Fonts/Supplemental/Songti.ttc')) if p.is_file()), None)
    cjk = ImageFont.truetype(str(cjk_path), 15) if cjk_path else None
    sx, sy = width / 2040, height / 1228
    rect = lambda x, y, w, h: tuple(round(v) for v in (x * sx, y * sy, w * sx, h * sy))

    wallpaper = Image.new('RGB', (width, height))
    draw = ImageDraw.Draw(wallpaper)
    for y in range(height):
        t = y / height
        draw.line((0, y, width, y), fill=(round(26 + 24 * t), round(48 + 40 * t), round(77 + 48 * t)))
    cloud = Image.frombytes('L', (96, 64), rng.randbytes(96 * 64)).resize((width, height), Image.Resampling.BICUBIC)
    wallpaper = Image.blend(wallpaper, Image.merge('RGB', (cloud, cloud, cloud)), .20)
    grain = Image.frombytes('L', (width, height), rng.randbytes(width * height))
    wallpaper = Image.blend(wallpaper, Image.merge('RGB', (grain, grain, grain)), .035)
    d = ImageDraw.Draw(wallpaper)
    for i in range(12):
        x = round(width * (.03 + i * .083))
        y = round(height * (.35 + .20 * math.sin(i * .9)))
        d.line((x, y, x + round(width * .35), height), fill=(49 + i, 79 + i, 110 + i), width=2)
    d.rectangle((0, 0, width, 30), fill=(21, 29, 42))
    d.text((18, 7), 'DeskPort Synthetic Desktop    File    Edit    View    Window', font=small, fill='#eef0f4')
    d.text((width - 170, 7), 'Mon  12:34:56', font=small, fill='#eef0f4')

    scenes = []

    def window(bounds, caption, dark):
        image = wallpaper.copy()
        mask = Image.new('L', image.size)
        x, y, w, h = bounds
        shadow = Image.new('RGBA', image.size)
        ImageDraw.Draw(shadow).rounded_rectangle((x - 8, y + 6, x + w + 8, y + h + 18), radius=14, fill=(0, 0, 0, 120))
        image = Image.alpha_composite(image.convert('RGBA'), shadow.filter(ImageFilter.GaussianBlur(10))).convert('RGB')
        d, m = ImageDraw.Draw(image), ImageDraw.Draw(mask)
        d.rounded_rectangle((x, y, x + w, y + h), radius=10, fill='#20232a' if dark else '#f8f8f9', outline='#858b94')
        d.rounded_rectangle((x, y, x + w, y + 36), radius=10, fill='#353941' if dark else '#e4e6e9')
        for i, color in enumerate(('#ff6058', '#ffbd2e', '#28c840')):
            d.ellipse((x + 14 + i * 20, y + 12, x + 25 + i * 20, y + 23), fill=color)
        d.text((x + 100, y + 10), caption, font=small, fill='#e6e9ed' if dark else '#232730')
        return image, mask

    def text(image, mask, xy, value, fill, use_font=body):
        ImageDraw.Draw(image).text(xy, value, font=use_font, fill=fill)
        ImageDraw.Draw(mask).text(xy, value, font=use_font, fill=255)

    def save(name, image, mask, roi, kind):
        mask = mask.filter(ImageFilter.MaxFilter(3))
        image.save(output / (name + '.png'))
        mask.save(output / (name + '-text-mask.png'))
        scenes.append({'id': name, 'path': name + '.png', 'textMaskPath': name + '-text-mask.png',
                       'roi': list(roi), 'backgroundRoi': list(rect(12, 1080, 70, 100)), 'kind': kind})

    editor_bounds = rect(165, 85, 1740, 1060)
    x, y, w, h = editor_bounds
    editor, editor_mask = window(editor_bounds, 'sample-project / stream_policy.cpp - Editor', True)
    d = ImageDraw.Draw(editor)
    d.rectangle((x, y + 38, x + 215, y + h), fill='#252832')
    for i, value in enumerate(('EXPLORER', 'sample-project', '  src/', '    capture.cpp', '    stream_policy.cpp', '    transport.cpp', '  tests/', '    frame_test.cpp', '  README.md')):
        text(editor, editor_mask, (x + 16, y + 61 + i * 25), value, '#c9ced9', small)
    tokens = ('if (frame.changed) {', 'return encoder.submit(frame);', 'const auto now = clock.microseconds();',
              'queue.push(Packet{timestamp, bytes});', 'while (session.running()) {', 'budget.refill(now - previous);',
              'assert(output.sequence == expected);', '// Preserve sharp text after an application switch.')
    for line in range(max(1, (h - 100) // 18)):
        value = f'{line + 1:3d}  ' + '    ' * (line % 4) + tokens[line % len(tokens)]
        if line % 3 == 1:
            value += '  // sample_' + rng.randbytes(10).hex()
        text(editor, editor_mask, (x + 235, y + 65 + line * 18), value[:max(20, (w - 260) // 8)],
             ('#d7dae0', '#7acfa2', '#e8c682', '#8bb9ef')[line % 4], small)
    editor_roi = (x + 235, y + 62, w - 270, min(h - 100, 820))
    save('editor-dark', editor, editor_mask, editor_roi, 'dense-code')
    caret = editor.copy()
    ImageDraw.Draw(caret).rectangle((x + 282, y + 65, x + 283, y + 80), fill='#eeeeee')
    save('editor-caret', caret, editor_mask, editor_roi, 'small-caret-change')

    edited, edited_mask = editor.copy(), editor_mask.copy()
    ty = y + 65 + 2 * 18
    ImageDraw.Draw(edited).rectangle((x + 232, ty - 1, x + w - 10, ty + 17), fill='#20232a')
    ImageDraw.Draw(edited_mask).rectangle((x + 232, ty - 1, x + w - 10, ty + 17), fill=0)
    text(edited, edited_mask, (x + 235, ty), '  3      const auto now = clock.microseconds() + 17;', '#e8c682', small)
    save('editor-typed', edited, edited_mask, (x + 232, ty - 2, min(760, w - 260), 22), 'small-persistent-text-edit')

    popup, popup_mask = editor.copy(), editor_mask.copy()
    px, py, pw, ph = rect(625, 345, 800, 430)
    pd = ImageDraw.Draw(popup)
    pd.rounded_rectangle((px - 3, py - 3, px + pw + 3, py + ph + 3), radius=13, fill='#111923')
    pd.rounded_rectangle((px, py, px + pw, py + ph), radius=11, fill='#f5f7fa')
    ImageDraw.Draw(popup_mask).rectangle((px - 3, py - 3, px + pw + 3, py + ph + 3), fill=0)
    text(popup, popup_mask, (px + 26, py + 23), 'Connection details', '#202733', title)
    for i, value in enumerate(('Synthetic test window - no personal data',
                               'Frame size: 2040 x 1228   Scale: 1x',
                               'Profile: office   Video: HEVC',
                               'A quick brown fox jumps over the lazy dog.',
                               '0123456789  {} [] () <> == != += ->',
                               'Small text must recover after this change.',
                               'Check glyph edges, dark borders and background.')):
        text(popup, popup_mask, (px + 27, py + 82 + i * 31), value, '#28303a', body)
    if cjk:
        text(popup, popup_mask, (px + 27, py + ph - 66), '\u5207\u6362\u7a97\u53e3\u540e\uff0c\u6587\u5b57\u5e94\u5f53\u5feb\u901f\u6062\u590d\u6e05\u6670\u3002', '#28303a', cjk)
    save('editor-popup', popup, popup_mask, (px + 20, py + 20, pw - 40, ph - 40), 'small-window-overlay')

    bounds = rect(230, 122, 1560, 985)
    doc, doc_mask = window(bounds, 'Engineering notes - Document', False)
    x, y, w, h = bounds
    text(doc, doc_mask, (x + 48, y + 70), 'Remote desktop quality regression', '#1c2636', title)
    paragraphs = (
        'The encoder receives a deterministic desktop image at each timeline event.',
        'A reference image, a packet trace and a decoded image identify quality loss.',
        'The source contains fine text, flat regions, shadows and textured wallpaper.',
        'The sequence alternates quiet pauses, rapid switches and small caret updates.',
        'This document is generated test data. It is not a captured work application.',
        'Measure text recovery at each change and account for unchanged background.',
    )
    for line in range(max(1, (h - 150) // 24)):
        value = f'{line + 1:02d}. ' + paragraphs[line % len(paragraphs)]
        if cjk and line % 6 == 5:
            text(doc, doc_mask, (x + 48, y + 128 + line * 24), '\u672c\u573a\u666f\u4e3a\u81ea\u52a8\u751f\u6210\u7684\u6d4b\u8bd5\u8d44\u6e90\uff0c\u7528\u4e8e\u68c0\u67e5\u4e2d\u82f1\u6587\u5b57\u5f62\u548c\u7ec6\u8282\u7684\u6062\u590d\u901f\u5ea6\u3002', '#313943', cjk)
        else:
            text(doc, doc_mask, (x + 48, y + 128 + line * 24), value[:max(20, (w - 100) // 9)], '#313943', body)
    save('document-light', doc, doc_mask, (x + 40, y + 65, w - 80, h - 110), 'light-text-document')

    bounds = rect(95, 65, 1830, 1110)
    terminal, term_mask = window(bounds, 'Build output - Terminal', True)
    x, y, w, h = bounds
    for line in range(max(1, (h - 75) // 17)):
        digest = rng.randbytes(18).hex()
        value = f'[{line:04d}/2048] sample_{line % 17:02d}.cpp  checksum={digest}  time={line * 13 % 297:03d}ms  PASS'
        text(terminal, term_mask, (x + 18, y + 52 + line * 17), value[:max(20, (w - 40) // 8)],
             '#92e6b0' if line % 7 else '#f0d38a', small)
    save('terminal', terminal, term_mask, (x + 15, y + 48, w - 30, h - 80), 'dense-terminal')

    bounds = rect(330, 105, 1500, 1025)
    browser, browser_mask = window(bounds, 'Synthetic metrics - Browser', False)
    x, y, w, h = bounds
    text(browser, browser_mask, (x + 30, y + 66), 'Office dashboard', '#163148', title)
    bd = ImageDraw.Draw(browser)
    chart = (x + 35, y + 130, x + w - 35, y + 390)
    bd.rectangle(chart, fill='#e8eff7', outline='#c1ccda')
    for row in range(1, 8):
        yy = chart[1] + row * (chart[3] - chart[1]) // 8
        bd.line((chart[0], yy, chart[2], yy), fill='#c8d6e6')
    for channel, color in enumerate(('#2678b8', '#da6855', '#41a477')):
        points = [(chart[0] + j * (chart[2] - chart[0]) // 100,
                   int((chart[1] + chart[3]) / 2 + 60 * math.sin(j * .18 + channel) + rng.randrange(-12, 13))) for j in range(101)]
        bd.line(points, fill=color, width=2)
    for line in range(max(1, (h - 460) // 23)):
        value = f'row-{line:03d}  request={rng.randbytes(10).hex()}  latency={line * 7 % 91:02d}.25 ms  successful'
        text(browser, browser_mask, (x + 34, y + 425 + line * 23), value[:max(20, (w - 70) // 9)], '#28394b', body)
    save('browser', browser, browser_mask, (x + 25, y + 420, w - 50, h - 465), 'mixed-chart-and-text')
    desktop_mask = Image.new('L', wallpaper.size)
    exposed = wallpaper.copy()
    for i in range(12):
        text(exposed, desktop_mask, (55, 90 + 37 * i), f'Synthetic resource {i + 1:02d} - Desktop item', '#eef4ff', body)
    save('desktop', exposed, desktop_mask, (45, 80, 470, 470), 'exposed-textured-background')

    sequence = [(0, 'editor-dark'), (700, 'editor-caret'), (1100, 'editor-dark'),
                (1800, 'editor-caret'), (2700, 'editor-popup'), (6600, 'document-light'),
                (10150, 'terminal'), (10950, 'browser'), (11820, 'editor-dark'),
                (15750, 'document-light'), (16450, 'editor-popup'), (20300, 'desktop'),
                (21600, 'terminal'), (24700, 'document-light'), (27900, 'browser'),
                (30500, 'editor-dark'), (31800, 'editor-typed'), (34150, 'editor-popup'), (38600, 'document-light')]
    events = []
    for i, (at, name) in enumerate(sequence):
        at = at + (rng.randrange(-90, 91) if i > 3 else 0)
        events.append({'atMs': at, 'scene': name, 'activity': True,
                       'verify': i > 3, 'label': 'caret' if i in (1, 2, 3) else
                       'text-edit' if name == 'editor-typed' else 'window-switch'})
    timeline = {'version': 1, 'seed': seed, 'width': width, 'height': height,
                'durationMs': 42000, 'nominalFps': 60, 'codec': 'hevc_videotoolbox',
                'bitrateKbps': 992, 'burst': 3, 'scenes': scenes, 'events': events,
                'encoderNegotiatedKbps': 7462, 'ceilingPermille': 133,
                'network': {'packetSize': 896, 'fecPercentage': 20, 'acceptanceMinParity': 2},
                'quality': {'psnrDb': 35, 'textPsnrDb': 30, 'maxRecoveryMs': 750,
                            'maxProjectedMBPerHour': 300},
                'provenance': {'kind': 'deterministic generated images; no screen capture',
                               'font': font_path.name,
                               'cjkFont': cjk_path.name if cjk_path else None,
                               'cjkFontSha256': hashlib.sha256(cjk_path.read_bytes()).hexdigest() if cjk_path else None,
                               'fontSha256': hashlib.sha256(font_path.read_bytes()).hexdigest()}}
    for scene in scenes:
        scene['sha256'] = hashlib.sha256((output / scene['path']).read_bytes()).hexdigest()
    (output / 'timeline.json').write_text(json.dumps(timeline, indent=2) + '\n')
    print(output / 'timeline.json')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--width', type=int, default=2040)
    p.add_argument('--height', type=int, default=1228)
    p.add_argument('--seed', type=int, default=20261102)
    a = p.parse_args()
    if a.width < 640 or a.height < 360 or a.width % 4 or a.height % 4:
        p.error('Dimensions must be multiples of four and at least 640x360')
    generate(a.output.resolve(), a.width, a.height, a.seed)


if __name__ == '__main__':
    main()
