#include "globals.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int width;
int height;

struct coordinate location;

int coordinate_valid(struct coordinate coord)
{
    return coord.x >= 0 && coord.x < height && coord.y >= 0 && coord.y < width;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        return 1;
    }
    if (sscanf(argv[1], "%d", &width) != 1) {
        return 1;
    }
    if (sscanf(argv[1], "%d", &height) != 1) {
        return 1;
    }
    srand(time(NULL));

    for (;;) {
        struct server_message message;
        if (read(STDIN_FILENO, &message, sizeof message) != sizeof message) {
            return 2;
        }
        location = message.pos;

        struct coordinate request;
        enum { UP, RIGHT, DOWN, LEFT, CURR, DONE } state = UP;
        for (;;) {
            switch (state) {
                case UP:
                    request.x = location.x - 1;
                    request.y = location.y;
                    state = RIGHT;
                    break;
                case RIGHT:
                    request.x = location.x;
                    request.y = location.y + 1;
                    state = DOWN;
                    break;
                case DOWN:
                    request.x = location.x + 1;
                    request.y = location.y;
                    state = LEFT;
                    break;
                case LEFT:
                    request.x = location.x;
                    request.y = location.y - 1;
                    state = CURR;
                    break;
                case CURR:
                    request = location;
                    state = DONE;
                    break;
                case DONE:
                    assert(0);
                    break;
            }
            if (state == DONE) {
                break;
            }

            int move_possible = coordinate_valid(request) &&
                (abs(request.x - message.adv_pos.x) + abs(request.y - message.adv_pos.y) >
                 abs(location.x - message.adv_pos.x) + abs(location.y - message.adv_pos.y));
            if (move_possible) {
                int i;
                for (i = 0; i < message.object_count && move_possible; i++) {
                    move_possible = !(request.x == message.object_pos[i].x &&
                            request.y == message.object_pos[i].y);
                }
            }

            if (move_possible) {
                break;
            }
        }

        struct ph_message req_msg;
        req_msg.move_request = request;
        if (write(STDOUT_FILENO, &req_msg, sizeof req_msg) != sizeof req_msg) {
            return 3;
        }
        usleep(10000*(1 + rand()%9));
    }
}
