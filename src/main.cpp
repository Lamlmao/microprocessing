#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <string>
#include <HardwareSerial.h>

#include "MQ135.h"
#include "PMserial.h"
#include "ESP_Mail_Client.h"
#include "LiquidCrystal_I2C.h"

#define WIFI_SSID "210 Tan Mai"
#define WIFI_PASSWORD "0903200166"

/* SMTP Server*/
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465

/* Sender's email */
#define AUTHOR_EMAIL "lamnh2362002@gmail.com"
#define AUTHOR_PASSWORD "loohaoowbjapknpj"

/* Recipient's email*/
#define RECIPIENT_EMAIL "mrbinh0845@gmail.com"

#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  10        /* Time ESP32 will go to sleep (in seconds) */

// Status
enum {
	safe,
	normal,
	no_long_exposure,
	not_safe,
	dangerous,
	sos,
};

#define PIN_MQ135 34
int mq135 = 34;
MQ135 mq135_sensor = MQ135(PIN_MQ135);
LiquidCrystal_I2C lcd(0x27,16,2);  // set the LCD address to 0x27 for a 16 chars and 2 line display
SerialPM pms(PMS7003, Serial2); // PMSx003, UART

SMTPSession smtp;
ESP_Mail_Session session;	/* Declare the session config data */
SMTP_Message message;	/* Declare the message class */
void smtpCallback(SMTP_Status status); // Provide status updates during the email sending process

typedef struct {
	uint16_t pm1p0;
	uint16_t pm2p5;
	uint16_t pm10;
	float co2;
} data_sensor;

int Dust_level = 0;
void SMS_alert(String textMsg);

void setup()
{
	// Initialize the lcd 
	lcd.init();     
	lcd.backlight();

	Serial.begin(9600);
	Serial2.begin(9600);

	// Connect WiFi
	Serial.print("Connecting to AP");
	WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
	while (WiFi.status() != WL_CONNECTED)
	{
		Serial.print(".");
		delay(250);
	}
	Serial.println("");
	Serial.println("WiFi connected.");
	Serial.println("IP address: ");
	Serial.println(WiFi.localIP());
	Serial.println();
	
	//Enable the debug via Serial port
	smtp.debug(1);		

	/* Set the callback function to get the sending results */
	smtp.callback(smtpCallback);
	/* Set the session config */
	session.server.host_name = SMTP_HOST;
	session.server.port = SMTP_PORT;
	session.login.email = AUTHOR_EMAIL;
	session.login.password = AUTHOR_PASSWORD;
	session.login.user_domain = "";

	/* Set the message headers */
	message.sender.name = "Air monitor device";
	message.sender.email = AUTHOR_EMAIL;
	message.subject = "Warning air quality!!!";
	message.addRecipient("User", RECIPIENT_EMAIL);
	message.text.charSet = "us-ascii";
	message.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
	message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_low;
	message.response.notify = esp_mail_smtp_notify_success | esp_mail_smtp_notify_failure | esp_mail_smtp_notify_delay;

}

void loop()
{
	Serial.println(millis());
	pms.init();

	esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

	data_sensor data_sensor_t = {0};
	// read the PM sensor
	pms.read();
	if (pms)
	{ // successful read
		data_sensor_t.pm1p0 = pms.pm01;
		data_sensor_t.pm2p5 = pms.pm25;
		data_sensor_t.pm10 = pms.pm10;
		Serial.printf("PM1.0 %2d, PM2.5 %2d, PM10 %2d [ug/m3]\n", pms.pm01, pms.pm25, pms.pm10);
	}
	else
	{ // something went wrong
		switch (pms.status)
		{
		case pms.ERROR_TIMEOUT:
		Serial.println(F(PMS_ERROR_TIMEOUT));
		break;
		case pms.ERROR_MSG_UNKNOWN:
		Serial.println(F(PMS_ERROR_MSG_UNKNOWN));
		break;
		case pms.ERROR_MSG_HEADER:
		Serial.println(F(PMS_ERROR_MSG_HEADER));
		break;
		case pms.ERROR_MSG_BODY:
		Serial.println(F(PMS_ERROR_MSG_BODY));
		break;
		case pms.ERROR_MSG_START:
		Serial.println(F(PMS_ERROR_MSG_START));
		break;
		case pms.ERROR_MSG_LENGTH:
		Serial.println(F(PMS_ERROR_MSG_LENGTH));
		break;
		case pms.ERROR_MSG_CKSUM:
		Serial.println(F(PMS_ERROR_MSG_CKSUM));
		break;
		case pms.ERROR_PMS_TYPE:
		Serial.println(F(PMS_ERROR_PMS_TYPE));
		break;
		}
	}

	// CO2
	data_sensor_t.co2 = analogRead(mq135);
	Serial.print("CO2: ");
  	Serial.print(data_sensor_t.co2);
  	Serial.println("ppm");

	// Display data

	char str_data[1];
	// Row 1
	lcd.setCursor(0,0);
	sprintf(str_data, "PM2.5:%u", data_sensor_t.pm2p5);
	lcd.print(str_data);		memset(str_data, 0, sizeof(str_data));
	// Row 2
	lcd.setCursor(0,1);
	if (data_sensor_t.pm2p5 <= 12)
	{
		Dust_level = safe;
		lcd.print("Very Safe <3");
	} else if (12 < data_sensor_t.pm2p5 && data_sensor_t.pm2p5 <= 35.4)
	{
		Dust_level = normal;
		lcd.print("Normal");
	} else if (35.5 < data_sensor_t.pm2p5 && data_sensor_t.pm2p5 <= 55.4)
	{
		Dust_level = no_long_exposure;
		lcd.print("Not Good.");		//no long exposure
	} else if (55.5 < data_sensor_t.pm2p5 && data_sensor_t.pm2p5 <= 150.4)
	{
		Dust_level = not_safe;
		lcd.print("Danger!");
	} else if (150.5 < data_sensor_t.pm2p5 && data_sensor_t.pm2p5 <= 250.4)
	{
		Dust_level = dangerous;
		lcd.print("Dangerous!!"); 
	} else {
		Dust_level = sos;
		lcd.print("S.O.S!!!");
	}
	
	String textmessage = "Warning!!!\nDust concentration at ";
	switch (Dust_level)
	{
		case no_long_exposure:
		{
			textmessage = textmessage + "AQI no_long_exposure level(101-150).";
			break;
		}
		case not_safe:
		{
			textmessage = textmessage + "AQI not safe level(151-200).";
			break;
		}
		case dangerous:
		{
			textmessage = textmessage + "AQI dangerous level(201-300).";
			break;
		}
		case sos:
		{
			textmessage = textmessage + "AQI dangerous level(>301).";
			break;
		}
	default:
		break;
	}
	SMS_alert(textmessage);
	smtp.sendingResult.clear();

	Serial.println("Going to sleep now");	delay(1000);
	Serial.flush(); 
	Serial.println(millis());

	esp_light_sleep_start();	// light sleep
}

/* Callback function to get the Email sending status */
void smtpCallback(SMTP_Status status)
{
	/* Print the current status */
	Serial.println(status.info());

	/* Print the sending result */
	if (status.success())
	{
		Serial.println("----------------");
		ESP_MAIL_PRINTF("Message sent success: %d\n", status.completedCount());
		ESP_MAIL_PRINTF("Message sent failed: %d\n", status.failedCount());
		Serial.println("----------------\n");
		struct tm dt;

		for (size_t i = 0; i < smtp.sendingResult.size(); i++)
		{
			/* Get the result item */
			SMTP_Result result = smtp.sendingResult.getItem(i);
			time_t ts = (time_t)result.timestamp;
			localtime_r(&ts, &dt);

			ESP_MAIL_PRINTF("Message No: %d\n", i + 1);
			ESP_MAIL_PRINTF("Status: %s\n", result.completed ? "success" : "failed");
			ESP_MAIL_PRINTF("Date/Time: %d/%d/%d %d:%d:%d\n", dt.tm_year + 1900, dt.tm_mon + 1, dt.tm_mday, dt.tm_hour, dt.tm_min, dt.tm_sec);
			ESP_MAIL_PRINTF("Recipient: %s\n", result.recipients);
			ESP_MAIL_PRINTF("Subject: %s\n", result.subject);
		}
		Serial.println("----------------\n");

	}
}

void SMS_alert(String textMsg)
{
	message.text.content = textMsg.c_str();

	/* Connect to server with the session config */
	if (!smtp.connect(&session))
		return;
	/* Start sending Email and close the session */
	if (!MailClient.sendMail(&smtp, &message))
		Serial.println("Error sending Email, " + smtp.errorReason());
}
