#define ATMEGA168

//#include "WProgram.h"

//#include <avr/io.h>
//#include <avr/interrupt.h>
//#include <avr/signal.h>
//#include <avr/delay.h>
//#include <stdio.h>
//#include <stdarg.h>

#include "../../arduino/ArduinoCmds.hpp"
#include "../../arduino/DataPacketStructs.hpp"
#include "EncoderDefines.h"
#include "EncoderFunc.h"

/* PIN DEFINITIONS */
/** SPI **/
#define SPI_MOSI 				/*13*/
#define SPI_MISO				7/*12*/
#define SPI_CLK					6/*11*/
#define SPI_SS_LEFT_MOTOR_ENCODER		5/*10*/
#define SPI_SS_RIGHT_MOTOR_ENCODER		4/*9*/
#define BITBANG_SPI 1

#define NUM_PK_STORE 25

#define TIMEOUT_LENGTH_MILLIS 1000

//TODO:
//	- generalize this to work for all of our code
//	- - pull/push data, read/set one/all/several values, fixed/variable length messages
//	- write SPI library
//	- comment everything


//TODO: make this a typedef (why doesn't it work?)
struct motorEncoderData{
	int leftMotorTick;
	int rightMotorTick;
	unsigned int time; //this should be made an int, if possible
};

/* VARIABLES */
/* SERIAL */
int incomingByte = 0;
int dl, dr;
unsigned int dt;
/* MOTOR ENCODERS */
struct motorEncoderData current = {}; // TODO: rename these
struct motorEncoderData previous = {};
//double heading = 0;
// Settings
unsigned long lastsendtime;
int interog_dl = 100;
int sendMode = MC_PULL;
int sendType = MC_SEND_DTICK;
long rx_num;
long tx_num;

long int global_time;
long int arduino_time;


//long unsigned int reply_dtick_packet_num[50];
packet_t packet_store[NUM_PK_STORE];
//reply_dtick_t reply_dtick_packet_store[50];
int packet_store_pos;

void setup(void) {
	/* open the serial port */
	Serial.begin(9600);
	
	/* set the pin modes */ //TODO: do this with a loop (?)
	/* SPI */
	pinMode(SPI_MISO, INPUT);
	//pinMode(SPI_MOSI, OUTPUT);
	pinMode(SPI_SS_LEFT_MOTOR_ENCODER, OUTPUT);
	pinMode(SPI_SS_RIGHT_MOTOR_ENCODER, OUTPUT);
	pinMode(SPI_CLK, OUTPUT);

	/* De-select all SPI devices */
	digitalWrite(SPI_SS_LEFT_MOTOR_ENCODER, HIGH);
	digitalWrite(SPI_SS_LEFT_MOTOR_ENCODER, HIGH);

	// Can this be removed?
	TCCR1A = 0;//set to counter mode
	//TCCR1B = (1<<CS12)|(1<<CS10);// clk1o/1024 table on pg134
	//TCCR1B = (1<<CS10);// no prescale clk1o/1
	TCCR1B = (1<<CS11)|(1<<CS10);//clkio/64
	
	lastsendtime = millis();
	global_time = millis();

	sendMode = MC_PULL;
	sendType = MC_SEND_DTICK;
	
	rx_num = 1;
	tx_num = 1;
	packet_store_pos = 0;

	for(int i = 0; i < (NUM_PK_STORE-1); i++){
		packet_store[i].head.packetnum = 0;
		packet_store[i].msg = NULL;
	}
	//Serial.flush();
}

void loop(void) {

	// TODO: don't do this if data is bad
	//previous = current;
	//readMotorEncoders(&current);

	//calcDelta();

	readSerial();
/*
	Serial.print("header len: ");
	Serial.println(sizeof(header_t));
	Serial.print("msg len: ");
	Serial.println(sizeof(reply_dtick_t));
	delay(3);
*/
}

void calcDelta(void){
	dl = delta(current.leftMotorTick, previous.leftMotorTick, 100, TOTAL_ENCODER_TICKS - 1,  LEFT_MOTOR_ENCODER_DIRECTION); 
	dr = delta(current.rightMotorTick, previous.rightMotorTick, 100, TOTAL_ENCODER_TICKS - 1, RIGHT_MOTOR_ENCODER_DIRECTION);

	if( current.time > previous.time ){
		dt = (current.time - previous.time);
	}
	else{
		dt =  ( ((long)current.time) + 65536) - previous.time;
	}
}

void readMotorEncoders(struct motorEncoderData *data) {

	unsigned int rawData;

	data->time = getTime(); // WARNING: the left and right values aren't measured at exactly the same time

	rawData = SPIReadInt(SPI_MISO, SPI_SS_LEFT_MOTOR_ENCODER, SPI_CLK);
	data->leftMotorTick = convertMotorEncoderFormat(rawData);

	/*Serial.print("Left Raw: ");
	Serial.print(rawData, BIN);
	Serial.print("\t");*/

	rawData = SPIReadInt(SPI_MISO, SPI_SS_RIGHT_MOTOR_ENCODER, SPI_CLK);
	data->rightMotorTick = convertMotorEncoderFormat(rawData);
	/*Serial.print("Right Raw: ");
	Serial.print(rawData, BIN);
	Serial.print("\t\n");*/
}

// must hold: maxDiff <= maxVal
int delta(int p2, int p1, int maxDiff, int maxVal, int dir) {
	int diff;
	if (dir == 0) {
		diff = p2 - p1;
	} else {
		diff = p1 - p2;
	}

	if (abs(diff) <= maxDiff) {
		return(diff);
	} else if (abs(maxVal-abs(diff)) <= maxDiff) {
		if (diff >= 0) {
			return(diff  - maxVal);
		} else {
			return(diff + maxVal);
		}
	} else {
		//error: large jump, don't use //TODO: make it not use data
		return(0);
	}
}

/* TODO: write comments
 *
 *
 *
 *
 */
int convertMotorEncoderFormat(unsigned int data) {
	//XX: trash data does not match the spec
	if( (data & 0x0078) | ~(data | 0x3F7F) ) {
		Serial.print("Frame error\n");
		//error
		return(0);
	} else {
		return( (data & 0x0007) | ((data & 0x3F00) >> 5) );
	}
}
/*
void readSerial(void) {
	if (Serial.available() > 0) {

		incomingByte = Serial.read();

		if (incomingByte == 'r') {
			sendStatus();
			return;//return here to keep from also pushing a packet, if PUSH is set, and the timer expired.
		} else if (incomingByte == 'w') {
			while (Serial.available()<2){}  // TODO: add timeout
			byte variableNumber = Serial.read();
			byte variableValue = Serial.read();
			setVariable(variableNumber, variableValue);
		} else if (incomingByte == 'i') {
			Serial.print("e");
		} else if(incomingByte == 'p') {
			unsigned long int packet_num;
			byte * bptr = (byte *)&packet_num;
			while (Serial.available()<4){}  // TODO: add timeout
			bptr[0] = Serial.read();
			bptr[1] = Serial.read();
			bptr[2] = Serial.read();
			bptr[3] = Serial.read();
			resend_packet(packet_num);
		} else {
			//error
		}
	}
	
	if(sendMode == PUSH){
		if(millis() < (lastsendtime + interog_dl)){//auto send timer expired
			sendStatus();
			lastsendtime = millis();
		}
	}
}
*/

void readSerial(void) {
	if (Serial.available() > 0) {
		while (Serial.available() < PACKET_HEADER_SIZE){}// TODO: add timeout
		
		header_t header;
		byte * headerptr = (byte*)&header;

		for(int i = 0; i < PACKET_HEADER_SIZE; i++){
			headerptr[i] = Serial.read();
		}
		rx_num++;
/*
		if(header.size > 0){
			for(int i = 0; i < header.size; i++){
				Serial.read();
			}
		}
*/

/*
		if(header.packetnum != rx_num){
			
		}
*/
		switch(header.cmd){

			case 'r':
			{
				header_t headerOut;
				headerOut.timestamp =  global_time + millis() - arduino_time;
				headerOut.packetnum = tx_num;
				headerOut.cmd = 'r';
				switch(sendType){
					case MC_SEND_CURRENT:
					{
						reply_current_t msg = getCurrentStatus();
						headerOut.size = sizeof(reply_current_t);

						serialPrintBytes(&headerOut, PACKET_HEADER_SIZE);
						serialPrintBytes(&msg, headerOut.size);
						savePacket(headerOut, (byte*)&msg);
						break;
					}
					case MC_SEND_DTICK:
					{
						reply_dtick_t msg = getEncoderStatus();
						headerOut.size = sizeof(reply_dtick_t);
		
						serialPrintBytes(&headerOut, PACKET_HEADER_SIZE);
						serialPrintBytes(&msg, headerOut.size);
						savePacket(headerOut, (byte*)&msg);
						break;
					}
				}
				tx_num++;
				return;//return here to keep from also pushing a packet, if PUSH is set, and the timer expired.
				break;
			}
			case 'w':
			{
				while (Serial.available()<2){}  // TODO: add timeout
				byte variableNumber = Serial.read();
				byte variableValue = Serial.read();
				setVariable(variableNumber, variableValue);

				header_t headerOut;
				headerOut.timestamp =  global_time + millis() - arduino_time;
				headerOut.packetnum = tx_num;
				headerOut.cmd = 'w';
				headerOut.size = 0;
				serialPrintBytes(&headerOut, PACKET_HEADER_SIZE);
				savePacket(headerOut, NULL);
				tx_num++;
				break;
			}
			case 'i':
			{
				header_t headerOut;
				headerOut.timestamp =  global_time + millis() - arduino_time;
				headerOut.packetnum = tx_num;
				headerOut.cmd = 'i';
				headerOut.size = 1;
				serialPrintBytes(&headerOut, PACKET_HEADER_SIZE);
				savePacket(headerOut, NULL);
				tx_num++;
				Serial.print('e');
				break;
			}
			case 'p':
			{
				long packet_num;
				byte * bptr = (byte *)&packet_num;
				while (Serial.available()<4){}  // TODO: add timeout
				bptr[0] = Serial.read();
				bptr[1] = Serial.read();
				bptr[2] = Serial.read();
				bptr[3] = Serial.read();
				resend_packet(packet_num);
				//tx_num++;//this is incr in the resend packet func
				break;
			}
			case 'm':
			{
				//set motor speed
			}
			default:
			{
				
				header_t headerOut;
				headerOut.timestamp =  global_time + millis() - arduino_time;
				headerOut.packetnum = tx_num;
				headerOut.cmd = 0xFF;
				headerOut.size = 0;
				serialPrintBytes(&headerOut, PACKET_HEADER_SIZE);
				tx_num++;
				break;
								
			}
		}

	}
	/*
	if(sendMode == PUSH){
		if(millis() < (lastsendtime + interog_dl)){//auto send timer expired
			sendStatus();
			lastsendtime = millis();
		}
	}
	*/
}

//TODO: make this use an array
void setVariable(byte num, byte val) {
	switch (num) {
		case MC_PUSHPULL:
			{
			sendMode = val;
			break;
			}
		case MC_RET_T:
			{
			sendType = val;
			break;
			}
		case MC_INTEROG_DL:
			{
			//TODO: keep val in sensible boundaries
			/*			
			if(val < XXX){
				val = XXX;
			}
			*/
			interog_dl = val;
			break;
			}
		case MC_SETCLK:
			{			
			unsigned long int mills;
			byte * bptr = (byte *)&mills;
			while (Serial.available()<3){}  // TODO: add timeout
			bptr[0] = val;
			bptr[1] = Serial.read();
			bptr[2] = Serial.read();
			bptr[3] = Serial.read();
			global_time = mills;
			arduino_time = millis();
			break;
			}
		default:
			//error
			break;
	}
	//Serial.println(num);  //TODO: check for errors
}


reply_current_t getCurrentStatus(){
	reply_current_t packet;
	packet.currentl = 0xDEAD;
	packet.currentr = 0xFACE;
	packet.dt = 0xBEEFBEEF;
	return(packet);
}

reply_dtick_t getEncoderStatus() {
	//serialPrintBytes(&(current.leftMotorTick), sizeof(int));  // only 9 bits are used
	//serialPrintBytes(&(current.rightMotorTick), sizeof(int));
	//serialPrintBytes(&heading, sizeof(double));
	static short test = 0;
	test++;
		reply_dtick_t packet;
		//packet.dl = dl;
		//packet.dr = dr;
		//packet.dt = dt;
		packet.dl = test;
		packet.dr = 0xFACE;
		packet.dt = 0xBEEFBEEF;

		return(packet);
}

void serialPrintBytes(void *data, int numBytes) {
	for (int i = 0; i < numBytes; i++) {
		Serial.print(((unsigned char *)data)[i], BYTE);
	}
}

// Fixed point only. Should make scientific notation option.
void serialPrintDouble(double data, int precision) {
	Serial.print((int)data);
	Serial.print(".");
	for(int i = 1; i < (precision+1); i++) {
		data = (data - (double)((int)data)) * 10;
		Serial.print((int)data);
	}
}

/* Get a single integer. Not multi-thread safe.*/
//TODO: change this to SPIReadBytes
int SPIReadInt(int inputPin, int slaveSelectPin, int clockPin) {
//#ifdef BITBANG_SPI
  	unsigned int data = 0;

	digitalWrite(slaveSelectPin, LOW);
	delayMicroseconds(500);//delay 100us before starting clock

	for(int i = 0; i < 16; i++){
		digitalWrite(clockPin, HIGH);
		delayMicroseconds(10);//delay of 10us before data is sent

		//int bit = digitalRead(inputPin);
		//Serial.print(bit);
		data |= digitalRead(inputPin) << (15 - i);
		//data |= bit << (15 - i);


		digitalWrite(clockPin, LOW);
		delayMicroseconds(50);//wait to make this a 20khz signal (encoder goes to 50khz)
	}
	digitalWrite(slaveSelectPin, HIGH);

	delayMicroseconds(50);//the last bit is held for 50us
	delayMicroseconds(1000);//data only refreshed every 1 ms -- note: delay(1) is inaccurate for a 1 ms wait, seems to be only accurate to about max 200us

	//Serial.print("\t");

	return(data);
//#else
//#ifdef HARD_SPI
	
//#else
//	#error "SPIReadInt: SPI mode not set.\n"
//#endif
//#endif
}

void resend_packet(long num){

	//Serial.println(tx_num);
	//Serial.println(tx_num);
	//Serial.println(tx_num);

	for(int i = 0; i < NUM_PK_STORE; i++ ){
		if(packet_store[i].head.packetnum == num){
			header_t headerOut;
			headerOut.timestamp = global_time + millis() - arduino_time;
			headerOut.packetnum = tx_num;
			headerOut.cmd = ARDUINO_RSND_PK_RESP;
			headerOut.size = PACKET_HEADER_SIZE + packet_store[i].head.size;
			byte body[PACKET_HEADER_SIZE + packet_store[i].head.size];
			
			memcpy(body, &packet_store[i].head, PACKET_HEADER_SIZE);
			memcpy(body + PACKET_HEADER_SIZE, packet_store[i].msg, packet_store[i].head.size);//&msg or msg

			serialPrintBytes(&headerOut, PACKET_HEADER_SIZE);
			serialPrintBytes(&packet_store[i].head, PACKET_HEADER_SIZE);
			serialPrintBytes(packet_store[i].msg, packet_store[i].head.size);
			savePacket(headerOut, body);
			tx_num++;
			return;
		}
	}

//		else{
	header_t headOut;
	headOut.timestamp = global_time + millis() - arduino_time;
	headOut.packetnum = tx_num;
	headOut.cmd = ARDUINO_ERROR_RESP;
	headOut.size = sizeof(long);
	long msg = (long)REQUESTED_PACKET_OUT_OF_RANGE;


	serialPrintBytes(&headOut, PACKET_HEADER_SIZE);
	serialPrintBytes(&msg, headOut.size);
	tx_num++;
}

unsigned int getTime(){
	unsigned int time;
	//TCNT1 is a 16 bit timer/counter ~ pg 121
	time = TCNT1L;//how often does this tick? tests say clkio = F_CPU.
	time |= TCNT1H << 8;
	return(time);
}

void resetTime(){
	TCNT1H = 0;
	TCNT1L = 0;
}


void savePacket(header_t head, byte * msg){

	if(packet_store_pos > (NUM_PK_STORE-1)){
		packet_store_pos = 0;
	}

	if(packet_store[packet_store_pos].msg != NULL){
		free(packet_store[packet_store_pos].msg);
		packet_store[packet_store_pos].msg = NULL;
	}

	packet_store[packet_store_pos].head = head;

	if(head.size > 0){
		packet_store[packet_store_pos].msg = (byte*)malloc(head.size);
		memcpy(packet_store[packet_store_pos].msg, msg, head.size);
	}
	packet_store_pos++;

}

//wait for a specifed number of bytes to be in the buffer for a certain amount of time
//read them if they apear
//ret false if they don't
//msg should be array of appropriate length - code will not assign memory
//true on success
//false on failure
bool serialReadBytesTimeout(int len, byte * msg)
{
	//Maybe return imediatly if there is no data availible
	//Only wait if there is a partial connection
	if(Serial.available() == 0)
	{
		return false;
	}

	int t1 = millis();

	while( (millis() - t1) < TIMEOUT_LENGTH_MILLIS)
	{
		if(Serial.available() >= len)
		{
			/*
			if(msg == NULL)
			{
				msg = (byte*)malloc(len);
			}
			*/
			for(int i = 0; i < len; i++)
			{
				msg[i] = Serial.read();
			}
			return true;//we got the message
		}
	}

	//otherwise timeout - flush link
	Serial.flush();
	return false;
}
