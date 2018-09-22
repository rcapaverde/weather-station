#include <SoftwareSerial.h>
#include "SparkFunBME280.h"

char ledPin = 8;

char windPin = 12;
int windCount = 0;
unsigned long windTimer = 0;
unsigned char windGuardTime = 40;
unsigned long windNextTick = 0;
char windCounterEnabled = true;

#define WINDCOUNT_TO_SPEED_KMH  119.36

unsigned short windSpeedArray[60];
char windSpeedHead = 0;
char windSpeedArrayCount = 0;

#define XBEE_SEND_COMMAND             0x01
#define XBEE_WAIT_COMMAND_OK          0x03
#define XBEE_READY                    0x84
#define XBEE_WAIT_ACK                 0x85
#define XBEE_ERROR                    0xFF

char xbeeRxPin = 11;
char xbeeTxPin = 10;
unsigned char xbeeBuffer[128];
unsigned char xbeeBufferPos = 0;
unsigned char xbeeEstado;
char xbeeStartIndex = 0;

#define BME280_INIT   0x01
#define BME280_READY  0x82
#define BME280_ERROR  0xFF

unsigned char bme280Estado = BME280_INIT;

char bme280CSPin = 10;

unsigned long nextPollBME280 = 0;
unsigned long nextDataSend = 0;
unsigned int recvTemperature = 0;
unsigned int recvPressure = 0;
unsigned int recvHumidity = 0;

SoftwareSerial xbeeSerial(xbeeRxPin, xbeeTxPin);
BME280 bme280;

void setup()
{
  pinMode(windPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);

  Serial.begin(9600);

  xbeeSerial.begin(9600);
  xbeeSerial.listen();

  // coloca o xbee em modo comando
  delay(1000);
  xbeeSerial.print("+++");
  delay(1200);

  // limpa a serial
  while (xbeeSerial.available())
    xbeeSerial.read();
  
  xbeeEstado = XBEE_SEND_COMMAND;

  Serial.println("TXWEATHER pronto");
}

char xbeeReceiveText()
{
  while (xbeeSerial.available())
  {
    xbeeBuffer[xbeeBufferPos] = xbeeSerial.read();
    if (xbeeBuffer[xbeeBufferPos] == 0xD || xbeeBufferPos >= sizeof(xbeeBuffer) - 1)
    {
      xbeeBuffer[xbeeBufferPos] = '\0';
      xbeeBufferPos = 0;
      return true;
    }
    xbeeBufferPos++;
  }
  return false;
}

char xbeeReceiveFrame()
{
  while (xbeeSerial.available())
  {
    xbeeBuffer[xbeeBufferPos] = xbeeSerial.read();
    if (xbeeBufferPos == 0 && xbeeBuffer[xbeeBufferPos] != 0x7E)
      return false;

    if (xbeeBufferPos == 3 && ((xbeeBuffer[1] << 8 | xbeeBuffer[2]) + 4) > sizeof(xbeeBuffer))
    {
      xbeeBufferPos == 0;
      return false;
    }
    
    if (xbeeBufferPos > 3 && ((xbeeBuffer[1] << 8 | xbeeBuffer[2]) + 4) <= xbeeBufferPos)
    {
      xbeeBufferPos = 0;
      return true;
    }
    xbeeBufferPos++;
  }
  return false;
}

void xbeeInicializar()
{
  char *xbeeStartSequence[] = {"ATAP1\r", "ATCN\r", NULL};
  char *cmd;
    
  switch (xbeeEstado)
  {
    case XBEE_SEND_COMMAND:
      cmd = xbeeStartSequence[xbeeStartIndex++];
      if (cmd == NULL)
      {
        xbeeEstado = XBEE_READY;
      }
      else
      {
        xbeeSerial.print(cmd);
        xbeeEstado = XBEE_WAIT_COMMAND_OK;
      }
      break;

    case XBEE_WAIT_COMMAND_OK:
      if (xbeeReceiveText())
      {
        if (strcmp(xbeeBuffer, "OK") == 0)
        {
          xbeeEstado = XBEE_SEND_COMMAND;
          digitalWrite(ledPin, LOW);
        }
        else
        {
          xbeeEstado = XBEE_ERROR;
        }
      }
      break;
  }
}

void bme280Inicializar()
{
  bme280.settings.commInterface = SPI_MODE;
  bme280.settings.chipSelectPin = bme280CSPin;
  bme280.settings.runMode = 3;
  bme280.settings.filter = 0;
  bme280.settings.tStandby = 5;
  bme280.settings.tempOverSample = 2;
  bme280.settings.pressOverSample = 2;
  bme280.settings.humidOverSample = 2;

  bme280Estado = (bme280.begin() == 0x60) ? BME280_READY : BME280_ERROR;
}

void verificarVento()
{
  unsigned long now = millis();

  // verifica se o timer virou
  // se este é o caso, ignora esta leitura e reinicia
  if (now < windTimer)
  {
    windCount = 0;
    windTimer = now;
    return;
  }

  char windPinLevel = digitalRead(windPin);

  if (!windCounterEnabled && !windPinLevel)
    windCounterEnabled = true;
  
  if (!windCounterEnabled || now < windNextTick || !windPinLevel)
    return;  

  windCount++;
  windNextTick = now + windGuardTime;
  
  if (now - windTimer >= 1000)
  {
    unsigned short windSpeed = windCount * WINDCOUNT_TO_SPEED_KMH;
    windSpeedArray[windSpeedHead++] = windSpeed;
    if (windSpeedHead >= sizeof(windSpeedArray))
      windSpeedHead = 0;
    if (windSpeedArrayCount < sizeof(windSpeedArray))
      windSpeedArrayCount++;

    char iWindSpeed = windSpeedHead - 1;
    char count = windSpeedArrayCount;
    unsigned int windSpeedAvg = 0;
    unsigned short windSpeedMax = -1;
    while (count)
    {
      if (iWindSpeed < 0)
        iWindSpeed = sizeof(windSpeedArray) - 1;
      windSpeedAvg += windSpeedArray[iWindSpeed];
      if (windSpeedMax < 0 || windSpeedArray[iWindSpeed] > windSpeedMax)
        windSpeedMax = windSpeedArray[iWindSpeed];
      count--;
      iWindSpeed--;
    }

    windSpeedAvg = windSpeedAvg / windSpeedArrayCount;

    windGuardTime = (windCount > 5) ? 500 / windCount : 100;
    windCount = 0;
    windTimer = now;
  }
}


void xbeeEnviarFrame()
{
  xbeeBufferPos = 0;

  // XBEE frame start
  xbeeBuffer[xbeeBufferPos++] = 0x7E;
  // XBEE frame length
  xbeeBuffer[xbeeBufferPos++] = 0;
  xbeeBuffer[xbeeBufferPos++] = 0;

  // TX (Transmit) Request: 16-bit address
  xbeeBuffer[xbeeBufferPos++] = 0x01;
  // Frame ID
  xbeeBuffer[xbeeBufferPos++] = 0x60;
  // Destination Address (broadcast)
  xbeeBuffer[xbeeBufferPos++] = 0xFF;
  xbeeBuffer[xbeeBufferPos++] = 0xFF;
  // Options
  xbeeBuffer[xbeeBufferPos++] = 0x01;
  
  // versão do frame
  xbeeBuffer[xbeeBufferPos++] = 0;
  // velocidade instantânea do vento
  xbeeBuffer[xbeeBufferPos++] = 0;
  xbeeBuffer[xbeeBufferPos++] = 0;
  // velocidade média do vento nos últimos 60 segundos
  xbeeBuffer[xbeeBufferPos++] = 0;
  xbeeBuffer[xbeeBufferPos++] = 0;
  // velocidade de rajada nos últimos 60 segundos
  xbeeBuffer[xbeeBufferPos++] = 0;
  xbeeBuffer[xbeeBufferPos++] = 0;
  // temperatura
  xbeeBuffer[xbeeBufferPos++] = recvTemperature >> 8;
  xbeeBuffer[xbeeBufferPos++] = recvTemperature & 0xFF;
  // pressão
  xbeeBuffer[xbeeBufferPos++] = recvPressure >> 8;
  xbeeBuffer[xbeeBufferPos++] = recvPressure & 0xFF;
  // humidade
  xbeeBuffer[xbeeBufferPos++] = recvHumidity;
  // voltagem da bateria
  xbeeBuffer[xbeeBufferPos++] = 0;

  xbeeBuffer[2] = xbeeBufferPos - 3;

  // checksum
  xbeeBuffer[xbeeBufferPos] = 0;
  int i;
  for (i = 0; i < xbeeBufferPos; i++)
  {
    xbeeSerial.write(xbeeBuffer[i]);
    if (i > 2)
      xbeeBuffer[xbeeBufferPos] += xbeeBuffer[i];
  }
  xbeeBuffer[xbeeBufferPos] = 0xFF - xbeeBuffer[xbeeBufferPos];
  xbeeSerial.write(xbeeBuffer[xbeeBufferPos]);
}

void loop()
{
  if (!(xbeeEstado & 0x80))
    xbeeInicializar();
  else if (!(bme280Estado & 0x80))
    bme280Inicializar();

  unsigned long now = millis();

  if (xbeeEstado == XBEE_ERROR)
  {
    digitalWrite(ledPin, now & 64); 
    return;
  }

  if (bme280Estado == BME280_READY && nextPollBME280 < now)
  {
    recvTemperature = (int)(bme280.readTempC() * 10);
    recvPressure = (int)(bme280.readFloatPressure() / 100);
    recvHumidity = (int)(bme280.readFloatHumidity());

    // uma leitura nos sensores de ambiente a cada 5 segundos
    nextPollBME280 = now + 5000;
  }

  if (xbeeEstado == XBEE_READY && nextDataSend < now)
  {
    digitalWrite(ledPin, HIGH);
    xbeeEnviarFrame();
    xbeeEstado = XBEE_WAIT_ACK;
  }

  else if (xbeeEstado == XBEE_WAIT_ACK)
  {
    if (xbeeReceiveFrame())
    {
      nextDataSend = now + 1000;
      xbeeEstado = XBEE_READY;
      digitalWrite(ledPin, LOW);
    }
  }

  verificarVento();
}

