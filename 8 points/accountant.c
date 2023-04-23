#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <signal.h>

#define SHM_KEY 3213
#define SEM_KEY 6232

typedef struct
{
    double sum;
    int num_clients_completed;
    int num_clients_total;
} shared_data_t;

shared_data_t *shared_data_ptr;

int shmid, semid;

double f(double x)
{
    return x * x / 1000.0;
}

double integrate(double a, double b, int all_op)
{
    double h = (b - a) / all_op;
    double sum = 0.0;
    double x;
    int i;
    for (i = 0; i < all_op; i++)
    {
        x = a + (i + 0.5) * h;
        sum += f(x);
    }
    return h * sum;
}

void child_process(int i, double a, double b, int all_op)
{
    double area;
    if (i > all_op)
    {
        return;
    }
    double step = (b - a) / (double)all_op;
    area = integrate(a + (step * (double)(i - 1)), a + (step * (double)i), all_op);
    shared_data_ptr->sum += area;
    return;
}

void signal_handler(int sig)
{
    if (shmdt(shared_data_ptr) == -1)
    {
        perror("Ошибка при отключении от разделяемой памяти");
        exit(1);
    }
    exit(0);
}

int main(int argc, char *argv[])
{
    double a, b;
    FILE *infile, *outfile;
    if (argc != 3)
    {
        fprintf(stderr, "Использование: %s <файл ввода> <файл вывода>\n", argv[0]);
        exit(1);
    }
    if ((infile = fopen(argv[1], "r")) == NULL)
    {
        perror("Ошибка при открытии входного файла!\n");
        exit(1);
    }
    if (fscanf(infile, "%lf %lf", &a, &b) != 2)
    {
        printf("Ошибка при чтении входных данных, убедитесь, что в файле ввода только 2 double числа.\n");
        exit(1);
    }
    if (a < 0 || b < 0)
    {
        printf("Ошибка при чтении входных данных, убедитесь, что числа неотрицательные!\n");
        exit(1);
    }

    // Получение доступа к разделяемой памяти
    if ((shmid = shmget(SHM_KEY, sizeof(shared_data_t), 0666)) == -1)
    {
        perror("Ошибка при получении доступа к разделяемой памяти");
        exit(1);
    }

    // Подключение к разделяемой памяти
    if ((shared_data_ptr = shmat(shmid, NULL, 0)) == (shared_data_t *)-1)
    {
        perror("Ошибка при получении указателя на разделяемую память");
        exit(1);
    }

    // Получение доступа к семафору
    if ((semid = semget(SEM_KEY, 1, 0666)) == -1)
    {
        perror("Ошибка при получении доступа к семафору");
        exit(1);
    }

    // Регистрация обработчика сигнала
    signal(SIGINT, signal_handler);

    // Ожидание начала работы сервера
    while (shared_data_ptr->num_clients_total == 0)
    {
        usleep(100);
    }

    int client_num = shared_data_ptr->num_clients_completed + 1;

    printf("Счетовод %d запущен!\n", client_num);
    struct sembuf sem_op;
    sem_op.sem_num = 0;
    sem_op.sem_op = 1;
    sem_op.sem_flg = 0;
    if (semop(semid, &sem_op, 1) == -1)
    {
        perror("Ошибка при отправке сигнала серверу");
        exit(1);
    }
    child_process(client_num, a, b, shared_data_ptr->num_clients_total);
    printf("Счетовод %d: общая сумма = %f\n", client_num, shared_data_ptr->sum);
    shared_data_ptr->num_clients_completed++;

    printf("Счетовод %d завершен\n", client_num);
    fclose(infile);
    // Отключение от разделяемой памяти
    if (shmdt(shared_data_ptr) == -1)
    {
        perror("Ошибка при отключении от разделяемой памяти");
        exit(1);
    }

    return 0;
}