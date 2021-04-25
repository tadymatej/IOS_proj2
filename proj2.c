#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>         
#include <sys/stat.h>
#include <semaphore.h>
#include <stdbool.h>
#include <errno.h>
#include <pthread.h>
#include <sys/mman.h>
#include <signal.h>
#include <sched.h>
#include <sys/ipc.h>
#include <sys/shm.h>


#define LOGGING 1
FILE *logFile;
#ifdef LOGGING
	#define LOG(Person, ID, message) (fprintf(logFile, "%d: %s| ID:%d |%s\n", (__LINE__), (Person), (ID), (message)))
	#define LOGINFO(Person, ID, message, info, infoNUM) (fprintf(logFile, "%d: %s| ID:%d | %s | %s:%d\n", (__LINE__), (Person), (ID), (message), (info), (infoNUM)))
#else
	#define LOG(Person, ID, message) 
	#define LOGINFO(Person, ID, message, info, infoNUM) 
#endif

typedef struct {
	pid_t PID;
	sem_t *santaAwake;
	sem_t *santaWakeUp;
	
	sem_t *waitingOnSanta;
	sem_t *santaWaiting;
} SantaT;

typedef struct {
	int len;
	int *arr;
} IntArr;

typedef struct {
	int totalCount;
	int waitingForHelpCount;
	int getHelpCount;
	IntArr PIDs;
	sem_t *queue;
	sem_t *countingEnabled;	/**< semafor určující, zda skřítek smí používat počítadla */
}SkritciT;

typedef struct {
	int totalCount;	/**< Počet sobů */
	int onHolidayCount;	/**< Počet sobů na dovolené */
	IntArr PIDs;	/**< Pole s PID procesů sobů */
	sem_t *countingEnabled;	/**< semafor určující, zda sob smí používat počítadla */
	sem_t *queue;	/**< semafor simulující frontu sobů, kteří se vrátili z dovolené */
	sem_t *allReturned;
} SobiT;

typedef struct {
	SantaT santa;
	SkritciT skritci;
	SobiT sobi;
	bool workshopClosed;
} Workshop;

void arrayPush(int *array, int item, int last) {
	array[last] = item;
}

sem_t *writing;
volatile unsigned int *actionNum; /**< Citac akci */
Workshop *workshop;	/**< Struktura obsahujici informace o workshopu */

typedef void (*Sob_Call)(FILE*, int, int, int);
typedef void (*Skritek_Call)(FILE*, int, int, int);
typedef void (*Santa_Call)(FILE*);

typedef enum {SANTA, SOB, SKRITEK};

/**
 * @todo shmdt(), shmctl(, IPC_RMID)
 * 
 */ 
int shareVar(int *id, void **var, unsigned int size) {
	if((*id = shmget(IPC_PRIVATE, size, IPC_CREAT | IPC_EXCL | 0666)) == -1) return -1;
	*var = shmat(*id, NULL, 0);
	return 0;
}

int semInit(sem_t **semaphore, int pshared, unsigned int value) {
	if((*semaphore = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0)) == MAP_FAILED) {
		fprintf(stderr, "Nepodarilo se vytvorit semafor\n");
		return -1;
	}
	if(sem_init(*semaphore, pshared, value) == -1) {
		fprintf(stderr, "Nepodarilo se vytvorit semafor\n");
		munmap(*semaphore, sizeof(sem_t));
		return -1;
	}
	return 0;
}

void semDelete(sem_t **semaphore) {
	sem_destroy(*semaphore);
	munmap(*semaphore, sizeof(sem_t));
}

IntArr createIntArr(int len) {
	IntArr intArr={0, NULL};
	intArr.arr = malloc(sizeof(int)*len);
	intArr.len=len;
	return intArr;
}

void deleteIntArr(IntArr *intArr) {
	free(intArr->arr);
	intArr->arr = NULL;
	intArr->len = 0;
}

int workshop_init(Workshop *workshop, int NE, int NR) {
	if(semInit(&workshop->sobi.queue, 1, 0) == -1) return -1;
	if(semInit(&workshop->santa.santaWakeUp, 1, 0) == -1) return -1;
	if(semInit(&workshop->sobi.countingEnabled, 1, 1) == -1) return -1;
	if(semInit(&workshop->skritci.countingEnabled, 1, 1) == -1) return -1;
	if(semInit(&workshop->skritci.queue, 1, 0) == -1) return -1;
	if(semInit(&workshop->santa.santaWaiting, 1, 0) == -1) return -1;
	if(semInit(&workshop->santa.waitingOnSanta, 1, 0) == -1) return -1;
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
 * @todo Dealokovani semaforů
 */ 
void workshop_free(Workshop *workshop) {
	deleteIntArr(&workshop->sobi.PIDs);
	deleteIntArr(&workshop->skritci.PIDs);
}

/**
 * Vygeneruje nahodne cislo z intervalu: <min, max)
 * 
 * @return Vraci nahodne cislo ze zadaneho intervalu
 */ 
int randomInt(int min, int max) {
	srand(time(NULL));
	return rand() % max + min;
}

void sleepRandom() {
	sleep(randomInt(1,1000)/1000);
}

//Usage: writeToFile(outputFile, "ahoj %d,%d\n", (int []){5,4}, 2);
void writeToFile(FILE *outputFile, char *msg, int arr[], int arrLen) {
	if(arrLen == 1) fprintf(stdout, msg, arr[0]);
	else if(arrLen == 2) fprintf(stdout, msg, arr[0], arr[1]);
	++(*actionNum);
}

void Santa(FILE *outputFile) {
	LOG("Santa", 0, "Začínám");
	while(true) {
		LOG("Santa", 0, "Čekám na writing");
		sem_wait(writing);
		writeToFile(outputFile, "%d: Santa: going to sleep\n", (int []){*actionNum}, 1);
		LOG("Santa", 0, "Uvolnil jsem writing");
		sem_post(writing);
		//sleep(0);	//Abych si zajistil, že nedojde ke context swapu, když budu nad sem_wait()
		sem_post(workshop->santa.santaAwake);	//Nyní nejsem vzhůru
		LOG("Santa", 0, "Jdu spát");
		sem_wait(workshop->santa.santaWakeUp);	//Nyní mě můžete zkusit vzbudit
		LOG("Santa", 0, "Byl jsem probuzen");
		if(workshop->sobi.onHolidayCount == 0) {
			LOG("Santa", 0, "Sobů je 0 na dovolené");
			LOG("Santa", 0, "Čekám na writing");
			sem_wait(writing);
			writeToFile(outputFile, "%d: Santa: closing workshop\n", (int []){*actionNum}, 1);
			LOG("Santa", 0, "Uvolnil jsem writing");
			sem_post(writing);
			workshop->workshopClosed = true;
			//Sob mi to zablokoval, já to uvolním skřítkům
			sem_post(workshop->santa.santaAwake);	//Nyní nezáleží na tom, jestli spím, nebo nespím
			sem_post(workshop->skritci.queue);		//Skřítci už si můžete brát dovolenou
			sem_post(workshop->sobi.allReturned);	//Sob to zablokoval, já to uvolním skřítkům co čekají na soby
			sem_post(workshop->sobi.queue);
			LOG("Santa", 0, "Čekám na posledního hitched soba");
			while(workshop->sobi.totalCount != 0) {
				sem_wait(workshop->santa.santaWakeUp); //Počkám si, než mi poslední sob řekne, že jsem ho hitchul
			}
			LOG("Santa", 0, "Poslední sob byl hitchnud, čekám na writing");
			sem_wait(writing);
			writeToFile(outputFile, "%d: Santa: Christmas started\n", (int []){*actionNum}, 1);
			LOG("Santa", 0, "Christmas started");
			sem_post(writing);
			LOG("Santa", 0, "KONEC");
			exit(0);
		}
		//Jde pomáhat skřítkům
		LOG("Santa", 0, "Čekám na writing");
		sem_wait(writing);
		writeToFile(outputFile, "%d: Santa: helping elves\n", (int []){*actionNum}, 1);
		LOG("Santa", 0, "Uvolnil jsem writing a pomáhám skřítkům");
		sem_post(writing);

		for(int i = 0; i < 3; ++i) {	//Řeknu 3 skřítkům, že mohou jít do dílny
			sem_post(workshop->skritci.queue);
		}
		LOG("Santa", 0, "Čekám než pomůžu všem skřítkům");
		sem_wait(workshop->santa.santaWakeUp);	//Počkám, než mi skřítek řekne, že 3 skřítci děkují za pomoc
		//Santa pomohl všem elfům
		LOG("Santa", 0, "Poslední skřítek odešel z dílny");
		workshop->skritci.getHelpCount = 0;	//Vynuluju počet, kolika jsem pomohl
	}
}

void skritekTakeHolidays(FILE *outputFile, int id) {
	LOG("Skřítek", id, "Beru si dovolenou");
	writeToFile(outputFile, "%d: Elf %d: taking holidays\n", (int []){*actionNum, id}, 2);
	sem_post(writing);
	LOG("Skřítek", id, "KONEC");
	exit(0);
}

void Skritek(FILE *outputFile, int id, int minSleep, int maxSleep) {
	sem_wait(writing);
	LOG("Skřítek", id, "Začínám");
	writeToFile(outputFile, "%d: Elf %d: started\n", (int []){*actionNum, id}, 2);
	sem_post(writing);
	while(true) {
		LOG("Skřítek", id, "Pracuji");
		usleep(randomInt(minSleep, maxSleep));//Nyní pracuje
		//Need help
		sem_wait(writing);
		LOG("Skřítek", id, "Potřebuji pomoc");
		writeToFile(outputFile, "%d: Elf %d: need help\n", (int []){*actionNum, id}, 2);
		sem_post(writing);
		
		LOG("Skřítek", id, "Čekám dokud je Santa vzhůru");
		sem_wait(workshop->santa.santaAwake);	//Čekám, dokud je santa vzhůru
		sem_post(workshop->santa.santaAwake);	//Dám vědět ostatním, že santa je vzhůru
		sem_wait(workshop->skritci.countingEnabled);
		workshop->skritci.waitingForHelpCount++;
		sem_post(workshop->skritci.countingEnabled);
		while(workshop->sobi.onHolidayCount == 0 && !workshop->workshopClosed) {
			LOG("Skřítek", id, "Dávám přednost sobům, je jich onHoliday 0");
			sem_wait(workshop->sobi.allReturned);	//Čeká dokud poslední sob nedá svolení pokračovat
			sem_post(workshop->sobi.allReturned);
			
		}
		if(workshop->workshopClosed) {
			LOG("Skřítek", id, "Dílna je zavřená");
			sem_post(workshop->santa.santaAwake);//Já jsem zjistil, že santa už zavřel dílnu, dávám vědět ostatním
			sem_post(workshop->skritci.queue);	// Uvolním i frontu, pokud někdo čeká před dílnou ať vědí, že můžou jít na dovolenou
			LOG("Skřítek", id, "Čekám na zápis");
			sem_wait(writing);
			skritekTakeHolidays(outputFile, id);
		}
		else if(workshop->skritci.waitingForHelpCount == 3) {
			LOG("Skřítek", id, "čekám na semafor santaAwake, jsem třetí skřítek v dílně (chci říct, že santa je vzhůru)");
			sem_wait(workshop->santa.santaAwake);	//Říkám ostatní, že nyní je santa vzhůru
			LOG("Skřítek", id, "Jsem třetí skřítek před dílnou, budím santu");
			sem_post(workshop->santa.santaWakeUp);//Budím santu
		}
		LOG("Skřítek", id, "Čekám ve skřítkovské frontě");
		sem_wait(workshop->skritci.queue);
		LOG("Skřítek", id, "Nyní jsem vstoupil do dílny");
		if(workshop->workshopClosed) {
			LOG("Skřítek", id, "Ale dílna je zavřená");
			sem_post(workshop->santa.santaAwake);//Já jsem zjistil, že santa už zavřel dílnu, dávám vědět ostatním
			sem_post(workshop->skritci.queue);	// Uvolním i frontu, pokud někdo čeká před dílnou ať vědí, že můžou jít na dovolenou
			LOG("Skřítek", id, "Čekám na zápis");
			sem_wait(writing);
			skritekTakeHolidays(outputFile, id);
		}
		LOG("Skřítek", id, "Čekám na zápis");
		sem_wait(writing);
		writeToFile(outputFile, "%d: Elf %d: get help\n", (int []){*actionNum, id}, 2);
		sem_post(writing);

		sem_wait(workshop->skritci.countingEnabled);
		workshop->skritci.waitingForHelpCount--;
		LOGINFO("Skřítek", id, "Dostává se mi pomoc", "Kolikátý jsem dostal pomoc:", workshop->skritci.getHelpCount+1);
		workshop->skritci.getHelpCount++;
		if(workshop->skritci.getHelpCount == 3) {
			LOG("Skřítek", id, "Jsem poslední skřítek, kterému santa pomohl, díky santo");
			sem_post(workshop->santa.santaWakeUp); //Díky santo za pomoc
		}
		sem_post(workshop->skritci.countingEnabled);
	}	
}

void Sob(FILE *outputFile, int id, int minSleep, int maxSleep) {
	sem_wait(writing);
	LOG("Sob", id, "Začal jsem nyní, jsem na dovolené");
	writeToFile(outputFile, "%d: RD %d: rstarted\n", (int []) {*actionNum, id}, 2);
	sem_post(writing);
	usleep(randomInt(minSleep, maxSleep));//Nyní je na dovolené
	sem_wait(writing);
	LOG("Sob", id, "Vrátil jsem se z dovolené");
	writeToFile(outputFile, "%d: RD %d: return home\n", (int []) {*actionNum, id}, 2);
	sem_post(writing);

	sem_wait(workshop->sobi.countingEnabled);
	workshop->sobi.onHolidayCount--;
	LOGINFO("Sob", id, "Snížil jsem naše počitadlo onHolidayCount", "a to na hodnotu:", workshop->sobi.onHolidayCount);
	sem_post(workshop->sobi.countingEnabled);
	
	if(workshop->sobi.onHolidayCount == 0) {
		LOG("Sob", id, "Jsem poslední sob, který se vrátil z dovolené, čekám na santu dokud je vzhůru");
		sem_wait(workshop->santa.santaAwake);	//Dokud je santa vzhůru, tak čeká
		sem_post(workshop->sobi.allReturned);	//Sám pro sebe si uvolní semafor
		sem_wait(workshop->sobi.allReturned);	//Zablokuje znovu tento semafor skřítkům
		LOG("Sob", id, "Jdu vzbudit santu");
		sem_post(workshop->santa.santaWakeUp);	//Santo vstávej
	}
	LOG("Sob", id, "Čekám v sobí frontě");
	sem_wait(workshop->sobi.queue);

	sem_wait(writing);
	LOG("Sob", id, "Byl jsem hitchnut");
	writeToFile(outputFile, "%d: RD %d: get hitched\n", (int []) {*actionNum, id}, 2);
	sem_post(writing);
	workshop->sobi.totalCount--;
	LOGINFO("Sob", id, "Snížil jsem naše počitadlo totalCount", "a to na hodnotu:", workshop->sobi.totalCount);
	sem_post(workshop->sobi.queue); //Další sob může!
	if(workshop->sobi.totalCount == 0) {
		LOG("Sob", id, "Jsem poslední hitchnutý sob");
		sem_post(workshop->santa.santaWakeUp);	//Santo, můžeš dát nápis christmas started
		//sem_post(workshop->santa.santaWaiting);	//TODO, provizorní!!
	}
	LOG("Sob", id, "KONEC");
	exit(0);

}

void createProcesses(void (*funcPointer)(), int count, int minSleep, int maxSleep, FILE *outputFile, int type) {
	for(int i=0; i < count; ++i) {
		int pid = fork();
		if(pid == 0) {
			if (type == SANTA) {
				workshop->santa.PID = getpid();
			} else if(type == SKRITEK) {
				workshop->skritci.PIDs.arr[i] = getpid();
			} else {
				workshop->sobi.PIDs.arr[i] = getpid();
			}
			funcPointer(outputFile, i+1, minSleep, maxSleep);
		} else if(pid == -1) {
			//Chyba
		} else {
			
		}
	}
}

void waitForEveryone(Workshop workshop) {
	for(int i = 0; i < workshop.skritci.PIDs.len; ++i) {
		waitpid(workshop.skritci.PIDs.arr[i], NULL, 0);
	}
	for(int i = 0; i < workshop.sobi.PIDs.len; ++i) {
		waitpid(workshop.sobi.PIDs.arr[i], NULL, 0);
	}
	waitpid(workshop.santa.PID, NULL, 0);
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
	if(outputFile == NULL) return EXIT_FAILURE;
	
	logFile = fopen("log.txt", "w");
	setbuf(logFile, NULL);	//TODO po debbugingu smazat
	setbuf(outputFile, NULL);	//Spravny vystup do souboru, zamezeni cekani na naplneni bufferu

	if(semInit(&writing, 1, 1) == -1) return EXIT_FAILURE;
	int idWorkshop, idActionNum;
	if(shareVar(&idWorkshop, (void **)&workshop, sizeof(Workshop)) == -1) return EXIT_FAILURE;
	shareVar(&idActionNum, (void **)&actionNum, sizeof(int));
	*actionNum = 1;
	if(workshop_init(workshop, NE, NR) == -1) return EXIT_FAILURE;
	

	//Hlavni proces
	createProcesses(santaF, 1, -1, -1, outputFile, SANTA);
	createProcesses(skritekF, NE, 0, TE+1, outputFile, SKRITEK);
	createProcesses(sobF, NR, TR/2, TR+1, outputFile, SOB);

	
	waitForEveryone(*workshop);
	fclose(outputFile);
	workshop_free(workshop);

	exit(0);

	(void) argc;
	(void) argv;
	return 0;
}
