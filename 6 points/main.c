#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>
#include <signal.h>

#define SEM_KEY 1234 // ключ для семафоров
#define SHM_KEY 5678 // ключ для разделяемой памяти

int shmid, semid;    // идентификаторы разделяемой памяти и семафоров
double *shared_area; // указатель на разделяемую память

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

void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM)
    {

        // Отключаемся от разделяемой памяти
        if (shmdt(shared_area) == -1)
        {
            perror("Ошибка при отключении от разделяемой памяти");
            exit(1);
        }

        // Удаляем разделяемую память и семафоры
        if (shmctl(shmid, IPC_RMID, NULL) == -1)
        {
            perror("Ошибка при удалении разделяемой памяти");
            exit(1);
        }
        if (semctl(semid, 0, IPC_RMID) == -1)
        {
            perror("Ошибка при удалении семафоров");
            exit(1);
        }
        exit(0);
    }
}

void child_process(int i, double a, double b, int all_op, FILE *outfile)
{
    double area;
    if (i > all_op)
    {
        return;
    }
    double step = (b - a) / (double)all_op;
    area = integrate(a + (step * (double)(i - 1)), a + (step * (double)i), all_op);
    shared_area[0] += area;
    fprintf(outfile, "Счетовод [%d] считал %.2f - %.2f и получил: ", i, a + (step * (double)(i - 1)), a + (step * (double)i));
    fprintf(outfile, "%lf кв.м\n", area);
    return;
}

union semun
{
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

int main(int argc, char *argv[])
{
    int num_processes;    // количество процессов
    union semun sem_args; // структура для задания параметров семафоров
    struct sembuf sem_op; // структура для выполнения операций над семафорами
    FILE *infile, *outfile;
    double a, b;

    if (argc != 4)
    {
        printf("Использование: %s <входной файл> <выходной> <кол-во процессов>\n", argv[0]);
        exit(1);
    }
    if ((infile = fopen(argv[1], "r")) == NULL)
    {
        perror("Ошибка при открытии входного файла!\n");
        exit(1);
    }
    if ((outfile = fopen(argv[2], "w")) == NULL)
    {
        perror("Ошибка при открытии выходного файла!\n");
        exit(1);
    }
    num_processes = atoi(argv[3]);
    printf("Агроном приказал %d счетоводам разделится и наконец посчитать площадь!\n", num_processes);
    printf("Cчитываем входные данные...\n");
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
    printf("Получили данные a = %lf, b= %lf.\n", a, b);
    // Создаем семафоры
    if ((semid = semget(SEM_KEY, 1, IPC_CREAT | 0666)) == -1)
    {
        perror("Ошибка при создании семафоров");
        exit(1);
    }
    sem_args.val = 1;
    if (semctl(semid, 0, SETVAL, sem_args) == -1)
    {
        perror("Ошибка при инициализации семафоров");
        exit(1);
    }

    // Создаем разделяемую память
    if ((shmid = shmget(SHM_KEY, sizeof(double), IPC_CREAT | 0666)) == -1)
    {
        perror("Ошибка при создании разделяемой памяти");
        exit(1);
    }

    // Получаем указатель на разделяемую память
    if ((shared_area = shmat(shmid, NULL, 0)) == (double *)-1)
    {
        perror("Ошибка при получении указателя на разделяемую память");
        exit(1);
    }

    // Инициализируем разделяемую память
    *shared_area = 0.0;

    // Добавляем хэндлеры сигналам.
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Создаем процессы
    for (int i = 1; i <= num_processes; ++i)
    {
        pid_t pid = fork();
        if (pid == -1)
        {
            perror("Ошибка при создании процесса");
            exit(1);
        }
        else if (pid == 0)
        {
            // Подключаемся к разделяемой памяти
            if ((shared_area = shmat(shmid, NULL, 0)) == (double *)-1)
            {
                perror("Ошибка при получении указателя на разделяемую память");
                exit(1);
            }

            // Подключаемся к семафорам
            if ((semid = semget(SEM_KEY, 1, 066)) == -1)
            {
                perror("Ошибка при получении идентификатора семафоров");
                exit(1);
            }
            // Ожидаем разрешения от семафора
            struct sembuf sops = {0, -1, 0}; // уменьшаем значение семафора на 1
            if (semop(semid, &sops, 1) == -1)
            {
                perror("Ошибка при ожидании семафора");
                exit(1);
            }
            // Добавляем значение в разделяемую память
            child_process(i, a, b, num_processes, outfile);
            // Освобождаем семафор
            sops.sem_op = 1;
            if (semop(semid, &sops, 1) == -1)
            {
                perror("Ошибка при освобождении семафора");
                exit(1);
            }
            // Отключаемся от разделяемой памяти
            if (shmdt(shared_area) == -1)
            {
                perror("Ошибка при отключении от разделяемой памяти");
                exit(1);
            }
            exit(0); // завершаем работу дочернего процесса
        }
    }

    // Ждем завершения всех дочерних процессов
    for (int i = 0; i < num_processes; i++)
    {
        wait(NULL);
    }

    // Подключаемся к разделяемой памяти
    if ((shared_area = shmat(shmid, NULL, 0)) == (double *)-1)
    {
        perror("Ошибка при получении указателя на разделяемую память");
        exit(1);
    }

    // Выводим результат
    printf("Завершаем..\n");
    fprintf(outfile, "Агроном и счетоводы получили общую площадь: %.6f кв.м\n", *shared_area);
    printf("Агроном и счетоводы получили общую площадь: %.6f кв.м\nПодробнее в файле вывода %s\n", *shared_area, argv[2]);

    // Отключаемся от разделяемой памяти
    if (shmdt(shared_area) == -1)
    {
        perror("Ошибка при отключении от разделяемой памяти");
        exit(1);
    }
    // Удаляем разделяемую память и семафоры
    if (shmctl(shmid, IPC_RMID, NULL) == -1)
    {
        perror("Ошибка при удалении разделяемой памяти");
        exit(1);
    }
    if (semctl(semid, 0, IPC_RMID) == -1)
    {
        perror("Ошибка при удалении семафоров");
        exit(1);
    }
    return 0;
}
