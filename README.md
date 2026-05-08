# Smart Desk Lighting Control with XIAO ESP32S3 Sense

This project is an embedded AI smart desk lighting system built with the Seeed Studio XIAO ESP32S3 Sense. The system uses the XIAO camera to classify the current desk scene and automatically changes the lighting mode of a cool-white / warm-white LED strip.

The final model classifies the desk into four states:

- `computer`
- `empty`
- `ipad`
- `phone`

Each class is mapped to a different lighting mode. For example, `computer` uses a cooler work light, `ipad` uses a brighter mixed light, `phone` uses a dim warm light, and `empty` turns the light off.

## Files

```text
src/
  xiao_camera_capture_server.ino   # XIAO camera server used for dataset collection
  xiao_collect.py                  # Python script for collecting images from XIAO
  xiao_desk_light_final.ino        # Final Arduino code for inference and light control

edge_impulse/
  ei-xiao_final-arduino-1.0.4-impulse-#1.zip   # Edge Impulse Arduino library

slides/
  Smart_Desk_Lighting_Demo_v2.pptx

report/
  final_report.pdf
  final_report.tex