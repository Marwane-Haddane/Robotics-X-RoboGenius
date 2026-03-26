// C++ code
//
#include <Servo.h>
int motor_right_positf=7;
int motor_right_negatif=8;
int motor_left_positf=12;
int motor_left_negatif=9;
int ena=5;
int enb=6;
int pomp = 3;
Servo servo1;



void setup()
{
  pinMode(motor_right_positf, OUTPUT);
  pinMode(motor_right_negatif, OUTPUT);
  pinMode(motor_left_positf, OUTPUT);
  pinMode(motor_left_negatif, OUTPUT);
  pinMode(pomp, OUTPUT);
  Serial.begin(9600);
  servo1.attach(11);

  analogWrite(ena,255);
  analogWrite(enb,255);
}

void loop()
{
  if(Serial.available())
  {
    char data = Serial.read();
    if(data=='F')
      forward();
    else if(data=='B')
      backward();
    else if(data=='R')
      RIGHTWARD();
    else if(data=='L')
      LEFTWARD();
    else if (data =='M'){
      for ( int i = 90 ; i > 45 ; i--){
        servo1.write(i);
        delay(20);
      }
      delay(2000);
      check_humidity();
      for ( int i = 45 ; i < 90 ; i++){
        servo1.write(i);
        delay(20);
      }
      delay(200);
    }
    else
     stop();
  }

  
}

void forward(){
  digitalWrite(motor_right_positf,HIGH);
  digitalWrite(motor_right_negatif,LOW);
  
  digitalWrite(motor_left_positf,HIGH);
  digitalWrite(motor_left_negatif,LOW);

}

void backward(){
  digitalWrite(motor_right_positf,LOW);
  digitalWrite(motor_right_negatif,HIGH);
  
  digitalWrite(motor_left_positf,LOW);
  digitalWrite(motor_left_negatif,HIGH);
}


void RIGHTWARD(){
  digitalWrite(motor_right_positf,HIGH);
  digitalWrite(motor_right_negatif,LOW);
  
  digitalWrite(motor_left_positf,LOW);
  digitalWrite(motor_left_negatif,HIGH);
}
void LEFTWARD(){
  digitalWrite(motor_right_positf,LOW);
  digitalWrite(motor_right_negatif,HIGH);
  
  digitalWrite(motor_left_positf,HIGH);
  digitalWrite(motor_left_negatif,LOW);
}
void stop(){
   digitalWrite(motor_right_positf,LOW);
  digitalWrite(motor_right_negatif,LOW);
  
  digitalWrite(motor_left_positf,LOW);
  digitalWrite(motor_left_negatif,LOW);
}
void check_humidity(){
  int x = analogRead(A0);
  int val = map (x, 0,1023 , 0,100);
  if ( val > 40 )
  {
    digitalWrite(pomp, HIGH);
    delay(2000);
  }
}