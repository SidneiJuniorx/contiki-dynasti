/*------------ STRUCTS --------------*/

struct instances_pool;

/*----------- FUNCTIONS -------------*/

void start_pool();

void keep_sink(int id, int metric);

void keep_client(int id, int metric);

void update_last_message(char *msg, int time);

void save_instance(int id);

int get_instance_status(int id);

void change_instance_status(int valor, int id);

void change_server_dao(int id, int valor);

void update_status_dao(int id);

int get_dao_relogio(int id);
