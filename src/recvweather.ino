#include <SoftwareSerial.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>

#define XBEE_SEND_COMMAND             0x01
#define XBEE_WAIT_COMMAND_OK          0x03
#define XBEE_READY                    0x84
#define XBEE_WAIT_ACK                 0x85
#define XBEE_ERROR                    0xFF

char xbeeRxPin = 3;
char xbeeTxPin = 4;
unsigned char xbeeBuffer[128];
unsigned char xbeeBufferPos = 0;
unsigned char xbeeBufferLen;
unsigned char xbeeEstado;
char xbeeStartIndex = 0;

SoftwareSerial xbeeSerial(xbeeRxPin, xbeeTxPin);

char lcdRSPin = 9;
char lcdENABLEPin = 8;
char lcdD4Pin = 10;
char lcdD5Pin = 11;
char lcdD6Pin = 12;
char lcdD7Pin = 13;

LiquidCrystal lcd(lcdRSPin, lcdENABLEPin, lcdD4Pin, lcdD5Pin, lcdD6Pin, lcdD7Pin);

char rotaryClockPin = 5;
char rotaryDataPin = 6;
char rotaryEnterPin = 7;

#define UNIDADE_VELOCIDADE_KMH      0
#define UNIDADE_VELOCIDADE_KNOTS    1
#define UNIDADE_VELOCIDADE_MS       2
#define UNIDADE_VELOCIDADE_MPH      3
#define UNIDADE_VELOCIDADE_LIMITE   UNIDADE_VELOCIDADE_MPH

char *siglasUnidadesVelocidade[] = {"Km/h", "knots", "m/s", "MPH"};
float fatoresUnidadesVelocidade[] = {1, 1.852, 3.6, 1.60934};

#define UNIDADE_TEMPERATURA_CENTIGRADOS   0
#define UNIDADE_TEMPERATURA_FAHRENHEIT    1
#define UNIDADE_TEMPERATURA_LIMITE        UNIDADE_TEMPERATURA_FAHRENHEIT

char *nomesUnidadesTemperatura[] = {"Centigrados", "Fahrenheit"};
char *siglasUnidadesTemperatura[] = {"oC", "oF"};

unsigned long dataRemoteTime = 0;
unsigned char dataRemoteRssi;
unsigned int dataRemoteVentoInstantaneo;
unsigned int dataRemoteVentoMedio;
unsigned int dataRemoteVentoRajada;
unsigned int dataRemoteTemperatura;
unsigned int dataRemotePressao;
unsigned int dataRemoteHumidade;
unsigned char dataRemoteBateria;
unsigned char dataInvalidar = true;

char temExtremos = false;
unsigned int minVento;
unsigned int maxVento;
unsigned int minTemperatura;
unsigned int maxTemperatura;

unsigned long dataLocalTime = 0;      
unsigned char dataLocalBateria;

#define TELA_INFO_VENTO       0x00
#define TELA_INFO_PRESSAO     0x01
#define TELA_INFO_EXTREMOS    0x02
#define TELA_MENU_VELOCIDADE  0x80
#define TELA_MENU_TEMPERATURA 0x81
#define TELA_MENU_LIMPAR_MAX  0x82
#define TELA_MENU_PAREAR      0x83
#define TELA_MENU_VERSAO      0x84
#define TELA_MENU_SAIR        0x85

unsigned char telaAtiva = TELA_INFO_VENTO;
unsigned char subtelaAtiva = 0;

char pareandoTempoRestante = 0;

struct {
  char unidadeVelocidade;
  char unidadeTemperatura;
  char txid[8];
} configuracao;


#define VERSAO "1.00"

void setup()
{
  Serial.begin(9600);

  xbeeSerial.begin(9600);
  xbeeSerial.listen();

  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("RX Weather");
  lcd.setCursor(0, 1);
  lcd.print("v.");
  lcd.print(VERSAO);

  // coloca o xbee em modo comando
  delay(1000);
  xbeeSerial.print("+++");
  delay(1200);

  // limpa a serial
  while (xbeeSerial.available())
    xbeeSerial.read();
  
  xbeeEstado = XBEE_SEND_COMMAND;

  pinMode(rotaryClockPin, INPUT);
  pinMode(rotaryDataPin, INPUT);
  pinMode(rotaryEnterPin, INPUT_PULLUP);
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
Serial.print("*");    
    xbeeBuffer[xbeeBufferPos] = xbeeSerial.read();
    if (xbeeBufferPos == 0 && xbeeBuffer[xbeeBufferPos] != 0x7E)
      return false;

    if (xbeeBufferPos > 3)
    {
      int len = (xbeeBuffer[1] << 8) | xbeeBuffer[2];
      if (len + 4 > sizeof(xbeeBuffer))  
      {
        xbeeBufferPos == 0;
        return false;
      }

      if (len + 4 == xbeeBufferPos + 1)
      {
        xbeeBufferLen = xbeeBufferPos;
        xbeeBufferPos = 0;
        return true;
      }
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
        xbeeEstado = strcmp(xbeeBuffer, "OK") == 0 ? XBEE_SEND_COMMAND : XBEE_ERROR;
      break;
  }
}

void velocidadeToLCD(char *linha, int velocidade)
{
  char temp[12];
  velocidade /= fatoresUnidadesVelocidade[configuracao.unidadeVelocidade];
  itoa(velocidade / 10, temp, 10);
  int len = strlen(temp);
  strncpy(linha, temp, len);
  linha[len++] = ','; 
  linha[len] = '0' + (velocidade % 10); 
}

void temperaturaToLCD(char *linha, int temperatura, char acrescentarUnidade)
{
  char temp[12];

  if (configuracao.unidadeTemperatura == UNIDADE_TEMPERATURA_FAHRENHEIT)
    temperatura = temperatura * 9 / 5 + 32;

  if (temperatura < 0)
  {
    linha[0] = '-';
    temperatura *= -1; 
  }
  
  itoa(temperatura / 10, temp, 10);
  int len = strlen(temp);
  strncpy(&linha[1], temp, len);
  len++;
  linha[len++] = ','; 
  linha[len++] = '0' + (temperatura % 10);
  if (acrescentarUnidade)
  {
    char *sigla = siglasUnidadesTemperatura[configuracao.unidadeTemperatura];
    strncpy(&linha[len], sigla, strlen(sigla));
  }
}

void humidadeToLCD(char *linha, int humidade)
{
  char temp[12];
  itoa(humidade, temp, 10);
  int len = strlen(temp);
  strncpy(&linha[3 - len], temp, len);
  linha[3] = '%'; 
}

void voltagemToLCD(char *linha, int voltagem)
{
  char temp[12];
  itoa(voltagem / 10, temp, 10);
  int len = strlen(temp);
  strncpy(linha, temp, len);
  linha[len++] = ','; 
  linha[len++] = '0' + (voltagem % 10); 
  linha[len] = 'V'; 
}

void pressaoToLCD(char *linha, int pressao)
{
  char temp[12];
  itoa(pressao, temp, 10);
  int len = strlen(temp);
  strncpy(linha, temp, len);
  linha[len++] = 'h'; 
  linha[len++] = 'P'; 
  linha[len] = 'a'; 
}

void atualizarTelaDados()
{
  char dadosDisponiveis = (millis() - dataRemoteTime) < 3000; 
  char linha0[17];
  char linha1[17];
  char *nome;

  memset(linha0, ' ', 16);
  memset(linha1, ' ', 16);

  switch (telaAtiva)
  {
  case TELA_INFO_VENTO:
    if (dadosDisponiveis)
    {
      velocidadeToLCD(&linha0[0], dataRemoteVentoInstantaneo);
      velocidadeToLCD(&linha0[5], dataRemoteVentoMedio);
      velocidadeToLCD(&linha0[10], dataRemoteVentoRajada);
      int index; 
      if (dataRemoteRssi < -80)
        index = 1;
      else if (dataRemoteRssi < -60)
        index = 2;
      else if (dataRemoteRssi < -45)
        index = 3;
      else if (dataRemoteRssi < -40)
        index = 4;
      else
        index = 5;
      linha0[15] = '0' + index; 

      temperaturaToLCD(&linha1[3], dataRemoteTemperatura, true);
      humidadeToLCD(&linha1[12], dataRemoteHumidade);
    }
    else
    {
      strcpy(linha0, "--,- --,- --,- 0");
      strcpy(linha1, "??  --,-    ---%");
    }
    break;

  case TELA_INFO_PRESSAO:
    if (dadosDisponiveis)
    {
      linha0[0] = 'R';
      linha0[1] = ':';
      voltagemToLCD(&linha0[2], dataRemoteBateria);

      pressaoToLCD(&linha0[9], dataRemotePressao);
    }
    else
    {
      strcpy(linha0, "R:--,-   ----   ");
    }

    linha1[0] = 'L';
    linha1[1] = ':';
    voltagemToLCD(&linha1[2], dataRemoteBateria);
    break;
  
  case TELA_INFO_EXTREMOS:
    strncpy(linha0, "VENTO:", 6);
    strncpy(linha1, "TEMP:", 5);
    if (temExtremos)
    {
      velocidadeToLCD(&linha0[6], minVento);
      velocidadeToLCD(&linha0[11], maxVento);
      temperaturaToLCD(&linha1[5], minTemperatura, false);
      temperaturaToLCD(&linha1[10], maxTemperatura, false);
    }
    else
    {
      strncpy(&linha0[6], "--,-", 4);
      strncpy(&linha0[11], "--,-", 4);
      strncpy(&linha1[6], "--,-", 4);
      strncpy(&linha1[10], "\x198", 1);
      strncpy(&linha1[11], "--,-", 4);
    }
    break;

  case TELA_MENU_VELOCIDADE:
    strcpy(linha0, "1.VELOCIDADE    ");
    nome = siglasUnidadesVelocidade[configuracao.unidadeVelocidade];
    strcpy(&linha1[16 - strlen(nome)], nome);
    break;
    
  case TELA_MENU_TEMPERATURA:
    strcpy(linha0, "2.TEMPERATURA   ");
    nome = nomesUnidadesTemperatura[configuracao.unidadeTemperatura];
    strcpy(&linha1[16 - strlen(nome)], nome);
    break;
    
  case TELA_MENU_LIMPAR_MAX:
    switch (subtelaAtiva)
    {
      case 0:
        strcpy(linha0, "3.LIMPAR MIN/MAX");
        break;
      case 1:
        strcpy(linha0, "LIMPAR?         ");
        strcpy(linha1, "        CANCELAR");
        break;
      case 2:
        strcpy(linha0, "LIMPAR?         ");
        strcpy(linha1, "              OK");
        break;
    }
    break;

  case TELA_MENU_PAREAR:
    switch (subtelaAtiva)
    {
      case 0:
        strcpy(linha0, "4.PAREAR        ");
        break;
      case 1:
        strcpy(linha0, "PAREAR?         ");
        strcpy(linha1, "        CANCELAR");
        break;
      case 2:
        strcpy(linha0, "PAREAR?         ");
        strcpy(linha1, "              OK");
        break;
      case 3:
        strcpy(linha0, "PAREANDO        ");
        char temp[8];
        itoa(temp, pareandoTempoRestante, 10);
        strcpy(&linha1[16 - strlen(temp)], temp);
        break;
      case 4:
        strcpy(linha0, "PAREANDO        ");
        strcpy(linha1, "  TEMPO ESGOTADO");
        break;
    }
    break;

  case TELA_MENU_VERSAO:
    strcpy(linha0, "5.VERSAO        ");
    strcpy(&linha1[16 - strlen(VERSAO)], VERSAO);
    break;

  case TELA_MENU_SAIR:
    strcpy(linha0, "6.SAIR          ");
    break;
  }

  lcd.setCursor(0, 0);
  linha0[16] = '\0';
  lcd.print(linha0);

  lcd.setCursor(0, 1);
  linha1[16] = '\0';
  lcd.print(linha1);
}

void tecladoSubir()
{
  switch (telaAtiva)
  {
  case TELA_INFO_VENTO:
    // ignora comando
    break;
  case TELA_INFO_PRESSAO:
    telaAtiva = TELA_INFO_VENTO;
    dataInvalidar = true;
    break;
  case TELA_INFO_EXTREMOS:
    telaAtiva = TELA_INFO_PRESSAO;
    dataInvalidar = true;
    break;
  case TELA_MENU_VELOCIDADE:
    // ignora comando
    break;
  case TELA_MENU_TEMPERATURA:
    telaAtiva = TELA_MENU_VELOCIDADE;
    dataInvalidar = true;
    break;
  case TELA_MENU_LIMPAR_MAX:
    switch (subtelaAtiva)
    {
      case 0:
        telaAtiva = TELA_MENU_TEMPERATURA;
        dataInvalidar = true;
        break;  
      case 2:
        subtelaAtiva = 1;
        dataInvalidar = true;
        break;  
    }
    break;
  case TELA_MENU_PAREAR:
    switch (subtelaAtiva)
    {
      case 0:
        telaAtiva = TELA_MENU_LIMPAR_MAX;
        subtelaAtiva = 0;
        dataInvalidar = true;
        break;  
      case 2:
        subtelaAtiva = 1;
        dataInvalidar = true;
        break;  
    }
    break;
  case TELA_MENU_VERSAO:
    telaAtiva = TELA_MENU_PAREAR;
    subtelaAtiva = 0;
    dataInvalidar = true;
    break;
  case TELA_MENU_SAIR:
    telaAtiva = TELA_MENU_VERSAO;
    dataInvalidar = true;
    break;
  }
}

void tecladoDescer()
{
  switch (telaAtiva)
  {
  case TELA_INFO_VENTO:
    telaAtiva = TELA_INFO_PRESSAO;
    dataInvalidar = true;
    break;
  case TELA_INFO_PRESSAO:
    telaAtiva = TELA_INFO_EXTREMOS;
    dataInvalidar = true;
    break;
  case TELA_INFO_EXTREMOS:
    // ignora comando
    break;
  case TELA_MENU_VELOCIDADE:
    telaAtiva = TELA_MENU_TEMPERATURA;
    dataInvalidar = true;
    break;
  case TELA_MENU_TEMPERATURA:
    telaAtiva = TELA_MENU_LIMPAR_MAX;
    subtelaAtiva = 0;
    dataInvalidar = true;
    break;
  case TELA_MENU_LIMPAR_MAX:
    switch (subtelaAtiva)
    {
      case 0:
        telaAtiva = TELA_MENU_PAREAR;
        subtelaAtiva = 0;
        dataInvalidar = true;
        break;  
      case 1:
        subtelaAtiva = 2;
        dataInvalidar = true;
        break;  
    }
    break;
  case TELA_MENU_PAREAR:
    switch (subtelaAtiva)
    {
      case 0:
        telaAtiva = TELA_MENU_VERSAO;
        dataInvalidar = true;
        break;  
      case 1:
        subtelaAtiva = 2;
        dataInvalidar = true;
        break;  
    }
    break;
  case TELA_MENU_VERSAO:
    telaAtiva = TELA_MENU_SAIR;
    dataInvalidar = true;
    break;
  case TELA_MENU_SAIR:
    // ignora comando
    break;
  }
}

void tecladoEnter()
{
  switch (telaAtiva)
  {
  case TELA_INFO_VENTO:
  case TELA_INFO_PRESSAO:
  case TELA_INFO_EXTREMOS:
    telaAtiva = TELA_MENU_VELOCIDADE;
    dataInvalidar = true;
    break;
  case TELA_MENU_VELOCIDADE:
    configuracao.unidadeVelocidade++;
    if (configuracao.unidadeVelocidade > UNIDADE_VELOCIDADE_LIMITE)
      configuracao.unidadeVelocidade = 0;
    dataInvalidar = true;
    break;
  case TELA_MENU_TEMPERATURA:
    configuracao.unidadeTemperatura++;
    if (configuracao.unidadeTemperatura > UNIDADE_TEMPERATURA_LIMITE)
      configuracao.unidadeTemperatura = 0;
    dataInvalidar = true;
    break;
  case TELA_MENU_LIMPAR_MAX:
    switch (subtelaAtiva)
    {
      case 0:
        subtelaAtiva = 1;
        break;
      case 1:
        subtelaAtiva = 0;
        break;
      case 2:
        // TODO: limpar mínimas e máximas
        break;
    }
    dataInvalidar = true;
    break;
  case TELA_MENU_PAREAR:
    switch (subtelaAtiva)
    {
      case 0:
        subtelaAtiva = 1;
        break;
      case 1:
      case 3:
        subtelaAtiva = 0;
        break;
      case 2:
        // TODO: iniciar o processo de pareamento
        break;
    }
    dataInvalidar = true;
    break;
  case TELA_MENU_VERSAO:
    // ignora comando
    break;
  case TELA_MENU_SAIR:
    telaAtiva = TELA_INFO_VENTO;
    dataInvalidar = true;
    break;
  }
}

void verificarTeclado()
{
  static char lastKey = 0;
  static char roldclk = 0;
  char key = 0;
  char rclk = digitalRead(rotaryClockPin);
  char rdata = digitalRead(rotaryDataPin);

  if (roldclk && !rclk)
    key = rdata ? 'U' : 'D';
  else if (!digitalRead(rotaryEnterPin))
    key = 'E';

  roldclk = rclk;

  if (lastKey == 0 && key != 0)
  {
   switch (key)
    {
    case 'U':
      tecladoSubir();
      break; 
    case 'D':
      tecladoDescer();
      break; 
    case 'E':
      tecladoEnter();
      break; 
    }
  }

  lastKey = key;
}

void loop()
{
  if (!(xbeeEstado & 0x80))
    xbeeInicializar();

  unsigned long now = millis();

  if (xbeeEstado == XBEE_READY)
  {
    if (xbeeReceiveFrame())
    {
      dataRemoteTime = now;
      dataRemoteRssi = xbeeBuffer[6];
      dataRemoteVentoInstantaneo = (xbeeBuffer[9] << 8) | xbeeBuffer[10];
      dataRemoteVentoMedio = (xbeeBuffer[11] << 8) | xbeeBuffer[12];
      dataRemoteVentoRajada = (xbeeBuffer[13] << 8) | xbeeBuffer[14];
      dataRemoteTemperatura = (xbeeBuffer[15] << 8) | xbeeBuffer[16];
      dataRemotePressao = (xbeeBuffer[17] << 8) | xbeeBuffer[18];
      dataRemoteHumidade = xbeeBuffer[19];
      dataRemoteBateria = xbeeBuffer[20];

      if (!temExtremos)
      {
        minVento = dataRemoteVentoMedio;
        maxVento = dataRemoteVentoMedio;
        minTemperatura = dataRemoteTemperatura;
        maxTemperatura = dataRemoteTemperatura;
        temExtremos = true;
      }
      else
      {
        if (dataRemoteVentoMedio < minVento)
          minVento = dataRemoteVentoMedio;
        if (dataRemoteVentoMedio > maxVento)
          maxVento = dataRemoteVentoMedio;
        if (dataRemoteTemperatura < minTemperatura)
          minTemperatura = dataRemoteTemperatura;
        if (dataRemoteTemperatura > maxTemperatura)
          maxTemperatura = dataRemoteTemperatura;
      }
      
      dataInvalidar = true;
    }
    
    else if (dataLocalTime - now > 1000)
    {
      dataLocalBateria = xbeeBuffer[19];
      dataLocalTime = now;      
      dataInvalidar = true;
    }
  }

  verificarTeclado();

  if (dataInvalidar)
  {
    atualizarTelaDados();
    dataInvalidar = false;
  }
}

