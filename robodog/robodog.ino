/*
 * Gömülü Sistemler Proje Ödevi
 * --------------------------------------------
 */

#include <Servo.h>

// Bacak servoları
Servo onSol, onSag, arkaSol, arkaSag;
// Kesme pinleri (2 ve 3 olması zorunlu)
const int soundPin1 = 2; 
const int soundPin2 = 3; 
// HC-SR04 pinleri
const int trigPin  = 7;
const int echoPin  = 8;

// Robotun o an ne yaptığını tuttuğumuz durum değişkeni
enum RobotState { IDLE, FORWARD, BACKWARD, SIT, WAVE_LEFT, WAVE_RIGHT };
RobotState currentState = IDLE;

// Timer ve bekleme işlemleri için değişkenler
volatile bool tick100ms = false;
volatile int timer2Counter = 0;
unsigned long lastMoveUpdate = 0;
int gaitStep = 0;
const int moveInterval = 500; // Her adım arası bekleme süresi

// Alkış sayacı ve debounce (üst üste yanlış saymaması için) değişkenleri
volatile int clapCount1 = 0;
volatile int clapCount2 = 0;
unsigned long lastClapTime1 = 0;
unsigned long lastClapTime2 = 0;
const unsigned long debounceTime = 300;
const unsigned long clapTimeout = 1500;

// Engel mesafesi
volatile long distance = 100;

// Timer2 Kesmesi: Her 1ms'de buraya giriyor.
ISR(TIMER2_COMPA_vect) {
  timer2Counter++;
  if (timer2Counter >= 100) { // 100 kere girince 100ms olmuş demek
    tick100ms = true;
    timer2Counter = 0; // Sayacı sıfırla ki baştan saysın
  }
}

void setup() {
  Serial.begin(9600); // Bluetooth ve debug için

  // Pin giriş çıkış ayarları
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(soundPin1, INPUT);
  pinMode(soundPin2, INPUT);

  // Servoları pinlere bağlıyoruz
  onSol.attach(11); onSag.attach(5);
  arkaSol.attach(6); arkaSag.attach(9);
  resetPosture(); // Başta düz dursun

  // Alkış sensörleri için dış kesmeler (Sinyal yükseldiğinde tetikleniyor)
  attachInterrupt(digitalPinToInterrupt(soundPin1), clapISR1, RISING);
  attachInterrupt(digitalPinToInterrupt(soundPin2), clapISR2, RISING);

  // Timer2 register ayarları (CTC modunda çalışıyor)
  cli(); // Ayar yaparken kesmeler karışmasın diye durdurduk
  TCCR2A = 0;
  TCCR2B = 0;
  TCNT2  = 0;
  OCR2A = 249; // 1ms elde etmek için değer
  TCCR2A |= (1 << WGM21);  
  TCCR2B |= (1 << CS22);   
  TIMSK2 |= (1 << OCIE2A); 
  sei(); // Ayarlar bitti, kesmeleri tekrar aç

  Serial.println("Sistem basladi. Timer2 calisiyor.");
}

void loop() {
  // Telefondan Bluetooth ile harf gelirse oku
  if (Serial.available()) {
    char cmd = Serial.read();
    processCommand(cmd);
  }

  // Timer2'den 100ms doldu bilgisi gelirse etrafı kontrol et
  if (tick100ms) {
    mesafeOlc();
    akilliKararMekanizmasi();
    tick100ms = false;
  }

  // delay() kullanmadığımız için fonksiyonları sürekli çağırıyoruz
  updateMovement();
  checkClapEvaluation();
}

// Sensörlere göre otonom tepki veren kısım
void akilliKararMekanizmasi() {

  // Köpek oturuyorsa ve önündeki engel çekildiyse tekrar ayağa kalksın
  if (currentState == SIT && distance > 20) {
    Serial.println("Engel gitti, ayaga kalkiliyor.");
    currentState = IDLE;
    resetPosture();
    return;
  }

  // Çok yakında bir şey varsa çarpmasın diye hemen otursun
  if (distance > 0 && distance < 8) {
    if (currentState != SIT) {
      Serial.println("Cok yakin! Otur.");
      currentState = SIT;
      sitPosture();
    }
    return;
  }

  // Yürürken engel çıkarsa dursun (IDLE moduna geçsin)
  if (distance > 0 && distance < 15) {
    if (currentState == FORWARD) {
      Serial.println("Engel var, durduruldu.");
      currentState = IDLE;
      resetPosture();
    }
  }
}

// Gelen karaktere göre durumu değiştir
void processCommand(char cmd) {
  switch (cmd) {
    case 'F': currentState = FORWARD; break;
    case 'B': currentState = BACKWARD; break;
    case 'S': currentState = IDLE; resetPosture(); break;
    case 'X': currentState = IDLE; resetPosture(); Serial.println("Acil Dur!"); break;
    case 'T': currentState = SIT; sitPosture(); break;
  }
}

// Yürüme animasyonlarını millis() ile beklemeden yaptığımız fonksiyon
void updateMovement() {
  unsigned long now = millis();
  
  // İleri gitme modu
  if (currentState == FORWARD) {
    if (now - lastMoveUpdate > moveInterval) {
      lastMoveUpdate = now;
      gaitStep = (gaitStep + 1) % 4; // 4 adımlı döngü
      
      // Bacakların sırayla atılması
      switch (gaitStep) {
        case 0: onSag.write(60);  arkaSol.write(120); break;
        case 1: onSag.write(120); arkaSol.write(60);  break;
        case 2: onSol.write(60);  arkaSag.write(120); break;
        case 3: onSol.write(120); arkaSag.write(60);  break;
      }
    }
  }
  // Geri gitme modu
  else if (currentState == BACKWARD) {
    if (now - lastMoveUpdate > moveInterval) {
      lastMoveUpdate = now;
      gaitStep = (gaitStep + 1) % 4;
      
      // Geri adım atma açıları
      switch (gaitStep) {
        case 0: onSag.write(120); arkaSol.write(60);  break;
        case 1: onSag.write(90);  arkaSol.write(90);  break;
        case 2: onSol.write(120); arkaSag.write(60);  break;
        case 3: onSol.write(90);  arkaSag.write(90);  break;
      }
    }
  }
  else if (currentState == SIT) {
    sitPosture();
  }
}

// Klasik HC-SR04 ping atma kodu
void mesafeOlc() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH, 30000); // Çok bekleyip kilitlenmesin diye 30ms limit
  if (duration > 0) {
    distance = duration * 0.034 / 2; // Sesi cm'ye çevir
  }
}

// Sol sensör için kesme (Gürültüden 2 kere saymasın diye zaman kontrolü var)
void clapISR1() { 
  unsigned long now = millis();
  if (now - lastClapTime1 > debounceTime) {
    clapCount1++;
    lastClapTime1 = now;
  }
}

// Sağ sensör için kesme
void clapISR2() { 
  unsigned long now = millis();
  if (now - lastClapTime2 > debounceTime) {
    clapCount2++;
    lastClapTime2 = now;
  }
}

// Alkış sayısına göre pati verme eylemleri
void checkClapEvaluation() {
  unsigned long now = millis();
  
  // Sol taraftan gelen sese tepki
  if (clapCount1 > 0 && (now - lastClapTime1 > clapTimeout)) {
    if (clapCount1 == 3) { // 3 kere alkışlanırsa sol patiyi kaldır
      Serial.println("Sol pati kalkiyor.");
      onSol.write(45);
      delay(1000); // Sadece animasyon geçişi için ufak bir delay 
      resetPosture();
    }
    else if (clapCount1 >= 4) { // 4 veya daha fazlaysa otur
      Serial.println("Otur.");
      arkaSol.write(60); arkaSag.write(120);
      delay(1000);
      resetPosture();
    }
    clapCount1 = 0; // İşlem bitince sayacı sıfırla
  }

  // Sağ taraftan gelen sese tepki
  if (clapCount2 > 0 && (now - lastClapTime2 > clapTimeout)) {
    if (clapCount2 == 2) { // 2 kere alkışlanırsa sağ patiyi kaldır
      Serial.println("Sag pati kalkiyor.");
      onSag.write(135);
      delay(1000);
      resetPosture();
    }
    clapCount2 = 0;
  }
}

// Arka bacakları kırarak oturma pozisyonu açıları
void sitPosture() {
  onSol.write(90);
  onSag.write(90);
  arkaSol.write(60);
  arkaSag.write(120);
}

// Tüm motorları 90 dereceye (dik duruşa) getir
void resetPosture() {
  onSol.write(90); onSag.write(90);
  arkaSol.write(90); arkaSag.write(90);
}