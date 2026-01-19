from flask import Flask, request, jsonify
import os
from datetime import datetime

app = Flask(__name__)

UPLOAD_FOLDER = 'uploads'
if not os.path.exists(UPLOAD_FOLDER):
    os.makedirs(UPLOAD_FOLDER)

# 66-byte BMP Header for 16-bit RGB565 (Corrected for bit-mask alignment)
BMP_HEADER = bytes([
    0x42, 0x4D,             # Signature 'BM'
    0x42, 0x58, 0x02, 0x00, # Total File Size (153666 bytes)
    0x00, 0x00, 0x00, 0x00, 
    0x42, 0x00, 0x00, 0x00, # Offset to pixel data (66 bytes)
    0x28, 0x00, 0x00, 0x00, # Header Size (40 bytes)
    0x40, 0x01, 0x00, 0x00, # Width: 320
    0xF0, 0x00, 0x00, 0x00, # Height: 240
    0x01, 0x00,             # Planes
    0x10, 0x00,             # 16-bit
    0x03, 0x00, 0x00, 0x00, # BI_BITFIELDS (Compression 3 - Required for 5-6-5)
    0x00, 0x58, 0x02, 0x00, # Image Size (153600 bytes)
    0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00,
    # BITMASKS: These tell the PC exactly how to read the colors
    0x00, 0xF8, 0x00, 0x00, # Red Mask (Bits 11-15)
    0xE0, 0x07, 0x00, 0x00, # Green Mask (Bits 5-10)
    0x1F, 0x00, 0x00, 0x00  # Blue Mask (Bits 0-4)
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
