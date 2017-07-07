#include <EEPROMex.h>
#include <sms.h>
#include "SIM900.h"
#include <SoftwareSerial.h>
#include <DHT.h>
#include <TimerOne.h>
#include <Thread.h>
#include "LcdContent.h"
#include "Schedule.h"
#include "Buttons.h"
#include "SMSProcessing.h"
#include <functional>
#include <Bounce2.h>
#include <SimpleTimer.h>
#include "Constans.h"

LcdContent::MODES _normalOrStopMode();
Thread threadEvery1s = Thread();
Thread threadEvery5s = Thread();
SimpleTimer lcdMessageTimer, lcdLightTimer, pumpOffTimer, timeout;

int pumpOffTimerId, lcdLightTimerId, lcdMessageTimerId;
bool isTimeplaneZone1InEEPROM = false;
bool isTimeplaneZone2InEEPROM = false;
int timelineStartPointerEEPROM;

unsigned long pumpOnTimeStamp = 0;
const float waterFlowK = 52;   // подобпать коэф. на месте. (дома на 5 литрах было ~85 или 52)



void setup()
{
	Serial.begin(19200);
	//GSM.begin();

	if (gsm.begin(19200)) {
		Serial.println("\nGSM STARTED");
		GSM.DeleteAllSMS();
		delay(500);
		gsm.SendATCmdWaitResp("AT+CNMI=2,2", 1000, 50, "OK", 2);

	}



	digitalWrite(RELAY1_PIN, HIGH);
	pinMode(RELAY1_PIN, OUTPUT);
	pinMode(WATER_LEVEL_PIN, INPUT_PULLUP);
	pinMode(LIGHT_SENSOR, INPUT);
	pinMode(LCD_LIGHT_RED, OUTPUT);
	dh11.begin();

	Timer1.initialize(1000000);
	Timer1.attachInterrupt(timer1_action);


	////// датчик потока
   // pinMode(WATER_FLOW_PIN, INPUT);
   // attachInterrupt(5, rpm, RISING);
	///////


	threadEvery5s.onRun(threadEvery5sAction);
	threadEvery5s.setInterval(5000);

	threadEvery1s.onRun(threadEvery1sAction);
	threadEvery1s.setInterval(1000);

	// инициализация часов
	clock.begin();
	// метод установки времени и даты в модуль вручную
	// clock.set(10,25,45,27,07,2005,THURSDAY);
	// метод установки времени и даты автоматически при компиляции
	clock.set(__TIMESTAMP__);

	LCD16x2.begin(16, 2);
	LCD16x2.command(0b101010);

	showLcdMessage(5000, 15000, LcdContent::MESSAGE, "     \xcf\xd0\xc8\xc2\xc5\xd2\x2c", "\xc3\xce\xd2\xce\xc2\xc0 \xca \xd0\xc0\xc1\xce\xd2\xc5\x90");
	EEEPROMRecovery();

	// кнопки
	pinMode(BUTTON_PUMP, INPUT);
	pinMode(BUTTON_LIGHT, INPUT);
	pinMode(BUTTON_3, INPUT);
	pinMode(BUTTON_4, INPUT);



	// после создания таска taskWateringId нужно перевести дисплей в режим NORMAL
	//"MO TU WE TH FR SA SU, 15:32:00"
	//taskWateringId = schedule.addTask("18:19:00", pumpOnWithoutSms);
	//saveTimeplanToEEPROM(0, taskWateringId);
	//lcdContent.Mode = LcdContent::NORMAL;

	pumpOff();
	timer1_action();
	threadEvery5s.run();

	//for (int addr = 0; addr<100; addr++) { // для всех ячеек памяти (для Arduino UNO 1024)
	//	byte val = EEPROM.read(addr); // считываем 1 байт по адресу ячейки
	//	Serial.print(addr); // выводим адрес в послед. порт 
	//	Serial.print("\t"); // табуляция
	//	Serial.println(val); // выводим значение в послед. порт
 //    }


}



void loop()
{

	if (threadEvery1s.shouldRun())
		threadEvery1s.run(); // запускаем поток		

	if (threadEvery5s.shouldRun())
		threadEvery5s.run(); // запускаем поток


	pumpOffTimer.run();
	timeout.run();
	// гасим дребезг контактов
	byte pressedButton = getPressedButton();

	switch (pressedButton)
	{
	case 0:
		button1Press();
		break;
	case 1:
		button2Press();
		break;
	case 2:
		button3Press();
		taskWateringId = schedule.addTask("SU, 18:19:00", pumpOnWithoutSms);
		saveTimeplanToEEPROM(0, taskWateringId);
		lcdContent.Mode = LcdContent::NORMAL;
		break;
	case 3:
		button4Press();
		//GSM.SendUSSD("#102#");		
		break;
	}


}

void EEEPROMRecovery() {	
	EEPROMx.setMemPool(10, EEPROMSizeMega);
	timelineStartPointerEEPROM = EEPROMx.getAddress(sizeof(byte));
	isTimeplaneZone1InEEPROM = EEPROMx.readInt(timelineStartPointerEEPROM);
	isTimeplaneZone2InEEPROM = EEPROMx.readInt(EEPROMx.getAddress(sizeof(byte)));	
	//Serial.println("isTimeplaneZone1InEEPROM               " + String(isTimeplaneZone1InEEPROM));

	if (isTimeplaneZone1InEEPROM) {
		int type = EEPROMx.readInt(EEPROMx.getAddress(sizeof(int)));		
		vector<int> daysOfWeekVector;
		for (int i = 0; i < 7; i++) {
			int day = EEPROMx.readInt(EEPROMx.getAddress(sizeof(int)));			
			if(day > 0)
				daysOfWeekVector.push_back(day);
		}
		int hours = EEPROMx.readInt(EEPROMx.getAddress(sizeof(int)));
		int minunes = EEPROMx.readInt(EEPROMx.getAddress(sizeof(int)));
		int seconds = EEPROMx.readInt(EEPROMx.getAddress(sizeof(int)));

		schedule.items.push_back(Schedule::ScheduleItem((Schedule::TYPE)type, daysOfWeekVector, hours, minunes, seconds, pumpOnWithSms));
		taskWateringId = schedule.items.size() - 1;		
	}

}


void saveTimeplanToEEPROM(int positionOrderInMemory, int taskId) {
	Serial.println("saveTaskToEEPROM");
	Serial.println("positionOrderInMemory" + String(positionOrderInMemory));
	if (positionOrderInMemory > 1) {
		Serial.println("FAIL positionOrderInMemory is max 1" );
		return;
	}

	clearTimeplanInEEPROM(positionOrderInMemory);
	Schedule::ScheduleItem &task = schedule.items.at(taskId);
	int structSize = sizeof(int) * 11;
	// стартовая позиция в памяти + размер флагов(есть ли расписание) + размер сохраняемой структуры 
	int structAddress = timelineStartPointerEEPROM + sizeof(byte) * 2 + structSize * positionOrderInMemory;
	
	// пишем признак того есть ли таймплан для зоны 1 или 2
	EEPROMx.writeByte(timelineStartPointerEEPROM + sizeof(byte)*positionOrderInMemory, 1);

	EEPROMx.writeInt(structAddress, task.type);
	structAddress += sizeof(int);
	for (int i = 0; i < 7; i++) {		
		EEPROMx.writeInt(structAddress, i < task.weekdays.size() ? task.weekdays[i] : -1);
		structAddress += sizeof(int);			
	}
	EEPROMx.writeInt(structAddress, task.hour);
	structAddress += sizeof(int);
	EEPROMx.writeInt(structAddress, task.minute);
	structAddress += sizeof(int);
	EEPROMx.writeInt(structAddress, task.second);
};

void clearTimeplanInEEPROM(int positionOrderInMemory) {
	if (positionOrderInMemory > 1) {
		Serial.println("FAIL positionOrderInMemory is max 1");
		return;
	}
	int structSize = sizeof(int) * 11;				
	int structAddress = timelineStartPointerEEPROM + sizeof(byte) * 2 + structSize * positionOrderInMemory;
	EEPROMx.writeByte(timelineStartPointerEEPROM + sizeof(byte)*positionOrderInMemory, 0);
	for (int i = structAddress; i < structAddress + structSize; i++) {
		EEPROMx.writeByte(i, 255);			
	}
		
}

void threadEvery1sAction() {

	int lightSensorState = digitalRead(LIGHT_SENSOR);
	if (lightSensorState != lastLightSensorState) {
		lastLightSensorState = lightSensorState;
		tone(BEEP_PIN, 5500, 200);
		Serial.println(" LIGHT_SENSOR: " + String(lightSensorState));
	}

	Serial.print(schedule.timeStr);
	Serial.println("\tTemperature: " + String(lastDH11_Temperature) + "C, Humidity: " + String(lastDH11_Humidity) + "%");

	//calcWater();
}



void threadEvery5sAction() {
	lastDH11_Temperature = dh11.readHumidity();
	lastDH11_Humidity = dh11.readTemperature();
	checkIncomingSMS();
	if (counter5second >= 720 || !isBalanceData) {
		GSM.SendUSSD("#102#");
		counter5second = 0;
	}
	if (isnan(lastDH11_Temperature) || isnan(lastDH11_Humidity)) {
		Serial.println("Failed to read from DHT sensor!");
	}
	counter5second++;
}

void timer1_action() {
	schedule.tact();
	checkWaterLevel();
	lcdContentBuilder();
	lcdRunner();
}

void lcdContentBuilder() {

	String addSpaceT = "", addSpaceH = "", animateframe, _first = "", _second = "";
	if (lastDH11_Temperature < 10) {
		addSpaceT = " ";
	}
	if (lastDH11_Humidity < 10) {
		addSpaceH = " ";
	}

	unsigned long diff;
	switch (lcdContent.Mode)
	{

	case  LcdContent::MESSAGE:
		_first = lcdContent.FirstRow;
		_second = lcdContent.SecondRow;
		break;
	case  LcdContent::MESSAGE_HALF:
		_first = String(schedule.timeStr) + " " + addSpaceT + String(lastDH11_Temperature) + "\xb0 " + addSpaceH + String(lastDH11_Humidity) + "%";
		_second = lcdContent.SecondRow;
		break;
	case  LcdContent::WATERING:
		diff = (unsigned long)watering_internal - (millis() - pumpOnTimeStamp) / 1000L;
		animateframe = watering_animate[diff % 4];
		_first = String(schedule.timeStr) + " " + addSpaceT + String(lastDH11_Temperature) + "\xb0 " + addSpaceH + String(lastDH11_Humidity) + "%";
		_second = " \xef\xee\xeb\xe8\xe2 " + animateframe + " " + distanceFormat(diff);

		break;
	case  LcdContent::STOP:
		_first = String(schedule.timeStr) + " " + addSpaceT + String(lastDH11_Temperature) + "\xb0 " + addSpaceH + String(lastDH11_Humidity) + "%";
		_second = "                 ";
		break;
	case  LcdContent::NORMAL:
		_first = String(schedule.timeStr) + " " + addSpaceT + String(lastDH11_Temperature) + "\xb0 " + addSpaceH + String(lastDH11_Humidity) + "%";
		_second = "    \xef\xf3\xf1\xea " + distanceFormat(schedule.timeLeftFor(taskWateringId));
		break;

	}

	lcdContent.set(_first, _second, lcdContent.Mode);

}

void checkWaterLevel() {
	sei();
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
	cli();
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

LcdContent::MODES _normalOrStopMode() {
	return taskWateringId == -1 ? LcdContent::STOP : LcdContent::NORMAL;
}


void pumpOn(bool isNeedSms = false) {
	if (pump_state != PUMP_STATES::WORKING) {
		lcdLightOn(5000);

		if (waterLevel_1 == LOW) {
			sendMessage("Warning! Can't start watering. No water.", isNeedSms, true);
			showLcdMessage(3000, 5000, LcdContent::MESSAGE_HALF, "\xcd\xe5\xeb\xfc\xe7\xff! \xcd\xe5\xf2 \xe2\xee\xe4\xfb");
			return;
		}
		pump_state = PUMP_STATES::WORKING;
		lcdContent.Mode = LcdContent::WATERING;
		digitalWrite(RELAY1_PIN, LOW);
		pumpOnTimeStamp = millis();
		pumpOffTimerId = pumpOffTimer.setTimeout((unsigned long)watering_internal * 1000L, isNeedSms ? pumpOffWithSms : pumpOffWithoutSms);
		sendMessage("Watering start.", isNeedSms);
	}

}

void pumpOnWithSms() {
	pumpOn(true);
}

void pumpOnWithoutSms() {
	pumpOn(false);
}

void pumpOff(bool isNeedSms = false) {
	pumpOffTimer.deleteTimer(pumpOffTimerId);
	if (pump_state != PUMP_STATES::WAITING) {
		lcdLightOn(5000);
		pump_state = PUMP_STATES::WAITING;
		lcdContent.Mode = _normalOrStopMode();
		digitalWrite(RELAY1_PIN, HIGH);
		sendMessage("Watering finish.", isNeedSms);
	}

}

void pumpOffWithSms() {
	pumpOff(true);
}

void pumpOffWithoutSms() {
	pumpOff(false);
}

void sendMessageEmergencyPumpOff() {
	sendMessage("Warning! Emergency stop watering. No water.", true, true);
}

void pumpOffEmergency() {
	pumpOffTimer.deleteTimer(pumpOffTimerId);
	if (pump_state != PUMP_STATES::WAITING) {
		lcdLightOn(5000);
		pump_state = PUMP_STATES::WAITING;
		lcdContent.Mode = _normalOrStopMode();
		digitalWrite(RELAY1_PIN, HIGH);
		timeout.setTimeout(1000, sendMessageEmergencyPumpOff);
		showLcdMessage(3000, 5000, LcdContent::MESSAGE_HALF, "\xd1\xf2\xee\xef! \xcd\xe5\xf2 \xe2\xee\xe4\xfb!");
	}
}

void lcdLightOn(int timer) {
	digitalWrite(LCD_LIGHT_RED, HIGH);
	lcdLightTimer.deleteTimer(lcdLightTimerId);
	lcdLightTimerId = lcdLightTimer.setTimeout(timer, lcdLightOff);
}

void lcdLightOff() {
	digitalWrite(LCD_LIGHT_RED, LOW);
}


void showLcdMessage(int showTimeout, int lightTimeout, LcdContent::MODES mode, char *msg0 = "", char *msg1 = "") {
	if (lcdContent.Mode == LcdContent::WATERING) {
		return;
	}

	lcdMessageTimer.deleteTimer(lcdMessageTimerId);
	lcdMessageTimerId = lcdMessageTimer.setTimeout(showTimeout, hideLcdMessage);

	if (mode == LcdContent::MESSAGE_HALF) {
		lcdContent.Mode = mode;
		lcdContent.setSecondLine(msg0, mode);
	}
	else if (mode == LcdContent::MESSAGE) {
		lcdContent.Mode = mode;
		lcdContent.set(msg0, msg1, mode);
	}

	lcdLightOn(lightTimeout);
	lcdContentBuilder();
	lcdRunner();
}


void hideLcdMessage() {
	lcdMessageTimer.deleteTimer(lcdMessageTimerId);
	if (lcdContent.Mode == LcdContent::MESSAGE || lcdContent.Mode == LcdContent::MESSAGE_HALF)
		lcdContent.Mode = _normalOrStopMode();
	lcdContentBuilder();
	lcdRunner();
}



void lcdRunner() {
	lcdMessageTimer.run();
	lcdLightTimer.run();
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

	//Serial.println("d: " + String(d) + "  h:" + String(h) + "  m:" + String(m) + +"  s:" + String(s));

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




void sendMessage(char* message, bool isNeedSms = false, bool isSendToEachHost = false) {

	if (isDisabledGSM) {
		return;
	}

	Serial.println("\nsendMessage: " + String(isNeedSms ? "with sms " : "no sms ") + String(TRUSTED_NUMBERS[lastHostNumberIndex]) + "\r\n\n" + String(message));
	if (!isNeedSms) {
		return;
	}

	if (isSendToEachHost) {
		unsigned length = sizeof(TRUSTED_NUMBERS) / sizeof(TRUSTED_NUMBERS[0]);
		for (int i = 0; i < length; i++)
		{
			sendSms(message, TRUSTED_NUMBERS[i]);
		}
	}
	else {
		sendSms(message, TRUSTED_NUMBERS[lastHostNumberIndex]);
	}
}

