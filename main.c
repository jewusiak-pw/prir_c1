#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/time.h>

#define BUFFOR_SIZE 80

// Funkcja obsługująca sygnał SIGUSR1
void signal_handler() {
    printf("Child (pid: %d) received SIGUSR1...\n", getpid());
}

int main() {
    printf("parent pid: %d\n", getpid());
    
    // ilość procesów 
    int procCount = 4;

    // read vector
    printf("Reading from file...\n");
    FILE *f = fopen("vector.dat", "r");
    if (f == NULL) {
        printf("File reading problem :( \n");
        exit(1);
    }
    char buffer[BUFFOR_SIZE + 1];
    double *vector;
    int i;
    fgets(buffer, BUFFOR_SIZE, f);
    int vectorSize = atoi(buffer);
    vector = malloc(sizeof(double) * vectorSize);
    for (i = 0; i < vectorSize; i++) {
        fgets(buffer, BUFFOR_SIZE, f);
        vector[i] = atof(buffer);
    }
    fclose(f);


    int partsize = (vectorSize % procCount) == 0 ? (vectorSize / procCount) : (vectorSize / procCount) + 1;
    printf("Part size: %d\n", partsize);

    // Ustawienie obsługi sygnału SIGUSR1
    signal(SIGUSR1, signal_handler);

    // utworzenie kluczy do pamieci współdzielonej
    key_t key = ftok("main.c", 67);
    key_t key_w = ftok("main.c", 68);

    // Tworzenie pamięci współdzielonej wektora źródłowego
    int shmid;
    if ((shmid = shmget(key, sizeof(double) * vectorSize, 0666 | IPC_CREAT)) == -1) {
        fprintf(stderr, "SHMGET1 failed");
        exit(1);
    }

    // Tworzenie pamięci współdzielonej wektora wyjściowego
    int shmid_w;
    if ((shmid_w = shmget(key_w, sizeof(double) * procCount, 0666 | IPC_CREAT)) == -1) {
        fprintf(stderr, "SHMGET2 failed");
        exit(2);
    }

    //tworzenie dzieci
    pid_t *pids = malloc(sizeof(pid_t) * procCount);
    for (i = 0; i < procCount; i++) {
        int pids_1 = fork();
        if (pids_1 < 0) {
            perror("Fork failed");
            exit(3);
        } else if (pids_1 == 0) { // Dziecko
            
            // Indeksy obliczane:
            int start = partsize * i;
            int bound = partsize * (i + 1) > vectorSize ? vectorSize : partsize * (i + 1);

            // Podłączenie pamięci współdzielonej wektora źródłowego
            double *tab;
            if ((tab = (double *) shmat(shmid, (void *) 0, 0)) < 0) {
                fprintf(stderr, "SHMAT1 failed");
                exit(1);
            }

            // Podłaczenie pamięci współdzielonej wektora wyjściowego
            double *tab_w;
            if ((tab_w = (double *) shmat(shmid_w, (void *) 0, 0)) < 0) {
                fprintf(stderr, "SHMAT1 failed");
                exit(1);
            }

            printf("Child %d pids[%d] will calculate (from %d, to %d, n=%d)...\n", getpid(), i, start, bound,
                   bound - start);

            pause(); // Czeka na sygnał od rodzica
            
            // Obliczenia...
            double sum = 0;
            for (int it = start; it < bound; it++) {
                sum += tab[it];
            }
            tab_w[i] = sum;

            exit(0);
        } else { // rodzic
            pids[i] = pids_1;
        }
    }


    // Podłaczenie pamięci współdzielonej wektora źródłowego
    double *tab;
    if ((tab = (double *) shmat(shmid, (void *) 0, 0)) < 0) {
        fprintf(stderr, "SHMAT1 failed @ parent");
        exit(1);
    }
    
    // kopiowanie do wektora źródłowego
    for (i = 0; i < vectorSize; i++)
        tab[i] = vector[i];

    // Podłaczenie pamięci współdzielonej wektora wyjściowego
    double *tab_w;
    if ((tab_w = (double *) shmat(shmid_w, (void *) 0, 0)) < 0) {
        fprintf(stderr, "SHMAT2 failed @ parent");
        exit(1);
    }

    // oczekiwanie na stworzenie dzieci
    sleep(2);
    struct timeval stop, start;

    printf("Starting calculation...\n");

    gettimeofday(&start, NULL);

    // Poinformowanie dzieci, że mogą rozpocząć przetwarzanie
    for (i = 0; i < procCount; i++) {
        kill(pids[i], SIGUSR1);
    }

    // Oczekiwanie na zakończenie obliczeń przez dzieci
    for (i = 0; i < procCount; i++) {
        wait(NULL);
    }

    double sum_f = 0;
    for (i = 0; i < procCount; i++)
        sum_f += tab_w[i];

    printf("FINAL SUM: %f\n", sum_f);
    gettimeofday(&stop, NULL);
    printf("FINAL TIME (N=%d) is %lld us.", procCount,
           ((long long) (stop.tv_sec - start.tv_sec) * 1000000) + ((long long) (stop.tv_usec - start.tv_usec) / 1));

    // Odłączenie od pamięci współdzielonej
    if (shmdt(tab) < 0 || shmdt(tab_w) < 0) {
        fprintf(stderr, "shmdt failed");
        exit(1);
    }

    // Usunięcie od pamięci współdzielonej
    if (shmctl(shmid, IPC_RMID, NULL) < 0 || shmctl(shmid_w, IPC_RMID, NULL) < 0) {
        fprintf(stderr, "smctl failed");
        exit(1);
    }

    free(pids);
    free(vector);
    
    return 0;
}
