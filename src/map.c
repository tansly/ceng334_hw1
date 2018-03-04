#include "globals.h"
#include "map.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#define IDX_OBSTACLE -1
#define IDX_EMPTY -2

enum die_reason {
    ERR_INPUT,
};

struct map_object {
    char (*represent)(void);
    void (*fayrap)(struct map_object *);
    void (*handle_move)(struct map_object *);
    int x;
    int y;
};

struct hunter {
    struct map_object base;
    int fd;
    pid_t pid;
    int energy;
};

struct prey {
    struct map_object base;
    int fd;
    pid_t pid;
    int energy;
};

struct obstacle {
    struct map_object base;
};

struct empty {
    struct map_object base;
};

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
} map;

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
    struct hunter *me = (struct hunter *)this;
    printf("hunter_fayrap(): %d %d\n", me->base.x, me->base.y);
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
    struct prey *me = (struct prey *)this;
    printf("prey_fayrap(): %d %d\n", me->base.x, me->base.y);
}

static void prey_handle_move(struct map_object *this)
{
}

void die(enum die_reason reason)
{
    switch (reason) {
        case ERR_INPUT:
            fprintf(stderr, "Input error\n");
            break;
        default:
            fprintf(stderr, "Unknown error %d\n", reason);
            break;
    }
    exit(EXIT_FAILURE);
}

void init_map(void)
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

    map.the_obstacle.base.represent = obstacle_represent;
    map.the_empty.base.represent = empty_represent;

    /* Empty spots */
    for (i = 0; i < map.width; i++) {
        for (j = 0; j < map.height; j++) {
            map.grid[grid_idx_(i, j)] = IDX_EMPTY;
        }
    }

    /* Obstacles */
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

    /* Hunters */
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
        map.hunters[i].energy = energy;
    }

    /* Preys */
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
        map.preys[i].energy = energy;
    }

    /* Set objects array */
    map.objects = malloc((map.n_hunters + map.n_preys) * sizeof *map.objects);
    for (i = 0; i < map.n_hunters; i++) {
        map.objects[i] = (struct map_object *)&map.hunters[i];
        map.grid[grid_idx_(map.objects[i]->x, map.objects[i]->y)] = i;
    }
    for (; i < map.n_hunters + map.n_preys; i++) {
        map.objects[i] = (struct map_object *)&map.preys[i - map.n_hunters];
        map.grid[grid_idx_(map.objects[i]->x, map.objects[i]->y)] = i;
    }

    /* Fayrap */
    for (i = 0; i < map.n_hunters + map.n_preys; i++) {
        map.objects[i]->fayrap(map.objects[i]);
    }
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
