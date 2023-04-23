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
#define NUM_CLIENTS 5

typedef struct
{
    double sum;
    int count;
    int num_clients;
} shared_data_t;

shared_data_t *shared_data;
sem_t *semaphore;

void cleanup()
{
    // Удаляем семафор и разделяемую память
    sem_close(semaphore);
    sem_unlink(SEM_NAME);
    munmap(shared_data, sizeof(shared_data_t));
    shm_unlink(SHM_NAME);
}

void sigint_handler(int signum)
{
    cleanup();
    exit(0);
}

int main(int argc, char *argv[])
{
    FILE *infile, *outfile;
    if (argc != 4)
    {
        fprintf(stderr, "Использование: %s <файл ввода> <файл вывода> <кол-во независимых процессов>\n", argv[0]);
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
    // Обработчик сигнала Ctrl+C
    signal(SIGINT, sigint_handler);

    // Создаем семафор
    semaphore = sem_open(SEM_NAME, O_CREAT | O_EXCL, 0666, 1);
    if (semaphore == SEM_FAILED)
    {
        perror("Ошибка при создании семафора");
        exit(1);
    }

    // Создаем разделяемую память
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("Ошибка при создании разделяемой памяти");
        exit(1);
    }

    // Устанавливаем размер разделяемой памяти
    if (ftruncate(shm_fd, sizeof(shared_data_t)) == -1)
    {
        perror("Ошибка при изменении размера разделяемой памяти");
        exit(1);
    }

    // Отображаем разделяемую память в адресное пространство текущего процесса
    shared_data = mmap(NULL, sizeof(shared_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED)
    {
        perror("Ошибка при отображении разделяемой памяти");
        exit(1);
    }

    // Инициализируем разделяемую память
    shared_data->sum = 0;
    shared_data->count = 0;
    shared_data->num_clients = num_processes;

    // Ожидаем завершения всех счетоводов.
    while (true)
    {
        sem_wait(semaphore);
        if (shared_data->count == num_processes)
        {
            break;
        }
        sem_post(semaphore);
    }

    if ((outfile = fopen(argv[2], "w")) == NULL)
    {
        perror("Ошибка при открытии выходного файла!\n");
        exit(1);
    }
    printf("Завершаем..\n");
    fprintf(outfile, "Агроном и счетоводы получили общую площадь: %.6f кв.м\n", shared_data->sum);
    printf("Агроном и счетоводы получили общую площадь: %.6f кв.м\nПодробнее в файле вывода %s\n", shared_data->sum, argv[2]);
    fclose(outfile);
    fclose(infile);
    cleanup();
    return 0;
}