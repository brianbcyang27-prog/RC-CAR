/*
  ESP8266 RC CAR
  Board: WeMos D1 R1 / ESP8266

  WiFi:
  SSID: RC_CAR
  Password: 12345678

  web host:
  http://192.168.4.1

  motor driver:
  DRV8833

  Pin
  AIN1 --> D1
  AIN2 --> D2
  BIN1 --> D5
  BIN2 --> D6
  STBY --> D7
  GND --> GND
  VM --> 5V
  NC --> nothing

  BO1 --> left motor
  BO2 -->left motor
  AO1 --> right motor
  AO2 --> right motor
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

const char* ssid = "RC_CAR";
const char* password = "12345678";

ESP8266WebServer server(80);

#define AIN1 D1
#define AIN2 D2
#define BIN1 D5
#define BIN2 D6
#define STBY D7

// --- Tuning ---
const int POWER_LIMIT_PERCENT = 55;              // Overall power cap, 0-100. Lower this if the board resets/browns out under load. Kept conservative because a 4x1.5V (6V) pack sags hard under motor stall current.
const int PWM_MAX = 1023;                        // Full PWM range of the DRV8833
const int MAX_SPEED = (PWM_MAX * POWER_LIMIT_PERCENT) / 100;  // Speed cap in PWM units, derived from POWER_LIMIT_PERCENT above
const int STICK_RANGE = 100;                     // Joystick input range (both axes)
const int TURN_SCALE = 500;                      // Max turn authority in PWM units
const unsigned long COMMAND_TIMEOUT = 1000;      // ms without a command before emergency stop
const unsigned long MOTOR_UPDATE_INTERVAL = 5;   // ms between motor ramp steps
const unsigned long RAMP_TIME_MS = 80;           // ms to go from 0 to full speed
const int acceleration = PWM_MAX / (RAMP_TIME_MS / MOTOR_UPDATE_INTERVAL);  // ramp steps per update

int currentLeft = 0;
int currentRight = 0;
int targetLeft = 0;
int targetRight = 0;

unsigned long lastCommand = 0;
unsigned long lastMotorUpdate = 0;


void setMotor(int left, int right)
{
  left = constrain(left, -PWM_MAX, PWM_MAX);
  right = constrain(right, -PWM_MAX, PWM_MAX);

  if (left > 0)
  {
    analogWrite(AIN1, left);
    digitalWrite(AIN2, LOW);
  }
  else if (left < 0)
  {
    digitalWrite(AIN1, LOW);
    analogWrite(AIN2, -left);
  }
  else
  {
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, LOW);
  }

  if (right > 0)
  {
    analogWrite(BIN1, right);
    digitalWrite(BIN2, LOW);
  }
  else if (right < 0)
  {
    digitalWrite(BIN1, LOW);
    analogWrite(BIN2, -right);
  }
  else
  {
    digitalWrite(BIN1, LOW);
    digitalWrite(BIN2, LOW);
  }
}


void enableMotors()
{
  digitalWrite(STBY, HIGH);
}


void disableMotors()
{
  digitalWrite(STBY, LOW);
}


void stopCar()
{
  targetLeft = 0;
  targetRight = 0;
}


void emergencyStop()
{
  targetLeft = 0;
  targetRight = 0;
  currentLeft = 0;
  currentRight = 0;
  setMotor(0, 0);
  disableMotors();
}


void updateMotor()
{
  // --- LEFT MOTOR ---
  if (currentLeft < targetLeft) {
    currentLeft += acceleration;
    if (currentLeft > targetLeft) currentLeft = targetLeft; // Snap to target, prevent overshoot
  }
  else if (currentLeft > targetLeft) {
    currentLeft -= acceleration;
    if (currentLeft < targetLeft) currentLeft = targetLeft;
  }

  // --- RIGHT MOTOR ---
  if (currentRight < targetRight) {
    currentRight += acceleration;
    if (currentRight > targetRight) currentRight = targetRight;
  }
  else if (currentRight > targetRight) {
    currentRight -= acceleration;
    if (currentRight < targetRight) currentRight = targetRight;
  }

  setMotor(currentLeft, currentRight);

  // Power down the driver once both motors are idle
  if (currentLeft == 0 && currentRight == 0) {
    disableMotors();
  }
}


void moveCar()
{
  if (server.hasArg("x") && server.hasArg("y"))
  {
    int x = constrain(server.arg("x").toInt(), -STICK_RANGE, STICK_RANGE);
    int y = constrain(server.arg("y").toInt(), -STICK_RANGE, STICK_RANGE);

    int forward = map(y, -STICK_RANGE, STICK_RANGE, -PWM_MAX, PWM_MAX);
    int turn = map(x, -STICK_RANGE, STICK_RANGE, -TURN_SCALE, TURN_SCALE);

    targetLeft = constrain(forward - turn, -MAX_SPEED, MAX_SPEED);
    targetRight = constrain(forward + turn, -MAX_SPEED, MAX_SPEED);

    enableMotors();
    lastCommand = millis();  // heartbeat: this is what keeps the failsafe from tripping

    server.sendHeader("Connection", "close");  // force the socket closed right away instead of lingering keep-alive
    server.send(200, "text/plain", "OK");
  }
}


//  WEB PAGE
const char webpage[] PROGMEM = R"=====(

<!DOCTYPE html>
<html>

  <head>
    <meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
    <title>RC CAR</title>
      <style>
      body{
        background:#111;
        color:white;
        font-family:Arial;
        text-align:center;
        margin:0;
        touch-action:none;
      }

      h1{
        margin-top:25px;
      }

      #status{
        color:#00ff88;
      }

      #joy{
        width:260px;
        height:260px;
        background:#222;
        border:3px solid #555;
        border-radius:50%;
        margin:40px auto;
        position:relative;
      }

      #stick{
        width:80px;
        height:80px;
        background:#888;
        border-radius:50%;
        position:absolute;
        left:90px;
        top:90px;
      }

      .info{
        font-size:18px;
        margin:15px;
      }

    </style>
  </head>
  <body>
    <h1>ESP8266 RC CAR</h1>

    <div id="status">
      Connected
    </div>

    <div class="info">
      Speed:
      <span id="speed">0</span>%
    </div>

    <div id="joy">
      <div id="stick"></div>
    </div>

    <div class="info">
      Open:
      192.168.4.1
    </div>


   <script>
    let joy=document.getElementById("joy");
    let stick=document.getElementById("stick");
    let speed=document.getElementById("speed");

    let active=false;
    let x=0;
    let y=0;

    let center=130;

    // IMPORTANT: this must fire on every tick while active, even if x/y
    // haven't changed. The ESP8266 uses each /move request as a heartbeat
    // to know the controller is still connected (see COMMAND_TIMEOUT).
    // Skipping "unchanged" sends here caused the car to hard-stop after
    // ~1s of holding the stick perfectly still (e.g. full throttle
    // straight-line driving) because no heartbeat ever reached the board.
    function send()
    {
    let rx=Math.round(x);
    let ry=Math.round(y);
    fetch("/move?x="+rx+"&y="+ry);
    }

    // Heartbeat: keeps the car alive while the stick is held.
    // Uses requestAnimationFrame instead of setInterval - on mobile
    // browsers, setInterval can get silently throttled while a touch is
    // held perfectly still (no touchmove events), which caused the car
    // to stop ~5s after holding the stick steady. requestAnimationFrame
    // is tied to screen refresh, not touch activity, so it keeps firing.
    // Rate capped at ~10/sec (100ms) - the ESP8266's web server is
    // single-threaded with limited socket slots, so fewer, steadier
    // requests are more reliable than firing as fast as possible.
    let lastSend=0;
    function heartbeat(now){
    if(active && now-lastSend>=100){
    lastSend=now;
    send();
    }
    requestAnimationFrame(heartbeat);
    }
    requestAnimationFrame(heartbeat);

    function move(px,py)
    {
    let rect=joy.getBoundingClientRect();

    let dx=px-(rect.left+center);
    let dy=py-(rect.top+center);

    let distance=Math.sqrt(dx*dx+dy*dy);

    if(distance>100)
    {
    dx=dx/distance*100;
    dy=dy/distance*100;
    }

    x=dx;
    y=-dy;

    stick.style.left=(90+x)+"px";
    stick.style.top=(90-y)+"px";

    speed.innerHTML=Math.round(Math.sqrt(x*x+y*y));
    }


    function reset(){
    x=0;
    y=0;

    stick.style.left="90px";
    stick.style.top="90px";

    fetch("/stop");
    speed.innerHTML=0;
    }


    joy.onmousedown=function(e){
    active=true;
    move(e.clientX,e.clientY);
    };


    document.onmousemove=function(e){
    if(active)
    move(e.clientX,e.clientY);
    };


    document.onmouseup=function(){
    active=false;
    reset();
    };


    joy.ontouchstart=function(e){
    active=true;
    move(
    e.touches[0].clientX,
    e.touches[0].clientY
    );
    };

    joy.ontouchmove=function(e){
    e.preventDefault();
    if(active){
    move(
    e.touches[0].clientX,
    e.touches[0].clientY
    );
    }
    };


    joy.ontouchend=function()
    {
    active=false;
    reset();
    };

    // A touch can be cancelled by the OS (incoming call, notification,
    // system gesture) without ever firing touchend. Without this, "active"
    // would stay true but no more touchmove events arrive, so the car
    // would keep coasting on its last command until COMMAND_TIMEOUT.
    joy.ontouchcancel=function()
    {
    active=false;
    reset();
    };

    // If the browser tab loses focus or the screen locks, stop immediately
    // instead of waiting out COMMAND_TIMEOUT.
    document.addEventListener("visibilitychange",function(){
    if(document.hidden){
    active=false;
    reset();
    }
    });

    window.onblur=function(){
    active=false;
    reset();
    };

  </script>

  </body>
</html>

)=====";


void setup()
{
  Serial.begin(115200);

  pinMode(AIN1,OUTPUT);
  pinMode(AIN2,OUTPUT);
  pinMode(BIN1,OUTPUT);
  pinMode(BIN2,OUTPUT);
  pinMode(STBY,OUTPUT);

  enableMotors();

  stopCar();

  WiFi.mode(WIFI_AP);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);  // Lower AP latency for responsive control
  WiFi.softAP(ssid,password);

  Serial.println();
  Serial.print("Open: ");
  Serial.println(WiFi.softAPIP());

  server.on("/",[](){
    server.sendHeader("Connection", "close");
    server.send_P(200,"text/html",webpage);
  });

  server.on("/move",moveCar);

  server.on("/stop",[](){
    stopCar();
    server.sendHeader("Connection", "close");
    server.send(200,"text/plain","STOP");
  });

  server.begin();

  Serial.println("Server started");
}


void loop()
{
  server.handleClient();

  // Only step the motor ramp every MOTOR_UPDATE_INTERVAL ms
  if (millis() - lastMotorUpdate >= MOTOR_UPDATE_INTERVAL) {
    updateMotor();
    lastMotorUpdate = millis();
  }

  if(millis() - lastCommand > COMMAND_TIMEOUT)
  {
    if (currentLeft != 0 || currentRight != 0) {
      emergencyStop();
    }
  }
}
