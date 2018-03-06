#include "globals.h"

#include <stdio.h>
#include <unistd.h>

int width;
int height;

struct coordinate location;

int coordinate_valid(struct coordinate coord)
{
    return coord.x > 0 && coord.x < height && coord.y > 0 && coord.y < width;
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

    for (;;) {
        struct server_message message;
        read(STDIN_FILENO, &message, sizeof message);
        location = message.pos;

        struct coordinate request;
        enum { UP, RIGHT, DOWN, LEFT, DONE } state = LEFT;
        while (state != DONE) {
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
                    state = DONE;
                    break;
            }

            int move_possible = coordinate_valid(request);
            if (move_possible) {
                int i;
                for (i = 0; i < message.object_count; i++) {
                    if (request.x == message.object_pos[i].x &&
                            request.y == message.object_pos[i].y) {
                        move_possible = 0;
                    }
                }
            }

            if (move_possible) {
                break;
            }
        }

        struct ph_message req_msg;
        req_msg.move_request = request;
        write(STDOUT_FILENO, &req_msg, sizeof req_msg);
    }
}
