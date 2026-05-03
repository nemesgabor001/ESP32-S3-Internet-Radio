/* INTERNET RADIO PROJECT - (ESP32-S3) Features: WebGUI, Skin Engine, Weather, Alarm, M3U Support, EQ, Spectrum/VU */
/* Created by: Gabor Nemes */

#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <HTTPClient.h>
#include "Audio.h"
#include "driver/rtc_io.h"
#include "time.h"
#include <WebServer.h>
#include <Preferences.h>
#include <vector>
#include <Update.h>
const char* ntpServer = "pool.ntp.org"; // NTP server for time sync
struct RadioStation { String name; String url; };
RadioStation stations[10] = { { "Dance Wave Retro", "http://dancewave.online/retrodance.mp3" }, { "Empty Slot 2", "" }, { "Empty Slot 3", "" }, { "Empty Slot 4", "" }, { "Empty Slot 5", "" }, { "Empty Slot 6", "" }, { "Empty Slot 7", "" }, { "Empty Slot 8", "" }, { "Empty Slot 9", "" }, { "Empty Slot 10", "" } };
std::vector<RadioStation> m3uTempList; // Temporary list for parsed M3U stations
struct Skin { uint16_t bgTop, bgBottom, textMain, textAccent, wifiSig, selMode, clk, hInfo, barLow, barMid, barHigh, volBar, almWarn; String hexTop, hexBottom, hexMain, hexAccent, hexWifi, hexSel, hexClk, hexHInfo, hexBarL, hexBarM, hexBarH, hexVol, hexAlm; };
Skin currentSkin; // Active UI color theme
#define I2S_BCK 15
#define I2S_DIN 16
#define I2S_LRC 17
#define PIN_A 4
#define PIN_B 5
#define PIN_K0 6
#define PIN_SW 7
#define TFT_BLK 1
int visualMode = 3; // 0: Off, 1: Spectrum, 2: VU, 3: Both
String owmKey = ""; // OpenWeatherMap API key
String owmCity = ""; // Target city for weather
float tempC = 0.0; // Current temperature
int weatherID = 800; // Weather condition ID
bool useCelsius = true;
int m3uCount = 0, currentStationIdx = 0, tempStationIdx = 0, mainVal = 5;
const uint8_t volCurve[] = { 0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 14, 17, 20, 23, 26, 30, 34, 38, 42, 46, 50, 55 }; // Non-linear volume curve
int gB = 0, gM = 0, gT = 0; // Equalizer gains: Bass, Mid, Treble
bool showSpectrum = true, isDimmed = false, forceRedraw = false;
unsigned long lastInteraction = 0, lastVolChange = 0;
volatile int encoderPos = 0;
String songTitle = "", lastDrawnSong = "INIT", lastDrawnStationName = "INIT";
int lastMain = -999, lastDrawnMode = -1, lastDrawnStationIdx = -1;
int barHeights[2] = { 0, 0 }, targetHeights[2] = { 0, 0 };
int alarmH = 7, alarmM = 0;
bool alarmActive = false, isAlarming = false, lastAlarmActive = false;
String lastDrawnTime = "";
int alarmVolume = 0;
unsigned long lastAlarmStep = 0, alarmStartMillis = 0;
String st_ssid = "", st_pass = "";
bool isAP = false; // Access Point mode flag
class LGFX_Config : public lgfx::LGFX_Device { lgfx::Panel_ST7789 _p; lgfx::Bus_SPI _b; public: LGFX_Config() { auto cB = _b.config(); cB.spi_host = SPI2_HOST; cB.pin_sclk = 12; cB.pin_mosi = 11; cB.pin_dc = 9; _b.config(cB); _p.setBus(&_b); auto cP = _p.config(); cP.pin_cs = 8; cP.pin_rst = 10; cP.panel_width = 240; cP.panel_height = 320; cP.bus_shared = true; _p.config(cP); setPanel(&_p); } };
LGFX_Config tft;
Audio audio;
WebServer server(80);
Preferences pref;
uint16_t hexTo565(String hex) { if (hex.startsWith("#")) hex.remove(0, 1); uint32_t rgb = strtoul(hex.c_str(), NULL, 16); return tft.color565((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF); }
void updateColors() { currentSkin.bgTop = hexTo565(currentSkin.hexTop); currentSkin.bgBottom = hexTo565(currentSkin.hexBottom); currentSkin.textMain = hexTo565(currentSkin.hexMain); currentSkin.textAccent = hexTo565(currentSkin.hexAccent); currentSkin.wifiSig = hexTo565(currentSkin.hexWifi); currentSkin.selMode = hexTo565(currentSkin.hexSel); currentSkin.clk = hexTo565(currentSkin.hexClk); currentSkin.hInfo = hexTo565(currentSkin.hexHInfo); currentSkin.barLow = hexTo565(currentSkin.hexBarL); currentSkin.barMid = hexTo565(currentSkin.hexBarM); currentSkin.barHigh = hexTo565(currentSkin.hexBarH); currentSkin.volBar = hexTo565(currentSkin.hexVol); currentSkin.almWarn = hexTo565(currentSkin.hexAlm); forceRedraw = true; }
void setBrightness(int duty) { ledcWrite(TFT_BLK, duty); }
void parseM3UPro(String playlistUrl) { HTTPClient http; http.begin(playlistUrl); http.setTimeout(3000); int httpCode = http.GET(); m3uTempList.clear(); String currentExtName = "Unknown Station"; if (httpCode == HTTP_CODE_OK) { WiFiClient* stream = http.getStreamPtr(); while (stream->available() && m3uTempList.size() < 30) { String line = stream->readStringUntil('\n'); line.trim(); if (line.startsWith("#EXTINF:")) { int commaIdx = line.indexOf(','); if (commaIdx != -1) currentExtName = line.substring(commaIdx + 1); } else if (line.startsWith("http")) { m3uTempList.push_back({ currentExtName, line }); currentExtName = "Unknown Station"; } } } http.end(); }
String parseM3U(String url) { if (!url.endsWith(".m3u") && !url.endsWith(".m3u8")) return url; HTTPClient http; http.begin(url); http.setTimeout(3000); int httpCode = http.GET(); String streamUrl = ""; if (httpCode == HTTP_CODE_OK) { WiFiClient* stream = http.getStreamPtr(); while (stream->available()) { String line = stream->readStringUntil('\n'); line.trim(); if (line.startsWith("http")) { streamUrl = line; break; } } } http.end(); return (streamUrl == "") ? url : streamUrl; }
void playStation(int idx) { if (isAP) return; songTitle = ""; String targetUrl = parseM3U(stations[idx].url); audio.connecttohost(targetUrl.c_str()); }
void saveSettings() { pref.begin("radio", false); pref.putInt("idx", currentStationIdx); pref.putInt("vol", mainVal); pref.putInt("bass", gB); pref.putInt("mid", gM); pref.putInt("treb", gT); pref.putBool("spec", showSpectrum); pref.putInt("almH", alarmH); pref.putInt("almM", alarmM); pref.putBool("almA", alarmActive); pref.putString("cTop", currentSkin.hexTop); pref.putString("cBot", currentSkin.hexBottom); pref.putString("cMain", currentSkin.hexMain); pref.putString("cAcc", currentSkin.hexAccent); pref.putString("cWifi", currentSkin.hexWifi); pref.putString("owmCity", owmCity); pref.putString("owmKey", owmKey); pref.putBool("useCelsius", useCelsius); pref.putString("cSel", currentSkin.hexSel); pref.putString("cClk", currentSkin.hexClk); pref.putString("cHInf", currentSkin.hexHInfo); pref.putString("cBarL", currentSkin.hexBarL); pref.putString("cBarM", currentSkin.hexBarM); pref.putString("cBarH", currentSkin.hexBarH); pref.putString("cVol", currentSkin.hexVol); pref.putString("cAlm", currentSkin.hexAlm); for (int i = 0; i < 10; i++) { pref.putString(("n" + String(i)).c_str(), stations[i].name); pref.putString(("u" + String(i)).c_str(), stations[i].url); } pref.end(); }
void loadSettings() { pref.begin("radio", true); currentStationIdx = pref.getInt("idx", 0); mainVal = pref.getInt("vol", 5); gB = pref.getInt("bass", 0); gM = pref.getInt("mid", 0); gT = pref.getInt("treb", 0); showSpectrum = pref.getBool("spec", true); alarmH = pref.getInt("almH", 7); alarmM = pref.getInt("almM", 0); alarmActive = pref.getBool("almA", false); st_ssid = pref.getString("ssid", ""); st_pass = pref.getString("pass", ""); owmCity = pref.getString("owmCity", "Budapest,HU"); owmKey = pref.getString("owmKey", ""); useCelsius = pref.getBool("useCelsius", true); currentSkin.hexTop = pref.getString("cTop", "#000000"); currentSkin.hexBottom = pref.getString("cBot", "#000000"); currentSkin.hexMain = pref.getString("cMain", "#FFFFFF"); currentSkin.hexAccent = pref.getString("cAcc", "#00FFFF"); currentSkin.hexWifi = pref.getString("cWifi", "#00FF00"); currentSkin.hexSel = pref.getString("cSel", "#0000FF"); currentSkin.hexClk = pref.getString("cClk", "#FFFFFF"); currentSkin.hexHInfo = pref.getString("cHInf", "#FFFFFF"); currentSkin.hexBarL = pref.getString("cBarL", "#00FF00"); currentSkin.hexBarM = pref.getString("cBarM", "#FFFF00"); currentSkin.hexBarH = pref.getString("cBarH", "#FF0000"); currentSkin.hexVol = pref.getString("cVol", "#00FFFF"); currentSkin.hexAlm = pref.getString("cAlm", "#FF0000"); updateColors(); for (int i = 0; i < 10; i++) { stations[i].name = pref.getString(("n" + String(i)).c_str(), stations[i].name); stations[i].url = pref.getString(("u" + String(i)).c_str(), stations[i].url); } pref.end(); tempStationIdx = currentStationIdx; lastAlarmActive = alarmActive; }
void goToSleep() { saveSettings(); audio.stopSong(); tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_WHITE); tft.drawCenterString("Power Off", 160, 100, &fonts::FreeSansBold12pt7b); uint64_t sleepTimeUS = 0; if (alarmActive) { struct tm ti; if (getLocalTime(&ti)) { long nowSec = ti.tm_hour * 3600 + ti.tm_min * 60 + ti.tm_sec; long alarmSec = alarmH * 3600 + alarmM * 60; long diffSec = (alarmSec > nowSec) ? (alarmSec - nowSec) : (86400 - nowSec + alarmSec); char buf[32]; snprintf(buf, sizeof(buf), "Alarm in %02d:%02d", (int)(diffSec / 3600), (int)((diffSec % 3600) / 60)); tft.setFont(&fonts::FreeSans9pt7b); tft.setTextColor(TFT_WHITE); tft.drawCenterString(buf, 160, 140); if (diffSec > 5) sleepTimeUS = (uint64_t)(diffSec - 5) * 1000000ULL; } } delay(5000); WiFi.disconnect(true); delay(100); tft.writeCommand(0x10); ledcWrite(TFT_BLK, 0); delay(50); rtc_gpio_hold_en((gpio_num_t)TFT_BLK); esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_K0, 0); if (sleepTimeUS > 0) esp_sleep_enable_timer_wakeup(sleepTimeUS); esp_deep_sleep_start(); }
void factoryReset() { tft.fillScreen(TFT_RED); tft.setTextColor(TFT_WHITE); tft.drawCenterString("FACTORY RESET", 160, 100, &fonts::FreeSansBold12pt7b); pref.begin("radio", false); pref.clear(); pref.end(); delay(3000); ESP.restart(); }
void handleRoot() { String h = "<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{background:#121212;color:orange;font-family:sans-serif;text-align:center;padding:10px;}.btn{background:#333;border:2px solid orange;color:#fff;padding:10px 15px;margin:5px;display:inline-block;text-decoration:none;border-radius:8px;}.card{background:#222; padding:15px; margin:10px auto; max-width:400px; border-radius:12px; border:1px solid cyan;}.step-btn {background: #333; border: 2px solid cyan; color: cyan; border-radius: 8px; display: inline-flex; align-items: center; justify-content: center; text-decoration: none; font-weight: bold; transition: 0.2s;}.step-btn:active { background: cyan; color: #222; }.alarm{border: 1px solid cyan; padding: 10px; margin: 10px auto; max-width: 400px; border-radius: 10px;}.wifi-sec{border: 1px solid #00FF00; padding: 10px; margin: 10px auto; max-width: 400px; border-radius: 10px;}input[type=range]{flex-grow:1; accent-color:cyan; cursor:pointer;}.setting-row{background:#222; padding:10px; margin:10px auto; max-width:400px; border-radius:12px; border:1px solid orange; color:white;}</style></head><body><h1>Internet Radio</h1><div><a href='/weather' class='btn' style='width:90px; border-color:#FFD700; color:#FFD700;'>WEATHER</a><a href='/stations' class='btn' style='width:90px; border-color:yellow; color:yellow;'>STATIONS</a><a href='/audio' class='btn' style='width:90px; border-color:cyan; color:cyan;'>AUDIO</a><a href='/skin' class='btn' style='width:90px; border-color:magenta; color:magenta;'>SKIN</a></div><br>"; if (showSpectrum) { h += "<div class='setting-row'><label>VISUAL MODE: </label><select onchange='updateVisual(this.value)' style='padding:5px; background:#333; color:white; border:1px solid orange; border-radius:5px;'><option value='1' " + String(visualMode == 1 ? "selected" : "") + ">Spectrum Only</option><option value='2' " + String(visualMode == 2 ? "selected" : "") + ">VU Meter Only</option><option value='3' " + String(visualMode == 3 ? "selected" : "") + ">BOTH (Spec + VU)</option></select></div>"; } h += "<h3>" + stations[currentStationIdx].name + "</h3><div class='card' style='border-color: cyan;'><div style='display:flex; justify-content:space-between; align-items:center; margin-bottom:10px;'><a href='/prev' class='step-btn' style='width:50px; height:35px;'> < </a><b style='color:cyan; font-size: 14px; letter-spacing:1px;'>VOLUME</b><a href='/next' class='step-btn' style='width:50px; height:35px;'> > </a></div><form action='/setvol'><input type='range' name='v' min='0' max='21' value='" + String(mainVal) + "' onchange='this.form.submit()' style='width:95%;'></form></div><div class='wifi-sec'><b>WIFI SETTINGS</b><br><small>Current: " + (WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "Disconnected") + "</small><form action='/setwifi' method='POST'>SSID: <select name='s' id='ssid_list'><option value='" + st_ssid + "'>" + st_ssid + "</option></select> <a href='/scan' target='_self' class='btn' style='padding:2px 5px;'>SCAN</a><br><br>Pass: <input type='password' name='p' id='pass_f' value='" + st_pass + "'><br><br><input type='submit' value='SAVE & RESTART' class='btn' style='border-color:#00FF00;'></form></div><div class='alarm'><b>ALARM</b><form action='/setalarm'>Time: <input type='number' name='h' min='0' max='23' value='" + String(alarmH) + "' style='width:50px'> : <input type='number' name='m' min='0' max='59' value='" + String(alarmM) + "' style='width:50px'><br><br><input type='checkbox' name='active' " + String(alarmActive ? "checked" : "") + "> Enabled <br><input type='submit' value='SAVE ALARM' class='btn' style='border-color:cyan;'></form></div><div style='text-align:center; margin-top:20px;'><button onclick='toggleVisual()' class='btn' style='width:130px; border-color:" + String(showSpectrum ? "#00FF00" : "#FF0000") + ";'>" + String(showSpectrum ? "VISUAL: ON" : "VISUAL: OFF") + "</button><a href='/update_ui' class='btn' style='width:130px; border-color:orange;'>OTA UPDATE</a><a href='/off' class='btn' style='width:130px; border-color:red; color:red;'>POWER OFF</a></div><script>function toggleVisual() { fetch('/togglespec').then(() => { window.location.reload(); }); } function updateVisual(val) { fetch('/setVisual?mode=' + val); } if(window.location.search.includes('scan=1')){ fetch('/scan_data').then(r=>r.json()).then(d=>{ var s=document.getElementById('ssid_list'); s.innerHTML=''; d.forEach(n=>{var o=document.createElement('option'); o.value=n; o.text=n; s.appendChild(o);}); }); }</script></body></html>"; server.send(200, "text/html", h); }
void handleUpdate() { String h = "<html><body style='background:#121212;color:orange;font-family:sans-serif;text-align:center;'><h1>Firmware Update</h1><form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><br><br><input type='submit' value='START UPDATE'></form></body></html>"; server.send(200, "text/html", h); }
void handleUpdateUpload() { HTTPUpload& upload = server.upload(); if (upload.status == UPLOAD_FILE_START) { if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial); } else if (upload.status == UPLOAD_FILE_WRITE) { if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial); } else if (upload.status == UPLOAD_FILE_END) { if (!Update.end(true)) Update.printError(Serial); } }
void handleStations() { String h = "<html><head><meta charset='utf-8'><style>body{background:#121212;color:yellow;font-family:sans-serif;text-align:center;}.btn{background:#333;border:2px solid yellow;color:#fff;padding:10px;margin:5px;display:inline-block;text-decoration:none;border-radius:8px;}table{width:100%;max-width:500px;margin:auto;border-collapse:collapse;}td{padding:8px;border-bottom:1px solid #444;}input[type=text]{width:90%;background:#222;color:#fff;border:1px solid #555;padding:5px;}</style></head><body><h1>Radio Stations</h1><a href='/' class='btn'>BACK TO RADIO</a><div style='background:#222; padding:15px; border-radius:10px; margin:10px auto; max-width:500px;'><h3>Import M3U</h3><form action='/scan_m3u'><input type='text' name='m3u_url' placeholder='http://.../playlist.m3u'><input type='submit' value='SCAN' class='btn' style='border-color:cyan;'></form><hr><form method='POST' action='/upload_m3u' enctype='multipart/form-data'><input type='file' name='f' accept='.m3u,.m3u8'><input type='submit' value='UPLOAD' class='btn' style='border-color:orange;'></form></div>"; if (m3uTempList.size() > 0) { h += "<div style='border:2px solid cyan; max-width:500px; margin:auto;'><h3>Found:</h3><table>"; for (size_t i = 0; i < m3uTempList.size(); i++) { h += "<tr><td>" + m3uTempList[i].name + "</td><td><form action='/apply_manual'><input type='hidden' name='u' value='" + m3uTempList[i].url + "'><input type='hidden' name='n' value='" + m3uTempList[i].name + "'><select name='s'>"; for (int s = 0; s < 10; s++) h += "<option value='" + String(s) + "'>Slot " + String(s + 1) + "</option>"; h += "</select><input type='submit' value='ADD' class='btn'></form></td></tr>"; } h += "</table><a href='/clear_list' class='btn' style='border-color:red;'>CLEAR LIST</a></div>"; } h += "<h3>Presets</h3><table>"; for (int i = 0; i < 10; i++) { h += "<tr><form action='/edit'><td>" + String(i + 1) + "</td><td><input type='text' name='name' value='" + stations[i].name + "'><br><input type='text' name='url' value='" + stations[i].url + "'></td><td><input type='hidden' name='id' value='" + String(i) + "'><input type='submit' value='SAVE' class='btn'></td></form></tr>"; } h += "</table></body></html>"; server.send(200, "text/html", h); }
void handleSkin() { String h = "<html><head><style>body{background:#121212;color:#eee;font-family:sans-serif;text-align:center;}.btn{background:#333;border:2px solid #555;color:#fff;padding:12px;margin:5px;display:inline-block;text-decoration:none;border-radius:8px;}.card{background:#222; border-radius:12px; padding:15px; margin:10px auto; max-width:400px; border:1px solid #444;}.row{display:flex; justify-content:space-between; margin:10px 0;}input[type=color]{width:50px; height:30px;}</style></head><body><h1>🎨 Skin Engine</h1><a href='/' class='btn' style='border-color:cyan;'>BACK TO RADIO</a><div class='card'><form action='/setskin'>"; auto row = [&](String label, String name, String val) { return "<div class='row'><span>" + label + "</span><input type='color' name='" + name + "' value='" + val + "'></div>"; }; h += row("Header Bar", "top", currentSkin.hexTop); h += row("Clock", "clk", currentSkin.hexClk); h += row("Header Info", "hinf", currentSkin.hexHInfo); h += row("Background", "bot", currentSkin.hexBottom); h += row("Main Text", "txt", currentSkin.hexMain); h += row("Accent Text", "acc", currentSkin.hexAccent); h += row("WiFi/Sig", "wifi", currentSkin.hexWifi); h += row("Selector", "sel", currentSkin.hexSel); h += "<input type='submit' value='APPLY & SAVE' class='btn' style='border-color:#00FF00; width:90%;'><br><a href='/defaultskin' class='btn' style='border-color:#FF0000; width:90%;'>RESET DEFAULT</a></form></div></body></html>"; server.send(200, "text/html", h); }
void handleWeatherPage() { String h = "<html><head><style>body{background:#121212;color:#FFD700;font-family:sans-serif;text-align:center;}.btn{background:#333;border:2px solid #FFD700;color:#fff;padding:12px;margin:5px;display:inline-block;text-decoration:none;border-radius:8px;}.card{background:#222; border-radius:12px; padding:15px; margin:10px auto; max-width:400px; border:1px solid #FFD700;}input[type=text], input[type=password]{width:90%; background:#333; color:#fff; border:1px solid #555; padding:8px; margin:5px 0;}</style></head><body><h1>🌤 Weather Settings</h1><a href='/' class='btn' style='border-color:cyan;'>BACK TO RADIO</a><div class='card'><form action='/setweather'><b>City, Country:</b><br><input type='text' name='city' value='" + owmCity + "'><br><b>OWM API Key:</b><br><input type='password' name='key' value='" + owmKey + "'><br><b>Unit:</b><br><input type='radio' name='u' value='C' " + String(useCelsius ? "checked" : "") + "> Celsius <input type='radio' name='u' value='F' " + String(!useCelsius ? "checked" : "") + "> Fahrenheit<br><br><input type='submit' value='APPLY' class='btn' style='border-color:#00FF00; width:95%;'></form></div></body></html>"; server.send(200, "text/html", h); }
void handleAudio() { String h = "<html><head><style>body{background:#121212;color:#eee;font-family:sans-serif;text-align:center;}.btn{background:#333;border:2px solid #555;color:#fff;padding:12px;margin:5px;display:inline-block;text-decoration:none;border-radius:8px;}.card{background:#222; border-radius:12px; padding:15px; margin:10px auto; max-width:400px; border:1px solid #444;}.eq-row{width:100%; display:flex; justify-content:space-between; margin:10px 0;}input[type=range]{width:180px; accent-color:cyan;}</style></head><body><h1>🔊 Audio Settings</h1><a href='/' class='btn' style='border-color:cyan;'>BACK TO RADIO</a><div class='card'><b>PRESETS</b><br><a href='/setpreset?p=rock' class='btn'>ROCK</a> <a href='/setpreset?p=pop' class='btn'>POP</a> <a href='/setpreset?p=jazz' class='btn'>JAZZ</a> <a href='/setpreset?p=flat' class='btn'>FLAT</a></div><div class='card'><b>3-BAND EQ</b><form action='/seteq'><div class='eq-row'><span>Bass</span><input type='range' name='b' min='-15' max='15' value='" + String(gB) + "' onchange='this.form.submit()'></div><div class='eq-row'><span>Mid</span><input type='range' name='m' min='-15' max='15' value='" + String(gM) + "' onchange='this.form.submit()'></div><div class='eq-row'><span>Treb</span><input type='range' name='t' min='-15' max='15' value='" + String(gT) + "' onchange='this.form.submit()'></div></form></div></body></html>"; server.send(200, "text/html", h); }
void drawAnalogVU() { if (isAP || !showSpectrum) return; static float needlePos = 0; static int lastVisualMode = -1; static unsigned long lastVU = 0; if (millis() - lastVU < 35) return; lastVU = millis(); if (visualMode != lastVisualMode) { tft.fillRect(0, 160, 320, 80, TFT_BLACK); lastVisualMode = visualMode; } uint32_t v = audio.getVUlevel(); needlePos += (map(constrain(v, 0, 70000), 0, 70000, 0, 100) - needlePos) * 0.15; int wBox = 70, hBox = 70, xBox = (320 - wBox) / 2, yBox = 160, cx = 160, cy = yBox + hBox + 5, r = 55; uint16_t amber = tft.color565(255, 185, 40); tft.startWrite(); tft.fillRoundRect(xBox, yBox, wBox, hBox, 4, amber); for (int a = -35; a <= 35; a += 10) { float rad = (a - 90) * 0.0174; tft.drawLine(cx + cos(rad) * (r - 4), cy + sin(rad) * (r - 4), cx + cos(rad) * r, cy + sin(rad) * r, a > 20 ? TFT_RED : TFT_BLACK); } float angle = (needlePos * 0.7) - 35, rad = (angle - 90) * 0.0174; tft.drawLine(cx, cy - 8, cx + cos(rad) * r, cy + sin(rad) * r, TFT_RED); tft.endWrite(); }
void drawWeatherIcon(int x, int y, int id) { tft.fillRect(x, y, 35, 35, currentSkin.bgTop); if (id == 800) { tft.fillCircle(x + 17, y + 17, 7, TFT_YELLOW); for (int i = 0; i < 8; i++) { float a = i * 45 * 0.01745; tft.drawLine(x + 17 + cos(a) * 9, y + 17 + sin(a) * 9, x + 17 + cos(a) * 14, y + 17 + sin(a) * 14, TFT_YELLOW); } } else if (id >= 801 && id <= 804) { uint16_t c = 0xCE79; tft.fillCircle(x + 12, y + 22, 6, c); tft.fillCircle(x + 18, y + 16, 9, c); tft.fillCircle(x + 25, y + 22, 6, c); tft.fillRect(x + 12, y + 22, 13, 6, c); } else if (id >= 500 && id <= 531) { tft.fillCircle(x + 14, y + 14, 5, 0xAD55); tft.fillCircle(x + 20, y + 10, 7, 0xAD55); tft.fillCircle(x + 26, y + 14, 5, 0xAD55); for (int i = 0; i < 3; i++) tft.drawLine(x + 15 + (i * 5), y + 18, x + 13 + (i * 5), y + 24, 0x041F); } else if (id >= 200 && id <= 232) { tft.fillCircle(x + 18, y + 14, 8, 0x738E); tft.drawLine(x + 18, y + 18, x + 14, y + 24, TFT_YELLOW); tft.drawLine(x + 14, y + 24, x + 19, y + 24, TFT_YELLOW); tft.drawLine(x + 19, y + 24, x + 15, y + 30, TFT_YELLOW); } else if (id >= 600 && id <= 622) { tft.fillCircle(x + 18, y + 14, 8, 0xAD55); for (int i = 0; i < 3; i++) { int hx = x + 12 + (i * 6), hy = y + 22; tft.drawLine(hx - 2, hy, hx + 2, hy, TFT_WHITE); tft.drawLine(hx, hy - 2, hx, hy + 2, TFT_WHITE); } } }
void updateWeatherUI() { static unsigned long lastOWM = 0; if (millis() - lastOWM > 900000 || lastOWM == 0) { if (WiFi.status() == WL_CONNECTED && !isAP && owmKey != "") { HTTPClient http; String url = "http://api.openweathermap.org/data/2.5/weather?q=" + owmCity + "&appid=" + owmKey + "&units=metric"; http.begin(url); if (http.GET() == HTTP_CODE_OK) { String payload = http.getString(); int tIdx = payload.indexOf("\"temp\":"); if (tIdx != -1) { String tStr = payload.substring(tIdx + 7); tempC = tStr.substring(0, tStr.indexOf(",")).toFloat(); } int idIdx = payload.indexOf("\"id\":", payload.indexOf("\"weather\":")); if (idIdx != -1) { String idStr = payload.substring(idIdx + 5); weatherID = idStr.substring(0, idStr.indexOf(",")).toInt(); } } http.end(); } lastOWM = millis(); } if (tempC != 0.0) { drawWeatherIcon(70, 2, weatherID); tft.setTextColor(currentSkin.hInfo); tft.setFont(&fonts::Font0); tft.setCursor(103, 12); if (useCelsius) tft.printf("%.1fC", tempC); else tft.printf("%.1fF", (tempC * 9.0 / 5.0) + 32.0); } }
void drawWifiSignal(int x, int y) { if (isAP) { tft.setTextColor(currentSkin.textAccent); tft.setFont(&fonts::Font0); tft.drawCenterString("AP", x + 12, y + 4); return; } int32_t r = WiFi.RSSI(); int b = (r > -50) ? 5 : (r > -63) ? 4 : (r > -75) ? 3 : (r > -85) ? 2 : (r > -95) ? 1 : 0; for (int i = 0; i < 5; i++) { int h = (i * 3) + 3; tft.fillRect(x + (i * 6) + 1, y + (15 - h) + 1, 4, h, 0x0000); uint16_t c = (i < b) ? ((i < 2) ? currentSkin.barLow : (i < 4) ? currentSkin.barMid : currentSkin.wifiSig) : tft.color565(40, 40, 40); tft.fillRect(x + (i * 6), y + (15 - h), 4, h, c); } }
void drawSpectrum() { if (!showSpectrum || isAP) return; static unsigned long lastSpecUpdate = 0; if (millis() - lastSpecUpdate < 40) return; lastSpecUpdate = millis(); uint32_t v = audio.getVUlevel(); for (int i = 0; i < 2; i++) { int targetH = constrain(map(v, 0, 70000, 0, 65), 0, 65); if (barHeights[i] < targetH) barHeights[i] += 4; else barHeights[i] -= 3; int hNow = constrain(barHeights[i], 0, 65); tft.fillRect(10 + (i * 28), 160, 24, 65 - hNow, currentSkin.bgBottom); for (int h = 0; h < hNow; h += 4) { uint16_t c = (h < 25) ? currentSkin.barLow : (h < 45) ? currentSkin.barMid : currentSkin.barHigh; tft.fillRect(10 + (i * 28), 225 - h, 24, 3, c); } } }
void audio_showstreamtitle(const char* info) { String s = String(info); s.trim(); if (s.length() > 0) songTitle = s; }
void taskControl(void* p) { pinMode(PIN_A, INPUT_PULLUP); pinMode(PIN_B, INPUT_PULLUP); pinMode(PIN_K0, INPUT_PULLUP); pinMode(PIN_SW, INPUT_PULLUP); static uint8_t old_AB = 0; for (;;) { old_AB <<= 2; old_AB |= (digitalRead(PIN_A) << 1) | digitalRead(PIN_B); int8_t diff = ((int8_t[]){ 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 })[(old_AB & 0x0F)]; if (diff != 0) { encoderPos += diff; lastInteraction = millis(); } vTaskDelay(1 / portTICK_PERIOD_MS); } }
void setup() { 
  rtc_gpio_hold_dis((gpio_num_t)TFT_BLK); 
  rtc_gpio_deinit((gpio_num_t)TFT_BLK); 
  pinMode(TFT_BLK, OUTPUT); 
  digitalWrite(TFT_BLK, LOW); 
  delay(50); 
  ledcAttach(TFT_BLK, 5000, 8); 
  setBrightness(255); 
  tft.init(); 
  tft.setRotation(1); 
  tft.fillScreen(TFT_BLACK); 
  loadSettings(); 

  if (st_ssid != "") { 
    WiFi.begin(st_ssid.c_str(), st_pass.c_str()); 
    int retry = 0; 
    while (WiFi.status() != WL_CONNECTED && retry < 30) { 
      delay(500); 
      retry++; 
    } 
  } 

  if (WiFi.status() != WL_CONNECTED) { 
    WiFi.softAP("Radio_Setup"); 
    isAP = true; 
  } else { 
    configTime(0, 0, ntpServer); 
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); 
    tzset(); 
  } 

  // Szerver útvonalak
  server.on("/", handleRoot); 
  server.on("/stations", handleStations); 
  server.on("/skin", handleSkin); 
  server.on("/audio", handleAudio); 
  server.on("/update_ui", handleUpdate); 
  server.on("/togglespec", []() { showSpectrum = !showSpectrum; tft.fillRect(0, 160, 320, 80, TFT_BLACK); server.send(200, "text/plain", "OK"); }); 
  server.on("/setVisual", HTTP_GET, []() { if (server.hasArg("mode")) { visualMode = server.arg("mode").toInt(); tft.fillRect(0, 160, 320, 80, TFT_BLACK); server.send(200, "text/plain", "OK"); } }); 
  server.on("/setvol", []() { mainVal = server.arg("v").toInt(); audio.setVolume(volCurve[mainVal]); lastVolChange = millis(); saveSettings(); server.sendHeader("Location", "/"); server.send(303); }); 
  server.on("/seteq", []() { gB = server.arg("b").toInt(); gM = server.arg("m").toInt(); gT = server.arg("t").toInt(); audio.setTone(gB, gM, gT); saveSettings(); server.sendHeader("Location", "/audio"); server.send(303); }); 
  server.on("/setpreset", []() { String p = server.arg("p"); if (p == "rock") { gB = 6; gM = -1; gT = 5; } else if (p == "pop") { gB = 3; gM = 2; gT = 2; } else if (p == "jazz") { gB = 4; gM = 0; gT = 3; } else { gB = 0; gM = 0; gT = 0; } audio.setTone(gB, gM, gT); saveSettings(); server.sendHeader("Location", "/audio"); server.send(303); }); 
  server.on("/setweather", []() { owmCity = server.arg("city"); owmKey = server.arg("key"); useCelsius = (server.arg("u") == "C"); saveSettings(); server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='5;url=/'></head><body style='font-family:sans-serif; text-align:center; padding-top:50px;'><h1>Settings saved!</h1><p>The radio is restarting with the new weather data...</p><p>Redirecting to home page in 5 seconds...</p></body></html>"); delay(1000); ESP.restart(); });
  server.on("/weather", handleWeatherPage); 
  server.on("/setwifi", HTTP_POST, []() { st_ssid = server.arg("s"); st_pass = server.arg("p"); pref.begin("radio", false); pref.putString("ssid", st_ssid); pref.putString("pass", st_pass); pref.end(); server.send(200, "text/html", "Restarting..."); delay(2000); ESP.restart(); }); 
  server.on("/scan_m3u", []() { String url = server.arg("m3u_url"); if (url != "") parseM3UPro(url); server.sendHeader("Location", "/stations"); server.send(303); }); 
  server.on("/apply_manual", []() { int sid = server.arg("s").toInt(); if (sid >= 0 && sid < 10) { stations[sid].name = server.arg("n"); stations[sid].url = server.arg("u"); saveSettings(); } server.sendHeader("Location", "/stations"); server.send(303); }); 
  server.on("/clear_list", []() { m3uTempList.clear(); server.sendHeader("Location", "/stations"); server.send(303); }); 
  server.on("/edit", []() { int id = server.arg("id").toInt(); if (id >= 0 && id < 10) { stations[id].url = server.arg("url"); stations[id].name = server.arg("name"); saveSettings(); } server.sendHeader("Location", "/stations"); server.send(303); }); 
  server.on("/scan", []() { server.sendHeader("Location", "/?scan=1"); server.send(303); }); 
  server.on("/scan_data", []() { int n = WiFi.scanNetworks(); String json = "["; for (int i = 0; i < n; i++) json += "\"" + WiFi.SSID(i) + "\"" + (i == n - 1 ? "" : ","); json += "]"; server.send(200, "application/json", json); }); 
  server.on("/setalarm", []() { alarmH = server.arg("h").toInt(); alarmM = server.arg("m").toInt(); alarmActive = server.hasArg("active"); saveSettings(); forceRedraw = true; server.sendHeader("Location", "/"); server.send(303); }); 
  server.on("/off", []() { server.send(200); delay(500); goToSleep(); }); 
  server.on("/prev", []() { currentStationIdx = (currentStationIdx - 1 + 10) % 10; playStation(currentStationIdx); server.sendHeader("Location", "/"); server.send(303); }); 
  server.on("/next", []() { currentStationIdx = (currentStationIdx + 1) % 10; playStation(currentStationIdx); server.sendHeader("Location", "/"); server.send(303); }); 
  server.on("/update", HTTP_POST, []() { server.send(200, "text/plain", (Update.hasError()) ? "ERROR" : "OK"); delay(1000); ESP.restart(); }, handleUpdateUpload); 
  server.on("/setskin", []() { currentSkin.hexTop = server.arg("top"); currentSkin.hexBottom = server.arg("bot"); currentSkin.hexMain = server.arg("txt"); currentSkin.hexAccent = server.arg("acc"); currentSkin.hexWifi = server.arg("wifi"); currentSkin.hexSel = server.arg("sel"); currentSkin.hexClk = server.arg("clk"); currentSkin.hexHInfo = server.arg("hinf"); updateColors(); saveSettings(); forceRedraw = true; server.sendHeader("Location", "/skin"); server.send(303); }); 
  server.on("/defaultskin", []() { currentSkin.hexTop = "#000000"; currentSkin.hexBottom = "#000000"; currentSkin.hexMain = "#FFFFFF"; currentSkin.hexAccent = "#00FFFF"; currentSkin.hexWifi = "#00FF00"; currentSkin.hexSel = "#0000FF"; currentSkin.hexClk = "#FFFFFF"; currentSkin.hexHInfo = "#FFFFFF"; currentSkin.hexBarL = "#00FF00"; currentSkin.hexBarM = "#FFFF00"; currentSkin.hexBarH = "#FF0000"; currentSkin.hexVol = "#00FFFF"; currentSkin.hexAlm = "#FF0000"; updateColors(); saveSettings(); forceRedraw = true; server.sendHeader("Location", "/skin"); server.send(303); }); 
  server.on("/upload_m3u", HTTP_POST, []() { server.sendHeader("Location", "/stations"); server.send(303); }, []() { 
    HTTPUpload& u = server.upload(); 
    static char urlBuf[128], nameBuf[64]; 
    static int urlIdx = 0, nameIdx = 0; 
    static bool collU = false, collN = false; 
    if (u.status == UPLOAD_FILE_START) { m3uTempList.clear(); urlIdx = 0; nameIdx = 0; collU = collN = false; } 
    else if (u.status == UPLOAD_FILE_WRITE) { 
      for (size_t i = 0; i < u.currentSize; i++) { 
        char c = u.buf[i]; 
        if (!collU && !collN && i + 7 < u.currentSize && u.buf[i] == '#' && u.buf[i + 1] == 'E') { collN = true; nameIdx = 0; continue; } 
        if (collN) { if (c != '\n' && c != '\r' && nameIdx < 63) nameBuf[nameIdx++] = c; else { nameBuf[nameIdx] = '\0'; collN = false; } continue; } 
        if (!collU && i + 3 < u.currentSize && u.buf[i] == 'h' && u.buf[i + 1] == 't') { collU = true; urlIdx = 0; } 
        if (collU) { if (urlIdx < 127 && c != '\n' && c != '\r' && c != ' ') urlBuf[urlIdx++] = c; else { urlBuf[urlIdx] = '\0'; if (urlIdx > 10 && m3uTempList.size() < 30) m3uTempList.push_back({ String(nameBuf), String(urlBuf) }); collU = false; } } 
      } 
    } 
  }); 

  server.begin(); 
  audio.setPinout(I2S_BCK, I2S_LRC, I2S_DIN); 
  audio.setVolume(volCurve[mainVal]); 
  audio.setTone(gB, gM, gT); 
  if (!isAP) playStation(currentStationIdx); 
  xTaskCreatePinnedToCore(taskControl, "Ctrl", 4096, NULL, 1, NULL, 0); 
  lastInteraction = millis(); 
  tft.fillScreen(currentSkin.bgBottom); 
} 

void loop() { 
  audio.loop(); 
  server.handleClient(); 
  
  bool sw = (digitalRead(PIN_SW) == LOW); 
  unsigned long cm = millis(); 
  struct tm ti; 
  bool timeValid = getLocalTime(&ti); 
  char currentTimeStr[10]; 
  
  if (timeValid) strftime(currentTimeStr, 10, "%H:%M", &ti); 
  else strcpy(currentTimeStr, "00:00"); 

  if (!isAP && timeValid && alarmActive) { 
    if (ti.tm_hour == alarmH && ti.tm_min == alarmM) { 
      if (!isAlarming && (cm - alarmStartMillis > 61000 || alarmStartMillis == 0)) { 
        isAlarming = true; alarmStartMillis = cm; alarmVolume = 5; 
        audio.setVolume(volCurve[alarmVolume]); 
        playStation(currentStationIdx); 
        setBrightness(255); 
        forceRedraw = true; 
      } 
      if (isAlarming && (cm - alarmStartMillis > 20000)) isAlarming = false; 
      if (isAlarming && alarmVolume < 9 && cm - lastAlarmStep > 4000) { 
        alarmVolume++; 
        audio.setVolume(volCurve[alarmVolume]); 
        lastAlarmStep = cm; 
      } 
    } else if (!isAlarming) alarmStartMillis = 0; 
  } 

  if (isAlarming && sw) { 
    isAlarming = false; 
    audio.setVolume(volCurve[mainVal]); 
    forceRedraw = true; 
  } 

  static bool volBarVisible = false; 
  if (cm - lastVolChange < 3050) volBarVisible = true; 
  else if (volBarVisible) { forceRedraw = true; volBarVisible = false; } 

  if (cm - lastInteraction > 30000 && !isAlarming) { 
    if (!isDimmed) { setBrightness(20); isDimmed = true; } 
  } else if (isDimmed) { 
    setBrightness(255); 
    isDimmed = false; 
  } 

  static uint32_t k0P = 0; 
  static bool k0A = false; 
  if (digitalRead(PIN_K0) == LOW) { 
    if (!k0A) { k0P = cm; k0A = true; } 
    if (cm - k0P > 5000) factoryReset(); 
  } else if (k0A) { 
    if (cm - k0P > 1500) goToSleep(); 
    k0A = false; 
  } 

  int curR = encoderPos / 4; 
  static int lastR = 0; 
  int d = curR - lastR; 
  lastR = curR; 

  if (d != 0) { 
    if (!sw) { 
      mainVal = constrain(mainVal + d, 0, 21); 
      audio.setVolume(volCurve[mainVal]); 
      lastVolChange = cm; 
      saveSettings(); 
    } else { 
      tempStationIdx = (tempStationIdx + d) % 10; 
      if (tempStationIdx < 0) tempStationIdx = 9; 
    } 
    lastInteraction = cm; 
    forceRedraw = true; 
  } 

  static bool lastSw = false; 
  if (!sw && lastSw) { 
    if (!isAP && tempStationIdx != currentStationIdx) { 
      currentStationIdx = tempStationIdx; 
      playStation(currentStationIdx); 
      saveSettings(); 
    } 
    forceRedraw = true; 
  } 
  lastSw = sw; 

  static uint32_t uiT = 0; 
  if (!isAP && showSpectrum) { 
    tft.startWrite(); 
    if (visualMode == 1 || visualMode == 3) drawSpectrum(); 
    if (visualMode == 2 || visualMode == 3) drawAnalogVU(); 
    tft.endWrite(); 
  } 

  if (cm - uiT > 200 || forceRedraw) { 
    uiT = cm; 
    tft.startWrite(); 
    int dV = isAlarming ? alarmVolume : mainVal; 
    if (dV != lastMain || sw != lastDrawnMode || tempStationIdx != lastDrawnStationIdx || String(currentTimeStr) != lastDrawnTime || forceRedraw || (cm - lastVolChange < 3000) || (cm - lastVolChange >= 3000 && cm - lastVolChange < 3200)) { 
      uint16_t headBg = sw ? currentSkin.selMode : (isAlarming ? currentSkin.almWarn : (isAP ? 0x001F : currentSkin.bgTop)); 
      tft.fillRect(0, 0, 320, 35, headBg); 
      tft.setFont(&fonts::Font0); 
      tft.setTextColor(currentSkin.hInfo); 
      tft.setTextDatum(TL_DATUM); 
      updateWeatherUI(); 
      tft.drawString(("V:" + String(dV < 10 ? "0" : "") + String(dV) + " | S:" + String(tempStationIdx + 1)).c_str(), 8, 12); 
      if (alarmActive) { 
        tft.setTextColor(currentSkin.almWarn); 
        char almBuf[15]; 
        snprintf(almBuf, sizeof(almBuf), "Alarm %02d:%02d", alarmH, alarmM); 
        tft.drawString(almBuf, 200, 12); 
      } 
      drawWifiSignal(285, 10); 
      tft.setTextDatum(TC_DATUM); 
      tft.setFont(&fonts::FreeSansBold12pt7b); 
      tft.setTextColor(currentSkin.clk); 
      tft.drawCenterString(isAP ? "SETUP MODE" : currentTimeStr, 160, 8); 
      if (!isAP && (isAlarming || cm - lastVolChange < 3000)) { 
        int vW = (dV * 320 / 21); 
        tft.fillRect(0, 33, vW, 2, currentSkin.volBar); 
        tft.fillRect(vW, 33, 320 - vW, 2, currentSkin.bgBottom); 
      } 
      lastMain = dV; lastDrawnMode = sw; lastDrawnStationIdx = tempStationIdx; lastDrawnTime = String(currentTimeStr); 
    } 
    String curN = sw ? stations[tempStationIdx].name : stations[currentStationIdx].name; 
    if (curN != lastDrawnStationName || songTitle != lastDrawnSong || forceRedraw) { 
      tft.fillRect(0, 36, 320, 105, currentSkin.bgBottom); 
      if (isAP) { 
        tft.setTextColor(currentSkin.textAccent); 
        tft.drawCenterString("Radio_Setup / 192.168.4.1", 160, 80, &fonts::FreeSans9pt7b); 
      } else { 
        tft.setTextColor(currentSkin.textMain); 
        tft.drawCenterString(curN.c_str(), 160, 60, &fonts::FreeSansBold12pt7b); 
        tft.setTextColor(currentSkin.textAccent); 
        tft.drawCenterString(songTitle.c_str(), 160, 115, &fonts::FreeSans9pt7b); 
      } 
      lastDrawnStationName = curN; lastDrawnSong = songTitle; 
    } 
    tft.setTextColor(currentSkin.textAccent); 
    tft.setTextDatum(BR_DATUM); 
    tft.drawString(isAP ? "192.168.4.1" : WiFi.localIP().toString().c_str(), 315, 235, &fonts::Font0); 
    forceRedraw = false; 
    tft.endWrite(); 
  } 
}
