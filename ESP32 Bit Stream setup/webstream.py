import os
import time
import random
import io
from http.server import SimpleHTTPRequestHandler, HTTPServer
from PIL import Image, ImageOps
import cv2  # OpenCV for seamless high-speed MP4 frame decoding

WATCH_FOLDER = "."
OUTPUT_BIN = "stream.bin"
PREVIEW_PNG = "preview.png"

# Allowed media formats
VALID_EXTENSIONS = ('.jpg', '.jpeg', '.png', '.bmp', '.gif', '.mp4', '.avi', '.mkv', '.mov')

# Global Playlist Engine State Registers
playlist = []
current_media_idx = -1
current_media_file = ""

# Track exactly when the current file started playing
media_start_time = 0.0
media_total_duration = 10.0 # Default minimum 10 seconds tracking fallback

# Global memory caches holding extracted bits to safeguard disk writes
cached_frames_bin = []
cached_frames_png = []
frame_delays = []
total_frames = 0

def discover_and_shuffle_media():
    """Scans directory for media files, ignoring script files, and shuffles them randomly."""
    global playlist, current_media_idx
    files = [f for f in os.listdir(WATCH_FOLDER) if os.path.isfile(os.path.join(WATCH_FOLDER, f))]
    
    media_files = []
    for f in files:
        ext = os.path.splitext(f)[1].lower() # FIXED: Added index [1] to pull extension string from tuple
        # Filter files cleanly based on requirements
        if ext in VALID_EXTENSIONS and not f.endswith(('.py', '.bat')):
            media_files.append(f)
            
    if not media_files:
        print("⚠️ No valid media formats detected in your current workspace directory.")
        playlist = []
        return

    # Randomly seed and shuffle the entire list array cleanly
    random.seed(time.time())
    random.shuffle(media_files)
    playlist = media_files
    current_media_idx = 0
    print(f"🎲 Shuffled playlist initialized with {len(playlist)} items.")
    print(f"📋 Queued Tracklist: {playlist}")

def load_and_process_media(filename):
    """Processes images, GIFs, and MP4 files into a standardized 128x64 black-and-white stream matrix."""
    global cached_frames_bin, cached_frames_png, frame_delays, total_frames, media_total_duration
    
    target_path = os.path.join(WATCH_FOLDER, filename)
    if not os.path.exists(target_path):
        return False

    print(f"🔄 Processing upcoming media asset: {filename}...")
    cached_frames_bin = []
    cached_frames_png = []
    frame_delays = []
    
    ext = os.path.splitext(filename)[1].lower() # FIXED: Added index [1] here as well

    # --- ROUTING LOGIC BRANCH A: HANDLE MP4/MOV VIDEO SELECTIONS ---
    if ext in ('.mp4', '.avi', '.mkv', '.mov'):
        try:
            cap = cv2.VideoCapture(target_path)
            fps = cap.get(cv2.CAP_PROP_FPS)
            if fps <= 0: fps = 20.0 # Standard defensive fallback calculation
            
            frame_delay_sec = 1.0 / fps
            frame_count = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
            
            # Compute actual natural runtime duration
            natural_duration = frame_count * frame_delay_sec
            media_total_duration = max(10.0, natural_duration) # Guarantee minimum 10 seconds rule
            
            while cap.isOpened():
                ret, frame = cap.read()
                if not ret:
                    break
                
                # Convert OpenCV BGR array directly into Pillow Image matrix
                rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                img = Image.fromarray(rgb_frame).convert("L")
                
                # Resize and fit to 128x64 dithered 1-bit Black & White matrix
                img = ImageOps.fit(img, (128, 64), Image.Resampling.LANCZOS).convert("1")
                
                cached_frames_bin.append(img.tobytes())
                
                png_buffer = io.BytesIO()
                img.save(png_buffer, format="PNG")
                cached_frames_png.append(png_buffer.getvalue())
                frame_delays.append(frame_delay_sec)
                
            cap.release()
            total_frames = len(cached_frames_bin)
            print(f"✅ Successfully converted MP4 clip: {total_frames} frames. Playback duration limit: {media_total_duration:.2f}s")
            return True
        except Exception as e:
            print(f"❌ Structural breakdown processing video file: {e}")
            return False

    # --- ROUTING LOGIC BRANCH B: HANDLE STANDARD IMAGES & ANIMS (GIF/PNG/JPG) ---
    try:
        img = Image.open(target_path)
        is_animated = getattr(img, "is_animated", False)
        total_frames = img.n_frames if is_animated else 1
        
        # Static asset handling defaults to 10 seconds threshold limit
        if not is_animated:
            media_total_duration = 10.0
        else:
            # Animation processing metrics tracking
            total_anim_time = 0.0
            for idx in range(total_frames):
                img.seek(idx)
                duration = img.info.get("duration", 100)
                if duration < 20: duration = 100
                total_anim_time += (duration / 1000.0)
            media_total_duration = max(10.0, total_anim_time) # Guarantee minimum 10 seconds rule

        for frame_idx in range(total_frames):
            img.seek(frame_idx)
            frame = img.convert("L")
            frame = ImageOps.fit(frame, (128, 64), Image.Resampling.LANCZOS).convert("1")
            
            cached_frames_bin.append(frame.tobytes())
            
            png_buffer = io.BytesIO()
            frame.save(png_buffer, format="PNG")
            cached_frames_png.append(png_buffer.getvalue())
            
            duration = img.info.get("duration", 100)
            if duration < 20: duration = 100
            frame_delays.append(duration / 1000.0)
            
        print(f"✅ Successfully processed image data block: {total_frames} frame(s). Playback duration limit: {media_total_duration:.2f}s")
        return True
    except Exception as e:
        print(f"❌ Structural breakdown processing photo/GIF data: {e}")
        return False

def update_playlist_state_machine():
    """Manages tracking times to hop smoothly between playlist queue items."""
    global playlist, current_media_idx, current_media_file, media_start_time, media_total_duration
    
    if not playlist:
        discover_and_shuffle_media()
        if not playlist:
            return

    now = time.time()
    
    # Check if it's time to cycle over to the next track entry frame list
    if current_media_file == "" or (now - media_start_time) >= media_total_duration:
        if current_media_file != "":
            print(f"⏰ Finished playing asset frame loop for tracking block. Advancing...")
            current_media_idx = (current_media_idx + 1) % len(playlist)
            
            # Shuffle playlist again when looping back around to keep it fresh
            if current_media_idx == 0:
                print("🔄 End of track rotation loop. Re-seeding deck arrays.")
                discover_and_shuffle_media()
                if not playlist: return

        current_media_file = playlist[current_media_idx]
        media_start_time = now
        
        # Load and decode into structural core storage layers
        success = load_and_process_media(current_media_file)
        if not success:
            # Skip corrupted parameters cleanly
            current_media_file = ""

def get_current_frame_index():
    """Tracks elapsed millisecond runtimes to deliver the exact matching frame index layout."""
    if total_frames <= 1:
        return 0
        
    total_duration = sum(frame_delays)
    elapsed_since_start = time.time() - media_start_time
    
    # Calculate loop cycle position matching timeline requirements
    current_time = elapsed_since_start % total_duration
    
    elapsed = 0.0
    for idx, delay in enumerate(frame_delays):
        elapsed += delay
        if current_time <= elapsed:
            return idx
    return 0

class AnimatedStreamHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        # Fire state machine check tick inside every coming web hook handler call
        update_playlist_state_machine()
        current_idx = get_current_frame_index()

        # ESP32 Endpoint structure path checking matches your source code configuration setup
        if self.path == f"/{OUTPUT_BIN}":
            self.send_response(200)
            self.send_header("Content-type", "application/octet-stream")
            self.end_headers()
            if cached_frames_bin and current_idx < len(cached_frames_bin):
                self.wfile.write(cached_frames_bin[current_idx])
            return
            
        # Monitor Preview Browser Endpoint asset alignment routing
        if self.path == f"/{PREVIEW_PNG}":
            self.send_response(200)
            self.send_header("Content-type", "image/png")
            self.end_headers()
            if cached_frames_png and current_idx < len(cached_frames_png):
                self.wfile.write(cached_frames_png[current_idx])
            return

        # Core Browser Dash Monitoring Screen View Layout Template
        if self.path == "/" or self.path == "/index.html":
            html_content = f"""
            <!DOCTYPE html>
            <html>
            <head>
                <title>ESP32 Playback Center Monitor</title>
                <style>
                    body {{ background-color: #121212; color: #ffffff; font-family: sans-serif; text-align: center; padding-top: 50px; }}
                    .oled-screen {{ border: 4px solid #333; padding: 10px; background-color: #000; display: inline-block; border-radius: 8px; margin-top: 20px; }}
                    img {{ width: 384px; height: 192px; image-rendering: pixelated; border: 1px solid #222; }}
                    .info {{ color: #00adb5; font-size: 18px; margin-top: 15px; font-weight: bold; }}
                </style>
                <script>
                    setInterval(function(){{
                        document.getElementById('preview').src = '/preview.png?t=' + Date.now();
                    }}, 60);
                </script>
            </head>
            <body>
                <h1>ESP32 Media Stream Playlist Dashboard</h1>
                <p>Current Active File Matrix Track Allocation Selector:</p>
                <div class="info">{{current_media_file if current_media_file else "SCANNING DIRECTORY ARRAY..."}}</div>
                <div class="oled-screen">
                    <img id="preview" src="/preview.png" alt="OLED Live Matrix Target Output Canvas Preview Monitor">
                </div>
                <p style="color: #888; font-size: 12px;">Automated 128x64 Binary Frame Buffer Decoupling Monitor Screen Engine</p>
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
    # Scan files array configuration definitions on engine deployment initializing setup routines
    discover_and_shuffle_media()
    
    server_address = ("", 8080)
    httpd = HTTPServer(server_address, AnimatedStreamHandler)
    print("PC Playback Playlist Engine deployed cleanly on network port 8080...")
    print("👉 Watch your randomized media slideshow run inside your computer browser at: http://localhost:8080/")
    httpd.serve_forever()
