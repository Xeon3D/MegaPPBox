"""Draw MegaPPBox's own toolbar icons into src/qt/icons (needs Pillow).

    python tools/icons/mkicons.py

Each icon is drawn at 256 px and reduced to the sizes the toolbar uses, so the
artwork can be regenerated rather than being a binary nobody can re-derive.
coin_1..4.ico and calibrate.ico come from PeepeeBox and are not drawn here.
"""
import os
from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(__file__), '..', '..', 'src', 'qt', 'icons')
SIZES = [(16, 16), (20, 20), (24, 24), (32, 32), (48, 48), (64, 64)]
N = 256


def save(img, name):
    img.save(os.path.join(OUT, name), sizes=SIZES)
    print('wrote', name)


def wrench():
    """Operator Setup: a spanner, the service engineer's tool."""
    im = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    steel, edge = (150, 160, 176, 255), (44, 50, 62, 255)
    # handle: a thick diagonal bar from bottom-left to the head
    d.line([(52, 204), (150, 106)], fill=edge, width=76)
    d.line([(52, 204), (150, 106)], fill=steel, width=52)
    d.ellipse([12, 164, 92, 244], fill=edge)
    d.ellipse([24, 176, 80, 232], fill=steel)
    # head: a disc with an open jaw toward the top-right
    d.ellipse([100, 18, 244, 162], fill=edge)
    d.ellipse([114, 32, 230, 148], fill=steel)
    d.polygon([(172, 90), (250, 12), (256, 70), (206, 124)], fill=(0, 0, 0, 0))
    d.polygon([(180, 90), (236, 34), (242, 62), (204, 106)], fill=(0, 0, 0, 0))
    # jaw faces
    d.line([(176, 88), (236, 28)], fill=edge, width=10)
    d.line([(206, 118), (250, 74)], fill=edge, width=10)
    # the head's highlight
    d.arc([132, 50, 212, 130], 200, 290, fill=(236, 240, 246, 255), width=8)
    return im


def library():
    """Machine Manager: a stack of disk images, one of them to be run."""
    im = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for i, (body, face) in enumerate([((58, 66, 82, 255), (104, 114, 134, 255)),
                                      ((58, 66, 82, 255), (120, 130, 150, 255)),
                                      ((58, 66, 82, 255), (140, 150, 170, 255))]):
        y = 150 - i * 52
        d.rounded_rectangle([20, y, 196, y + 64], radius=14, fill=body)
        d.rounded_rectangle([28, y + 8, 188, y + 44], radius=8, fill=face)
        d.ellipse([160, y + 48, 174, y + 60], fill=(96, 220, 96, 255) if i == 2 else (40, 44, 52, 255))
    # the play arrow: pick one and run it
    d.ellipse([140, 128, 252, 240], fill=(28, 120, 44, 255))
    d.ellipse([148, 136, 244, 232], fill=(52, 176, 72, 255))
    d.polygon([(180, 158), (180, 210), (224, 184)], fill=(255, 255, 255, 255))
    return im


save(wrench(), 'operator_setup.ico')
save(library(), 'machine_manager.ico')
