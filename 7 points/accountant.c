#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>

#define SHM_NAME "/shared_memory"
#define SEM_NAME "/shared_semaphore"

typedef struct
{
    double sum;
    int num_clients;
    int client_id;
} shared_data_t;

shared_data_t *shared_area;

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

void child_process(int i, double a, double b, int all_op, FILE *outfile)
{
    double area;
    if (i > all_op)
    {
        return;
    }
    double step = (b - a) / (double)all_op;
    area = integrate(a + (step * (double)(i - 1)), a + (step * (double)i), all_op);
    shared_area->sum += area;
    fprintf(outfile, "Счетовод [%d] считал %.2f - %.2f и получил: ", i, a + (step * (double)(i - 1)), a + (step * (double)i));
    fprintf(outfile, "%lf кв.м\n", area);
    return;
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
    if ((outfile = fopen(argv[2], "w")) == NULL)
    {
        perror("Ошибка при открытии выходного файла!\n");
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

    // Открываем разделяемую память
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0660);
    if (shm_fd == -1)
    {
        perror("Ошибка при открытии разделяемой памяти");
        exit(1);
    }

    // Отображаем разделяемую память в адресное пространство процесса
    shared_area = mmap(NULL, sizeof(shared_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_area == MAP_FAILED)
    {
        perror("Ошибка при отображении разделяемой памяти");
        exit(1);
    }

    // Открываем семафор
    sem_t *sem = sem_open(SEM_NAME, O_RDWR);
    if (sem == SEM_FAILED)
    {
        perror("Ошибка при открытии семафора");
        exit(1);
    }

    // Получаем текущее значение семафора
    int sem_value;
    sem_getvalue(sem, &sem_value);
    int client_id = shared_area->num_clients + 1;

    printf("Счетовод %d запущен. Текущее значение семафора: %d\n", client_id, sem_value);

    // Ожидаем разрешения на доступ к разделяемой памяти
    sem_wait(sem);

    // Доступ к разделяемой памяти
    child_process(client_id, a, b, shared_area->client_id, outfile);
    printf("Счетовод %d: общая сумма = %f\n", client_id, shared_area->sum);
    shared_area->num_clients += 1;

    // Освобождаем разделяемую память
    sem_post(sem);

    // Закрываем разделяемую память
    munmap(shared_area, sizeof(shared_data_t));
    close(shm_fd);

    // Закрываем семафор
    sem_close(sem);
    fclose(outfile);
    fclose(infile);
    printf("Счетовод %d завершен\n", client_id);
    return 0;
}