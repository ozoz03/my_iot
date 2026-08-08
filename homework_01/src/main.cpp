#include <Arduino.h>


#define LED 2
#define EXT_LED 4

// put function declarations here:
int myFunction(int, int);

void setup() {
  // put your setup code here, to run once:
  // int result = myFunction(2, 3);
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  pinMode(EXT_LED, OUTPUT);

}

void loop() {
  // S
  for (int i = 0; i < 3; ++i) {
    Serial.print(".");
    digitalWrite(LED, HIGH);
    digitalWrite(EXT_LED, HIGH);
    delay(200);
    // a pause
    Serial.print(" ");
    digitalWrite(LED, LOW);
    digitalWrite(EXT_LED, LOW);
    delay(200);
  }
  // O
  for (int i = 0; i < 3; ++i) {
    Serial.print("-");
    digitalWrite(LED, HIGH);
    digitalWrite(EXT_LED, HIGH);
    delay(600);
    // a pause
    Serial.print(" ");
    digitalWrite(LED, LOW);
    digitalWrite(EXT_LED, LOW);
    delay(200);
  }
  // S again
  for (int i = 0; i < 3; ++i) {
    Serial.print(".");
    digitalWrite(LED, HIGH);
    digitalWrite(EXT_LED, HIGH);
    delay(200);
    // a pause
    Serial.print(" ");
    digitalWrite(LED, LOW);
    digitalWrite(EXT_LED, LOW);
    delay(200);
  }
  // a pause between phrases
  Serial.println("");
  digitalWrite(LED, LOW);
  digitalWrite(EXT_LED, LOW);
  delay(200);
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}