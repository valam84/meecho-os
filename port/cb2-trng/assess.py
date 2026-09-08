#!/usr/bin/env python3
# Оценить поток байтов теми же мерами, что печатает ent(1). Работает на
# хосте, зависимостей нет.
#
#   python3 assess.py файл [ещё файл ...]
#
# Что считается и что это значит:
#
#   энтропия     бит на байт; 8.000 - предел. Меньше 7.99 на мегабайте уже
#                говорит о перекосе распределения.
#   хи-квадрат   256 корзин, 255 степеней свободы. Для случайного потока
#                значение около 255, а p сильно вне 0.01..0.99 - повод
#                смотреть дальше. Тест ОТВЕРГАЕТ, но не удостоверяет.
#   среднее      127.5 у равномерного.
#   корреляция   последовательная, соседних байтов; около нуля у случайного.
#   монобит      доля единиц среди всех битов; 0.5 у случайного.
#   Монте-Карло  оценка пи по парам координат; грубая, но ловит структуру.
#
# Ни одна из этих мер не доказывает, что числа годятся для криптографии.
# Они умеют только показать, что что-то не так, - и именно за этим нужны.

import math
import sys
from collections import Counter


def chi_square_p(chi2, df):
    """Верхний хвост распределения хи-квадрат, без внешних библиотек."""
    # Неполная гамма-функция Q(df/2, chi2/2) рядом/непрерывной дробью.
    a = df / 2.0
    x = chi2 / 2.0
    if x <= 0:
        return 1.0
    if x < a + 1.0:
        # Ряд для P(a, x), затем Q = 1 - P.
        term = 1.0 / a
        total = term
        n = a
        for _ in range(1000):
            n += 1.0
            term *= x / n
            total += term
            if abs(term) < abs(total) * 1e-15:
                break
        return 1.0 - total * math.exp(-x + a * math.log(x) - math.lgamma(a))
    # Непрерывная дробь для Q(a, x).
    tiny = 1e-300
    b = x + 1.0 - a
    c = 1.0 / tiny
    d = 1.0 / b
    h = d
    for i in range(1, 1000):
        an = -i * (i - a)
        b += 2.0
        d = an * d + b
        if abs(d) < tiny:
            d = tiny
        c = b + an / c
        if abs(c) < tiny:
            c = tiny
        d = 1.0 / d
        delta = d * c
        h *= delta
        if abs(delta - 1.0) < 1e-15:
            break
    return h * math.exp(-x + a * math.log(x) - math.lgamma(a))


def assess(path):
    with open(path, "rb") as f:
        data = f.read()
    n = len(data)
    if n < 1024:
        print("%s: слишком мало данных (%d байт)" % (path, n))
        return

    counts = Counter(data)
    expected = n / 256.0
    chi2 = sum((counts.get(b, 0) - expected) ** 2 / expected
               for b in range(256))
    p = chi_square_p(chi2, 255)

    entropy = 0.0
    for b in range(256):
        c = counts.get(b, 0)
        if c:
            pr = c / n
            entropy -= pr * math.log2(pr)

    mean = sum(data) / n

    # Последовательная корреляция.
    sx = sy = sxy = sx2 = sy2 = 0.0
    for i in range(n - 1):
        x = data[i]
        y = data[i + 1]
        sx += x
        sy += y
        sxy += x * y
        sx2 += x * x
        sy2 += y * y
    m = n - 1
    num = m * sxy - sx * sy
    den = math.sqrt(abs((m * sx2 - sx * sx) * (m * sy2 - sy * sy)))
    scc = num / den if den else 0.0

    ones = sum(bin(b).count("1") for b in data)
    monobit = ones / (n * 8.0)

    # Монте-Карло: пары по шесть байт на координату (как у ent).
    inside = total = 0
    step = 6
    for i in range(0, n - step + 1, step):
        x = int.from_bytes(data[i:i + 3], "big") / float(1 << 24)
        y = int.from_bytes(data[i + 3:i + 6], "big") / float(1 << 24)
        total += 1
        if x * x + y * y <= 1.0:
            inside += 1
    pi = 4.0 * inside / total if total else 0.0

    print("%s: %d байт" % (path, n))
    print("  энтропия     %.4f бит/байт (предел 8)" % entropy)
    print("  хи-квадрат   %.1f при 255 ст.св., p = %.4f" % (chi2, p))
    print("  среднее      %.3f (равномерное 127.5)" % mean)
    print("  корреляция   %+.5f (случайное около 0)" % scc)
    print("  монобит      %.5f (случайное 0.5)" % monobit)
    print("  Монте-Карло  пи = %.4f (ошибка %.2f%%)"
          % (pi, abs(pi - math.pi) / math.pi * 100.0))


def main():
    if len(sys.argv) < 2:
        print("usage: assess.py файл [ещё файл ...]", file=sys.stderr)
        return 2
    for path in sys.argv[1:]:
        assess(path)
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
