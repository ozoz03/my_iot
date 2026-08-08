#include <Arduino.h>


#define LED 2
#define EXT_LED 4

const int DOT = 200;
const int DASH = DOT * 3;
const int ELEMENT_GAP = DOT;      // gap between dot/dash within a letter
const int LETTER_GAP = DOT * 3;   // gap between letters
const int WORD_GAP = DOT * 7;     // gap between words/phrases

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  pinMode(EXT_LED, OUTPUT);

}

void blink(int duration) {
  digitalWrite(LED, HIGH);
  digitalWrite(EXT_LED, HIGH);
  delay(duration);
  digitalWrite(LED, LOW);
  digitalWrite(EXT_LED, LOW);
}

void sendDot() {
  Serial.print(".");
  blink(DOT);
  delay(ELEMENT_GAP);
}

void sendDash() {
  Serial.print("-");
  blink(DASH);
  delay(ELEMENT_GAP);
}

void sendLetterGap() {
  delay(LETTER_GAP - ELEMENT_GAP);
}

void sendWordGap() {
  Serial.println("");
  delay(WORD_GAP);
}

void sendS() {
  for (int i = 0; i < 3; ++i) {
    sendDot();
  }
}

void sendO() {
  for (int i = 0; i < 3; ++i) {
    sendDash();
  }
}

void sendSOS() {
  sendS();
  sendLetterGap();
  sendO();
  sendLetterGap();
  sendS();
  sendWordGap();
}

void loop() {
  sendSOS();
}
