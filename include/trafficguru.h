#ifndef TRAFFICGURU_H
#define TRAFFICGURU_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <stdbool.h>

// Include all system headers
#include "lane_process.h"
#include "queue.h"
#include "scheduler.h"
#include "synchronization.h"
#include "bankers_algorithm.h"
#include "performance_metrics.h"
#include "emergency_system.h"
#include "visualization.h"
#include "traffic_mutex.h"

// System constants
#define NUM_LANES 4
#define MAX_QUEUE_CAPACITY 20
#define DEFAULT_TIME_QUANTUM 3
#define CONTEXT_SWITCH_TIME 500  // milliseconds
#define VEHICLE_CROSS_TIME 3     // seconds (increased from 2 - each vehicle takes longer)
#define BATCH_EXIT_SIZE 3  // number of vehicles that can exit simultaneously
#define EMERGENCY_PROBABILITY 100 // 1 in 100 chance per check
#define SIMULATION_UPDATE_INTERVAL 300000 // microseconds (300ms for faster display updates)

// Lane definitions
#define LANE_NORTH 0
#define LANE_SOUTH 1
#define LANE_EAST 2
#define LANE_WEST 3

// Traffic simulation parameters
#define VEHICLE_ARRIVAL_RATE_MIN 3  // seconds (slowed down for better visibility)
#define VEHICLE_ARRIVAL_RATE_MAX 8  // seconds (slowed down for better visibility)

// --- DELETED ---
// The VisualizationSnapshot struct is removed to prevent deadlocks.
// The UI will use pthread_mutex_trylock() instead.
// --- END DELETED ---


// Global system state
typedef struct {
    LaneProcess lanes[NUM_LANES];           // All traffic lanes
    Scheduler scheduler;                    // Central scheduler
    IntersectionMutex intersection;         // Intersection synchronization
    BankersState bankers_state;             // Deadlock prevention
    PerformanceMetrics metrics;             // Performance tracking
    EmergencySystem emergency_system;       // Emergency vehicle handling
    Visualization visualization;            // Terminal interface
    SignalHistory signal_history;           // Signal change history
    bool simulation_running;                // Main simulation flag
    bool simulation_paused;                 // Pause/resume state
    time_t simulation_start_time;           // When simulation started
    time_t simulation_end_time;             // When simulation ends
    int total_vehicles_generated;           // Total vehicles in simulation
    pthread_t simulation_thread;            // Main simulation thread
    pthread_mutex_t global_state_lock;      // Global state protection
    
    // --- NEW FIELDS TO FIX "VALUES ARE 0" BUG ---
    int min_arrival_rate;                   // Min vehicle arrival rate (sec)
    int max_arrival_rate;                   // Max vehicle arrival rate (sec)
    pthread_t vehicle_generator_thread;     // Thread for generating vehicles
    // --- END NEW FIELDS ---
    
    // --- DELETED ---
    // Removed snapshot fields
    // --- END DELETED ---

} TrafficGuruSystem;

// Global system instance
extern TrafficGuruSystem* g_traffic_system;

// --- MODIFICATION ---
// Global flag to stop the main loop from any file
// 'extern' tells other files this variable exists
// 'volatile' ensures it's safe to use across threads/signals
extern volatile bool keep_running;
// --- END MODIFICATION ---

// System lifecycle functions
int init_traffic_guru_system();
void destroy_traffic_guru_system();
int start_traffic_simulation();
void stop_traffic_simulation();
void pause_traffic_simulation();
void resume_traffic_simulation();

// Main simulation loop
void* simulation_main_loop(void* arg);
void update_simulation_state();
void process_traffic_events();

// --- DELETED ---
// Removed update_visualization_snapshot prototype
// --- END DELETED ---

// Signal handlers
void handle_signal_interrupt(int sig);
void handle_signal_terminate(int sig);
void setup_signal_handlers();

// Utility functions
void print_system_info();
void print_usage(const char* program_name);
void cleanup_and_exit(int exit_code);
bool validate_system_state();

// Configuration functions
void set_simulation_duration(int seconds);
void set_vehicle_arrival_rate(int min_seconds, int max_seconds);
void set_time_quantum(int seconds);
void set_debug_mode(bool enabled);

// Logging functions
void log_system_event(const char* event);
void log_error(const char* error);
void log_debug(const char* message);
void log_performance_summary();

// Command line argument parsing
typedef struct {
    int duration;
    int min_arrival_rate;
    int max_arrival_rate;
    int time_quantum;
    SchedulingAlgorithm algorithm;
    bool debug_mode;
    bool no_color;
    bool help_requested;
} CommandLineArgs;

CommandLineArgs parse_command_line_args(int argc, char* argv[]);
void print_command_line_help();
void validate_command_line_args(CommandLineArgs* args);

#endif // TRAFFICGURU_H
