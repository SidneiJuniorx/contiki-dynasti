/*------------ STRUCTS --------------*/

struct instances_pool;

/*----------- FUNCTIONS -------------*/

void start_struct(clock_time_t period);

void keep_instances(int id, int metric);

void update_last_message(char *msg, int time);

int get_instance_status(int id);
