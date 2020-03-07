// ANDRE RIKER - ARIKER AT DEI.UC.PT
// UNIVERSIDADE DE COIMBRA


// ------------ STRUCTS --------------
struct battery;

struct stats;

struct energy_states;

// ----------- FUNCTIONS -------------
void update_battery();

void update_time_stats();

void battery_start(clock_time_t perioc, unsigned, double, unsigned, int, double);

void kinetic_model();

long double get_batt1();

long double get_batt2();

long double get_max_charge();

//double get_re();

//double get_elt();

//double get_eharm();

//int get_drain_rate();

//double get_solar_charging();
