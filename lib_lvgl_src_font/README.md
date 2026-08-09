Customized Montserrat fonts for MicroPythonOS.

Compared to stock LVGL built-in fonts, these add:
- diacritics 0x7F-0xFF
- general punctuation: en/em dash 0x2013-0x2014, curly quotes
  0x2018-0x2019 and 0x201C-0x201D, ellipsis 0x2026
- the Bitcoin B symbol 0x20BF
- the italic satoshi symbol 0x4E2F
- the regular satoshi symbol 0x4E30
- extra FontAwesome symbols (search, heart, star, qrcode, camera, btc,
  thumbs-up/down, share-alt, undo-alt, bitcoin-circle, headphones-alt)

Source fonts (committed alongside the generated .c files):

- `Montserrat-Medium-v9.ttf` — Montserrat Medium from
  https://github.com/JulietaUla/Montserrat (v9). Natively contains the
  diacritics, the punctuation block, and the Bitcoin B symbol.
- `SatoshiSymbol.ttf` — tiny donor font carrying only the two satoshi
  symbols at 0x4E2F (italic) and 0x4E30 (regular), reconstructed to match
  the metrics of the glyphs previously merged in via fontforge (the
  original merged TTF was not preserved). If you have a donor font with
  the original satoshi artwork, drop it in as `SatoshiSymbol.ttf` and
  regenerate.

Regenerating:

1) Make sure `npx` is available (nodejs); the script runs
   lv_font_conv via `npx lv_font_conv`.

2) Run:

```
./regenerate_fonts.sh
```

This regenerates all `lv_font_montserrat_*.c` files in this directory
with the exact per-size options each file was originally built with
(bpp, compression, subpixel), taking Latin + punctuation + Bitcoin B
from Montserrat, the satoshi symbols from SatoshiSymbol.ttf, and the
symbol set from FontAwesome5-Solid+Brands+Regular.woff (shipped with
LVGL in lib/lvgl/scripts/built_in_font/).

Notes:

- `lv_font_montserrat_14_aligned.c` is intentionally not regenerated:
  it is an ASCII-only test font (range 0x20-0x7F,0xB0,0x2022).
- Each generated .c file embeds the full lv_font_conv command in its
  header comment (`Opts:`), so any single size can be reproduced or
  tweaked individually.
- The MicroPythonOS build copies `lib_lvgl_src_font/*` flat over
  `lib/lvgl/src/font/`, so everything in this directory must stay at
  the top level (no subdirectories).
