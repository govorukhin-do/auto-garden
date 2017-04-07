#include <DHT.h>
#include <TimerOne.h>
#include <Thread.h>
#include "LcdContent.h"
#include "Schedule.h"
#include <functional>
#include <LiquidCrystal.h>
#include <Bounce2.h>
#include <SimpleTimer.h>
Bounce bouncer = Bounce();

Thread waterThread = Thread();
Thread printThread = Thread();
Thread threadEvery5s = Thread();


#define RELAY_PIN 7
#define WATER_FLOW_PIN 19
#define BEEP_PIN 43
#define DH11_PIN A8
#define WATER_LEVEL_PIN A1
#define PUMP_BTN_PIN 2

enum PUMP_STATES { WAITING, WORKING };
PUMP_STATES pump_state = PUMP_STATES::WAITING;

LiquidCrystal LCD16x2(4, 5, 10, 11, 12, 13);
LcdContent lcdContent = LcdContent();
SimpleTimer lcdIntroTimer, pumpOffTimer;

int pumpOffTimerId;
unsigned long watering_internal = 30;   // время полива
unsigned long pumpOnTimeStamp = 0;
int taskWateringId;
String watering_animate[4] = { "\x97", "\x96", "\x95", "\x94" };
const float waterFlowK = 52;   // подобпать коэф. на месте. (дома на 5 литрах было ~85 или 52)
volatile int waterLevel_1 = 0;

// создаём объект для работы с часами реального времени
RTC clock;
Schedule schedule(clock);

DHT dh11(DH11_PIN, DHT11);

volatile int lastDH11_Temperature = 0;
volatile int lastDH11_Humidity = 0;




void printTime() {


	Serial.print(schedule.timeStr);
	Serial.println("\tTemperature: " + String(lastDH11_Temperature) + "C, Humidity: " + String(lastDH11_Humidity) + "%");
	String addSpaceT = "", addSpaceH = "";
	if (lastDH11_Temperature < 10) {
		addSpaceT = " ";
	}
	if (lastDH11_Humidity < 10) {
		addSpaceH = " ";
	}


	lcdContent.set(String(schedule.timeStr) + " " + addSpaceT + String(lastDH11_Temperature) + "\xb0 " + addSpaceH + String(lastDH11_Humidity) + "%",
		String("    \xef\xf3\xf1\xea " + distanceFormat(schedule.timeLeftFor(taskWateringId))), LcdContent::NORMAL);


	if (lcdContent.Mode == LcdContent::WATERING) {
		unsigned long diff = (unsigned long)watering_internal - (millis() - pumpOnTimeStamp) / 1000L;
		String animateframe = watering_animate[diff % 4];

		lcdContent.set(String(schedule.timeStr) + " " + addSpaceT + String(lastDH11_Temperature) + "\xb0 " + addSpaceH + String(lastDH11_Humidity) + "%",
			String(" \xef\xee\xeb\xe8\xe2 " + animateframe + " " + distanceFormat(diff)), LcdContent::WATERING);

	}




}



void threadEvery5sAction() {


	lastDH11_Temperature = dh11.readHumidity();
	lastDH11_Humidity = dh11.readTemperature();

	// Check if any reads failed and exit early (to try again).
	if (isnan(lastDH11_Temperature) || isnan(lastDH11_Humidity)) {
		Serial.println("Failed to read from DHT sensor!");
		return;
	}


}

void timer1_action() {
	schedule.tact();

	int tmp = digitalRead(WATER_LEVEL_PIN);
	if (waterLevel_1 != tmp) {
		waterLevel_1 = tmp;
		if (waterLevel_1 == HIGH) {
			tone(BEEP_PIN, 3000, 200);
			Serial.println("WATER FULL!");
		}
		else {
			tone(BEEP_PIN, 2000, 200);
			Serial.println("WATER Empty!");
			pumpOffEmergency();
		}
	}

}


volatile int NbTopsFan;
void rpm() {
	cli();
	NbTopsFan++;
	sei();
	Serial.println("NbTopsFan: " + String(NbTopsFan));
}
double totalWater = 0;
unsigned long prevCallTime = 0;

void calcWater() {

	if (NbTopsFan != 0) {
		cli();
		double litersPerSec = NbTopsFan / waterFlowK * (1000.0 / (millis() - prevCallTime));

		Serial.println("litersPerSec: " + String(litersPerSec) + "      totalWater: " + String(totalWater));
		NbTopsFan = 0;
		totalWater += litersPerSec;
		prevCallTime = millis();
		sei();
	}
}



void setup()
{

	digitalWrite(RELAY_PIN, HIGH);
	pinMode(RELAY_PIN, OUTPUT);
	pinMode(WATER_LEVEL_PIN, INPUT_PULLUP);
	dh11.begin();




	////// датчик потока
   // pinMode(WATER_FLOW_PIN, INPUT);
   // attachInterrupt(5, rpm, RISING);
   // waterThread.onRun( calcWater );
  //  waterThread.setInterval(1000);
	///////


	threadEvery5s.onRun(threadEvery5sAction);
	threadEvery5s.setInterval(5000);

	printThread.onRun(printTime);
	printThread.setInterval(1000);
	// открываем последовательный порт
	Serial.begin(9600);
	// инициализация часов
	clock.begin();
	// метод установки времени и даты в модуль вручную
	// clock.set(10,25,45,27,07,2005,THURSDAY);
	// метод установки времени и даты автоматически при компиляции
	clock.set(__TIMESTAMP__);

	LCD16x2.begin(16, 2);
	LCD16x2.command(0b101010);
	lcdIntroTimer.setTimeout(5000, stopIntro);
	lcdContent.set("     \xcf\xd0\xc8\xc2\xc5\xd2\x2c",
		"\xc3\xce\xd2\xce\xc2\xc0 \xca \xd0\xc0\xc1\xce\xd2\xc5\x90", LcdContent::INTRO);

	Timer1.initialize(10000);
	Timer1.attachInterrupt(timer1_action);


	// кнопки

	pinMode(PUMP_BTN_PIN, INPUT); // кнопка на пине 2
	digitalWrite(PUMP_BTN_PIN, HIGH); // подключаем встроенный подтягивающий резистор
	bouncer.attach(PUMP_BTN_PIN); // устанавливаем кнопку
	bouncer.interval(5); // устанавливаем параметр stable interval = 5 мс


	/////

	//schedule.addTask("00:09", pumpOff);
	taskWateringId = schedule.addTask("18:19:00", pumpOn);

	pumpOff();
	threadEvery5s.run();
}


void loop()
{

	if (printThread.shouldRun())
		printThread.run(); // запускаем поток

	if (waterThread.shouldRun())
		waterThread.run(); // запускаем поток

	if (threadEvery5s.shouldRun())
		threadEvery5s.run(); // запускаем поток

	lcdRunner();
	pumpOffTimer.run();


	// гасим дребезг контактов
	if (bouncer.update())
	{
		if (bouncer.read() == 1)
		{
			switch (pump_state) {
			case PUMP_STATES::WORKING:
				pumpOff();
				break;
			case PUMP_STATES::WAITING:
				pumpOn();
				break;
			}
		}

	}


	//	tone(22, 2000, 500);
	//tone1.play(NOTE_B2,1000);

}



void pumpOn() {
	if (pump_state != PUMP_STATES::WORKING) {
		if (waterLevel_1 == LOW) {
			sendMessage("Warning! Can't start watering. No water.");
			return;
		}		
		sendMessage("Watering start.");
		pump_state = PUMP_STATES::WORKING;
		lcdContent.Mode = LcdContent::WATERING;
		digitalWrite(RELAY_PIN, LOW);
		pumpOnTimeStamp = millis();
		pumpOffTimerId = pumpOffTimer.setTimeout((unsigned long)watering_internal * 1000L, pumpOff);
	}

}
void pumpOff() {
	pumpOffTimer.deleteTimer(pumpOffTimerId);

	if (pump_state != PUMP_STATES::WAITING) {		
		sendMessage("Watering finish.");
		pump_state = PUMP_STATES::WAITING;
		lcdContent.Mode = LcdContent::NORMAL;
		digitalWrite(RELAY_PIN, HIGH);
	}

}

void pumpOffEmergency() {
	pumpOffTimer.deleteTimer(pumpOffTimerId);
	if (pump_state != PUMP_STATES::WAITING) {
		sendMessage("Warning! Emergency stop watering. No water.");		
		pump_state = PUMP_STATES::WAITING;
		lcdContent.Mode = LcdContent::NORMAL;
		digitalWrite(RELAY_PIN, HIGH);
	}		
}


void stopIntro() {
	LCD16x2.clear();
	if (lcdContent.Mode == LcdContent::INTRO)
		lcdContent.Mode = LcdContent::NORMAL;

}

void lcdRunner() {
	lcdIntroTimer.run();
	if (lcdContent.hasNew) {
		lcdContent.hasNew = false;
		LCD16x2.setCursor(0, 0);
		LCD16x2.print(lcdContent.FirstRow);
		LCD16x2.setCursor(0, 1);
		LCD16x2.print(lcdContent.SecondRow);

	}


}

String distanceFormat(unsigned long distance_in_second) {

	int s = distance_in_second % 60;
	distance_in_second /= 60;
	int m = distance_in_second % 60;
	distance_in_second /= 60;
	int h = distance_in_second % 24;
	distance_in_second /= 24;
	int d = distance_in_second;

	// Serial.println("d: " + String(d) + "  h:" + String(h) + "  m:" + String(m) + +"  s:" + String(s));

	String _d = d < 10 ? "0" + String(d) : String(d);
	String _h = h < 10 ? "0" + String(h) : String(h);
	String _m = m < 10 ? "0" + String(m) : String(m);
	String _s = s < 10 ? "0" + String(s) : String(s);

	String result = String();
	String addSpaces = "       ";

	if (d > 0) {
		result = String(d) + "\xe4 ";
		result += h > 0 ? _h + "\xf7" : (m > 0 ? _m + "\xec" : _s + "\xf1");

	}
	else {
		if (h > 0) {
			result = String(h) + "\xf7 ";
			result += m > 0 ? _m + "\xec" : _s + "\xf1";
		}
		else {
			result = _m + "\xec " + _s + "\xf1";
		}

	}

	if ((int)7 - (int)result.length() > 0) {
		result = addSpaces.substring(0, 7 - result.length()) + result;
	}
	return String(result);
}



void sendMessage(String str) {
	Serial.println(str);


}
