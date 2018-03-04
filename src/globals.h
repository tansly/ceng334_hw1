typedef struct coordinate {
    int x;
    int y;
} coordinate;

typedef struct server_message {
    coordinate pos;
    coordinate adv_pos;
    int object_count;
    coordinate object_pos[4];
} server_message;

typedef struct ph_message {
    coordinate move_request;
} ph_message;
