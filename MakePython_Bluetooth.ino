#include <Arduino.h>
#include <BluetoothA2DPSource.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// SD Card pins
#define SD_CS    22
#define SPI_MOSI 23
#define SPI_MISO 19
#define SPI_SCK  18

// OLED pins
#define OLED_SDA 4
#define OLED_SCL 5
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Button pins
const int Pin_vol_up = 39;
const int Pin_vol_down = 36;
const int Pin_mute = 35;
const int Pin_previous = 15;
const int Pin_pause = 33;
const int Pin_next = 2;

// Bluetooth device name
const char* EARPHONE_NAME = "OPPO Enco M32";

// History file on SD card
const char* HISTORY_FILE = "/playback.dat";

// App screens
enum Screen {
    SCREEN_CONNECTING,
    SCREEN_PLAYING,
    SCREEN_BROWSER
};
Screen currentScreen = SCREEN_CONNECTING;

// Bluetooth
BluetoothA2DPSource a2dp_source;
bool bt_connected = false;

// File management
#define MAX_FILES 100
String file_list[MAX_FILES];
int file_num = 0;
int file_index = 0;
int browser_index = 0;      // Currently highlighted file in browser
int browser_scroll = 0;     // Scroll offset for browser
File audioFile;

// WAV data tracking
uint32_t dataSize = 0;
uint32_t dataStart = 0;
uint32_t bytesPlayed = 0;

// Music info
struct MusicInfo {
    String name;
    String filename;
    uint32_t length;        // Duration in seconds
    uint32_t position;      // Current position in seconds
    uint32_t bytePosition;  // For precise resume
    bool playing;
    bool paused;
} music_info = {"", "", 0, 0, 0, false, false};

// Playback history
struct PlaybackHistory {
    String lastFile;
    uint32_t lastPosition;  // Byte position for resume
} history;

// Timing
uint32_t lastDisplayUpdate = 0;
uint32_t lastButtonCheck = 0;
uint32_t lastHistorySave = 0;

// Function declarations
void display_connecting();
void display_playing();
void display_browser();
bool openWavFile(String filename, uint32_t resumePosition = 0);
void closeAudioFile();
int get_music_list(fs::FS &fs, const char *dirname, String wavlist[], int maxFiles);
void loadHistory();
void saveHistory();
int findFileIndex(String filename);

// A2DP audio data callback
int32_t get_audio_data(Frame *frames, int32_t frameCount) {
    if (!audioFile || music_info.paused || !music_info.playing || !bt_connected) {
        memset(frames, 0, frameCount * sizeof(Frame));
        return frameCount;
    }
    
    uint32_t remaining = dataSize - bytesPlayed;
    if (remaining == 0) {
        memset(frames, 0, frameCount * sizeof(Frame));
        music_info.playing = false;
        return frameCount;
    }
    
    size_t bytesToRead = frameCount * sizeof(Frame);
    if (bytesToRead > remaining) {
        bytesToRead = remaining;
    }
    
    size_t bytesRead = audioFile.read((uint8_t*)frames, bytesToRead);
    
    if (bytesRead < frameCount * sizeof(Frame)) {
        size_t framesRead = bytesRead / sizeof(Frame);
        memset(&frames[framesRead], 0, (frameCount - framesRead) * sizeof(Frame));
    }
    
    bytesPlayed += bytesRead;
    music_info.position = bytesPlayed / 176400;
    music_info.bytePosition = bytesPlayed;
    
    return frameCount;
}

// Connection state callback
void connection_state_changed(esp_a2d_connection_state_t state, void *ptr) {
    Serial.printf("Connection state: %d\n", state);
    
    if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        Serial.println("*** CONNECTED! ***");
        bt_connected = true;
        currentScreen = SCREEN_PLAYING;
        
        // Resume last played file if history exists
        if (history.lastFile.length() > 0) {
            int idx = findFileIndex(history.lastFile);
            if (idx >= 0) {
                file_index = idx;
                browser_index = idx;
                openWavFile(file_list[file_index], history.lastPosition);
                Serial.printf("Resuming: %s at position %d\n", history.lastFile.c_str(), history.lastPosition);
            } else {
                // File not found, start first file
                if (file_num > 0) {
                    openWavFile(file_list[0]);
                }
            }
        } else if (file_num > 0) {
            openWavFile(file_list[0]);
        }
    } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        Serial.println("*** DISCONNECTED ***");
        bt_connected = false;
        saveHistory();  // Save before disconnect
        currentScreen = SCREEN_CONNECTING;
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=== Bluetooth WAV Player ===");

    // Button pins
    pinMode(Pin_vol_up, INPUT);
    pinMode(Pin_vol_down, INPUT);
    pinMode(Pin_mute, INPUT_PULLUP);
    pinMode(Pin_previous, INPUT_PULLUP);
    pinMode(Pin_pause, INPUT_PULLUP);
    pinMode(Pin_next, INPUT_PULLUP);

    // OLED init
    Wire.begin(OLED_SDA, OLED_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 failed"));
        while (1);
    }
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("BT WAV Player");
    display.setCursor(0, 16);
    display.println("Initializing...");
    display.display();

    // SD Card init
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    
    if (!SD.begin(SD_CS, SPI)) {
        Serial.println("SD Card Failed");
        display.setCursor(0, 32);
        display.println("SD ERROR!");
        display.display();
        while (1);
    }
    Serial.println("SD OK");

    // Get WAV files
    file_num = get_music_list(SD, "/", file_list, MAX_FILES);
    Serial.printf("Found %d WAV files\n", file_num);

    if (file_num == 0) {
        display.setCursor(0, 32);
        display.println("No WAV files!");
        display.display();
        while (1);
    }

    // Load playback history
    loadHistory();

    // Setup Bluetooth
    Serial.println("Starting Bluetooth...");
    currentScreen = SCREEN_CONNECTING;
    
    a2dp_source.set_auto_reconnect(false);
    a2dp_source.set_reset_ble(true);
    a2dp_source.set_on_connection_state_changed(connection_state_changed);
    
    Serial.printf("Connecting to: %s\n", EARPHONE_NAME);
    a2dp_source.start(EARPHONE_NAME, get_audio_data);
    
    Serial.println("Setup complete");
}

void loop() {
    // Auto-advance to next track when current ends
    if (bt_connected && !music_info.playing && !music_info.paused && file_num > 0 && currentScreen == SCREEN_PLAYING) {
        delay(300);
        file_index = (file_index + 1) % file_num;
        browser_index = file_index;
        openWavFile(file_list[file_index]);
    }

    // Periodic history save (every 10 seconds while playing)
    if (bt_connected && music_info.playing && !music_info.paused) {
        if (millis() - lastHistorySave > 10000) {
            saveHistory();
            lastHistorySave = millis();
        }
    }

    // Update display
    if (millis() - lastDisplayUpdate > 200) {
        lastDisplayUpdate = millis();
        
        switch (currentScreen) {
            case SCREEN_CONNECTING:
                display_connecting();
                break;
            case SCREEN_PLAYING:
                display_playing();
                break;
            case SCREEN_BROWSER:
                display_browser();
                break;
        }
    }

    // Button handling
    if (millis() - lastButtonCheck > 180) {
        handleButtons();
    }
    
    delay(10);
}

void handleButtons() {
    bool buttonPressed = false;
    
    switch (currentScreen) {
        case SCREEN_CONNECTING:
            // No controls while connecting
            break;
            
        case SCREEN_PLAYING:
            // NEXT - next track
            if (digitalRead(Pin_next) == LOW) {
                file_index = (file_index + 1) % file_num;
                browser_index = file_index;
                openWavFile(file_list[file_index]);
                buttonPressed = true;
            }
            
            // PREVIOUS - previous track
            if (digitalRead(Pin_previous) == LOW) {
                // If more than 3 seconds in, restart current track
                if (music_info.position > 3) {
                    openWavFile(file_list[file_index]);
                } else {
                    file_index = (file_index - 1 + file_num) % file_num;
                    browser_index = file_index;
                    openWavFile(file_list[file_index]);
                }
                buttonPressed = true;
            }
            
            // PAUSE - play/pause
            if (digitalRead(Pin_pause) == LOW) {
                music_info.paused = !music_info.paused;
                if (music_info.paused) {
                    saveHistory();  // Save when pausing
                }
                buttonPressed = true;
            }
            
            // MUTE - open file browser
            if (digitalRead(Pin_mute) == LOW) {
                currentScreen = SCREEN_BROWSER;
                browser_index = file_index;
                browser_scroll = max(0, browser_index - 2);
                buttonPressed = true;
            }
            break;
            
        case SCREEN_BROWSER:
            // NEXT - scroll down
            if (digitalRead(Pin_next) == LOW) {
                browser_index = (browser_index + 1) % file_num;
                // Adjust scroll
                if (browser_index >= browser_scroll + 5) {
                    browser_scroll = browser_index - 4;
                }
                if (browser_index < browser_scroll) {
                    browser_scroll = browser_index;
                }
                buttonPressed = true;
            }
            
            // PREVIOUS - scroll up
            if (digitalRead(Pin_previous) == LOW) {
                browser_index = (browser_index - 1 + file_num) % file_num;
                // Adjust scroll
                if (browser_index < browser_scroll) {
                    browser_scroll = browser_index;
                }
                if (browser_index >= browser_scroll + 5) {
                    browser_scroll = browser_index - 4;
                }
                buttonPressed = true;
            }
            
            // PAUSE - select and play file
            if (digitalRead(Pin_pause) == LOW) {
                file_index = browser_index;
                openWavFile(file_list[file_index]);
                currentScreen = SCREEN_PLAYING;
                buttonPressed = true;
            }
            
            // MUTE - back to playing screen (without changing track)
            if (digitalRead(Pin_mute) == LOW) {
                currentScreen = SCREEN_PLAYING;
                buttonPressed = true;
            }
            break;
    }
    
    if (buttonPressed) {
        lastButtonCheck = millis();
    }
}

void display_connecting() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    // Title
    display.setCursor(0, 0);
    display.println("=== BT PLAYER ===");
    
    // Status
    display.setCursor(0, 16);
    display.println("Connecting to:");
    
    display.setCursor(0, 28);
    String devName = String(EARPHONE_NAME);
    if (devName.length() > 21) devName = devName.substring(0, 18) + "...";
    display.println(devName);
    
    // Animated dots
    display.setCursor(0, 44);
    static int dots = 0;
    dots = (dots + 1) % 4;
    display.print("Please wait");
    for (int i = 0; i < dots; i++) display.print(".");
    
    // File count
    display.setCursor(0, 56);
    display.printf("%d files ready", file_num);
    
    display.display();
}

void display_playing() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    // Header with BT status
    display.setCursor(0, 0);
    display.print("BT:");
    display.print(bt_connected ? "OK" : "--");
    display.setCursor(40, 0);
    display.printf("Track %d/%d", file_index + 1, file_num);
    
    // Separator line
    display.drawLine(0, 9, 127, 9, SSD1306_WHITE);
    
    // Song name (larger area)
    display.setCursor(0, 12);
    String songName = music_info.name;
    if (songName.length() > 21) {
        songName = songName.substring(0, 18) + "...";
    }
    display.println(songName);
    
    // Time display
    display.setCursor(0, 24);
    int mins = music_info.position / 60;
    int secs = music_info.position % 60;
    int totalMins = music_info.length / 60;
    int totalSecs = music_info.length % 60;
    display.printf("%d:%02d / %d:%02d", mins, secs, totalMins, totalSecs);
    
    // Progress bar
    int barY = 35;
    int barWidth = 120;
    int barHeight = 6;
    int progress = 0;
    if (music_info.length > 0) {
        progress = (music_info.position * barWidth) / music_info.length;
    }
    display.drawRect(4, barY, barWidth, barHeight, SSD1306_WHITE);
    display.fillRect(4, barY, progress, barHeight, SSD1306_WHITE);
    
    // Status icon and text
    display.setCursor(0, 46);
    if (music_info.paused) {
        display.print("|| PAUSED");
    } else if (music_info.playing) {
        display.print("> PLAYING");
    } else {
        display.print("  STOPPED");
    }
    
    // Controls hint
    display.setCursor(0, 56);
    display.print("<< PAUSE >>  [MENU]");
    
    display.display();
}

void display_browser() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    // Header
    display.setCursor(0, 0);
    display.println("=== FILE BROWSER ===");
    
    // File list (5 visible items)
    int y = 11;
    int visibleItems = 5;
    
    for (int i = 0; i < visibleItems && (browser_scroll + i) < file_num; i++) {
        int idx = browser_scroll + i;
        display.setCursor(0, y);
        
        // Highlight current selection
        if (idx == browser_index) {
            display.fillRect(0, y - 1, 128, 10, SSD1306_WHITE);
            display.setTextColor(SSD1306_BLACK);
        } else {
            display.setTextColor(SSD1306_WHITE);
        }
        
        // Show playing indicator
        if (idx == file_index && music_info.playing) {
            display.print("> ");
        } else {
            display.print("  ");
        }
        
        // File name (extract from path, remove extension)
        String fname = file_list[idx];
        int lastSlash = fname.lastIndexOf('/');
        int lastDot = fname.lastIndexOf('.');
        if (lastDot == -1) lastDot = fname.length();
        fname = fname.substring(lastSlash + 1, lastDot);
        
        if (fname.length() > 18) {
            fname = fname.substring(0, 15) + "...";
        }
        display.println(fname);
        
        display.setTextColor(SSD1306_WHITE);
        y += 10;
    }
    
    // Scroll indicator
    if (file_num > visibleItems) {
        int scrollBarHeight = 50;
        int scrollBarY = 11;
        int thumbHeight = max(10, scrollBarHeight * visibleItems / file_num);
        int thumbY = scrollBarY + (browser_scroll * (scrollBarHeight - thumbHeight)) / max(1, file_num - visibleItems);
        
        display.drawRect(124, scrollBarY, 3, scrollBarHeight, SSD1306_WHITE);
        display.fillRect(124, thumbY, 3, thumbHeight, SSD1306_WHITE);
    }
    
    // Controls hint
    display.setCursor(0, 56);
    display.print("^v:Nav OK:Play [BACK]");
    
    display.display();
}

bool openWavFile(String filename, uint32_t resumePosition) {
    closeAudioFile();
    
    Serial.printf("Opening: %s\n", filename.c_str());
    
    // Store filename
    music_info.filename = filename;
    
    // Extract display name
    int lastSlash = filename.lastIndexOf('/');
    int lastDot = filename.lastIndexOf('.');
    if (lastDot == -1) lastDot = filename.length();
    music_info.name = filename.substring(lastSlash + 1, lastDot);
    
    audioFile = SD.open(filename);
    if (!audioFile) {
        Serial.println("Failed to open file");
        return false;
    }
    
    // Read WAV header
    uint8_t header[44];
    if (audioFile.read(header, 44) != 44) {
        audioFile.close();
        return false;
    }
    
    if (header[0] != 'R' || header[1] != 'I' || header[2] != 'F' || header[3] != 'F') {
        audioFile.close();
        return false;
    }
    
    uint32_t sampleRate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
    Serial.printf("Sample rate: %d Hz\n", sampleRate);
    
    // Find data chunk
    audioFile.seek(12);
    char chunkId[4];
    uint32_t chunkSize;
    
    while (audioFile.available()) {
        audioFile.read((uint8_t*)chunkId, 4);
        audioFile.read((uint8_t*)&chunkSize, 4);
        
        if (strncmp(chunkId, "data", 4) == 0) {
            dataSize = chunkSize;
            dataStart = audioFile.position();
            break;
        }
        audioFile.seek(audioFile.position() + chunkSize);
    }
    
    if (dataSize == 0) {
        audioFile.close();
        return false;
    }
    
    // Calculate duration
    music_info.length = dataSize / 176400;
    
    // Handle resume position
    if (resumePosition > 0 && resumePosition < dataSize) {
        // Align to frame boundary (4 bytes per frame)
        resumePosition = (resumePosition / 4) * 4;
        audioFile.seek(dataStart + resumePosition);
        bytesPlayed = resumePosition;
        music_info.position = bytesPlayed / 176400;
        music_info.bytePosition = bytesPlayed;
        Serial.printf("Resuming at %d seconds\n", music_info.position);
    } else {
        bytesPlayed = 0;
        music_info.position = 0;
        music_info.bytePosition = 0;
    }
    
    music_info.playing = true;
    music_info.paused = false;
    
    // Update history
    history.lastFile = filename;
    history.lastPosition = bytesPlayed;
    
    Serial.println("File opened successfully");
    return true;
}

void closeAudioFile() {
    if (audioFile) {
        audioFile.close();
    }
    music_info.playing = false;
    bytesPlayed = 0;
    dataSize = 0;
}

int get_music_list(fs::FS &fs, const char *dirname, String wavlist[], int maxFiles) {
    Serial.printf("Scanning: %s\n", dirname);
    int count = 0;
    
    File root = fs.open(dirname);
    if (!root || !root.isDirectory()) return 0;
    
    File file = root.openNextFile();
    while (file && count < maxFiles) {
        if (!file.isDirectory()) {
            String filename = String(file.name());
            if (filename.endsWith(".wav") || filename.endsWith(".WAV")) {
                if (!filename.startsWith("/")) filename = "/" + filename;
                wavlist[count] = filename;
                Serial.printf("  [%d] %s\n", count + 1, filename.c_str());
                count++;
            }
        }
        file = root.openNextFile();
    }
    
    // Sort alphabetically (simple bubble sort)
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (wavlist[j] > wavlist[j + 1]) {
                String temp = wavlist[j];
                wavlist[j] = wavlist[j + 1];
                wavlist[j + 1] = temp;
            }
        }
    }
    
    return count;
}

int findFileIndex(String filename) {
    for (int i = 0; i < file_num; i++) {
        if (file_list[i] == filename) {
            return i;
        }
    }
    return -1;
}

void loadHistory() {
    history.lastFile = "";
    history.lastPosition = 0;
    
    if (!SD.exists(HISTORY_FILE)) {
        Serial.println("No history file found");
        return;
    }
    
    File file = SD.open(HISTORY_FILE, FILE_READ);
    if (!file) {
        Serial.println("Failed to open history file");
        return;
    }
    
    // Read file line by line
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        
        if (line.startsWith("file=")) {
            history.lastFile = line.substring(5);
        } else if (line.startsWith("pos=")) {
            history.lastPosition = line.substring(4).toInt();
        }
    }
    
    file.close();
    
    Serial.printf("Loaded history: %s at %d bytes\n", history.lastFile.c_str(), history.lastPosition);
}

void saveHistory() {
    if (!music_info.filename.length()) return;
    
    // Update history from current playback
    history.lastFile = music_info.filename;
    history.lastPosition = music_info.bytePosition;
    
    // Delete old file
    if (SD.exists(HISTORY_FILE)) {
        SD.remove(HISTORY_FILE);
    }
    
    File file = SD.open(HISTORY_FILE, FILE_WRITE);
    if (!file) {
        Serial.println("Failed to save history");
        return;
    }
    
    file.printf("file=%s\n", history.lastFile.c_str());
    file.printf("pos=%d\n", history.lastPosition);
    
    file.close();
    
    Serial.printf("Saved history: %s at %d\n", history.lastFile.c_str(), history.lastPosition);
}