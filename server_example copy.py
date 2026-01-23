from flask import Flask, request, jsonify, render_template_string
import os
from datetime import datetime
from ultralytics import YOLO

app = Flask(__name__)

UPLOAD_FOLDER = 'actual_photos'
if not os.path.exists(UPLOAD_FOLDER):
    os.makedirs(UPLOAD_FOLDER)

# Parking lot state
parking_data = {
    'camera-triggered': "0",
    'lot_name': 'butt head',
    'total_spots': "67",
    'occupied_spots': "0",
    }

# Load YOLO model once at startup
model = YOLO("yolo11n.pt")

# 66-byte BMP Header for 16-bit RGB565 (Corrected for bit-mask alignment)
BMP_HEADER = bytes([
    0x42, 0x4D,             # Signature 'BM'
    0x42, 0x58, 0x02, 0x00, # Total File Size (153666 bytes)
    0x00, 0x00, 0x00, 0x00, 
    0x42, 0x00, 0x00, 0x00, # Offset to pixel data (66 bytes)
    0x28, 0x00, 0x00, 0x00, # Header Size (40 bytes)
    0xA0, 0x00, 0x00, 0x00, # Width: 320
    0x78, 0x00, 0x00, 0x00, # Height: 240
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
    global parking_data, TOTAL_SPOTS
    try:
        image_data = request.data # Get raw data from ESP32
        
        if not image_data:
            return jsonify({'error': 'No data'}), 400
        
        # Read metadata from headers
        parking_data['camera-triggered'] = request.headers.get('X-Camera-ID', 'Unknown')
        parking_data['lot_name'] = request.headers.get('X-Lot-Name', 'Unknown Lot')
        parking_data['total_spots'] = request.headers.get('X-Max-Spots', '20')
        parking_data['occupied_spots'] = request.headers.get('X-Available-Spots', '20')
                
        print(f"[CAMERA {parking_data['camera-triggered']}] Received image for {parking_data['lot_name']} (Max: {parking_data['total_spots']} spots)")
 
        # Save as .bmp and prepend the header so it is viewable
        filename = f"{parking_data['camera-triggered']}_{datetime.now().strftime('%H%M%S')}.bmp"
        filepath = os.path.join(UPLOAD_FOLDER, filename)
        
        with open(filepath, 'wb') as f:
            f.write(BMP_HEADER) # Add the header first
            f.write(image_data) # Append the raw pixels
        
        print(f"Saved: {filename} ({len(image_data)} bytes)")
        
        # Run YOLO detection
        results = model.predict(
            source=filepath,  # Use the saved image
            classes=[2,7,3],
            conf=0.25,
            save=False,
            show=False
        )

        num_cars = results[0].boxes.shape[0]
        print("[DETECTION] Number of cars detected:", num_cars, "\nfrom file:", filename)

        #return: 0 = no cars, 1 = car entered, 2 = car exited
        response_data = {
            'validation': 67
        }
        if(num_cars == 0):
            response_data['validation'] = 0
        elif(parking_data['camera-triggered'] == "1"):
            response_data['validation'] = 1
        elif(parking_data['camera-triggered'] == "2"):
            response_data['validation'] = 2
        
        return jsonify(response_data), 200
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

# Web dashboard route
@app.route('/')
def dashboard():
    html = '''
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>Texas A&M Parking Tracker</title>
        <style>
            :root {
                --tamu-maroon: #500000;
                --bg-gray: #f4f4f4;
                --text-dark: #333;
                --success-green: #28a745;
                --danger-red: #dc3545;
            }

            body {
                font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
                margin: 0;
                background-color: white;
                color: var(--text-dark);
            }

            /* Header Styles */
            header {
                background-color: var(--tamu-maroon);
                color: white;
                padding: 20px 10%;
                display: flex;
                justify-content: space-between;
                align-items: center;
            }

            .header-left h1 {
                margin: 0;
                font-size: 2.5rem;
            }

            .header-left p {
                margin: 5px 0 0;
                font-weight: 300;
            }

            .header-right {
                text-align: center;
            }

            .header-right .big-number {
                font-size: 3.5rem;
                font-weight: bold;
                display: block;
                line-height: 1;
            }

            .header-right span {
                font-size: 0.8rem;
                text-transform: uppercase;
            }

            /* Main Content */
            main {
                padding: 40px 10%;
            }

            h2 {
                color: var(--tamu-maroon);
                font-size: 2rem;
                margin-bottom: 30px;
            }

            /* Capacity Bar Section */
            .capacity-card {
                border: 2px solid var(--tamu-maroon);
                border-radius: 12px;
                padding: 30px;
                margin-bottom: 30px;
                position: relative;
            }

            .capacity-header {
                display: flex;
                justify-content: space-between;
                align-items: center;
                margin-bottom: 15px;
                font-weight: bold;
                font-size: 1.2rem;
            }

            .progress-container {
                background-color: #e9ecef;
                border-radius: 50px;
                height: 40px;
                width: 100%;
                overflow: hidden;
                position: relative;
            }

            .progress-bar {
                background-color: var(--tamu-maroon);
                height: 100%;
                width: 38%; /* Matches the 18/48 ratio */
            }

            .progress-text {
                position: absolute;
                width: 100%;
                text-align: center;
                top: 50%;
                transform: translateY(-50%);
                font-size: 0.9rem;
                color: var(--text-dark);
            }

            /* Stats Grid */
            .stats-grid {
                display: grid;
                grid-template-columns: repeat(3, 1fr);
                gap: 20px;
            }

            .stat-box {
                border: 2px solid;
                border-radius: 12px;
                padding: 20px;
                display: flex;
                justify-content: space-between;
                align-items: center;
            }

            .stat-info h3 {
                margin: 0;
                font-size: 0.9rem;
                font-weight: normal;
                color: #666;
            }

            .stat-info .stat-value {
                font-size: 2.2rem;
                font-weight: bold;
                display: block;
                margin-top: 5px;
            }

            .stat-icon {
                font-size: 2rem;
                opacity: 0.8;
            }

            /* Specific Colors for Stat Boxes */
            .total-box { border-color: var(--tamu-maroon); }
            .available-box { border-color: var(--success-green); color: var(--success-green); background-color: #f0fff4; }
            .occupied-box { border-color: var(--danger-red); color: var(--danger-red); background-color: #fff5f5; }

            /* Footer */
            footer {
                background-color: var(--tamu-maroon);
                color: white;
                text-align: center;
                padding: 15px;
                position: fixed;
                bottom: 0;
                width: 100%;
                font-size: 0.9rem;
            }

            /* Simple Responsive Layout */
            @media (max-width: 768px) {
                .stats-grid { grid-template-columns: 1fr; }
                header { flex-direction: column; text-align: center; }
                .header-right { margin-top: 20px; }
            }
        </style>
    </head>
    <body>

        <header>
            <div class="header-left">
                <h1>Texas A&M Parking</h1>
                <p>Real-time Lot Occupancy Tracker</p>
            </div>
            <div class="header-right">
                <span class="big-number">30</span>
                <span>Spots Available</span>
            </div>
        </header>

        <main>
            <h2>Lot 100 - Main Campus</h2>

            <div class="capacity-card">
                <div class="capacity-header">
                    <span>Lot Capacity</span>
                    <span>38%</span>
                </div>
                <div class="progress-container">
                    <div class="progress-bar"></div>
                    <div class="progress-text">18 / 48 spots filled</div>
                </div>
            </div>

            <div class="stats-grid">
                <div class="stat-box total-box">
                    <div class="stat-info">
                        <h3>Total Spots</h3>
                        <span class="stat-value" style="color: black;">48</span>
                    </div>
                    <div class="stat-icon">🚗</div>
                </div>

                <div class="stat-box available-box">
                    <div class="stat-info">
                        <h3>Available</h3>
                        <span class="stat-value">30</span>
                    </div>
                    <div class="stat-icon">✔️</div>
                </div>

                <div class="stat-box occupied-box">
                    <div class="stat-info">
                        <h3>Occupied</h3>
                        <span class="stat-value">18</span>
                    </div>
                    <div class="stat-icon">❌</div>
                </div>
            </div>
        </main>

        <footer>
            Gig 'em Aggies! | Texas A&M University Parking Services
        </footer>

    <script>
        function updateParkingUI(data) {
            const totalSpots = data.total_spots;
            const occupiedSpots = data.occupied_spots;
            const availableSpots = data.available_spots;
            const occupancyPercent = Math.round((occupiedSpots / totalSpots) * 100);

            // Update Lot Name
            document.querySelector('main h2').innerText = data.lot_name;

            // Update Text Numbers
            document.querySelector('.header-right .big-number').innerText = availableSpots;
            document.querySelector('.total-box .stat-value').innerText = totalSpots;
            document.querySelector('.available-box .stat-value').innerText = availableSpots;
            document.querySelector('.occupied-box .stat-value').innerText = occupiedSpots;
            
            // Update Progress Bar
            const progressBar = document.querySelector('.progress-bar');
            const progressText = document.querySelector('.progress-text');
            const percentLabel = document.querySelector('.capacity-header span:last-child');

            progressBar.style.width = occupancyPercent + "%";
            progressText.innerText = `${occupiedSpots} / ${totalSpots} spots filled`;
            percentLabel.innerText = occupancyPercent + "%";

            // Add smooth transition effect
            progressBar.style.transition = "width 0.5s ease-in-out";
        }

        function fetchData() {
            fetch('/api/status')
                .then(response => response.json())
                .then(data => {
                    updateParkingUI(data);
                })
                .catch(error => {
                    console.error('Error fetching parking data:', error);
                });
        }

        // Fetch data immediately on page load
        fetchData();

        // Auto-refresh every 5 seconds
        setInterval(fetchData, 5000);
    </script>
    </body>
    </html>
    '''
    return render_template_string(html)

# API endpoint for getting current parking status
@app.route('/api/status')
def get_status():
    total = int(parking_data.get('total_spots', 20))
    occupied = int(parking_data.get('occupied_spots', 0))
    available = total - occupied
    
    return jsonify({
        'lot_name': parking_data.get('lot_name', 'Unknown Lot'),
        'total_spots': total,
        'occupied_spots': occupied,
        'available_spots': available,
        'camera_triggered': parking_data.get('camera-triggered', 'Unknown')
    })

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
