#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <math.h>

// ===============================
// CONFIGURAZIONE LCD 20x4 I2C
// ===============================

// Indirizzo I2C piu' comune: 0x27.
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
// ORARI DI LETTURA CALCOLATI DAL SOLE
// ===============================

// Gli orari non sono salvati in una tabella.
// Vengono calcolati una volta al giorno con una formula astronomica
// basata su latitudine, longitudine e data letta dal DS1307.
// Il risultato e' espresso in minuti da mezzanotte:
//   05:50 -> 5 * 60 + 50 = 350
//   21:02 -> 21 * 60 + 2 = 1262

const float LAT_TORINO = 45.070312;
const float LON_TORINO = 7.6868565;

uint16_t minutoLetturaMattina = 0;  // alba
uint16_t minutoLetturaSera = 0;     // tramonto

int annoOrariCalcolati = -1;
byte meseOrariCalcolati = 0;
byte giornoOrariCalcolati = 0;


// ===============================
// MONITORAGGIO DOPO LETTURA
// ===============================

// Dopo una lettura programmata ad alba o tramonto:
// - il display resta sulla schermata umidita' per 2 ore;
// - i sensori vengono riletti davvero ogni 30 minuti;
// - fuori da questa finestra torna alla schermata di attesa.

const uint32_t DURATA_MONITORAGGIO_SEC = 2UL * 60UL * 60UL;       // 2 ore
const uint32_t INTERVALLO_MONITORAGGIO_SEC = 30UL * 60UL;         // 30 minuti

bool monitoraggioAttivo = false;

uint32_t fineMonitoraggioUnix = 0;
uint32_t prossimaLetturaMonitoraggioUnix = 0;
uint32_t ultimaLetturaMonitoraggioUnix = 0;

byte ultimoMinutoMonitorMostrato = 255;
byte ultimaOraMonitorMostrata = 255;
byte ultimoGiornoMonitorMostrato = 0;


// ===============================
// RISPARMIO ENERGETICO DISPLAY
// ===============================

// Fuori dalle finestre utili il display viene spento del tutto:
// - noBacklight(): spegne la retroilluminazione;
// - noDisplay(): nasconde i caratteri sul display.
//
// Il display si accende 15 minuti prima della prossima lettura
// programmata, resta acceso durante il monitoraggio, poi resta acceso
// ancora 5 minuti con il messaggio di spegnimento.

const uint32_t ANTICIPO_ACCENSIONE_DISPLAY_SEC = 15UL * 60UL;  // 15 minuti
const uint32_t RITARDO_SPEGNIMENTO_DISPLAY_SEC = 5UL * 60UL;   // 5 minuti

bool displayAcceso = true;
bool postSpegnimentoAttivo = false;
uint32_t finePostSpegnimentoUnix = 0;

byte ultimoMinutoPreMostrato = 255;
byte ultimaOraPreMostrata = 255;
byte ultimoGiornoPreMostrato = 0;

byte ultimoMinutoPostMostrato = 255;
byte ultimaOraPostMostrata = 255;
byte ultimoGiornoPostMostrato = 0;


// ===============================
// PULSANTE VISUALIZZAZIONE PROSSIMA ESECUZIONE
// ===============================

// Collegamento consigliato:
// - un lato del pulsante al pin 2;
// - l'altro lato del pulsante a GND.
// Usiamo INPUT_PULLUP, quindi:
// - pulsante non premuto -> HIGH;
// - pulsante premuto     -> LOW.

const byte PIN_PULSANTE_PROSSIMA_ESECUZIONE = 2;
const uint32_t DURATA_VISUALIZZAZIONE_MANUALE_SEC = 10UL;

// Debounce piu' alto per evitare doppie attivazioni dovute al rimbalzo meccanico.
const unsigned long DEBOUNCE_PULSANTE_MS = 250;

// Ciclo principale piu' rapido: cosi' la schermata compare quasi subito.
const unsigned long PAUSA_LOOP_MS = 50;

bool visualizzazioneManualeAttiva = false;
uint32_t fineVisualizzazioneManualeUnix = 0;

// D2 su Arduino Uno supporta interrupt esterno.
// L'interrupt intercetta anche pressioni brevi, che prima potevano essere perse
// perche' il loop controllava il pulsante solo ogni 1 secondo.
volatile bool richiestaVisualizzazioneDaPulsante = false;
unsigned long ultimoPulsanteAccettatoMs = 0;

byte ultimoMinutoProssimaMostrato = 255;
byte ultimaOraProssimaMostrata = 255;
byte ultimoGiornoProssimaMostrato = 0;
// ===============================
// PULSANTE LETTURA MANUALE
// ===============================

const byte PIN_PULSANTE_LETTURA_MANUALE = 3;

const uint32_t DURATA_DISPLAY_LETTURA_MANUALE_SEC = 5UL * 60UL;  // 5 minuti
const unsigned long DEBOUNCE_LETTURA_MANUALE_MS = 300;

volatile bool richiestaLetturaManuale = false;

unsigned long ultimoClickLetturaManualeMs = 0;

bool schermataLetturaManualeAttiva = false;
uint32_t fineSchermataLetturaManualeUnix = 0;

byte ultimoMinutoManualeMostrato = 255;
byte ultimaOraManualeMostrata = 255;
byte ultimoGiornoManualeMostrato = 0;

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
// terreno secco   -> valore alto
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

int valoriGrezzi[NUM_SENSORI] = {0, 0, 0, 0};
int percentualiUmidita[NUM_SENSORI] = {0, 0, 0, 0};

// Servono per evitare che Arduino faccia piu' letture
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
// PROTOTIPI
// ===============================

void mostraAttesaLCD(DateTime adesso);
bool deveFareLettura(DateTime adesso);
void registraLetturaEseguita(DateTime adesso);

void aggiornaOrariSeCambiaGiorno(DateTime adesso);
void calcolaOrariSoleDelGiorno(DateTime adesso);
void calcolaAlbaTramontoTorino(uint16_t anno, uint8_t mese, uint8_t giorno, uint16_t &albaMinuti, uint16_t &tramontoMinuti);
bool annoBisestile(uint16_t anno);
uint16_t giornoDellAnno(uint16_t anno, uint8_t mese, uint8_t giorno);
uint8_t giorniDelMese(uint16_t anno, uint8_t mese);
uint8_t ultimaDomenicaDelMese(uint16_t anno, uint8_t mese);
int offsetItaliaMinutiPerSole(uint16_t anno, uint8_t mese, uint8_t giorno);
uint8_t giornoSettimana(uint16_t anno, uint8_t mese, uint8_t giorno);
uint16_t normalizzaMinuti(float minuti);

void avviaMonitoraggio(DateTime adesso);
void gestisciMonitoraggio(DateTime adesso);
void mostraMonitoraggioLCD(DateTime adesso);
uint16_t minutiResiduiDaOra(uint32_t oraUnix, uint32_t scadenzaUnix);

void accendiDisplay();
void spegniDisplay();
void gestisciDisplayInAttesa(DateTime adesso);
void mostraPreAccensioneLCD(DateTime adesso, uint32_t prossimoLetturaUnix);
void avviaPostSpegnimento(DateTime adesso);
void gestisciPostSpegnimento(DateTime adesso);
void mostraPostSpegnimentoLCD(DateTime adesso);
uint32_t prossimoIstanteLetturaUnix(DateTime adesso);
uint32_t istanteLetturaSuDataUnix(DateTime data, uint16_t minutiDaMezzanotte);

void isrPulsanteProssimaEsecuzione();
void gestisciPulsanteProssimaEsecuzione(DateTime adesso);
void avviaVisualizzazioneManuale(DateTime adesso);
void gestisciVisualizzazioneManuale(DateTime adesso);
void mostraProssimaEsecuzioneLCD(DateTime adesso);
void stampaDataOraCompattaLCD(DateTime dataOra);
void stampaTempoMancanteLCD(uint32_t secondiMancanti);

void leggiTuttiISensori();
int leggiSingoloSensore(byte indiceSensore);
void aggiornaLCD(DateTime adesso);
void stampaPercentualeLCD(int percentuale);
void stampaDueCifreLCD(int valore);
void stampaOrarioDaMinutiLCD(uint16_t minutiDaMezzanotte);
void stampaDueCifreSerial(int valore);
bool esisteSensoreSottoSoglia();
uint16_t prossimoMinutoLettura(DateTime adesso);

void isrPulsanteLetturaManuale();
void gestisciPulsanteLetturaManuale(DateTime adesso);
void gestisciSchermataLetturaManuale(DateTime adesso);
void mostraLetturaManualeLCD(DateTime adesso);
// ===============================
// SETUP
// ===============================

void setup() {
  Serial.begin(9600);

  // Impostiamo i pin di alimentazione dei sensori come uscite
  for (byte i = 0; i < NUM_SENSORI; i++) {
    pinMode(pinAlimentazione[i], OUTPUT);
    digitalWrite(pinAlimentazione[i], LOW);
  }

  // Pulsante manuale per visualizzare la prossima esecuzione.
  // Collegamento corretto: D2 <-> pulsante <-> GND.
  pinMode(PIN_PULSANTE_PROSSIMA_ESECUZIONE, INPUT_PULLUP);
  attachInterrupt(
    digitalPinToInterrupt(PIN_PULSANTE_PROSSIMA_ESECUZIONE),
    isrPulsanteProssimaEsecuzione,
    FALLING
  );

  pinMode(PIN_PULSANTE_LETTURA_MANUALE, INPUT_PULLUP);
  attachInterrupt(
    digitalPinToInterrupt(PIN_PULSANTE_LETTURA_MANUALE),
    isrPulsanteLetturaManuale,
    FALLING
  );

  // Inizializzazione LCD
  lcd.init();
  lcd.display();
  lcd.backlight();
  displayAcceso = true;

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

  // Se il DS1307 non sta girando, lo segnaliamo.
  // Puoi impostarlo decommentando UNA SOLA VOLTA una delle righe rtc.adjust().
  if (!rtc.isrunning()) {
    Serial.println("RTC fermo. Decommenta rtc.adjust() per impostarlo.");
    // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

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

    Nota:
    Questo codice calcola alba/tramonto in ora civile italiana.
    Il DS1307 deve quindi essere mantenuto sull'ora locale corretta.
  */

  DateTime adesso = rtc.now();
  aggiornaOrariSeCambiaGiorno(adesso);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sistema pronto");
  lcd.setCursor(0, 1);
  lcd.print("Lett:");
  stampaOrarioDaMinutiLCD(minutoLetturaMattina);
  lcd.print(" ");
  stampaOrarioDaMinutiLCD(minutoLetturaSera);
  lcd.setCursor(0, 2);
  lcd.print("Sensori spenti");
  lcd.setCursor(0, 3);
  lcd.print("In attesa...");

  delay(2000);

  // Dopo il messaggio di avvio, applica subito la politica di risparmio.
  // Se manca piu' di 15 minuti alla prossima lettura, il display si spegne.
  ultimoMinutoPreMostrato = 255;
  gestisciDisplayInAttesa(rtc.now());
}


// ===============================
// LOOP PRINCIPALE
// ===============================

void loop() {
  DateTime adesso = rtc.now();

  // 1. Aggiorna alba/tramonto se è cambiato giorno.
  aggiornaOrariSeCambiaGiorno(adesso);

  // 2. Gestisce i due pulsanti.
  //    Queste funzioni devono solo attivare schermate o letture manuali,
  //    ma non devono impedire le letture automatiche.
  gestisciPulsanteLetturaManuale(adesso);
  gestisciPulsanteProssimaEsecuzione(adesso);

  // 3. PRIORITÀ MASSIMA:
  //    se è l'ora di una lettura programmata, la facciamo comunque,
  //    anche se era attiva una schermata manuale.
  if (deveFareLettura(adesso)) {
    visualizzazioneManualeAttiva = false;
    schermataLetturaManualeAttiva = false;
    postSpegnimentoAttivo = false;

    accendiDisplay();

    leggiTuttiISensori();
    registraLetturaEseguita(adesso);
    avviaMonitoraggio(adesso);

    delay(PAUSA_LOOP_MS);
    return;
  }

  // 4. Se siamo nelle 2 ore dopo una lettura automatica,
  //    continua a gestire le riletture reali ogni 30 minuti.
  //    Questa parte deve girare anche se sopra al display c'è una schermata manuale.
  if (monitoraggioAttivo) {
    gestisciMonitoraggio(adesso);

    // Se è attiva la schermata della lettura manuale, ha priorità grafica.
    if (schermataLetturaManualeAttiva) {
      gestisciSchermataLetturaManuale(adesso);
    }
    // Altrimenti, se è attiva la schermata "prossima esecuzione", mostra quella.
    else if (visualizzazioneManualeAttiva) {
      gestisciVisualizzazioneManuale(adesso);
    }

    delay(PAUSA_LOOP_MS);
    return;
  }

  // 5. Fuori dal monitoraggio, la schermata lettura manuale sensori
  //    resta visibile per 5 minuti.
  if (schermataLetturaManualeAttiva) {
    gestisciSchermataLetturaManuale(adesso);
    delay(PAUSA_LOOP_MS);
    return;
  }

  // 6. Schermata "prossima esecuzione" da pulsante.
  if (visualizzazioneManualeAttiva) {
    gestisciVisualizzazioneManuale(adesso);
    delay(PAUSA_LOOP_MS);
    return;
  }

  // 7. Finito il monitoraggio, display acceso ancora 5 minuti
  //    con messaggio di spegnimento.
  if (postSpegnimentoAttivo) {
    gestisciPostSpegnimento(adesso);
    delay(PAUSA_LOOP_MS);
    return;
  }

  // 8. Stato normale:
  //    display spento, oppure pre-accensione 15 minuti prima della lettura.
  gestisciDisplayInAttesa(adesso);

  delay(PAUSA_LOOP_MS);
}


void isrPulsanteLetturaManuale() {
  richiestaLetturaManuale = true;
}

// ===============================
// SCHERMATA DI ATTESA
// ===============================

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
  stampaOrarioDaMinutiLCD(minutoLetturaMattina);
  lcd.print(" ");
  stampaOrarioDaMinutiLCD(minutoLetturaSera);
  lcd.print("   ");

  lcd.setCursor(0, 3);
  lcd.print("In attesa...       ");
}


// ===============================
// CONTROLLO ORARIO LETTURA
// ===============================

bool deveFareLettura(DateTime adesso) {
  uint16_t minutoAdesso = adesso.hour() * 60 + adesso.minute();

  bool oraProgrammata =
    minutoAdesso == minutoLetturaMattina ||
    minutoAdesso == minutoLetturaSera;

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

void gestisciPulsanteLetturaManuale(DateTime adesso) {
  bool pulsantePremuto = false;

  noInterrupts();
  if (richiestaLetturaManuale) {
    richiestaLetturaManuale = false;
    pulsantePremuto = true;
  }
  interrupts();

  if (digitalRead(PIN_PULSANTE_LETTURA_MANUALE) == LOW) {
    pulsantePremuto = true;
  }

  if (!pulsantePremuto) {
    return;
  }

  unsigned long oraMs = millis();

  if (oraMs - ultimoClickLetturaManualeMs < DEBOUNCE_LETTURA_MANUALE_MS) {
    return;
  }

  ultimoClickLetturaManualeMs = oraMs;

  visualizzazioneManualeAttiva = false;

  accendiDisplay();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Lettura manuale");
  lcd.setCursor(0, 1);
  lcd.print("Attendere...");

  leggiTuttiISensori();

  schermataLetturaManualeAttiva = true;
  fineSchermataLetturaManualeUnix =
    adesso.unixtime() + DURATA_DISPLAY_LETTURA_MANUALE_SEC;

  ultimoMinutoManualeMostrato = 255;
  ultimaOraManualeMostrata = 255;
  ultimoGiornoManualeMostrato = 0;

  mostraLetturaManualeLCD(adesso);

}

void gestisciSchermataLetturaManuale(DateTime adesso) {
  uint32_t oraUnix = adesso.unixtime();

  if (oraUnix >= fineSchermataLetturaManualeUnix) {
    schermataLetturaManualeAttiva = false;

    ultimoMinutoMostrato = 255;

    return;
  }

  if (
    adesso.minute() != ultimoMinutoManualeMostrato ||
    adesso.hour() != ultimaOraManualeMostrata ||
    adesso.day() != ultimoGiornoManualeMostrato
  ) {
    mostraLetturaManualeLCD(adesso);

    ultimoMinutoManualeMostrato = adesso.minute();
    ultimaOraManualeMostrata = adesso.hour();
    ultimoGiornoManualeMostrato = adesso.day();
  }
}

void mostraLetturaManualeLCD(DateTime adesso) {
  if (!displayAcceso) {
    accendiDisplay();
  }

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Manuale ");
  stampaDueCifreLCD(adesso.hour());
  lcd.print(":");
  stampaDueCifreLCD(adesso.minute());

  uint32_t oraUnix = adesso.unixtime();

  uint32_t minutiResidui = 0;
  if (fineSchermataLetturaManualeUnix > oraUnix) {
    minutiResidui = (fineSchermataLetturaManualeUnix - oraUnix + 59) / 60;
  }

  lcd.print(" R:");
  lcd.print(minutiResidui);
  lcd.print("m");

  lcd.setCursor(0, 1);
  lcd.print("S1:");
  stampaPercentualeLCD(percentualiUmidita[0]);
  lcd.print(" S2:");
  stampaPercentualeLCD(percentualiUmidita[1]);

  lcd.setCursor(0, 2);
  lcd.print("S3:");
  stampaPercentualeLCD(percentualiUmidita[2]);
  lcd.print(" S4:");
  stampaPercentualeLCD(percentualiUmidita[3]);

  lcd.setCursor(0, 3);

  if (esisteSensoreSottoSoglia()) {
    lcd.print("STATO: SECCO       ");
  } else {
    lcd.print("STATO: OK          ");
  }
}

void registraLetturaEseguita(DateTime adesso) {
  ultimoAnnoLettura = adesso.year();
  ultimoMeseLettura = adesso.month();
  ultimoGiornoLettura = adesso.day();
  ultimaOraLettura = adesso.hour();
  ultimoMinutoLettura = adesso.minute();
}


// ===============================
// AGGIORNAMENTO ORARI ALBA/TRAMONTO
// ===============================

void aggiornaOrariSeCambiaGiorno(DateTime adesso) {
  bool giornoCambiato =
    adesso.year() != annoOrariCalcolati ||
    adesso.month() != meseOrariCalcolati ||
    adesso.day() != giornoOrariCalcolati;

  if (!giornoCambiato) {
    return;
  }

  calcolaOrariSoleDelGiorno(adesso);

  annoOrariCalcolati = adesso.year();
  meseOrariCalcolati = adesso.month();
  giornoOrariCalcolati = adesso.day();

  // Forza l'aggiornamento LCD con i nuovi orari.
  ultimoMinutoMostrato = 255;
}


void calcolaOrariSoleDelGiorno(DateTime adesso) {
  calcolaAlbaTramontoTorino(
    adesso.year(),
    adesso.month(),
    adesso.day(),
    minutoLetturaMattina,
    minutoLetturaSera
  );
}


void calcolaAlbaTramontoTorino(
  uint16_t anno,
  uint8_t mese,
  uint8_t giorno,
  uint16_t &albaMinuti,
  uint16_t &tramontoMinuti
) {
  const float PI_F = 3.14159265;
  const float RAD_TO_DEG_F = 180.0 / PI_F;
  const float DEG_TO_RAD_F = PI_F / 180.0;

  uint16_t n = giornoDellAnno(anno, mese, giorno);
  uint16_t giorniAnno = annoBisestile(anno) ? 366 : 365;

  // Formula NOAA semplificata. Usiamo il giorno dell'anno a mezzogiorno
  // circa: per irrigazione/lettura sensori l'errore residuo di pochi minuti
  // e' trascurabile.
  float gamma = 2.0 * PI_F / giorniAnno * (n - 1);

  float eqtime = 229.18 * (
    0.000075
    + 0.001868 * cos(gamma)
    - 0.032077 * sin(gamma)
    - 0.014615 * cos(2.0 * gamma)
    - 0.040849 * sin(2.0 * gamma)
  );

  float decl =
    0.006918
    - 0.399912 * cos(gamma)
    + 0.070257 * sin(gamma)
    - 0.006758 * cos(2.0 * gamma)
    + 0.000907 * sin(2.0 * gamma)
    - 0.002697 * cos(3.0 * gamma)
    + 0.001480 * sin(3.0 * gamma);

  // 90.833 gradi include la rifrazione atmosferica media e il raggio
  // apparente del Sole.
  float zenithRad = 90.833 * DEG_TO_RAD_F;
  float latRad = LAT_TORINO * DEG_TO_RAD_F;

  float cosHourAngle =
    (cos(zenithRad) / (cos(latRad) * cos(decl)))
    - tan(latRad) * tan(decl);

  // Protezione numerica: a Torino non servirebbe, ma evita NaN se
  // per arrotondamenti il valore esce leggermente da [-1, 1].
  if (cosHourAngle > 1.0) {
    cosHourAngle = 1.0;
  }
  if (cosHourAngle < -1.0) {
    cosHourAngle = -1.0;
  }

  float hourAngleDeg = acos(cosHourAngle) * RAD_TO_DEG_F;

  // Minuti UTC da mezzanotte. Longitudine est positiva.
  float albaUTC = 720.0 - 4.0 * (LON_TORINO + hourAngleDeg) - eqtime;
  float tramontoUTC = 720.0 - 4.0 * (LON_TORINO - hourAngleDeg) - eqtime;

  int offsetLocale = offsetItaliaMinutiPerSole(anno, mese, giorno);

  albaMinuti = normalizzaMinuti(albaUTC + offsetLocale);
  tramontoMinuti = normalizzaMinuti(tramontoUTC + offsetLocale);
}


bool annoBisestile(uint16_t anno) {
  if (anno % 400 == 0) {
    return true;
  }
  if (anno % 100 == 0) {
    return false;
  }
  return anno % 4 == 0;
}


uint8_t giorniDelMese(uint16_t anno, uint8_t mese) {
  switch (mese) {
    case 1: return 31;
    case 2: return annoBisestile(anno) ? 29 : 28;
    case 3: return 31;
    case 4: return 30;
    case 5: return 31;
    case 6: return 30;
    case 7: return 31;
    case 8: return 31;
    case 9: return 30;
    case 10: return 31;
    case 11: return 30;
    case 12: return 31;
    default: return 30;
  }
}


uint16_t giornoDellAnno(uint16_t anno, uint8_t mese, uint8_t giorno) {
  uint16_t n = giorno;

  for (uint8_t m = 1; m < mese; m++) {
    n += giorniDelMese(anno, m);
  }

  return n;
}


int offsetItaliaMinutiPerSole(uint16_t anno, uint8_t mese, uint8_t giorno) {
  // Regola europea/italiana attuale:
  // ora legale dall'ultima domenica di marzo all'ultima domenica di ottobre.
  // Per alba/tramonto ragioniamo per giorno intero:
  // - nel giorno del cambio di marzo alba/tramonto sono in UTC+2;
  // - nel giorno del cambio di ottobre alba/tramonto sono in UTC+1.
  uint8_t ultimaDomenicaMarzo = ultimaDomenicaDelMese(anno, 3);
  uint8_t ultimaDomenicaOttobre = ultimaDomenicaDelMese(anno, 10);

  bool oraLegale = false;

  if (mese > 3 && mese < 10) {
    oraLegale = true;
  } else if (mese == 3 && giorno >= ultimaDomenicaMarzo) {
    oraLegale = true;
  } else if (mese == 10 && giorno < ultimaDomenicaOttobre) {
    oraLegale = true;
  }

  return oraLegale ? 120 : 60;
}


uint8_t ultimaDomenicaDelMese(uint16_t anno, uint8_t mese) {
  uint8_t ultimoGiorno = giorniDelMese(anno, mese);
  uint8_t dowUltimo = giornoSettimana(anno, mese, ultimoGiorno);

  // giornoSettimana(): 0 = domenica, 1 = lunedi', ..., 6 = sabato.
  return ultimoGiorno - dowUltimo;
}


uint8_t giornoSettimana(uint16_t anno, uint8_t mese, uint8_t giorno) {
  // Algoritmo di Sakamoto. Restituisce:
  // 0 = domenica, 1 = lunedi', ..., 6 = sabato.
  static const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

  if (mese < 3) {
    anno -= 1;
  }

  return (anno + anno / 4 - anno / 100 + anno / 400 + t[mese - 1] + giorno) % 7;
}


uint16_t normalizzaMinuti(float minuti) {
  int valore = (int)(minuti + 0.5);

  while (valore < 0) {
    valore += 1440;
  }

  while (valore >= 1440) {
    valore -= 1440;
  }

  return (uint16_t)valore;
}


// ===============================
// MONITORAGGIO POST-LETTURA
// ===============================

void avviaMonitoraggio(DateTime adesso) {
  uint32_t oraUnix = adesso.unixtime();

  monitoraggioAttivo = true;
  ultimaLetturaMonitoraggioUnix = oraUnix;
  fineMonitoraggioUnix = oraUnix + DURATA_MONITORAGGIO_SEC;
  prossimaLetturaMonitoraggioUnix = oraUnix + INTERVALLO_MONITORAGGIO_SEC;

  ultimoMinutoMonitorMostrato = 255;
  ultimaOraMonitorMostrata = 255;
  ultimoGiornoMonitorMostrato = 0;

  accendiDisplay();

  if (!visualizzazioneManualeAttiva && !schermataLetturaManualeAttiva) {
    mostraMonitoraggioLCD(adesso);
  }
}


void gestisciMonitoraggio(DateTime adesso) {
  uint32_t oraUnix = adesso.unixtime();

  // Fine delle 2 ore di monitoraggio.
  // A 2 ore esatte torna alla schermata di attesa, senza fare una lettura extra.
  if (oraUnix >= fineMonitoraggioUnix) {
    monitoraggioAttivo = false;

    ultimoMinutoMostrato = 255;
    ultimoMinutoMonitorMostrato = 255;

    avviaPostSpegnimento(adesso);
    return;
  }

  // Ogni 30 minuti rilegge davvero tutti i sensori.
  if (oraUnix >= prossimaLetturaMonitoraggioUnix) {
    leggiTuttiISensori();
    ultimaLetturaMonitoraggioUnix = oraUnix;

    // Avanzo la scadenza teorica. Cosi', se il loop arriva qualche secondo dopo,
    // la prossima rilettura resta allineata ai 30 minuti previsti.
    prossimaLetturaMonitoraggioUnix += INTERVALLO_MONITORAGGIO_SEC;

    // Forza subito il ridisegno della schermata con i nuovi valori.
    ultimoMinutoMonitorMostrato = 255;
  }

  // Ridisegna la schermata di monitoraggio solo quando cambia il minuto.
  if (
    !visualizzazioneManualeAttiva &&
    !schermataLetturaManualeAttiva &&
    (
      adesso.minute() != ultimoMinutoMonitorMostrato ||
      adesso.hour() != ultimaOraMonitorMostrata ||
      adesso.day() != ultimoGiornoMonitorMostrato
    )
  ) {
    mostraMonitoraggioLCD(adesso);

    ultimoMinutoMonitorMostrato = adesso.minute();
    ultimaOraMonitorMostrata = adesso.hour();
    ultimoGiornoMonitorMostrato = adesso.day();
  }
}


void mostraMonitoraggioLCD(DateTime adesso) {
  uint32_t oraUnix = adesso.unixtime();

  lcd.clear();

  // Riga 0: modalita', ora e minuti residui
  lcd.setCursor(0, 0);
  lcd.print("Mon ");
  stampaDueCifreLCD(adesso.hour());
  lcd.print(":");
  stampaDueCifreLCD(adesso.minute());
  lcd.print(" R:");
  lcd.print(minutiResiduiDaOra(oraUnix, fineMonitoraggioUnix));
  lcd.print("m   ");

  // Riga 1: sensori 1 e 2
  lcd.setCursor(0, 1);
  lcd.print("S1:");
  stampaPercentualeLCD(percentualiUmidita[0]);
  lcd.print(" S2:");
  stampaPercentualeLCD(percentualiUmidita[1]);
  lcd.print("    ");

  // Riga 2: sensori 3 e 4
  lcd.setCursor(0, 2);
  lcd.print("S3:");
  stampaPercentualeLCD(percentualiUmidita[2]);
  lcd.print(" S4:");
  stampaPercentualeLCD(percentualiUmidita[3]);
  lcd.print("    ");

  // Riga 3: stato e prossima rilettura reale
  lcd.setCursor(0, 3);

  if (esisteSensoreSottoSoglia()) {
    lcd.print("SECCO ");
  } else {
    lcd.print("OK    ");
  }

  lcd.print("Next:");
  lcd.print(minutiResiduiDaOra(oraUnix, prossimaLetturaMonitoraggioUnix));
  lcd.print("m    ");
}


uint16_t minutiResiduiDaOra(uint32_t oraUnix, uint32_t scadenzaUnix) {
  if (scadenzaUnix <= oraUnix) {
    return 0;
  }

  return (uint16_t)((scadenzaUnix - oraUnix + 59UL) / 60UL);
}



// ===============================
// RISPARMIO ENERGETICO DISPLAY
// ===============================

void accendiDisplay() {
  if (!displayAcceso) {
    lcd.display();
    lcd.backlight();
    lcd.clear();

    displayAcceso = true;

    // Forza il ridisegno della schermata corrente.
    ultimoMinutoMostrato = 255;
    ultimoMinutoPreMostrato = 255;
    ultimoMinutoPostMostrato = 255;
    ultimoMinutoMonitorMostrato = 255;
  }
}


void spegniDisplay() {
  if (displayAcceso) {
    lcd.clear();
    lcd.noBacklight();
    lcd.noDisplay();

    displayAcceso = false;

    // Quando si riaccendera', ridisegnera' tutto da zero.
    ultimoMinutoMostrato = 255;
    ultimoMinutoPreMostrato = 255;
    ultimoMinutoPostMostrato = 255;
    ultimoMinutoMonitorMostrato = 255;
  }
}


void gestisciDisplayInAttesa(DateTime adesso) {
  if (visualizzazioneManualeAttiva) {
    return;
  }

  uint32_t oraUnix = adesso.unixtime();
  uint32_t prossimaLetturaUnix = prossimoIstanteLetturaUnix(adesso);

  uint32_t secondiAllaProssima = 0;
  if (prossimaLetturaUnix > oraUnix) {
    secondiAllaProssima = prossimaLetturaUnix - oraUnix;
  }

  // Accende il display solo negli ultimi 15 minuti prima della lettura.
  if (secondiAllaProssima <= ANTICIPO_ACCENSIONE_DISPLAY_SEC) {
    accendiDisplay();

    if (
      adesso.minute() != ultimoMinutoPreMostrato ||
      adesso.hour() != ultimaOraPreMostrata ||
      adesso.day() != ultimoGiornoPreMostrato
    ) {
      mostraPreAccensioneLCD(adesso, prossimaLetturaUnix);

      ultimoMinutoPreMostrato = adesso.minute();
      ultimaOraPreMostrata = adesso.hour();
      ultimoGiornoPreMostrato = adesso.day();
    }
  } else {
    postSpegnimentoAttivo = false;
    spegniDisplay();
  }
}


void mostraPreAccensioneLCD(DateTime adesso, uint32_t prossimoLetturaUnix) {
  uint32_t oraUnix = adesso.unixtime();
  uint16_t minutiMancanti = minutiResiduiDaOra(oraUnix, prossimoLetturaUnix);

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Accensione");

  lcd.setCursor(0, 1);
  lcd.print("dispositivo");

  lcd.setCursor(0, 2);
  lcd.print("Prima lettura tra");

  lcd.setCursor(0, 3);
  lcd.print(minutiMancanti);
  lcd.print(" minuti  ");
  stampaDueCifreLCD(adesso.hour());
  lcd.print(":");
  stampaDueCifreLCD(adesso.minute());
}


void avviaPostSpegnimento(DateTime adesso) {
  postSpegnimentoAttivo = true;
  finePostSpegnimentoUnix = adesso.unixtime() + RITARDO_SPEGNIMENTO_DISPLAY_SEC;

  ultimoMinutoPostMostrato = 255;
  ultimaOraPostMostrata = 255;
  ultimoGiornoPostMostrato = 0;

  accendiDisplay();

  if (!visualizzazioneManualeAttiva && !schermataLetturaManualeAttiva) {
    mostraPostSpegnimentoLCD(adesso);
  }
}


void gestisciPostSpegnimento(DateTime adesso) {
  uint32_t oraUnix = adesso.unixtime();

  if (oraUnix >= finePostSpegnimentoUnix) {
    postSpegnimentoAttivo = false;

    if (!visualizzazioneManualeAttiva) {
      spegniDisplay();
    }

    return;
  }

  if (
    !visualizzazioneManualeAttiva &&
    (
      adesso.minute() != ultimoMinutoPostMostrato ||
      adesso.hour() != ultimaOraPostMostrata ||
      adesso.day() != ultimoGiornoPostMostrato
    )
  ) {
    mostraPostSpegnimentoLCD(adesso);

    ultimoMinutoPostMostrato = adesso.minute();
    ultimaOraPostMostrata = adesso.hour();
    ultimoGiornoPostMostrato = adesso.day();
  }
}


void mostraPostSpegnimentoLCD(DateTime adesso) {
  uint32_t oraUnix = adesso.unixtime();
  uint16_t minutiMancanti = minutiResiduiDaOra(oraUnix, finePostSpegnimentoUnix);

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Spegnimento");

  lcd.setCursor(0, 1);
  lcd.print("dispositivo");

  lcd.setCursor(0, 2);
  lcd.print("Display OFF tra");

  lcd.setCursor(0, 3);
  lcd.print(minutiMancanti);
  lcd.print(" minuti  ");
  stampaDueCifreLCD(adesso.hour());
  lcd.print(":");
  stampaDueCifreLCD(adesso.minute());
}


uint32_t prossimoIstanteLetturaUnix(DateTime adesso) {
  uint32_t oraUnix = adesso.unixtime();

  uint32_t albaOggiUnix = istanteLetturaSuDataUnix(adesso, minutoLetturaMattina);
  if (oraUnix <= albaOggiUnix) {
    return albaOggiUnix;
  }

  uint32_t tramontoOggiUnix = istanteLetturaSuDataUnix(adesso, minutoLetturaSera);
  if (oraUnix <= tramontoOggiUnix) {
    return tramontoOggiUnix;
  }

  // Dopo il tramonto, la prossima lettura e' l'alba di domani.
  DateTime domani = adesso + TimeSpan(1, 0, 0, 0);

  uint16_t albaDomani = 0;
  uint16_t tramontoDomani = 0;
  calcolaAlbaTramontoTorino(
    domani.year(),
    domani.month(),
    domani.day(),
    albaDomani,
    tramontoDomani
  );

  return istanteLetturaSuDataUnix(domani, albaDomani);
}


uint32_t istanteLetturaSuDataUnix(DateTime data, uint16_t minutiDaMezzanotte) {
  uint8_t ora = minutiDaMezzanotte / 60;
  uint8_t minuto = minutiDaMezzanotte % 60;

  DateTime istante(
    data.year(),
    data.month(),
    data.day(),
    ora,
    minuto,
    0
  );

  return istante.unixtime();
}


// ===============================
// PULSANTE E SCHERMATA PROSSIMA ESECUZIONE
// ===============================

void isrPulsanteProssimaEsecuzione() {
  richiestaVisualizzazioneDaPulsante = true;
}


void gestisciPulsanteProssimaEsecuzione(DateTime adesso) {
  if (schermataLetturaManualeAttiva) {
    return;
  }
  bool pulsantePremuto = false;

  // Legge e azzera in modo sicuro il flag impostato dall'interrupt.
  noInterrupts();
  if (richiestaVisualizzazioneDaPulsante) {
    richiestaVisualizzazioneDaPulsante = false;
    pulsantePremuto = true;
  }
  interrupts();

  // Fallback: se per qualunque motivo l'interrupt non venisse agganciato,
  // questa lettura diretta permette comunque di rilevare il pulsante tenuto premuto.
  if (digitalRead(PIN_PULSANTE_PROSSIMA_ESECUZIONE) == LOW) {
    pulsantePremuto = true;
  }

  if (!pulsantePremuto) {
    return;
  }

  unsigned long adessoMs = millis();

  if (adessoMs - ultimoPulsanteAccettatoMs < DEBOUNCE_PULSANTE_MS) {
    return;
  }

  ultimoPulsanteAccettatoMs = adessoMs;

  Serial.println("Pulsante premuto: mostro prossima esecuzione.");
  avviaVisualizzazioneManuale(adesso);
}


void avviaVisualizzazioneManuale(DateTime adesso) {
  visualizzazioneManualeAttiva = true;
  fineVisualizzazioneManualeUnix = adesso.unixtime() + DURATA_VISUALIZZAZIONE_MANUALE_SEC;

  ultimoMinutoProssimaMostrato = 255;
  ultimaOraProssimaMostrata = 255;
  ultimoGiornoProssimaMostrato = 0;

  accendiDisplay();
  mostraProssimaEsecuzioneLCD(adesso);
}


void gestisciVisualizzazioneManuale(DateTime adesso) {
  uint32_t oraUnix = adesso.unixtime();

  if (oraUnix >= fineVisualizzazioneManualeUnix) {
    visualizzazioneManualeAttiva = false;

    // Forza il ridisegno della schermata corretta dopo l'uscita
    // dalla visualizzazione manuale.
    ultimoMinutoMostrato = 255;
    ultimoMinutoPreMostrato = 255;
    ultimoMinutoPostMostrato = 255;
    ultimoMinutoMonitorMostrato = 255;

    if (monitoraggioAttivo) {
      mostraMonitoraggioLCD(adesso);
    } else if (postSpegnimentoAttivo) {
      mostraPostSpegnimentoLCD(adesso);
    } else {
      gestisciDisplayInAttesa(adesso);
    }

    return;
  }

  if (
    adesso.minute() != ultimoMinutoProssimaMostrato ||
    adesso.hour() != ultimaOraProssimaMostrata ||
    adesso.day() != ultimoGiornoProssimaMostrato
  ) {
    mostraProssimaEsecuzioneLCD(adesso);

    ultimoMinutoProssimaMostrato = adesso.minute();
    ultimaOraProssimaMostrata = adesso.hour();
    ultimoGiornoProssimaMostrato = adesso.day();
  }
}


void mostraProssimaEsecuzioneLCD(DateTime adesso) {
  uint32_t oraUnix = adesso.unixtime();
  uint32_t eventoUnix;
  bool eventoMonitoraggio = false;

  // Se siamo nelle 2 ore di monitoraggio, la prossima esecuzione reale
  // e' la prossima rilettura dei sensori ogni 30 minuti.
  // Fuori dal monitoraggio, e' la prossima lettura programmata alba/tramonto.
  if (monitoraggioAttivo && prossimaLetturaMonitoraggioUnix < fineMonitoraggioUnix) {
    eventoUnix = prossimaLetturaMonitoraggioUnix;
    eventoMonitoraggio = true;
  } else {
    eventoUnix = prossimoIstanteLetturaUnix(adesso);
  }

  DateTime evento(eventoUnix);

  uint32_t secondiMancanti = 0;
  if (eventoUnix > oraUnix) {
    secondiMancanti = eventoUnix - oraUnix;
  }

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Prossima esecuzione");

  lcd.setCursor(0, 1);

  if (eventoMonitoraggio) {
    lcd.print("Rilettura sensori ");
  } else {
    uint16_t albaEvento = 0;
    uint16_t tramontoEvento = 0;

    calcolaAlbaTramontoTorino(
      evento.year(),
      evento.month(),
      evento.day(),
      albaEvento,
      tramontoEvento
    );

    uint16_t minutoEvento = evento.hour() * 60 + evento.minute();

    if (minutoEvento == albaEvento) {
      lcd.print("Lettura alba      ");
    } else if (minutoEvento == tramontoEvento) {
      lcd.print("Lettura tramonto  ");
    } else {
      lcd.print("Lettura sensori   ");
    }
  }

  lcd.setCursor(0, 2);
  stampaDataOraCompattaLCD(evento);
  lcd.print("        ");

  lcd.setCursor(0, 3);
  stampaTempoMancanteLCD(secondiMancanti);
  lcd.print("        ");
}


void stampaDataOraCompattaLCD(DateTime dataOra) {
  stampaDueCifreLCD(dataOra.day());
  lcd.print("/");
  stampaDueCifreLCD(dataOra.month());
  lcd.print(" ");
  stampaDueCifreLCD(dataOra.hour());
  lcd.print(":");
  stampaDueCifreLCD(dataOra.minute());
}


void stampaTempoMancanteLCD(uint32_t secondiMancanti) {
  uint32_t minuti = (secondiMancanti + 59UL) / 60UL;

  lcd.print("Tra ");

  if (minuti < 60UL) {
    lcd.print(minuti);
    lcd.print(" min");
  } else if (minuti < 1440UL) {
    uint32_t ore = minuti / 60UL;
    uint32_t minutiRestanti = minuti % 60UL;

    lcd.print(ore);
    lcd.print("h ");
    lcd.print(minutiRestanti);
    lcd.print("m");
  } else {
    uint32_t giorni = minuti / 1440UL;
    uint32_t oreRestanti = (minuti % 1440UL) / 60UL;

    lcd.print(giorni);
    lcd.print("g ");
    lcd.print(oreRestanti);
    lcd.print("h");
  }
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

  // Facciamo piu' letture e calcoliamo una media
  for (byte campione = 0; campione < numeroCampioni; campione++) {
    somma += analogRead(pinAnalogici[indiceSensore]);
    delay(pausaTraCampioni);
  }

  // Spegniamo subito il sensore
  digitalWrite(pinAlimentazione[indiceSensore], LOW);

  return somma / numeroCampioni;
}


// ===============================
// AGGIORNAMENTO LCD 20x4
// ===============================

void aggiornaLCD(DateTime adesso) {
  // Funzione mantenuta per compatibilita'.
  // Non contiene piu' delay(20000), perche' ora la persistenza sul display
  // viene gestita da monitoraggioAttivo per 2 ore.
  mostraMonitoraggioLCD(adesso);
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


void stampaOrarioDaMinutiLCD(uint16_t minutiDaMezzanotte) {
  uint8_t ora = minutiDaMezzanotte / 60;
  uint8_t minuto = minutiDaMezzanotte % 60;

  stampaDueCifreLCD(ora);
  lcd.print(":");
  stampaDueCifreLCD(minuto);
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


uint16_t prossimoMinutoLettura(DateTime adesso) {
  uint16_t minutoAdesso = adesso.hour() * 60 + adesso.minute();

  if (minutoAdesso < minutoLetturaMattina) {
    return minutoLetturaMattina;
  }

  if (minutoAdesso < minutoLetturaSera) {
    return minutoLetturaSera;
  }

  // Se sono gia' passati alba e tramonto, la prossima lettura e'
  // l'alba del giorno seguente. Per semplicita' mostro l'alba del giorno
  // corrente: dopo mezzanotte verra' aggiornata automaticamente.
  return minutoLetturaMattina;
}
