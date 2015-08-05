#define numberOfButtons 1

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include "PetitFS/diskio.h"
#include "PetitFS/pff.h"
#include "ButtonPress.h"

// obs³uga Timer0 z preskalerem = 8
#define TMR_START TCCR0B |= (1<<CS01)
#define TMR_STOP TCCR0B &= ~(1<<CS01)

//*************** obs³uga PetitFAT

#define SCK 	PB5
#define MOSI 	PB3
#define MISO 	PB4
#define CS 		PB2

volatile uint8_t can_read;

FATFS Fs;			/* File system object */
DIR Dir;			/* Directory object */
FILINFO Fno;		/* File information */

WORD rb;

static UINT play ( const char *fn );

#define BUF_SIZE 512			// maksymalny rozmiar pojedynczego bufora

uint8_t  buf[2][BUF_SIZE];		// podwójny bufor do odczytu z karty SD

volatile uint8_t nr_buf;		// indeks aktywnego buforu

volatile uint16_t xaxis=700;						//Zmienna do zapisu wartoœci przyspieszenia w osi X
volatile uint16_t yaxis=700;						//Zmienna do zapisu wartoœci przyspieszenia w osi Y
volatile uint16_t zaxis=700;						//Zmienna do zapisu wartoœci przyspieszenia w osi Z
volatile uint8_t hitpriority=0;                     //Zmienne s³u¿¹ce do ustalenia priorytetów odgrywanego dŸwiêku
volatile uint8_t swingdisable=0;
volatile uint8_t hitdisable=0;
int hit[10] = {1,3,2,1,3,2,1,3,1,1};		//Tablica i zmienne symuluj¹ce losowoœæ w odtwarzaniu dŸwiêku uderzenia
volatile uint8_t hitnumber = 1;						
volatile uint8_t hitcounter = 0;					
int swing[10] = {1,3,2,1,3,2,1,3,1,2};		//Tablice i zmienne symuluj¹ce losowoœæ w odtwarzaniu dŸwiêku uderzenia
volatile uint8_t swingnumber = 1;
volatile uint8_t swingcounter = 0;
uint8_t startup = 1;							//Zmienna do odtworzenia dŸwiêku w³¹czania
volatile uint8_t playstop = 0;						//Zmienna s³u¿¹ca do przerwania odtwarzania danego pliku dŸwiêkowego
volatile uint16_t counter;							//Zmienna s³u¿¹ca za zabezpieczenie przed przypadkowym zbyt du¿ym/ma³ym wynikiem odczytu akcelerometru
volatile uint8_t buttondisable;						//Zmienna s³u¿¹ca do od³¹czenia przycisku po jego przyciœniêciu
volatile uint8_t shutdown=0;						//Zmienna aktywuj¹ca sekwencjê przejœcia w tryb POWER DOWN
volatile uint16_t ledcounter;						//Zmienne do ustawienia czasu b³yskania diody
volatile uint8_t ledoff;							//Zmienna ropozczynaj¹ca instrukcjê wy³¹czania diody
volatile uint16_t swingcounter1;					//Zmienna do zliczenia iloœci odegranych próbek
volatile uint16_t hitcounter1;						//Zmienna do zliczenia iloœci odegranych próbek		

#define MAINLED 	PD2								//Definicja diody œwiêc¹cej stale oraz b³yskaj¹cej. PD0 - R PD1 - B PD2 - G
#define BLINKLED 	PD1

#define swingthresholdhigh 	350						//Progi dolne aktywuj¹ce zdarzenia swing i hit
#define swingthresholdlow 	200
#define swingthresholdhigh1 1000					//Progi górne aktywuj¹ce zdarzenia swing i hit
#define swingthresholdlow1 	850
//Sekwencja wy³¹czaj¹ca wszystkie port mikrokontrolera
#define SHUTDOWNSEQUENCE PORTD=0; \
PORTB=0; \
PORTC=0; 

//Sekwencje przechodz¹ce na kolejny wiersz tabeli z zapisanymi numerami plików
#define NEXTSWING swingcounter++; \
if (swingcounter==9) swingcounter=0;
#define NEXTHIT hitcounter++; \
if (hitcounter==9) hitcounter=0;

// **************** main() ***********************************************
int main(void) {
	
	DDRD  |= (1<<PD4)|(1<<PD0)|(1<<PD1)|(1<<PD2)|(1<<PD5)|(1<<PD7);			//Wyjœcia dla diód, przycisku i std.
	DDRB  |= (1<<PINB1)|(1<<PINB0);
	
	//DDRB |= (1<<PB0) | (1<<PB1);				
	PORTD |= (1<<PIND5);						//Wyjœcie na pin shutdown wzmacniacza
	PORTD |= (1<<PIND4);							//Obs³uga przycisku SLEEP
	
	set_sleep_mode(SLEEP_MODE_PWR_DOWN);  //Wybranie trybu uœpienia mikrokontrolera
		
	//Inicjalizacja SPI
	DDRB |= (1<<CS)|(1<<MOSI)|(1<<SCK)|(1<<CS);
	PORTB |= (1<<CS);
	SPCR |= (1<<SPE)|(1<<MSTR);		
	SPSR |= (1<<SPI2X);				// maksymalny zegar SCK

	//konfiguracja PWM na Timer1
	TCCR1A = (1<<WGM10)|(1<<COM1A1)|(0<<COM1A0);	//Wybór trybu PWM
	TCCR1B = (1<<CS10);								//Wybó preskalera PWM

	//konfiguracja Timer0 (samplowanie)
	TCCR0A = (1<<WGM01);		// tryb CTC (Compare MAtch)
	TIMSK0 = (1<<OCIE0A);		// zezwolenie na przerwanie CompareMatch A

	sei();		// globalne zezwolenie na przerwania
	
	OCR0A = (uint8_t)88;						//czêstotliwoœæ próbkowania, FCPU/8/samplerate
	
	
	// **************** akcelerometr **********************************
	
	ADCSRA |= 1<<ADPS2 | 1<<ADPS1 | 1<<ADPS0 | 1<<ADIE | 1<<ADEN;	//ustawienie preskalera, w³¹czenie przerwania i modu³u konwersji
	
	// **************** pêtla g³ówna **********************************
	while(1) {
		if (pf_mount(&Fs)) continue;		//inicjalizacja FS
		PORTB|=(1<<PINB0);
		while(1) {
			PORTD|=(1<<PIND7);
		if (pf_opendir(&Dir, "")) break;	//otwarcie g³ównego folderu karty
			
		if (startup==1)						//odtworzenie dŸwiêku w³¹czania 
		{
			play("startup.wav");
			startup=0;
			PORTD |= (1<<MAINLED);			//w³¹czenie diody g³ównej
		}
		
	// ********************* zdarzenie SWING  ****************
		if (hitpriority==1)				
		{
			swingnumber=swing[swingcounter];
			if (swingnumber==1)
			{
				NEXTSWING;
				play("swing.wav");
				break;
			}
			if (swingnumber==2)
			{
				NEXTSWING;
				play("swing2.wav");
				break;
			}
			if (swingnumber==3)
			{
				NEXTSWING;
				play("swing3.wav");
				break;
			}
		}
		
	// ******************** zdarzenie HIT ***********
		if (hitpriority==2)	
		{
			hitnumber=hit[hitcounter];
			PORTD |= (1<<BLINKLED);				//W³¹czenie dodatkowej diody
			ledoff=1;							//Rozpoczêcie instrukcji wy³¹czaj¹cej dodatkow¹ diodê
			if (hitnumber==1)
			{
				NEXTHIT;
				play("clash.wav");
				break;
			}
			if (hitnumber==2)
			{
				NEXTHIT;
				play("clash2.wav");
				break;
			}
			if (hitnumber==3)
			{
				NEXTHIT;
				play("clash3.wav");
				break;
			}
		}
		
	// ********************** sekwencja POWER DOWN  ******************	
		if (shutdown==1)
		{
			play("pwrdwn.wav");
			SHUTDOWNSEQUENCE;		//Wy³¹czenie wszystkich portów
			sleep_mode();			//Uœpienie w trybie power down, wy³¹czaj¹ce wszystkie zegary
		}
		
	// ********************** tryb ja³owy  ******************	
		else
		{
			if (play("hum.wav")) break;	
		}
		}

	}
}


//***************** przerwanie TIMER0 - samplowanie ******************************************
ISR(TIMER0_COMPA_vect) {
	
	
	static uint16_t buf_idx;		// indeks w pojedynczym buforze
	static uint8_t v1;			// zmienne do przechowywania próbek
	
				v1 = buf[nr_buf][buf_idx++];		// pobieramy próbkê MONO do zmiennej v1

	OCR1A = v1;									// próbka na wyjœcie PWM


	if( buf_idx > BUF_SIZE-1 ) {
		buf_idx=0;								// reset indeksu bufora
		can_read = 1;							// flaga = 1
		nr_buf ^= 0x01;							// zmiana bufora na kolejny
	}

	// ************************ aktywacja zdarzenia SWING *************
	if(((xaxis<swingthresholdhigh && xaxis>swingthresholdlow) || (yaxis<swingthresholdhigh && yaxis>swingthresholdlow) || (zaxis<swingthresholdhigh && zaxis>swingthresholdlow) || (xaxis<swingthresholdhigh1 && xaxis>swingthresholdlow1) || (yaxis<swingthresholdhigh1 && yaxis>swingthresholdlow1) || (zaxis<swingthresholdhigh1 && zaxis>swingthresholdlow1)) && swingdisable == 0)
	{
		counter++;
		if (counter>5)
		{
			hitpriority=1;
			playstop=1;
			counter=0;
			swingdisable=1;
		}
	}
	else
	{
		counter=0;
	}
	// *********************** aktywacja zdarzenia HIT ***********
	if((xaxis<swingthresholdlow || yaxis<swingthresholdlow || zaxis<swingthresholdlow || xaxis>swingthresholdhigh1 || yaxis>swingthresholdhigh1 || zaxis>swingthresholdhigh1) && hitdisable == 0)
	{
		hitpriority=2;
		playstop=1;
		counter=0;
		swingdisable=1; //blokuje zaktywacje zdarzenia swing
		hitdisable=1; //blokuje aktywacje zdarzenia hit
		hitcounter1=0;  //zeruje licznik w przypadku kolejnej aktywacji zdarzenia hit

	}
	
	// ******************  funkcja  b³yskania diody  ********************************	
	if (ledoff==1)
	{
		ledcounter++;
		if (ledcounter>5000)
		{
			PORTD ^= (1<<BLINKLED);
			ledcounter=0;
			ledoff=0;
		}
	}
	// ******************  funkcja  zabezpieczaj¹ca swing  ********************************
	if (hitpriority==1)
	{
		swingcounter1++;
		if (swingcounter1==10000)
		{
			swingdisable=0;		//aktywuje zdarzenie swing po odtworzeniu 10000 próbek
			hitdisable=0;		//aktywuje zdarzenie hit po odtworzeniu 10000 próbek
			swingcounter1=0;
			hitpriority=0;
		}
	}
	// ******************  funkcja  zabezpieczaj¹ca hit  ********************************
		if (hitpriority==2)
		{
			hitcounter1++;
			if (hitcounter1==5000)
			{
				hitdisable=0; //aktywuje zdarzenie hit po odtworzeniu 5000 próbek

			}
			if (hitcounter1==20000)
			{
				swingdisable=0; //aktywuje zdarzenie swing po odtworzeniu 20000 próbek
				hitcounter1=0;
				hitpriority=0;
			}
		}
	ADCSRA |= 1<<ADSC;							//start nowej konwersji ADC
}

// ******************  funkcja  P L A Y  ********************************
static UINT play ( const char *fn ) {

	FRESULT res;

	if ((res = pf_open(fn)) == FR_OK) {

		pf_lseek(44);
		
		pf_read(&buf[0][0], BUF_SIZE , &rb);	// za³aduj pierwsz¹ czêœæ bufora
		pf_read(&buf[1][0], BUF_SIZE , &rb);	// za³aduj drug¹ czêœæ bufora

		TMR_START;		// start Timera0 (samplowanie)

		while(1) {
			if( can_read ) {				// jeœli flaga ustawiona w obs³udze przerwania

				pf_read(&buf[ nr_buf ^ 0x01 ][0], BUF_SIZE , &rb);	// odczytaj kolejny bufor
				if( rb < BUF_SIZE ) break;		// jeœli koniec pliku przerwij pêtlê while(1)
				can_read = 0;
			}

			if (playstop==1)			//przerwanie odtwarzania
			{
				playstop=0;
				break;
			}
			if (ButtonPressed(0, PIND, 4, 3000) && buttondisable==0)    //aktywowanie sekwencji POWER DOWN
			{
				hitpriority=0;
				buttondisable=1;
				shutdown=1;
				playstop=1;
				swingdisable=1;
				hitdisable=1;

			}
		}
		TMR_STOP;	// wy³¹czenie Timera0 (samplowania)
	}

	return res;
}

ISR(ADC_vect)
{
	uint8_t Low = ADCL;
	uint16_t Result = ADCH<<8 | Low;	//zapisanie wyniku 10-bitowego z u¿yciem przesuniêcia
	switch (ADMUX)									//odczytywanie wartoœci przeyspieszenia z ka¿dej osi akcelerometru
	{
		case 0b00000000:
		xaxis=Result;
		ADMUX = 0b00000001;
		break;
		case 0b00000001:
		yaxis=Result;
		ADMUX = 0b00000010;
		break;
		case 0b00000010:
		zaxis=Result;
		ADMUX = 0b00000000;
		break;
		default:
		break;
	}
}

