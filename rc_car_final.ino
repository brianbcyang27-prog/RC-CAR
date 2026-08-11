#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

ESP8266WebServer server(80);

// 預設設定值
String ssid = "RC_CAR";
String password = "12345678";

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

String readSerialLine(unsigned long timeoutMs = 10000)
{
  String input = "";
  
  // instalize/clear serial input
  while (Serial.available()) {
    Serial.read();
  }

  unsigned long start = millis();
  
  while (millis() - start < timeoutMs)
  {
    if (Serial.available())
    {
      char c = Serial.read();

      if (c == '\n' || c == '\r')
      {
        delay(5);
        while (Serial.available())
        {
          char extra = Serial.read();
          if (extra != '\n' && extra != '\r')
          {
            input += extra;
          }
        }
        break;
      }

      if (c >= 32 && c <= 126)
      {
        input += c;
      }
    }
    yield();
  }
  return input;
}

void setupWiFiCredentials()
{
  Serial.println();
  Serial.println("========================================");
  Serial.println("             遙控車啟動設定");
  Serial.println("========================================");
  Serial.println();
  Serial.flush();

  Serial.println("Wi-Fi 名稱設定");
  Serial.println("----------------------------------------");
  Serial.println("請輸入 Wi-Fi 名稱：");
  Serial.print("  ");
  Serial.flush();

  String newSSID = readSerialLine(10000);

  if (newSSID.length() > 0)
  {
    ssid = newSSID;
    Serial.print("完成 - Wi-Fi 名稱設定為：");
    Serial.println(ssid);
  }

  Serial.println();
  Serial.flush();

  Serial.println();
  Serial.println("========================================");
  Serial.println("         ✓ Wi-Fi 熱點建立成功！");
  Serial.println("========================================");
  Serial.println();
  Serial.flush(); 
  delay(50);

  Serial.println("Wi-Fi 資訊");
  Serial.println("----------------------------------------");
  Serial.print("Wi-Fi 名稱：");
  Serial.println(ssid);
  Serial.print("Wi-Fi 密碼：");
  Serial.println(password);
  Serial.flush();
  delay(50);

  Serial.println();
  Serial.println("遙控車控制網址");
  Serial.println("----------------------------------------");
  Serial.print("網址：192.168.4.1");
  Serial.flush();
  delay(50);
  Serial.flush();
}


// motor control(simple joystick logic)
void setMotor(int left, int right)
{
  left = constrain(left, -1023, 1023);
  right = constrain(right, -1023, 1023);

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

void stopCar()
{
  targetLeft = 0;
  targetRight = 0;
}

void updateMotor()
{
  if (currentLeft < targetLeft)  currentLeft += acceleration;
  if (currentLeft > targetLeft)  currentLeft -= acceleration;
  if (currentRight < targetRight) currentRight += acceleration;
  if (currentRight > targetRight) currentRight -= acceleration;

  currentLeft = constrain(currentLeft, -1023, 1023);
  currentRight = constrain(currentRight, -1023, 1023);

  setMotor(currentLeft, currentRight);
}

void moveCar()
{
  if (server.hasArg("x") && server.hasArg("y"))
  {
    int x = server.arg("x").toInt();
    int y = server.arg("y").toInt();

    int forward = map(y, -100, 100, -1023, 1023);
    int turn = map(x, -100, 100, -500, 500);

    targetLeft = forward - turn;
    targetRight = forward + turn;

    targetLeft = constrain(targetLeft, -maxSpeed, maxSpeed);
    targetRight = constrain(targetRight, -maxSpeed, maxSpeed);

    lastCommand = millis();

    server.send(200, "text/plain", "OK");
  }
  else
  {
    server.send(400, "text/plain", "Bad Request");
  }
}

// webpage(html/css/js, all in one file)
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
      h1{ margin-top:25px; }
      #status{ color:#00ff88; }
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
      .info{ font-size:18px; margin:15px; }
    </style>
  </head>  
  <body>
    <h1>ESP8266 RC CAR</h1>
    
    <div id="status">Connected</div>
    <div class="info">Speed: <span id="speed">0</span>%</div>
    
    <div id="joy">
      <div id="stick"></div>
    </div>
    
  
   <script>
    let joy=document.getElementById("joy");
    let stick=document.getElementById("stick");
    let speed=document.getElementById("speed");
    
    let active=false;
    let x=0;
    let y=0;
    let center=130;
    
    let lastSendTime = 0;
    const throttleLimit = 100; // limite prevent overwellingin
    
    function send(force = false)
    {
      let now = Date.now();
      // If we aren't forcing the send, and 100ms hasn't passed, ignore it
      if(!force && now - lastSendTime < throttleLimit) {
        return;
      }
      lastSendTime = now;
      fetch("/move?x="+Math.round(x)+"&y="+Math.round(y)).catch(e => console.log(e));
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
      
      send(false); // send normal throttled command
    }
    
    function reset(){
      x=0;
      y=0;
      
      stick.style.left="90px";
      stick.style.top="90px";
      
      fetch("/stop").catch(e => console.log(e));
      speed.innerHTML=0;
    }
    
    joy.onmousedown=function(e){ active=true; move(e.clientX,e.clientY); };
    document.onmousemove=function(e){ if(active) move(e.clientX,e.clientY); };
    document.onmouseup=function(){ if(active) { active=false; reset(); } };
    
    joy.ontouchstart=function(e){ active=true; move(e.touches[0].clientX,e.touches[0].clientY); };
    joy.ontouchmove=function(e){ if(active){ move(e.touches[0].clientX,e.touches[0].clientY); } };
    joy.ontouchend=function(){ if(active){ active=false; reset(); } };
  </script>
  </body>
</html>
)=====";

// instalizeing function, calling setup function from above
void setup()
{
  Serial.begin(115200);
  delay(1000);

  setupWiFiCredentials();

  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);
  pinMode(STBY, OUTPUT);

  digitalWrite(STBY, HIGH);
  stopCar();

  // FIX BUG: avoid using too much energy cauzing ESP8266 to restart.
  WiFi.setOutputPower(15.0); 
  delay(500); // waiting for power to stabilize

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid.c_str(), password.c_str());

  delay(1500); 

  IPAddress ip = WiFi.softAPIP();

  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", webpage);
  });
  
  server.on("/move", HTTP_GET, moveCar);

  server.on("/stop", HTTP_GET, []() {
    stopCar();
    lastCommand = millis();
    server.send(200, "text/plain", "Stopped");
  });
  
  server.begin();

}


// main loop, fallback included
void loop()
{
  server.handleClient();
  updateMotor();

  if (millis() - lastCommand > timeout)
  {
    stopCar();
  }
}
