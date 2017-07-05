#include <NewPing.h>
#include <AFMotor.h>

#define BAUD_RATE 9600
#define OK "OK"
#define MAX_SPEED 255
#define TURN_SPEED 200
#define MAX_DISTANCE 50 // Maximum distance we want to ping for (in centimeters). Maximum sensor distance is rated at 400-500cm.
#define COLLISION_DISTANCE_CM 33.02
#define MOVE_FORWARD "1"
#define MOVE_BACKWARD "2"
#define TURN_LEFT "3"
#define TURN_RIGHT "4"
#define STOP "5"
#define HORN "6"

#define CMD_CHECK_CIPMUX 1
#define CMD_CIPMUX 2
#define CMD_CIPSERVER 3
#define CMD_SET_CIPSTO 4
#define CMD_COMPLETED 5

#define PIN_TCP_CONNECT A2
#define PIN_READY_TO_CONNECT A3
#define PIN_ERROR A4
#define PIN_HORN A5
#define PIN_ECHO_1 22
#define PIN_TRIGGER_1 23
#define PIN_ECHO_2 24
#define PIN_TRIGGER_2 25
const String KEY_INPUT = "INPT-";

//NewPing SONAR_1(PIN_TRIGGER_1, PIN_ECHO_1, MAX_DISTANCE); // NewPing setup of pins and maximum distance.
//NewPing SONAR_2(PIN_TRIGGER_2, PIN_ECHO_2, MAX_DISTANCE); // NewPing setup of pins and maximum distance.

AF_DCMotor MOTOR1(1);
AF_DCMotor MOTOR2(2);
AF_DCMotor MOTOR3(3);
AF_DCMotor MOTOR4(4);

byte mMovementSpeed;
boolean errorFound;
boolean clientConnected;
byte runningCommand;
String currentDirection;
boolean multipleConnectionsEnabled;
unsigned short lastCollisionDistance;
boolean sendingData;
byte lastConnectionId = 0;

void setup() {
  mMovementSpeed = MAX_SPEED;
  multipleConnectionsEnabled = false;
  clientConnected = false;
  errorFound = false;
  sendingData = false;
  runningCommand = 0;

  Serial.begin(BAUD_RATE);
  while (!Serial);
  Serial1.begin(BAUD_RATE);
  while (!Serial1);
  Serial.println("Setting up...");

  pinMode(PIN_ERROR, OUTPUT);
  pinMode(PIN_READY_TO_CONNECT, OUTPUT);
  pinMode(PIN_TCP_CONNECT, OUTPUT);
  pinMode(PIN_HORN, OUTPUT);

  //Enabling mulitple connections
  runningCommand = CMD_CHECK_CIPMUX;
  Serial1.println("AT+CIPMUX?");
}


void loop() {
  if (errorFound) {
    return;
  }

  while (Serial1.available()) {
    String result = Serial1.readStringUntil('\n');
    Serial.println("result:" + result);
    result.trim();

    if (result.length() == 0) {
      Serial.println("Whitespaces");
      continue;
    }

    if (result.startsWith("+IPD")) {
      //Note: Sometimes, after client disconnection if app doesn't close and attempt to send data to client then, client gets data on "last connected connection id".
      clientConnected = true;

      int startIndex = result.indexOf(':') + 1;
      startIndex = startIndex + 5; //Adding KEY_INPUT length too.
      String input = result.substring(startIndex);

      Serial.println("Data received:" + result);

      handleInput(input);

    } else if (sendingData) {
      if (result.startsWith(">")) {
        Serial1.print("{\"collision\":true,\"distance\":");
        Serial1.print(lastCollisionDistance);
        Serial1.println("}");
        Serial.println("Ready to send. Sending data...");

      } else if (result.equals("SEND OK")) {
        Serial.println("Sending data success!");
        sendingData = false;

      } else if (result.equals("SEND FAIL") || result.equals("ERROR")) {
        Serial.println("Sending data failed!");
        sendingData = false;
      }
    } else if (runningCommand == CMD_CHECK_CIPMUX) {

      if (result.startsWith("OK")) {
        sendNextCommand();

      } else if (result.startsWith("+CIPMUX:1")) {
        Serial.println("MUX ALREADY ENABLED");
        multipleConnectionsEnabled = true;

      } else if (result.startsWith("+CIPMUX:0")) {
        Serial.println("MUX DISABLED. ENABLING NOW.");
        multipleConnectionsEnabled = false;
      }
    } else if (runningCommand == CMD_CIPMUX) {

      if (result.startsWith("OK")) {
        Serial.println("CIPMUX SUCCESS");
        sendNextCommand();

      } else if (result.startsWith("link is builded")) {
        //TODO: I think "link is builded" isn't an error.
        Serial.println("CIPMUX Res: Error! Link is already builded");
        errorFound = true;

      } else if (result.startsWith("FAIL") || result.startsWith("ERROR")) {
        Serial.println("Error Found");
        errorFound = true;
      }
    } else if (runningCommand == CMD_CIPSERVER) {

      if (result.startsWith("OK") || result.equals("no change")) {
        Serial.println("CIPSERVER SUCCESS");
        sendNextCommand();

      } else if (result.startsWith("FAIL") || result.startsWith("ERROR")) {
        Serial.println("Error Found");
        errorFound = true;
      }
    } else if (runningCommand == CMD_SET_CIPSTO) {
      if (result.startsWith("OK")) {
        Serial.println("SET CIPSTO SUCCESS");
        sendNextCommand();

      } else if (result.startsWith("ERROR")) {
        Serial.println("Set cipsto failed.");
        errorFound = true;
      }
    } else if (result.startsWith("0,CONNECT") || result.startsWith("1,CONNECT") || result.startsWith("2,CONNECT") || result.startsWith("3,CONNECT")) {
      clientConnected = true;
      lastConnectionId = result.substring(0, 1).toInt();
      Serial.println("Client connected:" + result);

    } else if (result.startsWith("0,CLOSED") || result.startsWith("1,CLOSED") || result.startsWith("2,CLOSED") || result.startsWith("3,CLOSED")) {
      clientConnected = false;
      sendingData = false;
      Serial.println("Client disconnected" + result);

    } else {
      //Cheking for input received from controller.
      if (result.startsWith(KEY_INPUT)) {
        result = result.substring(5);
        handleInput(result);
        Serial.println("LoopOtherInput:" + result);

      } else {
        Serial.println("LoopOther:" + result);
      }
    }
  }

  refreshLEDs();

  //If client is connected and
  if (clientConnected) {
    getDistance();
  }
}

void refreshLEDs() {
  if (errorFound) {
    analogWrite(PIN_ERROR, 255);
    analogWrite(PIN_READY_TO_CONNECT, 0);
    analogWrite(PIN_TCP_CONNECT, 0);

  } else if (clientConnected) {
    analogWrite(PIN_ERROR, 0);
    analogWrite(PIN_READY_TO_CONNECT, 0);
    analogWrite(PIN_TCP_CONNECT, 255);

  } else if (runningCommand == CMD_COMPLETED) {
    analogWrite(PIN_ERROR, 0);
    analogWrite(PIN_READY_TO_CONNECT, 255);
    analogWrite(PIN_TCP_CONNECT, 0);
  }
}


void sendNextCommand() {
  if (runningCommand == CMD_CHECK_CIPMUX) {
    if (multipleConnectionsEnabled) {
      runningCommand = CMD_CIPSERVER;
      Serial1.println("AT+CIPSERVER=1,8888");
    } else {
      runningCommand = CMD_CIPMUX;
      Serial1.println("AT+CIPMUX=1");
    }
  } else if (runningCommand == CMD_CIPMUX) {
    runningCommand = CMD_CIPSERVER;
    Serial1.println("AT+CIPSERVER=1,8888");

  } else if (runningCommand == CMD_CIPSERVER) {
    runningCommand = CMD_SET_CIPSTO;
    Serial1.println("AT+CIPSTO=0");

  } else if (runningCommand == CMD_SET_CIPSTO) {
    runningCommand = CMD_COMPLETED;
    Serial.println("ALL DONE");
  }
}

void handleInput(String input) {
  input.trim();//trimming input before use

  if (input.equals(MOVE_FORWARD)) {
    stopRobot();
    delay(10);
    moveForward();

  } else if (input.equals(MOVE_BACKWARD)) {
    stopRobot();
    delay(10);
    moveBackward();

  } else if (input.equals(TURN_LEFT)) {
    stopRobot();
    delay(10);
    moveLeft();

  } else if (input.equals(TURN_RIGHT)) {
    stopRobot();
    delay(10);
    moveRight();

  } else if (input.equals(STOP)) {
    stopRobot();
    delay(10);

  }  else if (input.equals(HORN)) {
    giveHorn();

  } else {
    Serial.println("Unhandled input:" + input);
  }
}


void moveForward() {
  Serial.println("FORWARD...");

  currentDirection = MOVE_FORWARD;
  mMovementSpeed = MAX_SPEED;

  MOTOR1.setSpeed(mMovementSpeed);
  MOTOR2.setSpeed(mMovementSpeed);
  MOTOR3.setSpeed(mMovementSpeed);
  MOTOR4.setSpeed(mMovementSpeed);

  MOTOR1.run(FORWARD);
  MOTOR2.run(FORWARD);
  MOTOR3.run(FORWARD);
  MOTOR4.run(FORWARD);
}

void moveBackward() {
  Serial.println("BACKWARD...");

  currentDirection = MOVE_BACKWARD;
  mMovementSpeed = MAX_SPEED;

  MOTOR1.setSpeed(mMovementSpeed);
  MOTOR2.setSpeed(mMovementSpeed);
  MOTOR3.setSpeed(mMovementSpeed);
  MOTOR4.setSpeed(mMovementSpeed);

  MOTOR1.run(BACKWARD);
  MOTOR2.run(BACKWARD);
  MOTOR3.run(BACKWARD);
  MOTOR4.run(BACKWARD);
}

void moveLeft() {
  Serial.println("LEFT...");

  currentDirection = TURN_LEFT;
  mMovementSpeed = TURN_SPEED;

  MOTOR1.setSpeed(mMovementSpeed);
  MOTOR2.setSpeed(mMovementSpeed);
  MOTOR3.setSpeed(mMovementSpeed);
  MOTOR4.setSpeed(mMovementSpeed);

  MOTOR1.run(BACKWARD);
  MOTOR2.run(BACKWARD);
  MOTOR3.run(FORWARD);
  MOTOR4.run(FORWARD);
}


void moveRight() {
  Serial.println("RIGHT...");

  currentDirection = TURN_RIGHT;
  mMovementSpeed = TURN_SPEED;

  MOTOR1.setSpeed(mMovementSpeed);
  MOTOR2.setSpeed(mMovementSpeed);
  MOTOR3.setSpeed(mMovementSpeed);
  MOTOR4.setSpeed(mMovementSpeed);

  MOTOR1.run(FORWARD);
  MOTOR2.run(FORWARD);
  MOTOR3.run(BACKWARD);
  MOTOR4.run(BACKWARD);
}

void stopRobot() {
  Serial.println("STOP...");

  currentDirection = STOP;

  MOTOR1.run(RELEASE);
  MOTOR2.run(RELEASE);
  MOTOR3.run(RELEASE);
  MOTOR4.run(RELEASE);
}

void giveHorn() {
  analogWrite(PIN_HORN, 150);
  delay(200);
  analogWrite(PIN_HORN, 0);
}

void getDistance() {
  delay(50); //Wait 50ms between pings (about 20 pings/sec). 29ms should be the shortest delay between pings.

//  short distance1 = SONAR_1.ping_cm();
//  short distance2 = SONAR_2.ping_cm();
//
//  Serial.print("Ping: ");
//  Serial.print(distance1); // Send ping, get distance in cm and print result (0 = outside set distance range)
//  Serial.print(" : ");
//  Serial.print(distance2);
//  Serial.println(" cm");
//
//  if (currentDirection.equals(MOVE_FORWARD)) {
//    if (isDistanceAtCollision(distance1) && isDistanceAtCollision(distance2)) {
//      if (distance1 < distance2) {
//        lastCollisionDistance = distance1;
//      } else {
//        lastCollisionDistance = distance2;
//      }
//
//      onCollisionDetected();
//
//    } else if (isDistanceAtCollision(distance1)) {
//      lastCollisionDistance = distance1;
//      onCollisionDetected();
//
//    } else if (isDistanceAtCollision(distance2)) {
//      lastCollisionDistance = distance2;
//      onCollisionDetected();
//    }
//  }
}


void onCollisionDetected() {
  Serial.println("Collision Detected at " + lastCollisionDistance);
  //Stopping robot
  handleInput(STOP);

  if (!sendingData) {
    sendingData = true;
    Serial.println("Requesting ESP to send data");

    //Telling controller about collision
    if (lastCollisionDistance < 10) { 
      sendData(lastConnectionId, 33);
    } else {
      sendData(lastConnectionId, 34);
    }
  }
}


void sendData(byte connectionId, byte dataLength) {
  Serial1.print("AT+CIPSEND=");
  Serial1.print(lastConnectionId);
  Serial1.print(",");
  Serial1.println(dataLength);
}

boolean isDistanceAtCollision(int distance) {
  return distance > 0 && distance <= COLLISION_DISTANCE_CM;
}

