from flask import Flask, request, jsonify
import os
from datetime import datetime

app = Flask(__name__)

UPLOAD_FOLDER = 'uploads'
if not os.path.exists(UPLOAD_FOLDER):
    os.makedirs(UPLOAD_FOLDER)

# Standard 54-byte BMP Header for 320x240 RGB565
BMP_HEADER = bytes([
    0x42, 0x4D,             # Signature 'BM'
    0x36, 0x58, 0x02, 0x00, # File size (153654 bytes)
    0x00, 0x00, 0x00, 0x00, # Reserved
    0x36, 0x00, 0x00, 0x00, # Offset to pixel data (54 bytes)
    0x28, 0x00, 0x00, 0x00, # Header size (40 bytes)
    0x40, 0x01, 0x00, 0x00, # Width 320
    0xF0, 0x00, 0x00, 0x00, # Height 240
    0x01, 0x00,             # Planes
    0x10, 0x00,             # Bits per pixel (16)
    0x00, 0x00, 0x00, 0x00, # BI_RGB (No compression/masks)
    0x00, 0x58, 0x02, 0x00, # Image size
    0x00, 0x00, 0x00, 0x00, # X pixels per meter
    0x00, 0x00, 0x00, 0x00, # Y pixels per meter
    0x00, 0x00, 0x00, 0x00, # Colors used
    0x00, 0x00, 0x00, 0x00  # Important colors
])

@app.route('/upload', methods=['POST'])
def upload_image():
    try:
        image_data = request.data # Get raw data from ESP32
        
        if not image_data:
            return jsonify({'error': 'No data'}), 400
        
        # Save as .bmp and prepend the header so it is viewable
        filename = f"image_{datetime.now().strftime('%H%M%S')}.bmp"
        filepath = os.path.join(UPLOAD_FOLDER, filename)
        
        with open(filepath, 'wb') as f:
            f.write(BMP_HEADER) # Add the header first
            f.write(image_data) # Append the raw pixels
        
        print(f"Saved: {filename} ({len(image_data)} bytes)")
        return jsonify({'success': True}), 200
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
