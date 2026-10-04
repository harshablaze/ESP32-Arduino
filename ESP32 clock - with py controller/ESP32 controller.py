import io
import urllib.parse
from http.server import SimpleHTTPRequestHandler, HTTPServer

# Global internal states for your clock hardware configuration dashboard
clock_settings = {
    "brightness": 1,         # Value between 1 and 255
    "sleep": 0,              # 0 = Awake, 1 = Force Screen completely OFF
    "flip": 0,               # 0 = Normal Dark Background, 1 = Negative White Background
    "pixelShiftTest": 0,     # 0 = Normal Slow Shift, 1 = Fast-paced Shift Test Mode
    "glitch": 1,             # 0 = Disabled, 1 = Enabled Cyberpunk Glitch Effects
    "borderStyle": 0         # 0 = Solid Rounded Rect Box, 1 = Animated Crawling Dotted Border
}

class ClockControlHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        # 1. ESP32 JSON Endpoint: Serves target system configurations out to hardware
        if self.path == "/settings":
            self.send_response(200)
            self.send_header("Content-type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            
            import json
            response_data = json.dumps(clock_settings)
            self.wfile.write(bytes(response_data, "utf-8"))
            return

        # 2. Browser Dashboard Interface Home Endpoint (http://localhost:8080/)
        if self.path == "/" or self.path == "/index.html":
            self.send_response(200)
            self.send_header("Content-type", "text/html")
            self.end_headers()
            
            # Interactive control dashboard panel supporting the new parameters
            html_content = f"""
            <!DOCTYPE html>
            <html>
            <head>
                <title>OLED Clock Control Center</title>
                <meta name="viewport" content="width=device-width, initial-scale=1">
                <style>
                    body {{ background-color: #121212; color: #ffffff; font-family: -apple-system, sans-serif; text-align: center; padding: 20px 15px; }}
                    .card {{ background: #1e1e1e; padding: 25px; border-radius: 12px; display: inline-block; box-shadow: 0 4px 15px rgba(0,0,0,0.5); max-width: 340px; width: 100%; }}
                    h1 {{ margin-bottom: 5px; font-size: 22px; color: #00adb5; }}
                    p.status {{ color: #888; font-size: 13px; margin-bottom: 25px; }}
                    .control-group {{ margin-bottom: 22px; text-align: left; border-bottom: 1px solid #2a2a2a; padding-bottom: 15px; }}
                    .control-group:last-of-type {{ border-bottom: none; }}
                    label {{ font-weight: bold; display: block; margin-bottom: 8px; font-size: 14px; color: #eee; }}
                    .btn {{ display: block; width: 100%; padding: 12px; font-size: 14px; font-weight: bold; border: none; border-radius: 6px; cursor: pointer; color: white; transition: 0.2s; text-transform: uppercase; }}
                    .btn-off {{ background-color: #d9534f; }}
                    .btn-off:hover {{ background-color: #c9302c; }}
                    .btn-on {{ background-color: #5cb85c; }}
                    .btn-on:hover {{ background-color: #4cae4c; }}
                    .btn-flip {{ background-color: #f0ad4e; color: #121212; }}
                    .btn-flip:hover {{ background-color: #ec971f; }}
                    .btn-action {{ background-color: #00adb5; }}
                    .btn-action:hover {{ background-color: #008f96; }}
                    .btn-secondary {{ background-color: #555; }}
                    .btn-secondary:hover {{ background-color: #444; }}
                    input[type=range] {{ width: 100%; height: 8px; border-radius: 5px; background: #333; outline: none; }}
                </style>
            </head>
            <body>
                <div class="card">
                    <h1>OLED Clock Panel</h1>
                    <p class="status">Real-Time Settings Controller</p>
                    
                    <form action="/update" method="POST">
                        <!-- 1. BRIGHTNESS -->
                        <div class="control-group">
                            <label>Brightness Slider ({clock_settings['brightness']}/255):</label>
                            <input type="range" name="brightness" min="1" max="255" value="{clock_settings['brightness']}" onchange="this.form.submit()">
                        </div>
                        
                        <!-- 2. SCREEN SLEEP STATE -->
                        <div class="control-group">
                            <label>Screen Hardware Status:</label>
                            {"<button type='submit' name='sleep' value='1' class='btn btn-off'>TURN SCREEN OFF</button>" if clock_settings['sleep'] == 0 else "<button type='submit' name='sleep' value='0' class='btn btn-on'>WAKE SCREEN UP</button>"}
                        </div>

                        <!-- 3. INVERSION -->
                        <div class="control-group">
                            <label>Display Color Mode:</label>
                            {"<button type='submit' name='flip' value='1' class='btn btn-flip'>ACTIVATE NEGATIVE TEXT</button>" if clock_settings['flip'] == 0 else "<button type='submit' name='flip' value='0' class='btn btn-flip' style='background:#ffffff; color:#000;'>RESTORE DARK BACKGROUND</button>"}
                        </div>

                        <!-- 4. PIXEL SHIFT TEST -->
                        <div class="control-group">
                            <label>Pixel Shift Test Pacing:</label>
                            {"<button type='submit' name='pixelShiftTest' value='1' class='btn btn-action'>ENGAGE FAST SHIFT TEST (200ms)</button>" if clock_settings['pixelShiftTest'] == 0 else "<button type='submit' name='pixelShiftTest' value='0' class='btn btn-secondary'>RESTORE SLOW SHIFT (15 Min)</button>"}
                        </div>

                        <!-- 5. CYBERPUNK GLITCH -->
                        <div class="control-group">
                            <label>Cyberpunk Glitch Effect:</label>
                            {"<button type='submit' name='glitch' value='0' class='btn btn-off'>DISABLE GLITCH ANIMATION</button>" if clock_settings['glitch'] == 1 else "<button type='submit' name='glitch' value='1' class='btn btn-on'>ENABLE GLITCH ANIMATION</button>"}
                        </div>

                        <!-- 6. BORDER STYLE STYLE -->
                        <div class="control-group">
                            <label>Date Enclosure Border Style:</label>
                            {"<button type='submit' name='borderStyle' value='1' class='btn btn-action'>SWITCH TO DOTTED ANIMATION</button>" if clock_settings['borderStyle'] == 0 else "<button type='submit' name='borderStyle' value='0' class='btn btn-secondary'>SWITCH TO SOLID BORDER</button>"}
                        </div>
                    </form>
                </div>
            </body>
            </html>
            """
            self.wfile.write(bytes(html_content, "utf-8"))
            return

        return super().do_GET()

    def do_POST(self):
        global clock_settings
        if self.path == "/update":
            content_length = int(self.headers['Content-Length'])
            post_data = self.rfile.read(content_length).decode('utf-8')
            params = urllib.parse.parse_qs(post_data)

            # Update live tracking variables safely depending on form key inputs
            if 'brightness' in params:
                clock_settings['brightness'] = int(params['brightness'][0])
            if 'sleep' in params:
                clock_settings['sleep'] = int(params['sleep'][0])
            if 'flip' in params:
                clock_settings['flip'] = int(params['flip'][0])
            if 'pixelShiftTest' in params:
                clock_settings['pixelShiftTest'] = int(params['pixelShiftTest'][0])
            if 'glitch' in params:
                clock_settings['glitch'] = int(params['glitch'][0])
            if 'borderStyle' in params:
                clock_settings['borderStyle'] = int(params['borderStyle'][0])

            self.send_response(303)
            self.send_header('Location', '/')
            self.end_headers()

if __name__ == "__main__":
    server_address = ("", 8080)
    httpd = HTTPServer(server_address, ClockControlHandler)
    print("🚀 Separate Clock Configuration Server running on port 8080...")
    print("👉 Control your device from your PC browser at: http://localhost:8080/")
    httpd.serve_forever()
