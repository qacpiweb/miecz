1. trzaski (sprawdzic przyczyne - sprzetowa, czy programowa, sprobowalbym uzyc tez 16-bitowego licznika do pwma i pobawienie sie z opcjami, zuzycie 1 pwm
2. ktory tryb porownywania adc dla akcelerometru (moze stabilizator napiecia wystarczy, nieistotne jesli korzystamy z akcelerometru i2c)
3. ladne dzwieki (modulowany hum, zestaw clashy, swingow itd, podzial na np 2 zestawy - z nowej i starej trylogii)
4. akcelerometr i2c
5. (wszystko na zmiennych, zeby niektore rzeczy bylo latwo zamienic na wczytywane z pliku)wyrzucenie configu do pliku tekstowego na karcie sd wczytywanego przy uruchomieniu: kolor, thresholdy dla akcelerometru, nazwy plikow dzwiekowych
6. algorytm do wykrywania zdarzen akcelerometru (swingi clashe)
7. wybudzanie (mozliwosc hard reset? - zalatwione jezeli podpiecie usb bedzie trzymalo mikrokontroler na resecie, co i tak w zasadzie jest konieczne, bo nie chcemy modyfikowac zawartosci karty, jaklby nie daj Boze mikrokontroler byl wlaczony i z niego korzystal)
8. zeby mozna bylo podac dowolny kolor (podac ile r ile g i ile b i ile w zeby to dzialalo) (przemiatanie przez wszystkie mozliwosci) - zuzycie 4 kanalow pwm, sprzetowo na pinach okreslonych, ale potencjalnie tez programowo na dowolnym pinie (mozliwy konflikt z przerwaniami)
9. obadać co te timery robią znaczy jaka jest różnica między 8 bit i 16 bit oprócz oczywistej. i czy da się ten 16bit zrobić na 8 bit, czy to nie ma znaczenia i czy potencjalnie dźwięku tamtędy by nie dało się puścić
10. taktowanie 12 Mhz
11. Przerzucenie sie na atxmege, 32 Mhz, bez zewnetrznego rezonatora, lepsze timery, wszystko lepsze, potencjalnie moze rozwiazac problemy z pwm dla kolorow (posiada 3 countery 16-bitowe, z czego jeden ma az 4 outputy, potencjalnie zalatwi cala diode rgbw). Konwerter DAC moze pomoc z jakoscia dzwiekow, lub wyeliminowac trzaski, jezeli byly sprzetowe. Wyzsze taktowanie pozwoli tez chyba na lepsze algorytmy np wykrywania swingow, bo bedzie wiecej probek w danym okresie czasu i ogolnie wieksze mozliwosci.
