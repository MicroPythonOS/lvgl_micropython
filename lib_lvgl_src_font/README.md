Customized fonts to:
- add diacritics 0x7F-0xFF
- the Bitcoin B symbol 0x20BF
- the italic satoshi symbol 0x4E2F
- the regular satoshi symbol 0x4E30

Procedure:

1) get Montserrat-Medium_v9.ttf from Google Fonts

2) Open fontforge, New, "File" - "Run script" - Paste this script:

```
import fontforge

source = fontforge.open("Montserrat-Medium_v9.ttf")
target = fontforge.open("Montserrat-Medium_v9_plus.ttf")

# lightning bolt - not available in that source file
#source.selection.select(("unicode", None), 0x26a1)
#source.copy()
#target.selection.select(("unicode", None), 0x26a1)
#target.paste()

# italic satoshi symbol
source.selection.select(("unicode", None), 0x4e2f)
source.copy()
target.selection.select(("unicode", None), 0x4e2f)
target.paste()

# satoshi symbol
source.selection.select(("unicode", None), 0x4e30)
source.copy()
target.selection.select(("unicode", None), 0x4e30)
target.paste()


target.save("target_font.sfd")
target.generate("target_font.ttf")
```

3) copy target_font.ttf to MicroPythonOS/lvgl_micropython/lib/lvgl/scripts/built_in_font/

4) Patch the built_in_font_gen.py file:

```
echo "Expanding builtin font to include diacritics 0x7F-0xFF, the Bitcoin B symbol 0x20BF, the italic satoshi symbol 0x4E2F and the regular satoshi symbol 0x4E30"
fontgen_file=MicroPythonOS/lvgl_micropython/lib/lvgl/scripts/built_in_font/built_in_font_gen.py
sed -i.backup "s/default=\['0x20-0x7F,0xB0,0x2022'\],/default=\['0x20-0xFF,0xB0,0x2022,0x20BF,0x4E2F,0x4E30'\],/" "$fontgen_file"

Add this near the end:

# search, heart/love, zoom in, zoom out, QR code, camera, linux tux penguin, bitcoin without circle, feces/shit, bitcoin in circle, piggy bank, no dollar, peace
syms += ",0xf002,0xf004,0xf00e,0xf010,0xf029,0xf030,0xf15a,0xf17c,0xf2fe,0xf379,0xf4d3,0xf4e8,0xf67c"

```

5) Make sure you have https://github.com/lvgl/lv_font_conv installed on your path (needs npm so nodejs)

6) Run:

```
cd MicroPythonOS/lvgl_micropython/lib/lvgl/scripts/built_in_font
python3 generate_all.py
```

7) Copy the generated files from MicroPythonOS/lvgl_micropython/lib/lvgl/src/fonts/ to MicroPythonOS/lvgl_micropython/lib_lvgl_src_fonts/

