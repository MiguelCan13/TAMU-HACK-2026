"""
Simple Python Flask server to receive images from ESP32
Install: pip install flask
Run: python server_example.py
"""

from flask import Flask, request, jsonify
import os
from datetime import datetime

app = Flask(__name__)

# Create uploads directory if it doesn't exist
UPLOAD_FOLDER = 'uploads'
if not os.path.exists(UPLOAD_FOLDER):
    os.makedirs(UPLOAD_FOLDER)

@app.route('/upload', methods=['POST'])
def upload_image():
    """Receive image from ESP32 and save it"""
    try:
        # Get the raw image data
        image_data = request.data
        
        if not image_data:
            return jsonify({'error': 'No image data received'}), 400
        
        # Get optional headers
        device_id = request.headers.get('X-Device-ID', 'unknown')
        timestamp = request.headers.get('X-Timestamp', 'unknown')
        
        # Generate filename with timestamp
        filename = f"image_{datetime.now().strftime('%Y%m%d_%H%M%S')}.jpg"
        filepath = os.path.join(UPLOAD_FOLDER, filename)
        
        # Save the image
        with open(filepath, 'wb') as f:
            f.write(image_data)
        
        print(f"Image received from {device_id}")
        print(f"Saved as: {filepath}")
        print(f"Size: {len(image_data)} bytes")
        
        return jsonify({
            'success': True,
            'filename': filename,
            'size': len(image_data),
            'message': 'Image uploaded successfully'
        }), 200
        
    except Exception as e:
        print(f"Error: {str(e)}")
        return jsonify({'error': str(e)}), 500

@app.route('/status', methods=['GET'])
def status():
    """Check if server is running"""
    return jsonify({
        'status': 'online',
        'upload_folder': UPLOAD_FOLDER
    })

if __name__ == '__main__':
    print("=" * 50)
    print("ESP32 Image Upload Server")
    print("=" * 50)
    print(f"Upload folder: {os.path.abspath(UPLOAD_FOLDER)}")
    print("Starting server on http://0.0.0.0:5000")
    print("Endpoint: POST /upload")
    print("=" * 50)
    
    # Run on all interfaces so ESP32 can connect
    app.run(host='0.0.0.0', port=5000, debug=True)
