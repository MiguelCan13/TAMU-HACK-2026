from flask import Flask, request, jsonify
import os
from datetime import datetime

app = Flask(__name__)

UPLOAD_FOLDER = 'uploads'
if not os.path.exists(UPLOAD_FOLDER):
    os.makedirs(UPLOAD_FOLDER)

# Standard 54-byte BMP Header for 320x240 RGB565
BMP_HEADER = bytes([
    0x42, 0x4D, 0x36, 0x58, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 
    0x28, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x01, 0x00, 
    0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
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
