int buzzer = 11 ;
void setup() {
  pinMode(buzzer, OUTPUT);
  Serial.begin(9600);

}

void loop() {
  if(Serial.available()){
    char val= Serial.read();
    if (val=='1'){
      digitalWrite(buzzer, HIGH);
    }
    else if (val=='0'){
    	digitalWrite(buzzer, LOW);
    }
  }
}
