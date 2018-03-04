#include "globals.h"
#include "map.h"

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define IDX_OBSTACLE -1
#define IDX_EMPTY -2

enum die_reason {
    ERR_INPUT,
    ERR_FORK,
    ERR_SOCKET,
    ERR_EXEC
};

struct map_object {
    char (*represent)(void);
    void (*fayrap)(struct map_object *);
    void (*handle_move)(struct map_object *);
    int x;
    int y;
    int fd;
    pid_t pid;
    int energy;
};

struct hunter {
    struct map_object base;
};

struct prey {
    struct map_object base;
};

struct obstacle {
    struct map_object base;
};

struct empty {
    struct map_object base;
};

static char obstacle_represent(void);
static char empty_represent(void);

static struct {
    int *grid;
    int width;
    int height;
    int n_hunters;
    int n_preys;
    struct obstacle the_obstacle;
    struct empty the_empty;
    struct hunter *hunters;
    struct prey *preys;
    struct map_object **objects;
    struct pollfd *fds;
} map = {
    .the_obstacle = { .base.represent = obstacle_represent },
    .the_empty = { .base.represent = empty_represent }
};

static void die(enum die_reason reason)
{
    switch (reason) {
        case ERR_INPUT:
            fprintf(stderr, "Input error\n");
            break;
        case ERR_FORK:
            fprintf(stderr, "fork() error\n");
            break;
        case ERR_SOCKET:
            fprintf(stderr, "Socket error\n");
            break;
        case ERR_EXEC:
            fprintf(stderr, "exec() et al. error\n");
            break;
        default:
            fprintf(stderr, "Unknown error %d\n", reason);
            break;
    }
    exit(EXIT_FAILURE);
}

static inline int grid_idx_(int x, int y)
{
    return x*map.width + y;
}

static char obstacle_represent(void)
{
    return 'X';
}

static char empty_represent(void)
{
    return ' ';
}

static char hunter_represent(void)
{
    return 'H';
}

static void hunter_fayrap(struct map_object *this)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNIX, sv) == -1) {
        perror("hunter_fayrap()");
        die(ERR_SOCKET);
    }
    fcntl(sv[0], F_SETFD, FD_CLOEXEC);
    pid_t pid;
    pid = fork();
    if (pid == -1) {
        perror("hunter_fayrap()");
        die(ERR_FORK);
    } else if (pid > 0) {
        close(sv[1]);
        this->fd = sv[0];
        this->pid = pid;
    } else {
        /* Child */
        // close(sv[0]); FD_CLOEXEC
        dup2(sv[1], STDIN_FILENO);
        dup2(sv[1], STDOUT_FILENO);
        close(sv[1]);

        /* args */
        int size;
        char *arg1, *arg2;

        size = snprintf(NULL, 0, "%d", map.width);
        size++;
        arg1 = malloc(size);
        snprintf(arg1, size, "%d", map.width);

        size = snprintf(NULL, 0, "%d", map.height);
        size++;
        arg2 = malloc(size);
        snprintf(arg2, size, "%d", map.height);

        /* exec() it */
        execl("./hunter", arg1, arg2, NULL);
        /* Error */
        perror("hunter_fayrap()");
        die(ERR_EXEC);
    }
}

static void hunter_handle_move(struct map_object *this)
{
}

static char prey_represent(void)
{
    return 'P';
}

static void prey_fayrap(struct map_object *this)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNIX, sv) == -1) {
        perror("prey_fayrap()");
        die(ERR_SOCKET);
    }
    fcntl(sv[0], F_SETFD, FD_CLOEXEC);
    pid_t pid;
    pid = fork();
    if (pid == -1) {
        perror("prey_fayrap()");
        die(ERR_FORK);
    } else if (pid > 0) {
        close(sv[1]);
        this->fd = sv[0];
        this->pid = pid;
    } else {
        /* Child */
        // close(sv[0]); FD_CLOEXEC
        dup2(sv[1], STDIN_FILENO);
        dup2(sv[1], STDOUT_FILENO);
        close(sv[1]);

        /* args */
        int size;
        char *arg1, *arg2;

        size = snprintf(NULL, 0, "%d", map.width);
        size++;
        arg1 = malloc(size);
        snprintf(arg1, size, "%d", map.width);

        size = snprintf(NULL, 0, "%d", map.height);
        size++;
        arg2 = malloc(size);
        snprintf(arg2, size, "%d", map.height);

        /* exec() it */
        execl("./prey", arg1, arg2, NULL);
        /* Error */
        perror("prey_fayrap()");
        die(ERR_EXEC);
    }
}

static void prey_handle_move(struct map_object *this)
{
}

static void init_grid(void)
{
    int width, height;
    int i, j;
    /* Get map dimensions and create grid */
    if (scanf("%d %d", &width, &height) != 2) {
        die(ERR_INPUT);
    }
    map.width = width;
    map.height = height;
    map.grid = malloc(map.width * map.height * sizeof *map.grid);

    /* Empty spots */
    for (i = 0; i < map.width; i++) {
        for (j = 0; j < map.height; j++) {
            map.grid[grid_idx_(i, j)] = IDX_EMPTY;
        }
    }
}

static void init_obstacles(void)
{
    int i;
    int n_obstacles;
    if (scanf("%d", &n_obstacles) != 1) {
        die(ERR_INPUT);
    }
    for (i = 0; i < n_obstacles; i++) {
        int x, y;
        if (scanf("%d %d", &x, &y) != 2) {
            die(ERR_INPUT);
        }
        map.grid[grid_idx_(x, y)] = IDX_OBSTACLE;
    }
}

static void init_hunters(void)
{
    int i;
    if (scanf("%d", &map.n_hunters) != 1) {
        die(ERR_INPUT);
    }
    map.hunters = malloc(map.n_hunters * sizeof *map.hunters);
    for (i = 0; i < map.n_hunters; i++) {
        int x, y, energy;
        if (scanf("%d %d %d", &x, &y, &energy) != 3) {
            die(ERR_INPUT);
        }
        map.hunters[i].base.represent = hunter_represent;
        map.hunters[i].base.fayrap = hunter_fayrap;
        map.hunters[i].base.handle_move = hunter_handle_move;
        map.hunters[i].base.x = x;
        map.hunters[i].base.y = y;
        map.hunters[i].base.energy = energy;
    }
}

static void init_preys(void)
{
    int i;
    if (scanf("%d", &map.n_preys) != 1) {
        die(ERR_INPUT);
    }
    map.preys = malloc(map.n_preys * sizeof *map.preys);
    for (i = 0; i < map.n_preys; i++) {
        int x, y, energy;
        if (scanf("%d %d %d", &x, &y, &energy) != 3) {
            die(ERR_INPUT);
        }
        map.preys[i].base.represent = prey_represent;
        map.preys[i].base.fayrap = prey_fayrap;
        map.preys[i].base.handle_move = prey_handle_move;
        map.preys[i].base.x = x;
        map.preys[i].base.y = y;
        map.preys[i].base.energy = energy;
    }
}

static void init_objects(void)
{
    int i;
    map.objects = malloc((map.n_hunters + map.n_preys) * sizeof *map.objects);
    for (i = 0; i < map.n_hunters; i++) {
        map.objects[i] = (struct map_object *)&map.hunters[i];
        map.grid[grid_idx_(map.objects[i]->x, map.objects[i]->y)] = i;
    }
    for (; i < map.n_hunters + map.n_preys; i++) {
        map.objects[i] = (struct map_object *)&map.preys[i - map.n_hunters];
        map.grid[grid_idx_(map.objects[i]->x, map.objects[i]->y)] = i;
    }
}

static void fayrapla(void)
{
    int i;
    map.fds = malloc((map.n_hunters + map.n_preys) * sizeof *map.fds);
    for (i = 0; i < map.n_hunters + map.n_preys; i++) {
        struct map_object *object = map.objects[i];
        object->fayrap(object);
        map.fds[i].fd = object->fd;
        map.fds[i].events = POLLIN;
    }
}


void init_map(void)
{
    init_grid();
    init_obstacles();
    init_hunters();
    init_preys();
    init_objects();
    fayrapla();
}

static struct map_object *grid_get(int x, int y)
{
    int idx = map.grid[grid_idx_(x, y)];
    if (idx == IDX_OBSTACLE) {
        /* Obstacle */
        return (struct map_object *)&map.the_obstacle;
    } else if (idx == IDX_EMPTY) {
        /* Empty */
        return (struct map_object *)&map.the_empty;
    } else {
        return map.objects[idx];
    }
}

void clean_map(void)
{
    free(map.grid);
    free(map.hunters);
    free(map.preys);
    free(map.objects);
    free(map.fds);
}

static void print_map(void)
{
    int i, j;
    putchar('+');
    for (i = 0; i < map.width; i++) {
        putchar('-');
    }
    putchar('+');
    putchar('\n');

    for (i = 0; i < map.width; i++) {
        putchar('|');
        for (j = 0; j < map.height; j++) {
            printf("%c", grid_get(i, j)->represent());
        }
        putchar('|');
        putchar('\n');
    }

    putchar('+');
    for (i = 0; i < map.width; i++) {
        putchar('-');
    }
    putchar('+');
    putchar('\n');
}

void run_simulation(void)
{
    print_map();
}
