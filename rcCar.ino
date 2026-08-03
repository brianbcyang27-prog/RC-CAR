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

int maxSpeed = 850;
int acceleration = 20;

int currentLeft = 0;
int currentRight = 0;
int targetLeft = 0;
int targetRight = 0;

unsigned long lastCommand = 0;
const int timeout = 500;


void setMotor(int left, int right)
{
  left = constrain(left, -1023, 1023);
  right = constrain(right, -1023, 1023);

  if(left > 0)
  {
    analogWrite(AIN1, left);
    digitalWrite(AIN2, LOW);
  }
  else if(left < 0)
  {
    digitalWrite(AIN1, LOW);
    analogWrite(AIN2, -left);
  }
  else
  {
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, LOW);
  }

  if(right > 0)
  {
    analogWrite(BIN1, right);
    digitalWrite(BIN2, LOW);
  }
  else if(right < 0)
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


void stopCar()
{
  targetLeft = 0;
  targetRight = 0;
}


void updateMotor()
{
  if(currentLeft < targetLeft)
    currentLeft += acceleration;

  if(currentLeft > targetLeft)
    currentLeft -= acceleration;

  if(currentRight < targetRight)
    currentRight += acceleration;

  if(currentRight > targetRight)
    currentRight -= acceleration;

  currentLeft = constrain(currentLeft,-1023,1023);
  currentRight = constrain(currentRight,-1023,1023);

  setMotor(currentLeft,currentRight);
}


void moveCar()
{
  if(server.hasArg("x") && server.hasArg("y"))
  {
    int x = server.arg("x").toInt();
    int y = server.arg("y").toInt();

    int forward = map(y,-100,100,-1023,1023);
    int turn = map(x,-100,100,-500,500);

    targetLeft = forward - turn;
    targetRight = forward + turn;

    targetLeft = constrain(targetLeft,-maxSpeed,maxSpeed);
    targetRight = constrain(targetRight,-maxSpeed,maxSpeed);

    lastCommand = millis();

    server.send(200,"text/plain","OK");
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
      已連接
    </div>
    
    <div class="info">
      速度:
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
    
    function send()
    {
    fetch(
    "/move?x="+Math.round(x)+"&y="+Math.round(y)
    );
    }
      
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
    
    send();
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
  
  digitalWrite(STBY,HIGH);
  
  stopCar();
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid,password);
  
  Serial.println();
  Serial.print("Open: ");
  Serial.println(WiFi.softAPIP());
  
  server.on("/",[](){
    server.send_P(200,"text/html",webpage);
  });
  
  server.on("/move",moveCar);
  
  server.on("/stop",[](){
    stopCar();
    server.send(200,"text/plain","STOP");
  });
  
  server.begin();
  
  Serial.println("Server started");
}



void loop()
{
  server.handleClient();
  
  updateMotor();
  
  if(millis()-lastCommand > timeout)
  {
  stopCar();
  }
}
