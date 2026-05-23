#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>

// ===============================
// CONFIGURAZIONE LCD 20x4 I2C
// ===============================

// Indirizzo I2C più comune: 0x27.
// Se il display non mostra nulla, prova 0x3F.
LiquidCrystal_I2C lcd(0x27, 20, 4);


// ===============================
// CONFIGURAZIONE RTC DS1307
// ===============================

RTC_DS1307 rtc;


// ===============================
// CONFIGURAZIONE SENSORI
// ===============================

const byte NUM_SENSORI = 4;

// Pin analogici collegati all'uscita AO dei moduli sensore
const byte pinAnalogici[NUM_SENSORI] = {
  A0, A1, A2, A3
};

// Pin digitali usati per alimentare separatamente i sensori
const byte pinAlimentazione[NUM_SENSORI] = {
  8, 9, 10, 11
};


// ===============================
// ORARI DI LETTURA
// ===============================

const byte ORA_LETTURA_MATTINA = 18;
const byte MINUTO_LETTURA_MATTINA = 41;

const byte ORA_LETTURA_SERA = 22;
const byte MINUTO_LETTURA_SERA = 15;

// ===============================
// PARAMETRI LETTURA SENSORI
// ===============================

// Tempo di stabilizzazione dopo aver acceso un sensore
const unsigned long tempoStabilizzazioneSensore = 200;

// Numero di letture da mediare per ogni sensore
const byte numeroCampioni = 10;

// Pausa tra i campioni dello stesso sensore
const unsigned long pausaTraCampioni = 10;


// ===============================
// CALIBRAZIONE SENSORI
// ===============================

// Valori iniziali da calibrare singolarmente.
// Nei sensori resistivi tipici:
// terreno secco  -> valore alto
// terreno bagnato -> valore basso

int valoreSecco[NUM_SENSORI] = {
  1023, 1023, 1023, 1023
};

int valoreBagnato[NUM_SENSORI] = {
  300, 300, 300, 300
};


// ===============================
// SOGLIA AVVISO
// ===============================

const int sogliaUmiditaBassa = 30;


// ===============================
// VARIABILI GLOBALI
// ===============================

int valoriGrezzi[NUM_SENSORI];
int percentualiUmidita[NUM_SENSORI];

// Servono per evitare che Arduino faccia più letture
// durante lo stesso minuto programmato.
int ultimoAnnoLettura = -1;
byte ultimoMeseLettura = 0;
byte ultimoGiornoLettura = 0;

byte ultimaOraLettura = 255;
byte ultimoMinutoLettura = 255;

byte ultimoMinutoMostrato = 255;
byte ultimaOraMostrata = 255;
byte ultimoGiornoMostrato = 0;


// ===============================
// SETUP
// ===============================

void setup() {
  // Impostiamo i pin di alimentazione dei sensori come uscite
  for (byte i = 0; i < NUM_SENSORI; i++) {
    pinMode(pinAlimentazione[i], OUTPUT);
    digitalWrite(pinAlimentazione[i], LOW);
  }

  // Inizializzazione LCD
  lcd.init();
  lcd.backlight();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sistema umidita'");
  lcd.setCursor(0, 1);
  lcd.print("LCD + RTC DS1307");
  lcd.setCursor(0, 2);
  lcd.print("Avvio...");

  // Inizializzazione RTC
  if (!rtc.begin()) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ERRORE RTC");
    lcd.setCursor(0, 1);
    lcd.print("DS1307 non visto");

    Serial.println("ERRORE: DS1307 non trovato sul bus I2C.");
    while (1) {
      delay(10);
    }
  }

  // Se il DS1307 non sta girando, lo impostiamo
  // alla data/ora di compilazione dello sketch.
  if (!rtc.isrunning()) {
    Serial.println("RTC fermo. Imposto data/ora di compilazione.");
    //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  //rtc.adjust(DateTime(2026, 5, 23, 15, 38, 30));
  /*
    ATTENZIONE:
    Se il DS1307 ha un'ora sbagliata ma risulta comunque "running",
    puoi forzare l'orario UNA SOLA VOLTA decommentando una di queste righe.

    Metodo 1: usa data/ora di compilazione dello sketch
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

    Metodo 2: imposta manualmente data e ora
    rtc.adjust(DateTime(2026, 5, 23, 18, 30, 0));

    Dopo aver caricato lo sketch e impostato l'RTC,
    ricommenta la riga e ricarica lo sketch.
  */

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sistema pronto");
  lcd.setCursor(0, 1);
  lcd.print("Lett:");
  stampaDueCifreLCD(ORA_LETTURA_MATTINA);
  lcd.print(":");
  stampaDueCifreLCD(MINUTO_LETTURA_MATTINA);
  lcd.print(" ");
  stampaDueCifreLCD(ORA_LETTURA_SERA);
  lcd.print(":");
  stampaDueCifreLCD(MINUTO_LETTURA_SERA);
  lcd.setCursor(0, 2);
  lcd.print("Sensori spenti");
  lcd.setCursor(0, 3);
  lcd.print("In attesa...");

  delay(2000);
}


// ===============================
// LOOP PRINCIPALE
// ===============================

void loop() {
  DateTime adesso = rtc.now();

  // Aggiorna la schermata di attesa solo quando cambia il minuto
  if (
    adesso.minute() != ultimoMinutoMostrato ||
    adesso.hour() != ultimaOraMostrata ||
    adesso.day() != ultimoGiornoMostrato
  ) {
    mostraAttesaLCD(adesso);

    ultimoMinutoMostrato = adesso.minute();
    ultimaOraMostrata = adesso.hour();
    ultimoGiornoMostrato = adesso.day();
  }

  // Controlla se siamo in un orario programmato
  if (deveFareLettura(adesso)) {
    leggiTuttiISensori();
    aggiornaLCD(adesso);
    registraLetturaEseguita(adesso);

    // Forza il ridisegno della schermata di attesa dopo la schermata risultati
    ultimoMinutoMostrato = 255;
  }

  delay(1000);
}


void mostraAttesaLCD(DateTime adesso) {
  lcd.setCursor(0, 0);
  lcd.print("Ora RTC: ");
  stampaDueCifreLCD(adesso.hour());
  lcd.print(":");
  stampaDueCifreLCD(adesso.minute());
  lcd.print("       ");

  lcd.setCursor(0, 1);
  lcd.print("Data: ");
  stampaDueCifreLCD(adesso.day());
  lcd.print("/");
  stampaDueCifreLCD(adesso.month());
  lcd.print("/");
  lcd.print(adesso.year());
  lcd.print(" ");

  lcd.setCursor(0, 2);
  lcd.print("Lett: ");
  stampaDueCifreLCD(ORA_LETTURA_MATTINA);
  lcd.print(":");
  stampaDueCifreLCD(MINUTO_LETTURA_MATTINA);
  lcd.print(" ");
  stampaDueCifreLCD(ORA_LETTURA_SERA);
  lcd.print(":");
  stampaDueCifreLCD(MINUTO_LETTURA_SERA);
  lcd.print("   ");

  lcd.setCursor(0, 3);
  lcd.print("In attesa...       ");
}

// ===============================
// CONTROLLO ORARIO LETTURA
// ===============================

bool deveFareLettura(DateTime adesso) {
  bool orarioMattina =
    adesso.hour() == ORA_LETTURA_MATTINA &&
    adesso.minute() == MINUTO_LETTURA_MATTINA;

  bool orarioSera =
    adesso.hour() == ORA_LETTURA_SERA &&
    adesso.minute() == MINUTO_LETTURA_SERA;

  bool oraProgrammata = orarioMattina || orarioSera;

  if (!oraProgrammata) {
    return false;
  }

  // Evita una seconda lettura nello stesso minuto programmato
  bool giaLettoInQuestoOrario =
  ultimoAnnoLettura == adesso.year() &&
  ultimoMeseLettura == adesso.month() &&
  ultimoGiornoLettura == adesso.day() &&
  ultimaOraLettura == adesso.hour() &&
  ultimoMinutoLettura == adesso.minute();

  if (giaLettoInQuestoOrario) {
    return false;
  }

  return true;
}


void registraLetturaEseguita(DateTime adesso) {
  ultimoAnnoLettura = adesso.year();
  ultimoMeseLettura = adesso.month();
  ultimoGiornoLettura = adesso.day();
  ultimaOraLettura = adesso.hour();
  ultimoMinutoLettura = adesso.minute();
}


// ===============================
// LETTURA DI TUTTI I SENSORI
// ===============================

void leggiTuttiISensori() {
  for (byte i = 0; i < NUM_SENSORI; i++) {
    valoriGrezzi[i] = leggiSingoloSensore(i);

    percentualiUmidita[i] = map(
      valoriGrezzi[i],
      valoreSecco[i],
      valoreBagnato[i],
      0,
      100
    );

    percentualiUmidita[i] = constrain(percentualiUmidita[i], 0, 100);
  }
}


// ===============================
// LETTURA DI UN SINGOLO SENSORE
// ===============================

int leggiSingoloSensore(byte indiceSensore) {
  long somma = 0;

  // Accendiamo solo il sensore richiesto
  digitalWrite(pinAlimentazione[indiceSensore], HIGH);

  // Aspettiamo che il modulo si stabilizzi
  delay(tempoStabilizzazioneSensore);

  // Facciamo più letture e calcoliamo una media
  for (byte campione = 0; campione < numeroCampioni; campione++) {
    somma += analogRead(pinAnalogici[indiceSensore]);
    delay(pausaTraCampioni);
  }

  // Spegniamo subito il sensore
  digitalWrite(pinAlimentazione[indiceSensore], LOW);

  return somma / numeroCampioni;
}


// ===============================
// STAMPA SU MONITOR SERIALE
// ===============================




// ===============================
// AGGIORNAMENTO LCD 20x4
// ===============================

void aggiornaLCD(DateTime adesso) {
  lcd.clear();

  // Riga 0: ora e data dell'ultimo aggiornamento
  lcd.setCursor(0, 0);
  lcd.print("Agg ");
  stampaDueCifreLCD(adesso.hour());
  lcd.print(":");
  stampaDueCifreLCD(adesso.minute());
  lcd.print(" ");
  stampaDueCifreLCD(adesso.day());
  lcd.print("/");
  stampaDueCifreLCD(adesso.month());

  // Riga 1: sensori 1 e 2
  lcd.setCursor(0, 1);
  lcd.print("S1:");
  stampaPercentualeLCD(percentualiUmidita[0]);
  lcd.print(" S2:");
  stampaPercentualeLCD(percentualiUmidita[1]);

  // Riga 2: sensori 3 e 4
  lcd.setCursor(0, 2);
  lcd.print("S3:");
  stampaPercentualeLCD(percentualiUmidita[2]);
  lcd.print(" S4:");
  stampaPercentualeLCD(percentualiUmidita[3]);

  // Riga 3: stato generale e prossima lettura
  lcd.setCursor(0, 3);

  if (esisteSensoreSottoSoglia()) {
    lcd.print("STATO: SECCO ");
  } else {
    lcd.print("STATO: OK    ");
  }

  lcd.print("P:");
  stampaDueCifreLCD(prossimaOraLettura(adesso));
  delay(20000);
}


// ===============================
// FUNZIONI DI SUPPORTO LCD/SERIAL
// ===============================

void stampaPercentualeLCD(int percentuale) {
  // Stampa sempre 4 caratteri:
  // "  5%"
  // " 45%"
  // "100%"
  if (percentuale < 100) {
    lcd.print(" ");
  }

  if (percentuale < 10) {
    lcd.print(" ");
  }

  lcd.print(percentuale);
  lcd.print("%");
}


void stampaDueCifreLCD(int valore) {
  if (valore < 10) {
    lcd.print("0");
  }
  lcd.print(valore);
}


void stampaDueCifreSerial(int valore) {
  if (valore < 10) {
    Serial.print("0");
  }
  Serial.print(valore);
}


bool esisteSensoreSottoSoglia() {
  for (byte i = 0; i < NUM_SENSORI; i++) {
    if (percentualiUmidita[i] < sogliaUmiditaBassa) {
      return true;
    }
  }

  return false;
}


byte prossimaOraLettura(DateTime adesso) {
  if (adesso.hour() < ORA_LETTURA_MATTINA) {
    return ORA_LETTURA_MATTINA;
  }

  if (adesso.hour() < ORA_LETTURA_SERA) {
    return ORA_LETTURA_SERA;
  }

  return ORA_LETTURA_MATTINA;
}