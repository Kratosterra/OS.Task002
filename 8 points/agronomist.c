#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <errno.h>

#define SHM_KEY 3213
#define SEM_KEY 6232

struct shared_data
{
    double sum;
    int num_clients_completed;
    int num_clients_total;
};

void sigint_handler(int sig)
{
    printf("\nПринят сигнал SIGINT. Завершение работы сервера.\n");
    exit(0);
}

int main(int argc, char *argv[])
{
    FILE *infile, *outfile;
    if (argc != 4)
    {
        fprintf(stderr, "Использование: %s <файл ввода> <файл вывода> <кол-во независимых проццессов>\n", argv[0]);
        exit(1);
    }
    if ((infile = fopen(argv[1], "r")) == NULL)
    {
        perror("Ошибка при открытии входного файла!\n");
        exit(1);
    }
    int num_processes = atoi(argv[3]);
    printf("Агроном приказал %d счетоводам разделится и наконец посчитать площадь!\n", num_processes);
    if (num_processes < 1)
    {
        printf("Неправильное кол-во процессов: %s\n", argv[3]);
        exit(1);
    }

    // Установка обработчика сигнала SIGINT
    signal(SIGINT, sigint_handler);

    // Создание/подключение к разделяемой памяти
    int shmid;
    struct shared_data *shared_data_ptr;
    if ((shmid = shmget(SHM_KEY, sizeof(struct shared_data), IPC_CREAT | 0666)) == -1)
    {
        perror("Ошибка при создании/подключении к разделяемой памяти");
        exit(1);
    }
    if ((shared_data_ptr = shmat(shmid, NULL, 0)) == (struct shared_data *)-1)
    {
        perror("Ошибка при получении указателя на разделяемую память");
        exit(1);
    }

    // Создание/подключение к семафорам
    int semid;
    if ((semid = semget(SEM_KEY, 1, IPC_CREAT | 0666)) == -1)
    {
        perror("Ошибка при создании/подключении к семафорам");
        exit(1);
    }

    // Инициализация семафора
    union semun
    {
        int val;
        struct semid_ds *buf;
        short *array;
    } arg;
    arg.val = 1;
    if (semctl(semid, 0, SETVAL, arg) == -1)
    {
        perror("Ошибка при инициализации семафора");
        exit(1);
    }

    // Инициализация разделяемой памяти
    shared_data_ptr->sum = 0;
    shared_data_ptr->num_clients_completed = 0;
    shared_data_ptr->num_clients_total = num_processes;

    // Ожидание завершения всех клиентов
    double answer;
    while (shared_data_ptr->num_clients_completed < num_processes)
    {
        // Ожидание сигнала от клиента
        struct sembuf sem_op;
        sem_op.sem_num = 0;
        sem_op.sem_op = -1;
        sem_op.sem_flg = 0;
        if (semop(semid, &sem_op, 1) == -1)
        {
            perror("Ошибка при ожидании сигнала от клиента");
            exit(1);
        }
        if (shared_data_ptr->num_clients_completed > 0)
        {
            printf("Счетовод %d отработал, текущая сумма: %f\n",
                   shared_data_ptr->num_clients_completed, shared_data_ptr->sum);
            answer = shared_data_ptr->sum;
        }

        // Сигнал клиенту о завершении обновления
        sem_op.sem_num = 1;
        sem_op.sem_op = 1;
        sem_op.sem_flg = 0;
        if (semop(semid, &sem_op, 1) == -1)
        {
        }
    }
    if ((outfile = fopen(argv[2], "w")) == NULL)
    {
        perror("Ошибка при открытии выходного файла!\n");
        exit(1);
    }
    // Вывод общего результата
    printf("Завершаем..\n");
    fprintf(outfile, "Агроном и счетоводы получили общую площадь: %.6f кв.м\n", answer);
    printf("Агроном и счетоводы получили общую площадь: %.6f кв.м\nПодробнее в файле вывода %s\n", answer, argv[2]);

    // Отключение от разделяемой памяти
    if (shmdt(shared_data_ptr) == -1)
    {
        perror("Ошибка при отключении от разделяемой памяти");
        exit(1);
    }
    // Удаление разделяемой памяти
    if (shmctl(shmid, IPC_RMID, NULL) == -1)
    {
        perror("Ошибка при удалении разделяемой памяти");
        exit(1);
    }

    // Удаление семафоров
    if (semctl(semid, 0, IPC_RMID) == -1)
    {
        perror("Ошибка при удалении семафоров");
        exit(1);
    }
    fclose(outfile);
    fclose(infile);
    return 0;
}