import sys
from PIL import Image

for path in sys.argv[1:]:
    img = Image.open(path).convert("RGBA")
    bg = Image.new("RGBA", img.size, (0, 0, 0, 255))
    bg.alpha_composite(img)
    bg.convert("RGB").save(path)
    print("ok:", path)
