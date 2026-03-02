The SenseCAP Indicator D1 Pro has a black screen because TFT_eSPI is the wrong library.
The display is an RGB panel driven by ESP32-S3 RGB interface, not SPI.

Fix:
1. Replace TFT_eSPI with Arduino_GFX (moononournation/Arduino_GFX) in platformio.ini
2. Known pins: BL=GPIO45, RGB data via parallel RGB interface, CS/RST via PCA9535 I2C expander at 0x20
3. Look at https://github.com/moononournation/Arduino_GFX/discussions/334 for reference
4. Rewrite the display init in ui.cpp/ui.h using Arduino_GFX + Arduino_ESP32RGBPanel
5. Keep LVGL integration working
6. Run: python3 -m platformio run
7. Fix ALL errors until SUCCESS
8. git add -A && git commit -m "Replace TFT_eSPI with Arduino_GFX RGB panel" && git push origin main
9. openclaw system event --text "Done: Arduino_GFX display driver ready" --mode now
