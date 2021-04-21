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


typedef struct {
	pid_t PID;
} SantaT;

typedef struct {
	int len;
	int *arr;
} IntArr;

typedef struct {
	int totalCount;
	int waitingForHelpCount;
	int onHolidayCount;
	IntArr PIDs;
	sem_t *sem;
}SkritciT;

typedef struct {
	int totalCount;
	int onHolidayCount;
	int hitchedCount;
	IntArr PIDs;
	int *queueIndexes;
	sem_t *sem;
} SobiT;

typedef struct {
	SantaT santa;
	SkritciT skritci;
	SobiT sobi;
	bool christmas;
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

int semInit(sem_t **semaphore, int pshared, int value) {
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

int workshop_init(Workshop *workshop, SantaT santa, SkritciT skritci, SobiT sobi) {
	if(semInit(&workshop->skritci.sem, 1, 1) == -1) return -1;
	if(semInit(&workshop->sobi.sem, 1, 1) == -1) {
		semDelete(&workshop->skritci.sem);
		return -1;
	}
	workshop->santa = santa;
	workshop->skritci = skritci;
	workshop->sobi = sobi;
	workshop->christmas = false;
	workshop->sobi.PIDs = createIntArr(workshop->sobi.totalCount);
	if(workshop->sobi.PIDs.arr == NULL) return -1;
	workshop->sobi.queueIndexes = malloc(workshop->sobi.totalCount * sizeof(int));
	workshop->sobi.onHolidayCount = workshop->sobi.totalCount;

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
	if(workshop->sobi.queueIndexes != NULL) free(workshop->sobi.queueIndexes);
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
	while(true) {
		sem_wait(writing);
		writeToFile(outputFile, "%d: Santa: going to sleep\n", (int []){*actionNum}, 1);
		sem_post(writing);
		kill(getpid(), SIGSTOP);
		if(workshop->sobi.onHolidayCount == 0) {
			//Jde zaprahnout soby
			sem_wait(writing);
			writeToFile(outputFile, "%d: Santa: closing_workshop\n", (int []){*actionNum}, 1);
			sem_post(writing);
			//while(workshop->sobi.hitchedCount != workshop->sobi.totalCount) { //Je to pasivní čekání??
				//Zapřahuje soby
				//kill(getpid(), SIGSTOP);
				for(int i=0; i < workshop->sobi.totalCount; ++i) {
					kill(workshop->sobi.queueIndexes[i], SIGCONT);
				}
			//}
			sem_wait(writing);
			writeToFile(outputFile, "%d: Santa: Christmas started\n", (int []){*actionNum}, 1);
			sem_post(writing);
			workshop->christmas = true;
			exit(0);
		} else {
			//Jde pomáhat skřítkům
			sem_wait(writing);
			writeToFile(outputFile, "%d: Santa: helping elves\n", (int []){*actionNum}, 1);
			sem_post(writing);
			//Pomáhá jim pořád, nekontroluju, jestli za tu dobu, co třem pomohl nenaskákali další => jenom pomáhá
			int neededHelpCount = workshop->skritci.waitingForHelpCount;
			while(workshop->skritci.waitingForHelpCount > neededHelpCount - 3) {
				//Pomáhá skřítkům
				//kill(getpid(), SIGSTOP);
			}
		}
	}
}

void Skritek(FILE *outputFile, int id, int minSleep, int maxSleep) {
	sem_wait(writing);
	writeToFile(outputFile, "%d: Elf %d: started\n", (int []){*actionNum, id}, 2);
	sem_post(writing);
	while(true) {
		sleep(1);
		usleep(randomInt(minSleep, maxSleep));//Nyní pracuje
		//Need help
		sem_wait(writing);
		writeToFile(outputFile, "%d: Elf %d: need help\n", (int []){*actionNum, id}, 2);
		sem_post(writing);
		workshop->skritci.waitingForHelpCount++;
		if(workshop->christmas) {
			sem_wait(writing);
			writeToFile(outputFile, "%d: Elf %d: taking holidays\n", (int []){*actionNum, id}, 2);
			sem_post(writing);
			exit(0);
		}
		//TODO: zjistit, jestli je dílna prázdná
		else if(workshop->skritci.waitingForHelpCount >= 3) {	// || workshop->santa.helping == true
			//Vzbudí santu
			kill(workshop->santa.PID, SIGCONT);
			sem_wait(writing);
			writeToFile(outputFile, "%d: Elf %d: get help\n", (int []){*actionNum, id}, 2);
			sem_post(writing);
		} else {
			kill(getpid(), SIGSTOP);
			//sem_wait(workshop->skritci.sem);
		}
	}
	sleepRandom();

}

void Sob(FILE *outputFile, int id, int minSleep, int maxSleep) {
	sem_wait(writing);
	writeToFile(outputFile, "%d: RD %d: rstarted\n", (int []) {*actionNum, id}, 2);
	sem_post(writing);
	usleep(randomInt(minSleep, maxSleep));//Nyní je na dovolené
	sem_wait(writing);
	writeToFile(outputFile, "%d: RD %d: return home\n", (int []) {*actionNum, id}, 2);
	sem_post(writing);
	
	//Obsluha fronty, v které se vrací sobi z dovolené
	arrayPush(workshop->sobi.queueIndexes, getpid(), workshop->sobi.hitchedCount);
	if(workshop->sobi.onHolidayCount == 0) kill(workshop->santa.PID, SIGCONT);
	
	kill(getpid(), SIGSTOP);
	workshop->sobi.hitchedCount++;
	sem_wait(writing);
	writeToFile(outputFile, "%d: RD %d: get hitched\n", (int []) {*actionNum, id}, 2);
	sem_post(writing);
	exit(0);

	/*while(workshop->sobi.hitchedCount != workshop->sobi.totalCount) {
		writeToFile(outputFile, "%d: RD %d: get hitched\n", (int []) {actionNum, id}, 2);
		workshop->sobi.hitchedCount++;
		exit(0);
	}*/
}

void createProcesses(void (*funcPointer)(), int count, int minSleep, int maxSleep, FILE *outputFile, int type) {
	(void) funcPointer;
	(void) count;
	(void) minSleep;
	(void) maxSleep;
	(void) outputFile;
	//int stav;
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
	setbuf(outputFile, NULL);	//Spravny vystup do souboru, zamezeni cekani na naplneni bufferu

	if(semInit(&writing, 1, 1) == -1) return EXIT_FAILURE;
	int idWorkshop, idActionNum;
	if(shareVar(&idWorkshop, (void **)&workshop, sizeof(Workshop)) == -1) return EXIT_FAILURE;
	shareVar(&idActionNum, (void **)&actionNum, sizeof(int));
	*actionNum = 1;
	if(workshop_init(workshop,
				 (SantaT) {false},
				 (SkritciT) {NE, 0, 0, {0, NULL}, NULL},
				 (SobiT) {NR, 0, 0, {0, NULL}, NULL, NULL}  ) == -1) return EXIT_FAILURE;
	

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
