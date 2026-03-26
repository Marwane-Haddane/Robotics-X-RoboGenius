
int VL=5;
int VR=6;
int LeftP=9;
int LeftN=12;
int RightP=7;
int RightN=8;
char command;
int carSpeed =155;
int IRRp=A5,IRMp=A3,IRLp=A0;
int IRR,IRM,IRL;

int threshold=350; 

void setup() {
  // put your setup code here, to run once:
pinMode(VL,OUTPUT);
pinMode(VR,OUTPUT);
pinMode(LeftN,OUTPUT);
pinMode(LeftP,OUTPUT);
pinMode(RightN,OUTPUT);
pinMode(RightP,OUTPUT);
pinMode(13,OUTPUT);


digitalWrite(13,HIGH); // the power source of the IR
pinMode(IRRp,INPUT);
pinMode(IRLp,INPUT);
pinMode(IRMp,INPUT);

Serial.begin(9600);

}


//////////
void goLeft(int viL = carSpeed, int viR = carSpeed);
void goRight(int viL = carSpeed, int viR = carSpeed); 
/////////


void loop() {
  // put your main code here, to run repeatedly:

  if(Serial.available()>0  ){
    command = Serial.read();
    if (command == 'F') {
      goForward();
    } 
    else if (command == 'B') {
      goBackward();
    } 
    else if (command == 'L') {
      goLeft();
    } 
    else if (command == 'R') {
      goRight();
    } 
    // --- Speed Controls ---
    else if (command == '1') {
      carSpeed = 90;  // Low speed (approx 35%)
    } 
    else if (command == '2') {
      carSpeed = 170; // Medium speed (approx 65%)
    } 
    else if (command == '3') {
      carSpeed = 255; // Full speed (100%)
    } 
    else if (command == '4' ) {
       // switch to the line follower mode
       carSpeed = 100;
       delay(500);
       while(true){
              // Check if a new command has been sent to stop the loop
              if (Serial.available() > 0) {
                char newCommand = Serial.read();
                if (newCommand == '0') { // Use '0' as the "stop" command
                    stopCar();
                    break; // Exit the while(true) loop
                }
              }
        
            IRR=analogRead(IRRp);
            IRM=digitalRead(IRMp);
            IRL=analogRead(IRLp);
            Serial.print("RIGHT :  ");
            Serial.print(IRR);
            Serial.print("   //   Med :  ");
            Serial.print(IRM);
            Serial.print("   //   LEFT :  ");
            Serial.print(IRL);
            Serial.println();

            // Condition 1: ON TRACK
            // Left(White), Mid(Black), Right(White)
            if (IRL < threshold && IRM == 0 && IRR < threshold) {
              goForward();
            }

            // Condition 2: VEERING RIGHT
            // The line is under the LEFT sensor
            // Left(Black), Mid(White) , Right(White)
            else if (IRL > threshold && IRM == 1) {
              if ( IRL>600){
                goLeft(190,190); // trun left with high speed 
              }else {
              goLeft(100,100); // Turn left to get back on the line
              }
              
            }

            // Condition 3: VEERING LEFT
            // The line is under the RIGHT sensor
            // Left(White), Mid(White), Right(Black)
            else if (IRL < threshold && IRM == 1 && IRR > threshold) {
              if ( IRR>600){
                goRight(190,190); // trun right with high speed 
              }else {
              goRight(100,100); // Turn right to get back on the line
              }
              
            }

            // Condition 4: LOST THE LINE
            // All sensors are on white
            // Left(White), Mid(White), Right(White)
            else if (IRL < threshold && IRM == 1 && IRR < threshold) {
              stopCar(); // Stop if we lost the line
              break;
            }

       }
    }
    else{
      stopCar();
    }
  }




}




void goBackward() {
  // Left Motor Forward
  digitalWrite(LeftP, HIGH);
  digitalWrite(LeftN, LOW);
  // Right Motor Forward
  digitalWrite(RightP, HIGH);
  digitalWrite(RightN, LOW);
  // Set speed for both
  analogWrite(VL, carSpeed);
  analogWrite(VR, carSpeed);
}

void goForward() {
  // Left Motor Backward
  digitalWrite(LeftP, LOW);
  digitalWrite(LeftN, HIGH);
  // Right Motor Backward
  digitalWrite(RightP, LOW);
  digitalWrite(RightN, HIGH);
  // Set speed for both
  analogWrite(VL, carSpeed);
  analogWrite(VR, carSpeed);
}

void goLeft(int viL = carSpeed , int viR=carSpeed) {
  // Left Motor Backward
  digitalWrite(LeftP, LOW);
  digitalWrite(LeftN, HIGH);
  // Right Motor Forward
  digitalWrite(RightP, HIGH);
  digitalWrite(RightN, LOW);
  // Set speed for both
  analogWrite(VL, viL);
  analogWrite(VR, viR);
}


void goRight(int viL = carSpeed , int viR=carSpeed) {
  // Left Motor Forward
  digitalWrite(LeftP, HIGH);
  digitalWrite(LeftN, LOW);
  // Right Motor Backward
  digitalWrite(RightP, LOW);
  digitalWrite(RightN, HIGH);
  // Set speed for both
  analogWrite(VL, viL);
  analogWrite(VR, viR);
}

void stopCar() {
  // Left Motor Stop (Brake)
  digitalWrite(LeftP, LOW);
  digitalWrite(LeftN, LOW);
  // Right Motor Stop (Brake)
  digitalWrite(RightP, LOW);
  digitalWrite(RightN, LOW);
  // Set speed to 0
  analogWrite(VL, 0);
  analogWrite(VR, 0);
}