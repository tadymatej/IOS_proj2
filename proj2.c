/************************************************************
* Název projektu: IOS - projekt 2 - synchronizace procesů
* Datum: 1.5. 2021
* Podlední změna: 16.4. 2021
* **********************************************************/
/**
* @file proj2.c
* 
* @author Matěj Žalmánek (xzalma00) 
************************************************************/ 

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>       
#include <semaphore.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/shm.h>

/**
 * Struktura obsahující informace o santovi a jeho semafory, které slouží pro komunikaci s ním
 */ 
typedef struct {
	pid_t PID;	/**< PID procesu santy */
	sem_t *santaAwake;	/**< semafor určující, zda je santa vzhůru či nikoliv */
	sem_t *santaWakeUp;	/**< Semafor určující, zda santa spí či nikoliv */
} SantaT;

/**
 * Struktura pole, s očekávanou statickou velikostí (známou v době prvního spuštění = init v main())
 */ 
typedef struct {
	int len;	/**< Délka pole */
	int *arr;	/**< Pole samotné */
} IntArr;

/**
 * Struktura obsahující informace o skřítcích a jejich semafory
 */ 
typedef struct {
	int totalCount;	/**< Celkový počet skřítků pracujících pro santy (NE) */
	int waitingForHelpCount;	/**< Počet skřítků, kteří čekají na pomoc od santy */
	int getHelpCount;			/**< Počet skřítků, kteří dostali pomoc od santy */
	IntArr PIDs;				/**< Pole všech PID procesů skřítků */
	sem_t *queue;				/**< Fronta skřítků, kteří čekají na pomoc od santy */
	sem_t *countingEnabled;		/**< semafor určující, zda skřítek smí používat počítadla */
}SkritciT;

/**
 * Struktura obsahující informace o sobech a jejich semafory
 */ 
typedef struct {
	int totalCount;		/**< Počet sobů, kteří nejsou hitchnutí */
	int onHolidayCount;	/**< Počet sobů na dovolené */
	IntArr PIDs;	/**< Pole s PID procesů sobů */
	sem_t *countingEnabled;	/**< semafor určující, zda sob smí používat počítadla */
	sem_t *queue;	/**< semafor simulující frontu sobů, kteří se vrátili z dovolené */
	sem_t *allReturned;	/**< Semafor, který určuje, zda se poslední sob vrátil z dovolené */
} SobiT;

/**
 * Struktura obsahující informace o Santově dílně
 */ 
typedef struct {
	SantaT santa;	/**< Santa samotný */
	SkritciT skritci;	/**< Santovi skřítci */
	SobiT sobi;			/**< Santovi sobi */
	bool workshopClosed;/**< Informace, zda byl zavřen workshop = Santa jde zapřahovat soby */
} Workshop;

sem_t *writing;	/**< Semafor povolující jednotlivcům zápis do souboru */
volatile unsigned int *actionNum; /**< Citac akci */
Workshop *workshop;	/**< Struktura obsahujici informace o workshopu */

typedef void (*Sob_Call)(FILE*, int, int, int);		/**< Ukazatel na funkci, která se očekává, že bude Sob()*/
typedef void (*Skritek_Call)(FILE*, int, int, int);	/**< Ukazatel na funkci, která se očekává, že bude Skritek() */
typedef void (*Santa_Call)(FILE*);					/**< Ukazatel na funkci, která se očekává, že bude Santa() */

/**
 * Vytvoří sdílenou paměť o určité velikosti a vrátí ukazatel na tuto sdílenou paměť
 * @param id Ukazatel, do kterého se nahraje ID přidělené sdílené paměti
 * @param var Proměnná, do které se nahraje ukazatel na sdílenou paměť
 * @param size Požadovaná velikost sdílené paměti
 * @return Vrací 0, pokud se podaří sdílet paměť, v opačném případě vrací -1
 */ 
int shareVar(int *id, void **var, unsigned int size) {
	if((*id = shmget(IPC_PRIVATE, size, IPC_CREAT | IPC_EXCL | 0666)) == -1) return -1;
	*var = shmat(*id, NULL, 0);
	if(*var == (void *) -1) {
		*var = NULL;
		return -1;
	}
	return 0;
}

/**
 * Uvolní sdílenou paměť, která byla vytvořena funkcí shareVar
 * @param shmid = ID shared memory
 * @param var Odkaz na sdílenou paměť, kterou poté vynuluje
 */ 
void removeSharedVar(int shmid, void **var) {
	if(*var != NULL) {
		shmctl(shmid, IPC_RMID, NULL);
		shmdt(*var);
	}
	*var = NULL;
}

/**
 * Inicializuje semafor - vytvoří pro něj sdílenou paměť a nastaví defaultní hodnoty
 * 
 * @param semaphore semafor, do kterého se má nahrát sdílený semafor
 * @param pshared Pokud je nenulové, sdílí se s ostatními procesy
 * @param value Inicializační hodnota semaforu
 * @return Vrací -1, pokud se semafor nepovede alokovat / vytvořit, pokud se vše povede, vrací 0
 */ 
int semInit(sem_t **semaphore, int pshared, unsigned int value) {
	if((*semaphore = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0)) == MAP_FAILED) {
		return -1;
	}
	if(sem_init(*semaphore, pshared, value) == -1) {
		munmap(*semaphore, sizeof(sem_t));
		return -1;
	}
	return 0;
}

/**
 * Zruší celý semafor
 * @param semaphore Semafor, který má být zrušen
 */ 
void semDelete(sem_t **semaphore) {
	sem_destroy(*semaphore);
	munmap(*semaphore, sizeof(sem_t));
}

/**
 * Vytvoří pole integerů
 * 
 * @param len Požadovaná velikost pole
 * @return Vrací strukturu IntArr, pokud se strukturu nepodaří alokovat, nastaví IntArr.len na 0, v opačném případě na požadovanou velikost, vprvní položku v poli nastaví na -1
 */ 
IntArr createIntArr(int len) {
	IntArr intArr={0, NULL};
	intArr.arr = malloc(sizeof(int)*len);
	if(intArr.arr != NULL)
	{
			intArr.arr[0] = -1;
			intArr.len=len;
	}
	return intArr;
}

/**
 * Dealokuje z paměti strukturu IntArr
 * 
 * @param intArr Pole, které má být dealokováno
 */ 
void deleteIntArr(IntArr *intArr) {
	free(intArr->arr);
	intArr->arr = NULL;
	intArr->len = 0;
}

/**
 * Inicializuje workshop santy = účastníky dílny + jejich semafory
 * @param workshop Ukazatel na strukturu workshop, která se inicializuje
 * @param NE Počet skřítků, který má být ve workshopu
 * @param NR Počet sobů, který má být ve workshopu
 * @return Vrací 0, pokud se vše povede alokovat, v opačném případě vrací -1 a očekává se uvolnění prostředku pomocí workshop_destroy()
 */ 
int workshop_init(Workshop *workshop, int NE, int NR) {
	if(semInit(&workshop->sobi.queue, 1, 0) == -1) return -1;
	if(semInit(&workshop->santa.santaWakeUp, 1, 0) == -1) return -1;
	if(semInit(&workshop->sobi.countingEnabled, 1, 1) == -1) return -1;
	if(semInit(&workshop->skritci.countingEnabled, 1, 1) == -1) return -1;
	if(semInit(&workshop->skritci.queue, 1, 0) == -1) return -1;
	if(semInit(&workshop->santa.santaAwake, 1, 0) == -1) return -1;
	if(semInit(&workshop->sobi.allReturned, 1, 0) == -1) return -1;
	workshop->skritci.totalCount = NE;
	workshop->skritci.waitingForHelpCount = 0;
	workshop->skritci.getHelpCount = 0;
	workshop->sobi.totalCount = NR;
	workshop->sobi.onHolidayCount = NR;
	workshop->workshopClosed = false;
	workshop->sobi.PIDs = createIntArr(workshop->sobi.totalCount);
	if(workshop->sobi.PIDs.arr == NULL) return -1;

	workshop->skritci.PIDs = createIntArr(workshop->skritci.totalCount);
	if(workshop->skritci.PIDs.arr == NULL) {
		deleteIntArr(&workshop->skritci.PIDs);
		return -1;
	}
	return 0;
}

/**
 * Dealokuje workshop
 * @param workshop Workshop, který má být dealokován
 */ 
void workshop_destroy(Workshop *workshop) {
	if(workshop == NULL) return;
	deleteIntArr(&workshop->sobi.PIDs);
	deleteIntArr(&workshop->skritci.PIDs);
	semDelete(&workshop->sobi.queue);
	semDelete(&workshop->santa.santaWakeUp);
	semDelete(&workshop->sobi.countingEnabled);
	semDelete(&workshop->skritci.countingEnabled);
	semDelete(&workshop->skritci.queue);
	semDelete(&workshop->santa.santaAwake);
	semDelete(&workshop->sobi.allReturned);
}

/**
 * Vygeneruje nahodne cislo z intervalu: <min, max)
 * 
 * @param min Začátek intervalu, číslo se do intervalu zahrnuje
 * @param max Konec intervalu, číslo se do intervalu NEzahrnuje
 * @return Vraci nahodne cislo ze zadaneho intervalu
 */ 
int randomInt(int min, int max) {
	srand(time(NULL));
	return rand() % max + min;
}

/**
 * Funkce, která zapíše zprávu do souboru
 * Usage: 
 * sem_wait(writing);	- pokud se sdílí soubor mezi procesy
 * writeToFile(outputFile, "ahoj %d,%d\n", (int []){5,4}, 2);
 * sem_post(writing);
 * @param outputFile Soubor, do kterého se má zapsat zpráva
 * @param msg Formátovaný text zprávy, obsahující maximálně 2x znak '%'
 * @param arr pole hodnot, pro výpis ve formátovacím řetězci msg
 * @param arrLen číslo určující, kolik hodnot je ve formátovacím řetězci očekáváno, že se vypíše 
 */
void writeToFile(FILE *outputFile, char *msg, int arr[], int arrLen) {
	if(arrLen == 1) {
		 fprintf(outputFile, msg, arr[0]);
	}
	else if(arrLen == 2) {
		fprintf(outputFile, msg, arr[0], arr[1]);
	}
	++(*actionNum);
}

/**
 * Obsluha santy
 * @param outputFile soubor, do kterého má santa zapisovat zprávy
 */ 
void Santa(FILE *outputFile) {

	while(true) {

		sem_wait(writing);
		writeToFile(outputFile, "%d: Santa: going to sleep\n", (int []){*actionNum}, 1);
		sem_post(writing);
		//sleep(0);	//Abych si zajistil, že nedojde ke context swapu, když budu nad sem_wait()
		sem_post(workshop->santa.santaAwake);	//Nyní nejsem vzhůru
		//workshop->skritci.waitingForHelpCount--;

		sem_wait(workshop->santa.santaWakeUp);	//Nyní mě můžete zkusit vzbudit

		if(workshop->sobi.onHolidayCount == 0) {

			sem_wait(writing);
			writeToFile(outputFile, "%d: Santa: closing workshop\n", (int []){*actionNum}, 1);
			sem_post(writing);
			workshop->workshopClosed = true;
			//Sob mi to zablokoval, já to uvolním skřítkům
			sem_post(workshop->santa.santaAwake);	//Nyní nezáleží na tom, jestli spím, nebo nespím
			sem_post(workshop->skritci.queue);		//Skřítci už si můžete brát dovolenou
			sem_post(workshop->sobi.allReturned);	//Sob to zablokoval, já to uvolním skřítkům co čekají na soby
			sem_post(workshop->sobi.queue);

			while(workshop->sobi.totalCount != 0) {
				sem_wait(workshop->santa.santaWakeUp); //Počkám si, než mi poslední sob řekne, že jsem ho hitchul
			}

			sem_wait(writing);
			writeToFile(outputFile, "%d: Santa: Christmas started\n", (int []){*actionNum}, 1);

			sem_post(writing);

			exit(0);
		}
		//Jde pomáhat skřítkům

		sem_wait(writing);
		writeToFile(outputFile, "%d: Santa: helping elves\n", (int []){*actionNum}, 1);

		sem_post(writing);

		for(int i = 0; i < 3; ++i) {	//Řeknu 3 skřítkům, že mohou jít do dílny
			sem_post(workshop->skritci.queue);
		}

		sem_wait(workshop->santa.santaWakeUp);	//Počkám, než mi skřítek řekne, že 3 skřítci děkují za pomoc
		//Santa pomohl všem elfům

		sem_wait(workshop->skritci.countingEnabled);
		workshop->skritci.getHelpCount = 0;	//Vynuluju počet, kolika jsem pomohl
		sem_post(workshop->skritci.countingEnabled);
		for(int i = 0; i < 3; ++i)  {
			workshop->skritci.waitingForHelpCount--;
		}
	}
}

/**
 * Funkce obsluhující zprávu skřítka, že si bere dovolenou - končí proces
 * @param outputFile Soubor, do kterého se má zpráva zapsat
 * @param id Id skřítka, který tuto zprávu vypisuje
 */ 
void skritekTakeHolidays(FILE *outputFile, int id) {
	writeToFile(outputFile, "%d: Elf %d: taking holidays\n", (int []){*actionNum, id}, 2);
	sem_post(writing);
	exit(0);
}

/**
 * Obsluha jednoho skřítka
 * @param outputFile soubor, do kterého má skřítek zapisovat zprávy
 * @param id ID skřítka v rámci workshopu
 * @param minSleep minimální doba, po kterou má skřítek pracovat
 * @param maxSleep maximální doba, po kterou má skřítek pracovat
 */ 
void Skritek(FILE *outputFile, int id, int minSleep, int maxSleep) {
	sem_wait(writing);
	writeToFile(outputFile, "%d: Elf %d: started\n", (int []){*actionNum, id}, 2);
	sem_post(writing);
	while(true) {

		usleep(randomInt(minSleep, maxSleep));//Nyní pracuje
		//Need help
		sem_wait(writing);
		writeToFile(outputFile, "%d: Elf %d: need help\n", (int []){*actionNum, id}, 2);
		sem_post(writing);
		

		sem_wait(workshop->santa.santaAwake);	//Čekám, dokud je santa vzhůru
		sem_post(workshop->santa.santaAwake);	//Dám vědět ostatním, že santa je vzhůru
		while(workshop->sobi.onHolidayCount == 0 && !workshop->workshopClosed) {
			sem_wait(workshop->sobi.allReturned);	//Čeká dokud poslední sob nedá svolení pokračovat
			sem_post(workshop->sobi.allReturned);
		}

		sem_wait(workshop->skritci.countingEnabled);	//Zaberu si počítadlo, abych měl jistotu, že než dojdu ke kontrole, zda jsem 3., aby jsme tam nedošli dva
		workshop->skritci.waitingForHelpCount++;
		if(workshop->workshopClosed) {
			sem_post(workshop->skritci.countingEnabled);	// Uvolním počitadlo

			sem_post(workshop->santa.santaAwake);//Já jsem zjistil, že santa už zavřel dílnu, dávám vědět ostatním
			sem_post(workshop->skritci.queue);	// Uvolním i frontu, pokud někdo čeká před dílnou ať vědí, že můžou jít na dovolenou
			sem_wait(writing);
			skritekTakeHolidays(outputFile, id);
		}
		else if(workshop->skritci.waitingForHelpCount == 3) {
			sem_wait(workshop->santa.santaAwake);	//Říkám ostatní, že nyní je santa vzhůru
			sem_post(workshop->santa.santaWakeUp); //Budím santu
			sem_post(workshop->skritci.countingEnabled);	// Uvolním počitadlo
		}
		else {
			sem_post(workshop->skritci.countingEnabled);	//Uvolním počitadlo
		}

		sem_wait(workshop->skritci.queue);
		if(workshop->workshopClosed) {
			sem_post(workshop->santa.santaAwake);//Já jsem zjistil, že santa už zavřel dílnu, dávám vědět ostatním
			sem_post(workshop->skritci.queue);	// Uvolním i frontu, pokud někdo čeká před dílnou ať vědí, že můžou jít na dovolenou
			sem_wait(writing);
			skritekTakeHolidays(outputFile, id);
		}
		sem_wait(writing);
		writeToFile(outputFile, "%d: Elf %d: get help\n", (int []){*actionNum, id}, 2);
		sem_post(writing);

		sem_wait(workshop->skritci.countingEnabled);
		workshop->skritci.getHelpCount++;
		if(workshop->skritci.getHelpCount == 3) {
			sem_post(workshop->santa.santaWakeUp); //Díky santo za pomoc
		}
		sem_post(workshop->skritci.countingEnabled);
	}	
}

/**
 * Obsluha jednoho soba
 * @param outputFile soubor, do kterého má sob zapisovat zprávy
 * @param id ID soba v rámci workshopu
 * @param minSleep minimální doba, po kterou má sob být na dovolené
 * @param maxSleep maximální doba, po kterou má být sob na dovolené
 */ 
void Sob(FILE *outputFile, int id, int minSleep, int maxSleep) {
	sem_wait(writing);

	writeToFile(outputFile, "%d: RD %d: rstarted\n", (int []) {*actionNum, id}, 2);
	sem_post(writing);
	usleep(randomInt(minSleep, maxSleep));//Nyní je na dovolené
	sem_wait(writing);

	writeToFile(outputFile, "%d: RD %d: return home\n", (int []) {*actionNum, id}, 2);
	sem_post(writing);

	sem_wait(workshop->sobi.countingEnabled);	//Zablokuju si počitadlo, abychom nemohli přijít dva sobi jako "poslední"
	workshop->sobi.onHolidayCount--;

	if(workshop->sobi.onHolidayCount == 0) {
		sem_post(workshop->sobi.countingEnabled);

		sem_wait(workshop->santa.santaAwake);	//Dokud je santa vzhůru, tak čeká
		sem_post(workshop->sobi.allReturned);	//Sám pro sebe si uvolní semafor
		sem_wait(workshop->sobi.allReturned);	//Zablokuje znovu tento semafor skřítkům

		sem_post(workshop->santa.santaWakeUp);	//Santo vstávej
	}
	else {
		sem_post(workshop->sobi.countingEnabled);
	}

	sem_wait(workshop->sobi.queue);

	sem_wait(writing);

	writeToFile(outputFile, "%d: RD %d: get hitched\n", (int []) {*actionNum, id}, 2);
	sem_post(writing);
	workshop->sobi.totalCount--;

	sem_post(workshop->sobi.queue); //Další sob může!
	if(workshop->sobi.totalCount == 0) {

		sem_post(workshop->santa.santaWakeUp);	//Santo, můžeš dát nápis christmas started
	}

	exit(0);
}

/**
 * Ukončí všechny vytvořené procesy
 * Ukončuje tak dlouho, dokud nenarazí na PID == -1 = v tomto místě se nepovedlo vytvořit proces, nebo do počtu celkových procesů
 * 
 */ 
void killAllProcesses() {
	for(int i=0; i < workshop->skritci.PIDs.len; ++i) {
		int PID = workshop->skritci.PIDs.arr[i];
		if(PID == -1) break;
		kill(PID, SIGKILL);
	}
	for(int i=0; i < workshop->sobi.PIDs.len; ++i) {
		int PID = workshop->sobi.PIDs.arr[i];
		if(PID == -1) break;
		kill(PID, SIGKILL);
	}
	if(workshop->santa.PID != -1) kill(workshop->santa.PID, SIGKILL);
}

/**
 * Vytvoří všechny procesy jednoho typu
 * @param funcPointer Ukazatel na funkci, který se bude volat pro potomka
 * @param count Počet procesů daného typu, který se má vytvořit
 * @param minSleep minimální doba uspání procesu (pro volání usleep())
 * @param maxSleep maximální doba uspání procesu (pro volání usleep())
 * @param outputFile soubor, do kterého budou procesy zapisovat
 * @param pidArr Pole, do kterého se mají ukládat PIDs vytvořených procesů
 */ 
int createProcesses(void (*funcPointer)(), int count, int minSleep, int maxSleep, FILE *outputFile, int *pidArr) {
	for(int i=0; i < count; ++i) {
		int pid = fork();
		if(pid == 0) {
			pidArr[i] = getpid();
			funcPointer(outputFile, i+1, minSleep, maxSleep);
		} else if(pid == -1) {
			pidArr[i] = -1;
			return 1;
		} 
	}
	return 0;
}

/**
 * Funkce, ve které hlavní proces čeká na skončení všech podprocesů
 * @param workshop Workshop, ve které jsou všechny podprocesy
 */ 
void waitForEveryone(Workshop workshop) {
	for(int i = 0; i < workshop.skritci.PIDs.len; ++i) {
		waitpid(workshop.skritci.PIDs.arr[i], NULL, 0);
	}
	for(int i = 0; i < workshop.sobi.PIDs.len; ++i) {
		waitpid(workshop.sobi.PIDs.arr[i], NULL, 0);
	}
	waitpid(workshop.santa.PID, NULL, 0);
}

void releaseResources(FILE **file, int idWorkshop, int idActionNum) {
	if(*file != NULL) {
		fclose(*file);
		*file = NULL;
	}
	workshop_destroy(workshop);
	removeSharedVar(idWorkshop, (void **) &workshop);
	removeSharedVar(idActionNum, (void **) &actionNum);
	semDelete(&writing);
}

int main(int argc, char **argv)
{
	if(argc != 5) {
		fprintf(stderr, "Ocekavany pocet argumentu: 4, ale zadano: %d\n", (argc-1));
		return EXIT_FAILURE;
	}
	int NE = atoi(argv[1]);
	if(NE <= 0 || NE >= 1000) {
		fprintf(stderr, "Pocet skritku je mimo rozsah (0,1000)\n");
		return EXIT_FAILURE;
	}
	int NR = atoi(argv[2]);
	if(NR <= 0 || NR >= 20) {
		fprintf(stderr, "Pocet sobu je mimo rozsah (0,20)\n");
		return EXIT_FAILURE;
	}
	int TE = atoi(argv[3]);
	if(TE < 0 || TE > 1000) {
		fprintf(stderr, "Maximalni doba, po kterou skritek pracuje samostatne je mimo rozsah <0,1000>\n");
		return EXIT_FAILURE;
	}
	int TR = atoi(argv[4]);
	if(TR < 0 || TR > 1000) {
		fprintf(stderr, "Maximalni doba, po ktere se sob vraci z dovolene domu je mimo rozsah <0,1000>\n");
		return EXIT_FAILURE;
	}
	Santa_Call santaF = Santa;
	Skritek_Call skritekF = Skritek;
	Sob_Call sobF = Sob;
	FILE *outputFile = fopen("proj2.out", "w");
	if(outputFile == NULL) {
		fprintf(stderr, "Nepodarilo se otevrit soubor\n");
		return EXIT_FAILURE;
	}
	
	setbuf(outputFile, NULL);	//Spravny vystup do souboru, zamezeni cekani na naplneni bufferu

	int idWorkshop = -1, idActionNum = -1;
	if(semInit(&writing, 1, 1) == -1) {
		releaseResources(&outputFile, idWorkshop, idActionNum);
		fprintf(stderr, "Nastal problem v alokaci pameti a zdroju\n");
		return EXIT_FAILURE;
	}
	if(shareVar(&idWorkshop, (void **)&workshop, sizeof(Workshop)) == -1)  {
		releaseResources(&outputFile, idWorkshop, idActionNum);
		fprintf(stderr, "Nastal problem v alokaci pameti a zdroju\n");
		return EXIT_FAILURE;
	}
	if(shareVar(&idActionNum, (void **)&actionNum, sizeof(int)) == -1) {
		releaseResources(&outputFile, idWorkshop, idActionNum);
		fprintf(stderr, "Nastal problem v alokaci pameti a zdroju\n");
		return EXIT_FAILURE;
	}
	*actionNum = 1;
	if(workshop_init(workshop, NE, NR) == -1) {
		releaseResources(&outputFile, idWorkshop, idActionNum);
		fprintf(stderr, "Nastal problem v alokaci pameti a zdroju\n");
		return EXIT_FAILURE;
	}

	//Hlavni proces
	int failure = 0;
	failure = createProcesses(santaF, 1, -1, -1, outputFile, &workshop->santa.PID);
	if(!failure) failure = createProcesses(skritekF, NE, 0, TE+1, outputFile, workshop->skritci.PIDs.arr);
	if(!failure) failure = createProcesses(sobF, NR, TR/2, TR+1, outputFile, workshop->sobi.PIDs.arr);
	if(failure) {
		releaseResources(&outputFile, idWorkshop, idActionNum);
		killAllProcesses();
		fprintf(stderr, "Nepodařilo se vytvořit procesy\n");
		return EXIT_FAILURE;
	}

	waitForEveryone(*workshop);
	releaseResources(&outputFile, idWorkshop, idActionNum);
	return EXIT_SUCCESS;
}

/***************** Konec souboru proj2.c ***************/
