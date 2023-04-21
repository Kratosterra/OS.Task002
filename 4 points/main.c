#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>

#define SHM_NAME "/shm_are_cool"
#define SEM_NAME "/sem_are_cool"
#define BUF_SIZE 100

int num_processes;
double *shared_area;
sem_t *sem_area;
double esp;

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
        sem_unlink(SEM_NAME);
        sem_close(sem_area);
        shm_unlink(SHM_NAME);
        printf("\nАааааа, выпустите меня отсюда!!!!\n");
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
    sem_wait(sem_area);
    shared_area[0] += area;
    fprintf(outfile, "Счетовод [%d] считал %.2f - %.2f и получил: ", i, a + (step * (double)(i - 1)), a + (step * (double)i));
    fprintf(outfile, "%lf кв.м\n", area);
    sem_post(sem_area);
    return;
}

int main(int argc, char *argv[])
{
    double a, b, eps;
    FILE *infile, *outfile;
    int fd_shm;
    pid_t pid;

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
    if (num_processes < 1)
    {
        printf("Неправильное кол-во процессов: %s\n", argv[3]);
        exit(1);
    }

    printf("Создаём общую память...\n");
    if ((fd_shm = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666)) == -1)
    {
        perror("Ошибка при создании shared memory");
        exit(1);
    }
    printf("Изменяем размер общей памяти...\n");
    if (ftruncate(fd_shm, sizeof(int) + sizeof(double) * num_processes) == -1)
    {
        perror("Ошибка при изменении размера shared memory");
        exit(1);
    }
    if ((shared_area = mmap(NULL, sizeof(double), PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0)) == MAP_FAILED)
    {
        perror("Ошибка при разметке shared memory");
        exit(1);
    }
    for (int i = 0; i < num_processes; i++)
    {
        shared_area[i] = 0.0;
    }
    printf("Cоздаём семафор...\n");
    if ((sem_area = sem_open(SEM_NAME, O_RDONLY | O_CREAT | O_EXCL, 0666, 1)) == SEM_FAILED)
    {
        perror("Ошибка при создании семафора!");
        exit(1);
    }
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
    printf("Настраиваем хэндлер сигналов завершения...\n");
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    printf("Создаём процессы...\n");
    for (int i = 1; i <= num_processes; ++i)
    {
        pid = fork();
        if (pid == -1)
        {
            perror("Ошибка при создании процесса!");
            exit(1);
        }
        if (pid == 0)
        {
            child_process(i, a, b, num_processes, outfile);
            exit(0);
        }
    }
    while (wait(NULL) != -1)
        ;
    printf("Завершаем..\n");
    fprintf(outfile, "Агроном и счетоводы получили общую площадь: %.6f кв.м\n", shared_area[0]);
    printf("Агроном и счетоводы получили общую площадь: %.6f кв.м\nПодробнее в файле вывода %s\n", shared_area[0], argv[2]);
    sem_unlink(SEM_NAME);
    sem_close(sem_area);
    shm_unlink(SHM_NAME);
    return 0;
}