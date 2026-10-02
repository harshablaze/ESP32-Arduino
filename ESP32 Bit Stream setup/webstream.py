import os
import time
from http.server import SimpleHTTPRequestHandler, HTTPServer
from PIL import Image, ImageOps

WATCH_FOLDER = "."
TARGET_FILE = "play.gif"  # Change to play.jpg or play.gif as needed
OUTPUT_BIN = "stream.bin"
PREVIEW_PNG = "preview.png"

# Global memory to cache frames so we don't stress the hard drive
cached_frames_bin = []
cached_frames_png = []
frame_delays = []
total_frames = 0
last_loaded_file = ""
last_modified_time = 0

def load_and_process_media():
    global cached_frames_bin, cached_frames_png, frame_delays, total_frames, last_loaded_file, last_modified_time
    
    target_path = os.path.join(WATCH_FOLDER, TARGET_FILE)
    if not os.path.exists(target_path):
        return

    # Check if the file has changed or been swapped out
    current_mod_time = os.path.getmtime(target_path)
    if TARGET_FILE == last_loaded_file and current_mod_time == last_modified_time:
        return # File hasn't changed, keep using the cache

    print(f"🔄 Processing new file: {TARGET_FILE}...")
    
    # Reset caches
    cached_frames_bin = []
    cached_frames_png = []
    frame_delays = []
    
    try:
        img = Image.open(target_path)
        
        # Check if it's an animated GIF
        is_animated = getattr(img, "is_animated", False)
        total_frames = img.n_frames if is_animated else 1
        
        for frame_idx in range(total_frames):
            img.seek(frame_idx)
            
            # 1. Resize and fit to 128x64 pixels
            frame = img.convert("L")
            frame = ImageOps.fit(frame, (128, 64), Image.Resampling.LANCZOS)
            frame = frame.convert("1") # Dithered 1-bit Black & White
            
            # 2. Cache raw binary for ESP32
            cached_frames_bin.append(frame.tobytes())
            
            # 3. Cache PNG byte data for Browser (keeps it in RAM, no disk writes)
            import io
            png_buffer = io.BytesIO()
            frame.save(png_buffer, format="PNG")
            cached_frames_png.append(png_buffer.getvalue())
            
            # 4. Grab frame speed (default to 100ms if not specified)
            duration = img.info.get("duration", 100)
            if duration < 20: duration = 100 # Fix for broken GIF headers
            frame_delays.append(duration / 1000.0) # convert to seconds
            
        last_loaded_file = TARGET_FILE
        last_modified_time = current_mod_time
        print(f"✅ Successfully processed {total_frames} frame(s).")
        
    except Exception as e:
        print(f"❌ Error processing media: {e}")

def get_current_frame_index():
    if total_frames <= 1:
        return 0
    
    # Calculate loop position based on total animation length
    total_duration = sum(frame_delays)
    current_time = time.time() % total_duration
    
    elapsed = 0.0
    for idx, delay in enumerate(frame_delays):
        elapsed += delay
        if current_time <= elapsed:
            return idx
    return 0

class AnimatedStreamHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        load_and_process_media()
        current_idx = get_current_frame_index()

        # ESP32 Endpoint: Serves the binary chunk of the CURRENT frame
        if self.path == f"/{OUTPUT_BIN}":
            self.send_response(200)
            self.send_header("Content-type", "application/octet-stream")
            self.end_headers()
            if cached_frames_bin:
                self.wfile.write(cached_frames_bin[current_idx])
            return
            
        # Browser Preview Image Endpoint: Serves the PNG data of the CURRENT frame
        if self.path == f"/{PREVIEW_PNG}":
            self.send_response(200)
            self.send_header("Content-type", "image/png")
            self.end_headers()
            if cached_frames_png:
                self.wfile.write(cached_frames_png[current_idx])
            return

        # Browser Dashboard Home Endpoint (http://localhost:8080/)
        if self.path == "/" or self.path == "/index.html":
            # Simple HTML page with a fast auto-refreshing preview image (every 50ms)
            html_content = """
            <!DOCTYPE html>
            <html>
            <head>
                <title>ESP32 OLED Stream Monitor</title>
                <style>
                    body { background-color: #121212; color: #ffffff; font-family: sans-serif; text-align: center; padding-top: 50px; }
                    .oled-screen { 
                        border: 4px solid #333; padding: 10px; background-color: #000; 
                        display: inline-block; border-radius: 8px; margin-top: 20px;
                    }
                    img { 
                        width: 384px; height: 192px; 
                        image-rendering: pixelated; 
                        border: 1px solid #222;
                    }
                </style>
                <script>
                    // Rapidly poll the server for the current animated frame frame
                    setInterval(function(){
                        document.getElementById('preview').src = '/preview.png?t=' + Date.now();
                    }, 60); // ~15-20 FPS preview matching typical GIF speeds
                </script>
            </head>
            <body>
                <h1>ESP32 Live Stream Monitor</h1>
                <p>Playing: <strong>""" + TARGET_FILE + """</strong></p>
                <div class="oled-screen">
                    <img id="preview" src="/preview.png" alt="OLED Preview">
                </div>
                <p style="color: #888; font-size: 12px;">Simulated 128x64 Resolution</p>
            </body>
            </html>
            """
            self.send_response(200)
            self.send_header("Content-type", "text/html")
            self.end_headers()
            self.wfile.write(bytes(html_content, "utf-8"))
            return

        return super().do_GET()

if __name__ == "__main__":
    server_address = ("", 8080)
    httpd = HTTPServer(server_address, AnimatedStreamHandler)
    print("PC Stream Server running on port 8080...")
    print("👉 View your moving animation at: http://localhost:8080/")
    httpd.serve_forever()
