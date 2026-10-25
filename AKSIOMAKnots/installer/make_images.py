"""
Wizard images of the installer, made from icons/aksioma_logo.png:
images/wizard_<w>x<h>.png (the large image on the Welcome and
Finished pages: the whole logo on its own background) and
images/small_<w>x<h>.png (the small image at the top of the other pages:
the knot on a rounded dark tile). Several sizes of each, so
that Setup can pick a sharp one for every DPI setting.
"""

from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
LOGO = HERE.parent / "icons" / "aksioma_logo.png"
OUT = HERE / "images"

# background of the logo (and of the large image)
BACKGROUND = (9, 9, 9)

# parts of the logo: the knot and the word AKSIOMA (with a little margin)
KNOT_BOX = (175, 10, 552, 361)
WORD_BOX = (39, 415, 696, 516)

# 100% size of the modern wizard images and the DPI scales provided
LARGE = (164, 314)
SMALL = (55, 58)
SCALES = (1.0, 1.25, 1.5, 2.0, 2.5)


def scaled(size, scale):
    return round(size[0] * scale), round(size[1] * scale)


def fit(image, width, height):
    """Scaled down (high quality) to fit width x height, aspect kept."""
    factor = min(width / image.width, height / image.height)
    return image.resize((max(1, round(image.width * factor)),
                         max(1, round(image.height * factor))),
                        Image.LANCZOS)


def transparent(image):
    """The light knot on its dark background as colour + alpha: the
    brightness above the background is the coverage."""
    rgba = Image.new("RGBA", image.size)
    source = image.load()
    target = rgba.load()

    for y in range(image.height):
        for x in range(image.width):
            r, g, b = source[x, y]
            peak = max(r, g, b)
            alpha = max(0.0, min(1.0, (peak - BACKGROUND[0]) / (255 - BACKGROUND[0])))

            if alpha <= 0.0:
                target[x, y] = (0, 0, 0, 0)
                continue

            # the colour before it was blended with the background
            def unblend(c):
                return round(max(0, min(255, (c - BACKGROUND[0] * (1 - alpha)) / alpha)))

            # full opacity for the body of the strands, soft edges
            target[x, y] = (unblend(r), unblend(g), unblend(b), round(255 * min(1.0, alpha * 1.6)))

    return rgba


def large_image(logo, size):
    width, height = size
    image = Image.new("RGB", size, BACKGROUND)

    knot = fit(logo.crop(KNOT_BOX), round(width * 0.74), height)
    word = fit(logo.crop(WORD_BOX), round(width * 0.70), height)

    gap = round(height * 0.05)
    top = round((height - (knot.height + gap + word.height)) * 0.42)

    image.paste(knot, ((width - knot.width) // 2, top))
    image.paste(word, ((width - word.width) // 2, top + knot.height + gap))

    return image


def small_image(knot, size):
    """The knot on a rounded dark tile, like the program's icon: it reads
    on the dark wizard and also on a light one (high-contrast themes turn
    the dark style off)."""
    width, height = size
    image = Image.new("RGBA", size, (0, 0, 0, 0))

    # square tile, drawn 4x larger for smooth corners
    side = min(width, height)
    big = side * 4
    tile = Image.new("RGBA", (big, big), (0, 0, 0, 0))

    ImageDraw.Draw(tile).rounded_rectangle(
        (0, 0, big - 1, big - 1), radius=round(big * 0.2), fill=BACKGROUND + (255,))

    tile = tile.resize((side, side), Image.LANCZOS)

    mark = fit(knot, round(side * 0.76), round(side * 0.76))

    # the knot's visual centre is a little below the middle of its box
    tile.alpha_composite(mark, ((side - mark.width) // 2, (side - mark.height) // 2 + round(side * 0.02)))

    image.alpha_composite(tile, ((width - side) // 2, (height - side) // 2))

    return image


def main():
    logo = Image.open(LOGO).convert("RGB")
    knot = transparent(logo.crop(KNOT_BOX))

    OUT.mkdir(exist_ok=True)

    for old in OUT.glob("*.png"):
        old.unlink()

    for scale in SCALES:
        size = scaled(LARGE, scale)
        large_image(logo, size).save(OUT / f"wizard_{size[0]}x{size[1]}.png", optimize=True)

        size = scaled(SMALL, scale)
        small_image(knot, size).save(OUT / f"small_{size[0]}x{size[1]}.png", optimize=True)

    for path in sorted(OUT.glob("*.png")):
        print(path.name)


if __name__ == "__main__":
    main()
