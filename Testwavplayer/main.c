#define numberOfButtons 1

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include "PetitFS/diskio.h"
#include "PetitFS/pff.h"
#include "ButtonPress.h"

// obs�uga Timer0 z preskalerem = 8
#define TMR_START TCCR0B |= (1<<CS01)
#define TMR_STOP TCCR0B &= ~(1<<CS01)

//*************** obs�uga PetitFAT

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

uint8_t  buf[2][BUF_SIZE];		// podw�jny bufor do odczytu z karty SD

volatile uint8_t nr_buf;		// indeks aktywnego buforu

volatile uint16_t xaxis=700;						//Zmienna do zapisu warto�ci przyspieszenia w osi X
volatile uint16_t yaxis=700;						//Zmienna do zapisu warto�ci przyspieszenia w osi Y
volatile uint16_t zaxis=700;						//Zmienna do zapisu warto�ci przyspieszenia w osi Z
volatile uint8_t hitpriority=0;                     //Zmienne s�u��ce do ustalenia priorytet�w odgrywanego d�wi�ku
volatile uint8_t swingdisable=0;
volatile uint8_t hitdisable=0;
int hit[10] = {1,3,2,1,3,2,1,3,1,1};		//Tablica i zmienne symuluj�ce losowo�� w odtwarzaniu d�wi�ku uderzenia
volatile uint8_t hitnumber = 1;						
volatile uint8_t hitcounter = 0;					
int swing[10] = {1,3,2,1,3,2,1,3,1,2};		//Tablice i zmienne symuluj�ce losowo�� w odtwarzaniu d�wi�ku uderzenia
volatile uint8_t swingnumber = 1;
volatile uint8_t swingcounter = 0;
uint8_t startup = 1;							//Zmienna do odtworzenia d�wi�ku w��czania
volatile uint8_t playstop = 0;						//Zmienna s�u��ca do przerwania odtwarzania danego pliku d�wi�kowego
volatile uint16_t counter;							//Zmienna s�u��ca za zabezpieczenie przed przypadkowym zbyt du�ym/ma�ym wynikiem odczytu akcelerometru
volatile uint8_t buttondisable;						//Zmienna s�u��ca do od��czenia przycisku po jego przyci�ni�ciu
volatile uint8_t shutdown=0;						//Zmienna aktywuj�ca sekwencj� przej�cia w tryb POWER DOWN
volatile uint16_t ledcounter;						//Zmienne do ustawienia czasu b�yskania diody
volatile uint8_t ledoff;							//Zmienna ropozczynaj�ca instrukcj� wy��czania diody
volatile uint16_t swingcounter1;					//Zmienna do zliczenia ilo�ci odegranych pr�bek
volatile uint16_t hitcounter1;						//Zmienna do zliczenia ilo�ci odegranych pr�bek		

#define MAINLED 	PD2								//Definicja diody �wi�c�cej stale oraz b�yskaj�cej. PD0 - R PD1 - B PD2 - G
#define BLINKLED 	PD1

#define swingthresholdhigh 	350						//Progi dolne aktywuj�ce zdarzenia swing i hit
#define swingthresholdlow 	200
#define swingthresholdhigh1 1000					//Progi g�rne aktywuj�ce zdarzenia swing i hit
#define swingthresholdlow1 	850
//Sekwencja wy��czaj�ca wszystkie port mikrokontrolera
#define SHUTDOWNSEQUENCE PORTD=0; \
PORTB=0; \
PORTC=0; 

//Sekwencje przechodz�ce na kolejny wiersz tabeli z zapisanymi numerami plik�w
#define NEXTSWING swingcounter++; \
if (swingcounter==9) swingcounter=0;
#define NEXTHIT hitcounter++; \
if (hitcounter==9) hitcounter=0;

// **************** main() ***********************************************
int main(void) {
	
	DDRD  |= (1<<PD4)|(1<<PD0)|(1<<PD1)|(1<<PD2)|(1<<PD5)|(1<<PD7);			//Wyj�cia dla di�d, przycisku i std.
	DDRB  |= (1<<PINB1)|(1<<PINB0);
	
	//DDRB |= (1<<PB0) | (1<<PB1);				
	PORTD |= (1<<PIND5);						//Wyj�cie na pin shutdown wzmacniacza
	PORTD |= (1<<PIND4);							//Obs�uga przycisku SLEEP
	
	set_sleep_mode(SLEEP_MODE_PWR_DOWN);  //Wybranie trybu u�pienia mikrokontrolera
		
	//Inicjalizacja SPI
	DDRB |= (1<<CS)|(1<<MOSI)|(1<<SCK)|(1<<CS);
	PORTB |= (1<<CS);
	SPCR |= (1<<SPE)|(1<<MSTR);		
	SPSR |= (1<<SPI2X);				// maksymalny zegar SCK

	//konfiguracja PWM na Timer1
	TCCR1A = (1<<WGM10)|(1<<COM1A1)|(0<<COM1A0);	//Wyb�r trybu PWM
	TCCR1B = (1<<CS10);								//Wyb� preskalera PWM

	//konfiguracja Timer0 (samplowanie)
	TCCR0A = (1<<WGM01);		// tryb CTC (Compare MAtch)
	TIMSK0 = (1<<OCIE0A);		// zezwolenie na przerwanie CompareMatch A

	sei();		// globalne zezwolenie na przerwania
	
	OCR0A = (uint8_t)88;						//cz�stotliwo�� pr�bkowania, FCPU/8/samplerate
	
	
	// **************** akcelerometr **********************************
	
	ADCSRA |= 1<<ADPS2 | 1<<ADPS1 | 1<<ADPS0 | 1<<ADIE | 1<<ADEN;	//ustawienie preskalera, w��czenie przerwania i modu�u konwersji
	
	// **************** p�tla g��wna **********************************
	while(1) {
		if (pf_mount(&Fs)) continue;		//inicjalizacja FS
		PORTB|=(1<<PINB0);
		while(1) {
			PORTD|=(1<<PIND7);
		if (pf_opendir(&Dir, "")) break;	//otwarcie g��wnego folderu karty
			
		if (startup==1)						//odtworzenie d�wi�ku w��czania 
		{
			play("startup.wav");
			startup=0;
			PORTD |= (1<<MAINLED);			//w��czenie diody g��wnej
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
			PORTD |= (1<<BLINKLED);				//W��czenie dodatkowej diody
			ledoff=1;							//Rozpocz�cie instrukcji wy��czaj�cej dodatkow� diod�
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
			SHUTDOWNSEQUENCE;		//Wy��czenie wszystkich port�w
			sleep_mode();			//U�pienie w trybie power down, wy��czaj�ce wszystkie zegary
		}
		
	// ********************** tryb ja�owy  ******************	
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
	static uint8_t v1;			// zmienne do przechowywania pr�bek
	
				v1 = buf[nr_buf][buf_idx++];		// pobieramy pr�bk� MONO do zmiennej v1

	OCR1A = v1;									// pr�bka na wyj�cie PWM


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
	
	// ******************  funkcja  b�yskania diody  ********************************	
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
	// ******************  funkcja  zabezpieczaj�ca swing  ********************************
	if (hitpriority==1)
	{
		swingcounter1++;
		if (swingcounter1==10000)
		{
			swingdisable=0;		//aktywuje zdarzenie swing po odtworzeniu 10000 pr�bek
			hitdisable=0;		//aktywuje zdarzenie hit po odtworzeniu 10000 pr�bek
			swingcounter1=0;
			hitpriority=0;
		}
	}
	// ******************  funkcja  zabezpieczaj�ca hit  ********************************
		if (hitpriority==2)
		{
			hitcounter1++;
			if (hitcounter1==5000)
			{
				hitdisable=0; //aktywuje zdarzenie hit po odtworzeniu 5000 pr�bek

			}
			if (hitcounter1==20000)
			{
				swingdisable=0; //aktywuje zdarzenie swing po odtworzeniu 20000 pr�bek
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
		
		pf_read(&buf[0][0], BUF_SIZE , &rb);	// za�aduj pierwsz� cz�� bufora
		pf_read(&buf[1][0], BUF_SIZE , &rb);	// za�aduj drug� cz�� bufora

		TMR_START;		// start Timera0 (samplowanie)

		while(1) {
			if( can_read ) {				// je�li flaga ustawiona w obs�udze przerwania

				pf_read(&buf[ nr_buf ^ 0x01 ][0], BUF_SIZE , &rb);	// odczytaj kolejny bufor
				if( rb < BUF_SIZE ) break;		// je�li koniec pliku przerwij p�tl� while(1)
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
		TMR_STOP;	// wy��czenie Timera0 (samplowania)
	}

	return res;
}

ISR(ADC_vect)
{
	uint8_t Low = ADCL;
	uint16_t Result = ADCH<<8 | Low;	//zapisanie wyniku 10-bitowego z u�yciem przesuni�cia
	switch (ADMUX)									//odczytywanie warto�ci przeyspieszenia z ka�dej osi akcelerometru
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

