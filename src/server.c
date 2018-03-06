#include "globals.h"

#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define IDX_OBSTACLE -1
#define IDX_EMPTY -2

/* TODO: Add more error checking, e.g. to close() calls.
 */

enum die_reason {
    ERR_INPUT,
    ERR_FORK,
    ERR_SOCKET,
    ERR_EXEC,
    ERR_KILL,
    ERR_WAIT,
    ERR_READ,
    ERR_WRITE,
};

struct map_object {
    char (*represent)(void);
    void (*fayrap)(struct map_object *);
    int (*handle_move)(struct map_object *, int, int);
    int x;
    int y;
    int idx;
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

static inline int grid_idx_(int x, int y)
{
    return x*map.width + y;
}

static void grid_set(int x, int y, int idx)
{
    map.grid[grid_idx_(x, y)] = idx;
}

static int grid_get_idx(int x, int y)
{
    return map.grid[grid_idx_(x, y)];
}

static struct map_object *grid_get_object(int x, int y)
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
        case ERR_KILL:
            fprintf(stderr, "kill() error\n");
            break;
        case ERR_WAIT:
            fprintf(stderr, "wait() error\n");
            break;
        case ERR_READ:
            fprintf(stderr, "read() error\n");
            break;
        case ERR_WRITE:
            fprintf(stderr, "write() error\n");
            break;
        default:
            fprintf(stderr, "Unknown error %d\n", reason);
            break;
    }
    exit(EXIT_FAILURE);
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
        execl("./hunter", "./hunter", arg1, arg2, NULL);
        /* Error */
        perror("hunter_fayrap()");
        die(ERR_EXEC);
    }
}

static int move_possible(struct map_object *this, struct map_object *target)
{
    return target->represent() == map.the_empty.base.represent() ||
            this->represent() != target->represent();
}

static void send_new_state(struct map_object *this)
{
    struct server_message state;
    memset(&state, 0xff, sizeof state);
    state.pos.x = this->x;
    state.pos.y = this->y;

    /* Closest adversary */
    int i, min_dist, min_idx;
    for (i = 0; map.objects[i]->idx == -1 ||
            map.objects[i]->represent() == this->represent(); i++) {
        /* Skip dead objects, there will always be at least one alive here */
        assert(i < map.n_hunters + map.n_preys);
    }
    struct map_object *adv = map.objects[i];
    min_dist = abs(adv->x - this->x) + abs(adv->y - this->y);
    min_idx = i;
    for (; i < map.n_hunters + map.n_preys; i++) {
        adv = map.objects[i];
        if (adv->idx == -1 || adv->represent() == this->represent()) {
            /* Dead or not an adversary */
            continue;
        }
        int dist;
        dist = abs(adv->x - this->x) + abs(adv->y - this->y);
        if (dist < min_dist) {
            min_dist = dist;
            min_idx = i;
        }
    }
    adv = map.objects[min_idx];
    state.adv_pos.x = adv->x;
    state.adv_pos.y = adv->y;

    /* Neighbouring objects */
    state.object_count = 0;
    if (this->x - 1 > 0) {
        struct map_object *neighbour = grid_get_object(this->x - 1, this->y);
        if (!move_possible(this, neighbour)) {
            struct coordinate coord = { this->x - 1, this->y };
            state.object_pos[state.object_count++]  = coord;
        }
    }
    if (this->y + 1 < map.width) {
        struct map_object *neighbour = grid_get_object(this->x, this->y + 1);
        if (!move_possible(this, neighbour)) {
            struct coordinate coord = { this->x, this->y + 1 };
            state.object_pos[state.object_count++]  = coord;
        }
    }
    if (this->x + 1 < map.height) {
        struct map_object *neighbour = grid_get_object(this->x + 1, this->y);
        if (!move_possible(this, neighbour)) {
            struct coordinate coord = { this->x + 1, this->y };
            state.object_pos[state.object_count++]  = coord;
        }
    }
    if (this->y - 1 > 0) {
        struct map_object *neighbour = grid_get_object(this->x, this->y - 1);
        if (!move_possible(this, neighbour)) {
            struct coordinate coord = { this->x, this->y - 1 };
            state.object_pos[state.object_count++]  = coord;
        }
    }
    if (write(this->fd, &state, sizeof state) != sizeof state) {
        die(ERR_WRITE);
    }
}

static int hunter_handle_move(struct map_object *this, int x, int y)
{
    struct map_object *target = grid_get_object(x, y);
    int moved;
    if (move_possible(this, target)) {
        grid_set(x, y, this->idx);
        if (grid_get_idx(this->x, this->y) == this->idx) {
            grid_set(this->x, this->y, IDX_EMPTY);
        } else {
            assert(grid_get_object(this->x, this->y)->represent() == 'P');
            /* someone (a prey) stomped over me, leave them */
        }
        this->x = x;
        this->y = y;
        this->energy--;

        moved = 1;
    } else {
        /* Move impossible */
        moved = 0;
    }
    send_new_state(this);
    return moved;
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
        execl("./prey", "./prey", arg1, arg2, NULL);
        /* Error */
        perror("prey_fayrap()");
        die(ERR_EXEC);
    }
}

static int prey_handle_move(struct map_object *this, int x, int y)
{
    struct map_object *target = grid_get_object(x, y);
    int moved;
    if (move_possible(this, target)) {
        grid_set(x, y, this->idx);
        if (grid_get_idx(this->x, this->y) == this->idx) {
            grid_set(this->x, this->y, IDX_EMPTY);
        } else {
            assert(grid_get_object(this->x, this->y)->represent() == 'H');
            /* someone (a hunter) stomped over me, leave them */
        }
        this->x = x;
        this->y = y;

        moved = 1;
    } else {
        /* Move impossible */
        moved = 0;
    }
    send_new_state(this);
    return moved;
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
        map.objects[i]->idx = i;
        map.grid[grid_idx_(map.objects[i]->x, map.objects[i]->y)] = i;
    }
    for (; i < map.n_hunters + map.n_preys; i++) {
        map.objects[i] = (struct map_object *)&map.preys[i - map.n_hunters];
        map.objects[i]->idx = i;
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

static void send_initial_states(void)
{
    int i;
    for (i = 0; i < map.n_hunters + map.n_preys; i++) {
        struct map_object *object = map.objects[i];
        object->handle_move(object, object->x, object->y);
    }
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
            printf("%c", grid_get_object(i, j)->represent());
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

void init_map(void)
{
    init_grid();
    init_obstacles();
    init_hunters();
    init_preys();
    init_objects();
    fayrapla();
    send_initial_states();
    print_map();
}

void clean_map(void)
{
    int i;
    for (i = 0; i < map.n_hunters + map.n_preys; i++) {
        struct map_object *object = map.objects[i];
        if (object->pid > 0) {
            if (kill(object->pid, SIGTERM) == -1) {
                perror("run_simulation()");
                die(ERR_KILL);
            }
            if (waitpid(object->pid, NULL, 0) == -1) {
                perror("run_simulation()");
                die(ERR_WAIT);
            }
            close(map.fds[object->idx].fd);
        }
    }
    free(map.grid);
    free(map.hunters);
    free(map.preys);
    free(map.objects);
    free(map.fds);
}

void run_simulation(void)
{
    int hunters_alive = map.n_hunters, preys_alive = map.n_preys;
    int updated = 0;
    while (hunters_alive && preys_alive) {
        if (poll(map.fds, map.n_hunters + map.n_preys, -1) > 0) {
            int i;
            for (i = 0; i < map.n_hunters + map.n_preys; i++) {
                int revents = map.fds[i].revents;
                if (revents & POLLIN) {
                    /* Data ready from a child */
                    struct ph_message message;
                    struct map_object *object;
                    if (read(map.fds[i].fd, &message, sizeof message) != sizeof message) {
                        die(ERR_READ);
                    }
                    object = map.objects[i];
                    updated = object->handle_move(object, message.move_request.x,
                            message.move_request.y);
                }
            }
        }

        int i;
        preys_alive = 0;
        for (i = 0; i < map.n_preys; i++) {
            struct map_object *prey = (struct map_object *)&map.preys[i];
            if (prey->idx < 0) {
                /* Already dead */
                /* NOTHING */
            } else if (prey->idx != grid_get_idx(prey->x, prey->y)) {
                /* Hunter on prey */
                if (kill(prey->pid, SIGTERM) == -1) {
                    perror("run_simulation()");
                    die(ERR_KILL);
                }
                if (waitpid(prey->pid, NULL, 0) == -1) {
                    perror("run_simulation()");
                    die(ERR_WAIT);
                }
                close(map.fds[prey->idx].fd);
                map.fds[prey->idx].fd = -1;
                prey->fd = -1;
                prey->pid = -1;
                /* Add prey energy to hunter */
                grid_get_object(prey->x, prey->y)->energy += prey->energy;
                /* Invalidate remaining attributes */
                prey->x = -1;
                prey->y = -1;
                prey->idx = -1;
                prey->energy = 0;

                /* Map is updated */
                updated = 1;
            } else {
                /* Alive */
                preys_alive++;
            }
        }

        hunters_alive = 0;
        for (i = 0; i < map.n_hunters; i++) {
            struct map_object *hunter = (struct map_object *)&map.hunters[i];
            if (hunter->idx < 0) {
                /* Already dead */
                /* NOTHING */
            } else if (hunter->energy == 0) {
                /* Hunter died */
                if (kill(hunter->pid, SIGTERM) == -1) {
                    perror("run_simulation()");
                    die(ERR_KILL);
                }
                if (waitpid(hunter->pid, NULL, 0) == -1) {
                    perror("run_simulation()");
                    die(ERR_WAIT);
                }
                close(map.fds[hunter->idx].fd);
                map.fds[hunter->idx].fd = -1;
                hunter->fd = -1;
                hunter->pid = -1;
                /* Update the grid */
                grid_set(hunter->x, hunter->y, IDX_EMPTY);
                /* Invalidate remaining attributes */
                hunter->x = -1;
                hunter->y = -1;
                hunter->idx = -1;
                hunter->energy = 0;

                /* Map is updated */
                updated = 1;
            } else {
                /* Alive */
                hunters_alive++;
            }
        }

        if (updated) {
            print_map();
            updated = 0;
        }
    }
}

int main(void)
{
    init_map();
    run_simulation();
    clean_map();
    return 0;
}
