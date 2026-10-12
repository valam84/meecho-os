#!/usr/bin/env python3
# fbocr.py: прочитать текст консоли с экрана - вывод fbpeek обратно в буквы.
#
#   ssh плата 'fbpeek 0 0 160' | python3 fbocr.py sys/dev/wsfont/spleen8x16.h
#
# fbpeek печатает клетки 8x16 знаками '#'/'.'; этот скрипт берёт тот же
# шрифт, которым рисует драйвер (Spleen 8x16 из дерева), и узнаёт каждую
# клетку целиком. Курсор - клетка с обращёнными цветами - узнаётся по
# обращённому образу и печатается как '_' поверх буквы в отдельной строке.
# Незнакомая клетка - '?'. Зачем: без монитора это доказательство, что на
# экране именно этот текст, а не «что-то светится».
import re
import sys


def load_font(path):
    text = open(path, encoding="latin-1").read()
    body = text[text.index("_data[] = {"):]
    nums = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", body)]
    glyphs = {}
    first = 0x20
    for i in range(len(nums) // 16):
        g = tuple(nums[i * 16:(i + 1) * 16])
        glyphs.setdefault(g, chr(first + i))
    return glyphs


def main():
    glyphs = load_font(sys.argv[1])
    inverse = {tuple(b ^ 0xff for b in g): c for g, c in glyphs.items()}
    lines = [l.rstrip("\n") for l in sys.stdin if set(l.strip()) <= set("#.") and l.strip()]
    for r in range(0, len(lines) - 15, 16):
        block = lines[r:r + 16]
        width = min(len(l) for l in block) // 8
        out, cur = [], []
        for c in range(width):
            g = tuple(int(block[y][c * 8:c * 8 + 8].replace("#", "1").replace(".", "0"), 2)
                      for y in range(16))
            if g in glyphs:
                out.append(glyphs[g])
                cur.append(" ")
            elif g in inverse:
                out.append(inverse[g])
                cur.append("_")
            else:
                out.append("?")
                cur.append(" ")
        print("".join(out).rstrip())
        if "_" in cur:
            print("".join(cur).rstrip() + "   <- cursor")


main()
