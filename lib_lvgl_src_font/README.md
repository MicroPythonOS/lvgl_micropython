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

3) copy target_font.ttf to lvgl_micropython/lib/lvgl/scripts/built_in_font/

4) Patch the built_in_font_gen.py file:

```
echo "Expanding builtin font to include diacritics 0x7F-0xFF, the Bitcoin B symbol 0x20BF, the italic satoshi symbol 0x4E2F and the regular satoshi symbol 0x4E30"
fontgen_file=lvgl_micropython/lib/lvgl/scripts/built_in_font/built_in_font_gen.py
sed -i.backup "s/default=\['0x20-0x7F,0xB0,0x2022'\],/default=\['0x20-0xFF,0xB0,0x2022,0x20BF,0x4E2F,0x4E30'\],/" "$fontgen_file"

syms += ",0xf002,0xf004,0xf005,0xf00e,0xf010,0xf029,0xf030" # search, heart, star, search-plus, search-minus, qrcode, camera
syms += ",0xf15a,0xf164,0xf165,0xf1e0" # btc (without circle), thumbs-up, thumbs-down, share-alt
syms += ",0xf2ea,0xf379,0xf58f" # undo-alt, bitcoin (in circle), headphones-alt
```

--- generate_all.py.orig        2026-04-25 19:57:19.436409434 +0200
+++ generate_all.py     2026-04-25 20:36:17.054599074 +0200
@@ -21,19 +21,19 @@
 os.system("./built_in_font_gen.py --size 18 -o lv_font_montserrat_18.c --bpp 4")
 
 print("\nGenerating 20 px")
-os.system("./built_in_font_gen.py --size 20 -o lv_font_montserrat_20.c --bpp 4")
+os.system("./built_in_font_gen.py --size 20 -o lv_font_montserrat_20.c --bpp 4 --compressed")
 
 print("\nGenerating 22 px")
 os.system("./built_in_font_gen.py --size 22 -o lv_font_montserrat_22.c --bpp 4")
 
 print("\nGenerating 24 px")
-os.system("./built_in_font_gen.py --size 24 -o lv_font_montserrat_24.c --bpp 4")
+os.system("./built_in_font_gen.py --size 24 -o lv_font_montserrat_24.c --bpp 4 --compressed")
 
 print("\nGenerating 26 px")
 os.system("./built_in_font_gen.py --size 26 -o lv_font_montserrat_26.c --bpp 4")
 
 print("\nGenerating 28 px")
-os.system("./built_in_font_gen.py --size 28 -o lv_font_montserrat_28.c --bpp 4")
+os.system("./built_in_font_gen.py --size 28 -o lv_font_montserrat_28.c --bpp 4 --compressed")
 
 print("\nGenerating 30 px")
 os.system("./built_in_font_gen.py --size 30 -o lv_font_montserrat_30.c --bpp 4")
@@ -42,7 +42,7 @@
 os.system("./built_in_font_gen.py --size 32 -o lv_font_montserrat_32.c --bpp 4")
 
 print("\nGenerating 34 px")
-os.system("./built_in_font_gen.py --size 34 -o lv_font_montserrat_34.c --bpp 4")
+os.system("./built_in_font_gen.py --size 34 -o lv_font_montserrat_34.c --bpp 4 --compressed")
 
 print("\nGenerating 36 px")
 os.system("./built_in_font_gen.py --size 36 -o lv_font_montserrat_36.c --bpp 4")
@@ -51,7 +51,7 @@
 os.system("./built_in_font_gen.py --size 38 -o lv_font_montserrat_38.c --bpp 4")
 
 print("\nGenerating 40 px")
-os.system("./built_in_font_gen.py --size 40 -o lv_font_montserrat_40.c --bpp 4")
+os.system("./built_in_font_gen.py --size 40 -o lv_font_montserrat_40.c --bpp 4 --compressed")
 
 print("\nGenerating 42 px")
 os.system("./built_in_font_gen.py --size 42 -o lv_font_montserrat_42.c --bpp 4")
@@ -63,7 +63,7 @@
 os.system("./built_in_font_gen.py --size 46 -o lv_font_montserrat_46.c --bpp 4")
 
 print("\nGenerating 48 px")
-os.system("./built_in_font_gen.py --size 48 -o lv_font_montserrat_48.c --bpp 4")
+os.system("./built_in_font_gen.py --size 48 -o lv_font_montserrat_48.c --bpp 4 --compressed")


5) Make sure you have https://github.com/lvgl/lv_font_conv installed on your path (needs npm so nodejs)

6) Run:

```
cd lvgl_micropython/lib/lvgl/scripts/built_in_font
python3 generate_all.py
```

7) Copy the generated files from lvgl_micropython/lib/lvgl/src/fonts/ to lvgl_micropython/lib_lvgl_src_fonts/

