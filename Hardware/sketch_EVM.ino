#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "ESP32_Voting_Machine";
const char* password = "12345678password";

WebServer server(80);

// Hardware Pin Definitions
const int BTN_C1    = 12; // Candidate 1 Button
const int BTN_C2    = 14; // Candidate 2 Button
const int BTN_C3    = 27; // Candidate 3 Button

const int LED_GREEN = 2;  // Ready / Allow LED
const int LED_RED   = 4;  // Locked Out LED
const int LED_C1    = 16; // Candidate 1 LED (RX2)
const int LED_C2    = 17; // Candidate 2 LED (TX2)
const int LED_C3    = 5;  // Candidate 3 LED

// System State Variables
bool sessionActive = false;
bool readyForVote  = false;
unsigned long startTime = 0;

int votesC1 = 0;
int votesC2 = 0;
int votesC3 = 0;

unsigned long lastDebounce = 0;
const unsigned long debounceDelay = 350;

// Winner Result Blink Variables
int winnerLED = -1;
unsigned long winnerBlinkStart = 0;
unsigned long lastWinnerBlinkTime = 0;
bool winnerBlinkState = false;

void setLED(int pin, bool state) {
  digitalWrite(pin, state ? HIGH : LOW);
}

void turnOffAllLEDs() {
  setLED(LED_GREEN, false);
  setLED(LED_RED,   false);
  setLED(LED_C1,    false);
  setLED(LED_C2,    false);
  setLED(LED_C3,    false);
}

void setup() {
  Serial.begin(115200);

  pinMode(BTN_C1, INPUT_PULLUP);
  pinMode(BTN_C2, INPUT_PULLUP);
  pinMode(BTN_C3, INPUT_PULLUP);

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_C1, OUTPUT);
  pinMode(LED_C2, OUTPUT);
  pinMode(LED_C3, OUTPUT);

  turnOffAllLEDs();

  WiFi.softAP(ssid, password);
  Serial.println("\n--- ESP32 Voting Machine Online ---");
  Serial.print("IP Address: "); Serial.println(WiFi.softAPIP());

  server.enableCORS(true);

  // Status Polling Endpoint
  server.on("/status", HTTP_GET, []() {
    unsigned long elapsed = sessionActive ? (millis() - startTime) / 1000 : 0;
    int totalVotes = votesC1 + votesC2 + votesC3;

    String json = "{";
    json += "\"session\":" + String(sessionActive ? "true" : "false") + ",";
    json += "\"ready\":" + String(readyForVote ? "true" : "false") + ",";
    json += "\"total\":" + String(totalVotes) + ",";
    json += "\"elapsed\":" + String(elapsed);
    json += "}";

    server.send(200, "application/json", json);
  });

  // Start Session Endpoint
  server.on("/start", HTTP_GET, []() {
    sessionActive = true;
    readyForVote = true;
    votesC1 = 0; votesC2 = 0; votesC3 = 0;
    startTime = millis();
    winnerLED = -1;

    turnOffAllLEDs();
    setLED(LED_GREEN, true); // Turn ON Green LED to start polling

    server.send(200, "text/plain", "STARTED");
  });

  // Stop Session Endpoint
  server.on("/stop", HTTP_GET, []() {
    sessionActive = false;
    readyForVote = false;
    winnerLED = -1;
    
    turnOffAllLEDs(); // Turn OFF all LEDs when session is stopped

    server.send(200, "text/plain", "STOPPED");
  });

  // Allow Next Voter Endpoint
  server.on("/allow", HTTP_GET, []() {
    if (sessionActive) {
      readyForVote = true;
      turnOffAllLEDs();
      setLED(LED_GREEN, true); // Green ON for next voter
    }
    server.send(200, "text/plain", "ALLOWED");
  });

  // Results Endpoint
  server.on("/results", HTTP_GET, []() {
    struct Candidate { int id; int votes; };
    Candidate list[3] = { {1, votesC1}, {2, votesC2}, {3, votesC3} };

    for (int i = 0; i < 2; i++) {
      for (int j = i + 1; j < 3; j++) {
        if (list[j].votes > list[i].votes) {
          Candidate temp = list[i];
          list[i] = list[j];
          list[j] = temp;
        }
      }
    }

    if (list[0].id == 1) winnerLED = LED_C1;
    else if (list[0].id == 2) winnerLED = LED_C2;
    else if (list[0].id == 3) winnerLED = LED_C3;

    winnerBlinkStart = millis();

    String json = "{\"rank\":[";
    json += "{\"id\":" + String(list[0].id) + ",\"votes\":" + String(list[0].votes) + "},";
    json += "{\"id\":" + String(list[1].id) + ",\"votes\":" + String(list[1].votes) + "},";
    json += "{\"id\":" + String(list[2].id) + ",\"votes\":" + String(list[2].votes) + "}";
    json += "]}";

    server.send(200, "application/json", json);
  });

  server.begin();
}

void loop() {
  server.handleClient();
  unsigned long now = millis();

  // Handle Candidate Buttons
  if (sessionActive && readyForVote && (now - lastDebounce > debounceDelay)) {
    int b1 = digitalRead(BTN_C1);
    int b2 = digitalRead(BTN_C2);
    int b3 = digitalRead(BTN_C3);

    if (b1 == LOW || b2 == LOW || b3 == LOW) {
      delay(50); // Debounce filter
      b1 = digitalRead(BTN_C1);
      b2 = digitalRead(BTN_C2);
      b3 = digitalRead(BTN_C3);

      int voted = 0;
      if (b1 == LOW) voted = 1;
      else if (b2 == LOW) voted = 2;
      else if (b3 == LOW) voted = 3;

      if (voted > 0) {
        lastDebounce = now;
        readyForVote = false; // Lock system immediately

        if (voted == 1) votesC1++;
        else if (voted == 2) votesC2++;
        else if (voted == 3) votesC3++;

        // Clear all LEDs, then turn ON Red LED and ONLY the voted candidate LED
        turnOffAllLEDs();
        setLED(LED_RED, true);

        if (voted == 1) setLED(LED_C1, true);
        else if (voted == 2) setLED(LED_C2, true);
        else if (voted == 3) setLED(LED_C3, true);
      }
    }
  }

  // Non-blocking Winner LED Blinking on View Results (Blinks for 6 seconds)
  if (winnerLED != -1) {
    if (now - winnerBlinkStart < 6000) {
      if (now - lastWinnerBlinkTime > 120) {
        lastWinnerBlinkTime = now;
        winnerBlinkState = !winnerBlinkState;
        setLED(winnerLED, winnerBlinkState);
      }
    } else {
      setLED(winnerLED, false);
      winnerLED = -1;
    }
  }
}