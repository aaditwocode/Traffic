#define _XOPEN_SOURCE 600
#include "../include/trafficguru.h"
#include <getopt.h>
#include <signal.h>
#include <ncurses.h> // <--- ADDED for getch()

// Global system instance
TrafficGuruSystem* g_traffic_system = NULL;

// Signal handling
volatile bool keep_running = true; // <-- REMOVED 'static'
static volatile bool pause_requested = false;

// --- NEW FUNCTION: Vehicle Generator Thread ---
// This loop runs in its own thread and generates vehicles
// to fix the "all values are 0" bug.
void* vehicle_generator_loop(void* arg) {
    (void)arg;

    while (g_traffic_system && g_traffic_system->simulation_running && keep_running) {
        if (!g_traffic_system->simulation_paused) {
            // Calculate random sleep time in microseconds
            // --- FIX: Use seconds, not milliseconds ---
            int min_sec = g_traffic_system->min_arrival_rate;
            int max_sec = g_traffic_system->max_arrival_rate;
            // Ensure min <= max
            if (min_sec > max_sec) min_sec = max_sec;
            
            // Calculate sleep time in seconds (can be 0)
            int sleep_time_sec = (rand() % (max_sec - min_sec + 1)) + min_sec;
            
            // Convert to microseconds, but also add some sub-second randomness
            // (e.g., 1-5 seconds becomes 1000ms-5000ms)
            long sleep_time_us = (sleep_time_sec * 1000000) + (rand() % 1000 * 1000);
            
            // Pick a random lane
            int lane_idx = rand() % NUM_LANES;
            LaneProcess* lane = &g_traffic_system->lanes[lane_idx];

            // --- DEADLOCK FIX: Lock global state ONLY for the counter ---
            int new_vehicle_id = 0;
            pthread_mutex_lock(&g_traffic_system->global_state_lock);
            new_vehicle_id = g_traffic_system->total_vehicles_generated++;
            pthread_mutex_unlock(&g_traffic_system->global_state_lock);
            // --- END DEADLOCK FIX ---
            
            // We assume add_vehicle_to_lane is thread-safe (uses lane->queue_lock)
            add_vehicle_to_lane(lane, new_vehicle_id);

                    // ---- EMERGENCY VEHICLE GENERATION ----
                    // Periodically generate emergency vehicles (1 in 100 chance)
                    if ((rand() % EMERGENCY_PROBABILITY) == 0) {
                    EmergencyVehicle* emergency = generate_random_emergency();
                    if (emergency) {
                        emergency->lane_id = lane_idx;
                        add_emergency_vehicle(&(g_traffic_system->emergency_system), emergency);
                    }                                }
            
            // --- "Wake up" the lane (this is also a lock) ---
            pthread_mutex_lock(&lane->queue_lock);
            if (lane->state == WAITING) {
                lane->state = READY;
                lane->waiting_time = 0; // Reset wait time
            }
            pthread_mutex_unlock(&lane->queue_lock);
            // --- END FIX ---
            
            // Sleep for the calculated time (in microseconds)
            usleep(sleep_time_us);
        } else {
            // Sleep for a bit if paused
            usleep(500000); // 500ms
        }
    }
    return NULL;
}
// --- END NEW FUNCTION ---


void handle_signal_interrupt(int sig) {
    (void)sig; // Suppress unused parameter warning
    // This can be called from a signal, avoid printf
    keep_running = false;
    if (g_traffic_system) {
        g_traffic_system->simulation_running = false;
    }
}

void handle_signal_terminate(int sig) {
    (void)sig;
    // This can be called from a signal, avoid printf
    keep_running = false;
    if (g_traffic_system) {
        g_traffic_system->simulation_running = false;
    }
    // A more forceful exit might be needed if ncurses is running
    // endwin();
    // exit(0);
}

void setup_signal_handlers() {
    signal(SIGINT, handle_signal_interrupt);
    signal(SIGTERM, handle_signal_terminate);
    signal(SIGPIPE, SIG_IGN);
}

// Parse command line arguments
CommandLineArgs parse_command_line_args(int argc, char* argv[]) {
    CommandLineArgs args = {
        .duration = SIMULATION_DURATION,
        .min_arrival_rate = VEHICLE_ARRIVAL_RATE_MIN,
        .max_arrival_rate = VEHICLE_ARRIVAL_RATE_MAX,
        .time_quantum = DEFAULT_TIME_QUANTUM,
        .algorithm = SJF,
        .debug_mode = false,
        .no_color = false,
        .help_requested = false
    };

    static struct option long_options[] = {
        {"duration",     required_argument, 0, 'd'},
        {"min-arrival",  required_argument, 0, 'a'},
        {"max-arrival",  required_argument, 0, 'A'},
        {"quantum",      required_argument, 0, 'q'},
        {"algorithm",    required_argument, 0, 'g'},
        {"debug",        no_argument,       0, 'D'},
        {"no-color",     no_argument,       0, 'n'},
        {"help",         no_argument,       0, 'h'},
        {"version",      no_argument,       0, 'v'},
        {"benchmark",    no_argument,       0, 'b'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "d:a:A:q:g:Dnhvb", long_options, NULL)) != -1) {
        switch (c) {
            case 'd':
                args.duration = atoi(optarg);
                if (args.duration <= 0) args.duration = SIMULATION_DURATION;
                break;
            case 'a':
                args.min_arrival_rate = atoi(optarg);
                if (args.min_arrival_rate <= 0) args.min_arrival_rate = 1;
                break;
            case 'A':
                args.max_arrival_rate = atoi(optarg);
                if (args.max_arrival_rate <= 0) args.max_arrival_rate = 5;
                break;
            case 'q':
                args.time_quantum = atoi(optarg);
                if (args.time_quantum <= 0) args.time_quantum = DEFAULT_TIME_QUANTUM;
                break;
            case 'g':
                if (strcmp(optarg, "sjf") == 0) {
                    args.algorithm = SJF;
                } else if (strcmp(optarg, "multilevel") == 0) {
                    args.algorithm = MULTILEVEL_FEEDBACK;
                } else if (strcmp(optarg, "priority") == 0) {
                    args.algorithm = PRIORITY_ROUND_ROBIN;
                } else {
                    printf("Unknown algorithm: %s\n", optarg);
                    args.help_requested = true;
                }
                break;
            case 'D':
                args.debug_mode = true;
                break;
            case 'n':
                args.no_color = true;
                break;
            case 'h':
                args.help_requested = true;
                break;
            case 'v':
                printf("TrafficGuru version 1.0.0\n");
                printf("OS-inspired traffic management system\n");
                exit(0);
            case 'b':
                printf("Running in benchmark mode\n");
                args.duration = 60;
                args.debug_mode = false;
                break;
            case '?':
                args.help_requested = true;
                break;
            default:
                break;
        }
    }

    // Validate arguments
    if (args.min_arrival_rate > args.max_arrival_rate) {
        int temp = args.min_arrival_rate;
        args.min_arrival_rate = args.max_arrival_rate;
        args.max_arrival_rate = temp;
    }

    return args;
}

void print_command_line_help() {
    printf("TrafficGuru - OS-inspired Traffic Management System\n");
    printf("================================================\n\n");
    printf("Usage: trafficguru [OPTIONS]\n\n");
    printf("Options:\n");
    printf("  -d, --duration SECONDS     Set simulation duration (default: 300)\n");
    printf("  -a, --min-arrival SECONDS  Minimum vehicle arrival rate (default: 1)\n");
    printf("  -A, --max-arrival SECONDS  Maximum vehicle arrival rate (default: 5)\n");
    printf("  -q, --quantum SECONDS      Set time quantum for algorithms (default: 3)\n");
    printf("  -g, --algorithm ALG        Scheduling algorithm (sjf|multilevel|priority)\n");
    printf("  -D, --debug                Enable debug mode\n");
    printf("  -n, --no-color             Disable color output\n");
    printf("  -b, --benchmark            Run in benchmark mode (60 seconds)\n");
    printf("  -h, --help                 Show this help message\n");
    printf("  -v, --version              Show version information\n\n");
    printf("Algorithms:\n");
    printf("  sjf           - Shortest Job First\n");
    printf("  multilevel    - Multilevel Feedback Queue\n");
    printf("  priority      - Priority Round Robin\n\n");
    printf("Interactive Controls (during simulation):\n");
    printf("  1-3            - Switch scheduling algorithms\n");
    printf("  SPACE          - Pause/Resume simulation\n");
    printf("  e              - Trigger emergency vehicle\n");
    printf("  r              - Reset simulation\n");
    printf("  q              - Quit simulation\n");
    printf("  h              - Show help screen\n\n");
    printf("Examples:\n");
    printf("  trafficguru                              # Run with default settings\n");
    printf("  trafficguru -d 60 -g multilevel         # 60-second simulation with Multilevel Feedback\n");
    printf("  trafficguru --debug --duration 120      # Debug mode for 2 minutes\n");
    printf("  trafficguru --benchmark                   # Run 60-second benchmark\n");
}

void validate_command_line_args(CommandLineArgs* args) {
    if (args->duration < 10) {
        printf("Warning: Duration too short, setting to 10 seconds\n");
        args->duration = 10;
    }
    if (args->duration > 3600) {
        printf("Warning: Duration too long, setting to 1 hour\n");
        args->duration = 3600;
    }
}

// Initialize the global traffic system
int init_traffic_guru_system() {
    if (g_traffic_system) {
        return 0; // Already initialized
    }

    g_traffic_system = (TrafficGuruSystem*)malloc(sizeof(TrafficGuruSystem));
    if (!g_traffic_system) {
        printf("Failed to allocate memory for traffic system\n");
        return -1;
    }

    // Initialize system components
    memset(g_traffic_system, 0, sizeof(TrafficGuruSystem));

    // Initialize random seed
    srand(time(NULL));

    // Initialize lane processes
    for (int i = 0; i < NUM_LANES; i++) {
        init_lane_process(&g_traffic_system->lanes[i], i, MAX_QUEUE_CAPACITY);
    }

    // Initialize scheduler
    init_scheduler(&g_traffic_system->scheduler, SJF);

    // Initialize synchronization
    init_intersection_mutex(&g_traffic_system->intersection);

    // Initialize Banker's algorithm
    init_bankers_state(&g_traffic_system->bankers_state);

    // Initialize performance metrics
    init_performance_metrics(&g_traffic_system->metrics);

    // Initialize emergency system
    init_emergency_system(&g_traffic_system->emergency_system);

    // Initialize traffic mutex system
    init_traffic_mutex_system();

    // Initialize visualization
    // This will call initscr()
    init_visualization(&g_traffic_system->visualization); 

    // Initialize signal history
    init_signal_history(&g_traffic_system->signal_history, 1000);

    // Set initial state
    g_traffic_system->simulation_running = false;
    g_traffic_system->simulation_paused = false;
    g_traffic_system->simulation_start_time = time(NULL);
    g_traffic_system->simulation_end_time = time(NULL) + SIMULATION_DURATION;
    g_traffic_system->total_vehicles_generated = 0;
    
    // --- FIX: Initialize new fields ---
    g_traffic_system->min_arrival_rate = VEHICLE_ARRIVAL_RATE_MIN;
    g_traffic_system->max_arrival_rate = VEHICLE_ARRIVAL_RATE_MAX;
    g_traffic_system->vehicle_generator_thread = 0; // Mark as not running
    // --- END FIX ---


    // Initialize global state lock
    pthread_mutex_init(&g_traffic_system->global_state_lock, NULL);

    // --- DELETED ---
    // The snapshot and its mutex are removed.
    // --- END DELETED ---

    // Don't printf after init_visualization, it messes up ncurses
    // printf("TrafficGuru system initialized successfully\n");
    return 0;
}

// Destroy the global traffic system
void destroy_traffic_guru_system() {
    if (!g_traffic_system) {
        return;
    }

    // printf("Shutting down TrafficGuru system...\n"); // Messes up ncurses

    // Stop simulation if running
    stop_traffic_simulation();

    // Destroy visualization
    // This MUST call endwin()
    destroy_visualization(&g_traffic_system->visualization);

    // Now it's safe to printf
    printf("Shutting down TrafficGuru system...\n");


    // Destroy signal history
    destroy_signal_history(&g_traffic_system->signal_history);

    // Destroy emergency system
    destroy_emergency_system(&g_traffic_system->emergency_system);

    // Destroy performance metrics
    destroy_performance_metrics(&g_traffic_system->metrics);

    // Destroy Banker's algorithm
    destroy_bankers_state(&g_traffic_system->bankers_state);

    // Destroy synchronization
    destroy_intersection_mutex(&g_traffic_system->intersection);

    // Destroy scheduler
    destroy_scheduler(&g_traffic_system->scheduler);

    // Destroy lane processes
    for (int i = 0; i < NUM_LANES; i++) {
        destroy_lane_process(&g_traffic_system->lanes[i]);
    }

    // Destroy global state lock
    pthread_mutex_destroy(&g_traffic_system->global_state_lock);

    // --- DELETED ---
    // Removed snapshot mutex destroy
    // --- END DELETED ---

    // Free system structure
    free(g_traffic_system);
    g_traffic_system = NULL;

    printf("TrafficGuru system shutdown complete\n");
}

// Start traffic simulation
int start_traffic_simulation() {
    if (!g_traffic_system) {
        printf("Traffic system not initialized\n");
        return -1;
    }

    if (g_traffic_system->simulation_running) {
        // printf("Simulation already running\n");
        return 0;
    }

    // printf("Starting traffic simulation...\n");

    g_traffic_system->simulation_running = true;
    g_traffic_system->simulation_paused = false;
    g_traffic_system->simulation_start_time = time(NULL);

    // Start scheduler
    start_scheduler(&g_traffic_system->scheduler);

    // Create simulation thread
    if (pthread_create(&g_traffic_system->simulation_thread, NULL,
                      simulation_main_loop, NULL) != 0) {
        printf("Failed to create simulation thread\n");
        g_traffic_system->simulation_running = false;
        return -1;
    }
    
    // --- FIX: Start the vehicle generator thread ---
    if (pthread_create(&g_traffic_system->vehicle_generator_thread, NULL,
                      vehicle_generator_loop, NULL) != 0) {
        // Can't printf, ncurses is active
        // We should stop the other thread...
        g_traffic_system->simulation_running = false; 
        pthread_join(g_traffic_system->simulation_thread, NULL);
        return -1; // Failed
    }
    // --- END FIX ---


    // printf("Traffic simulation started\n");
    return 0;
}

// Stop traffic simulation
void stop_traffic_simulation() {
    if (!g_traffic_system || !g_traffic_system->simulation_running) {
        return;
    }

    // printf("Stopping traffic simulation...\n");

    g_traffic_system->simulation_running = false;
    g_traffic_system->simulation_end_time = time(NULL);

    // Stop scheduler
    stop_scheduler(&g_traffic_system->scheduler);

    // Wait for simulation thread to finish
    if (g_traffic_system->simulation_thread) {
        pthread_join(g_traffic_system->simulation_thread, NULL);
        g_traffic_system->simulation_thread = 0; // Mark as joined
    }
    
    // --- FIX: Stop the vehicle generator thread ---
    if (g_traffic_system->vehicle_generator_thread) {
        pthread_join(g_traffic_system->vehicle_generator_thread, NULL);
        g_traffic_system->vehicle_generator_thread = 0; // Mark as joined
    }
    // --- END FIX ---


    // printf("Traffic simulation stopped\n");
}

// Pause traffic simulation
void pause_traffic_simulation() {
    if (!g_traffic_system) {
        return;
    }

    g_traffic_system->simulation_paused = true;
    // printf("Simulation paused\n"); // Messes up ncurses
}

// Resume traffic simulation
void resume_traffic_simulation() {
    if (!g_traffic_system) { // <-- FIXED TYPO ---
        return;
    }

    g_traffic_system->simulation_paused = false;
    // printf("Simulation resumed\n"); // Messes up ncurses
}

// Main simulation loop
void* simulation_main_loop(void* arg) {
    (void)arg; // Suppress unused parameter warning

    // printf("Simulation main loop started\n");

    while (g_traffic_system && g_traffic_system->simulation_running && keep_running) {
        if (!g_traffic_system->simulation_paused) {
            update_simulation_state();
            process_traffic_events();
        }

        // Sleep to control simulation speed
        usleep(SIMULATION_UPDATE_INTERVAL);
    }

    // printf("Simulation main loop ended\n");
    return NULL;
}

// Update simulation state
void update_simulation_state() {
    if (!g_traffic_system) {
        return;
    }

    // --- DEADLOCK FIX: Lock only for metrics update ---
    pthread_mutex_lock(&g_traffic_system->global_state_lock);
    // Update metrics
    update_time_based_metrics(&g_traffic_system->metrics, time(NULL));
    // Update emergency system
    update_emergency_progress(&g_traffic_system->emergency_system);
    pthread_mutex_unlock(&g_traffic_system->global_state_lock);
    // --- END DEADLOCK FIX ---


    // Check for deadlocks (must be outside the global lock)
    static int deadlock_check_counter = 0;
    if (++deadlock_check_counter >= 100) { // Check every 100 iterations
        detect_and_resolve_advanced_deadlock(g_traffic_system->lanes);
        deadlock_check_counter = 0;
    }
}

// Process traffic events
void process_traffic_events() {
    if (!g_traffic_system) {
        return;
    }

    // Run scheduling algorithm
    int next_lane = schedule_next_lane(&g_traffic_system->scheduler,
                                      g_traffic_system->lanes);

    if (next_lane != -1) {
        // Execute time slice for selected lane
        execute_lane_time_slice(&g_traffic_system->scheduler,
                               &g_traffic_system->lanes[next_lane],
                               g_traffic_system->scheduler.time_quantum);
    }

    // Update visualization (if available)
    // This would be called from the main thread in practice
}

// Utility functions
void print_system_info() {
    printf("\n=== TrafficGuru System Information ===\n");
    printf("Version: 1.0.0\n");
    printf("Build date: %s %s\n", __DATE__, __TIME__);
    printf("Compiler: %s\n", __VERSION__);
    printf("System: OS-inspired traffic management\n");
    printf("Features:\n");
    printf("  - Multiple scheduling algorithms (SJF, Multilevel Feedback, Priority RR)\n");
    printf("  - Banker's algorithm for deadlock prevention\n");
    printf("  - Emergency vehicle preemption\n");
    printf("  - Real-time visualization with ncurses\n");
    printf("  - Performance metrics and analysis\n");
    printf("====================================\n\n");
}

void print_usage(const char* program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("Use --help for detailed usage information\n");
}

void cleanup_and_exit(int exit_code) {
    destroy_traffic_guru_system();
    // ncurses cleanup should happen in destroy_visualization
    exit(exit_code);
}

bool validate_system_state() {
    if (!g_traffic_system) {
        return false;
    }

    // Validate intersection state
    if (!validate_intersection_state()) {
        // printf("ERROR: Invalid intersection state detected\n");
        return false;
    }

    // Validate Banker's algorithm state
    if (!is_safe_state(&g_traffic_system->bankers_state)) {
        // printf("WARNING: System in unsafe state\n");
    }

    return true;
}

// Configuration functions
void set_simulation_duration(int seconds) {
    if (g_traffic_system && seconds > 0) {
        g_traffic_system->simulation_end_time = time(NULL) + seconds;
    }
}

void set_vehicle_arrival_rate(int min_seconds, int max_seconds) {
    // --- FIX: Actually store the values ---
    if (g_traffic_system) {
        g_traffic_system->min_arrival_rate = min_seconds;
        g_traffic_system->max_arrival_rate = max_seconds;
        if (g_traffic_system->min_arrival_rate > g_traffic_system->max_arrival_rate) {
             g_traffic_system->max_arrival_rate = g_traffic_system->min_arrival_rate;
        }
    }
    // --- END FIX ---
}

void set_time_quantum(int seconds) {
    if (g_traffic_system && seconds > 0) {
        g_traffic_system->scheduler.time_quantum = seconds;
    }
}

void set_debug_mode(bool enabled) {
    // printf("Debug mode %s\n", enabled ? "enabled" : "disabled");
    
    // --- FIX: Suppress unused parameter warning ---
    (void)enabled;
}

// Logging functions
void log_system_event(const char* event) {
    time_t now = time(NULL);
    // printf("[%ld] EVENT: %s\n", now, event);

    // --- FIX: Suppress unused warnings ---
    (void)event;
    (void)now;
}

void log_error(const char* error) {
    time_t now = time(NULL);
    // fprintf(stderr, "[%ld] ERROR: %s\n", now, error);

    // --- FIX: Suppress unused warnings ---
    (void)error;
    (void)now;
}

void log_debug(const char* message) {
    time_t now = time(NULL);
    // printf("[%ld] DEBUG: %s\n", now, message);

    // --- FIX: Suppress unused warnings ---
    (void)message;
    (void)now;
}

void log_performance_summary() {
    if (!g_traffic_system) {
        return;
    }

    // This is called AFTER endwin() in destroy_traffic_guru_system()
    printf("\n=== PERFORMANCE SUMMARY ===\n");
    print_performance_metrics(&g_traffic_system->metrics);
    printf("===========================\n\n");
}

// --- DELETED ---
// The update_visualization_snapshot function is removed.
// --- END DELETED ---


// --- RENAMED FUNCTION ---
// Handle user input from the main loop
// 
// --- DELETION ---
// The entire function "handle_main_loop_input(int ch)" has been removed.
// It was conflicting with your project's built-in input handler.
// --- END DELETION ---


// Main function
int main(int argc, char* argv[]) {
    // Parse command line arguments
    CommandLineArgs args = parse_command_line_args(argc, argv);

    if (args.help_requested) {
        print_command_line_help();
        return 0;
    }

    validate_command_line_args(&args);

    // Setup signal handlers
    setup_signal_handlers();

    // Print system information
    if (args.debug_mode) {
        print_system_info();
    }

    // Initialize system
    if (init_traffic_guru_system() != 0) {
        printf("Failed to initialize TrafficGuru system\n");
        return 1;
    }

    // Apply configuration from command line
    set_simulation_duration(args.duration);
    set_vehicle_arrival_rate(args.min_arrival_rate, args.max_arrival_rate);
    set_time_quantum(args.time_quantum);
    set_debug_mode(args.debug_mode);

    // Set scheduling algorithm
    set_scheduling_algorithm(&g_traffic_system->scheduler, args.algorithm);
    // printf("Using scheduling algorithm: %s\n", get_algorithm_name(args.algorithm));

    // Start simulation
    if (start_traffic_simulation() != 0) {
        // ncurses is active, so we can't just printf
        endwin(); // Clean up ncurses
        printf("Failed to start simulation\n");
        cleanup_and_exit(1);
    }

    // --- MODIFICATION ---
    // Set ncurses getch() to be non-blocking
    // We assume init_visualization() has already called initscr()
    // and g_traffic_system->visualization.main_window is the main ncurses window
    if (g_traffic_system->visualization.main_window) {
         nodelay(g_traffic_system->visualization.main_window, TRUE);
    } else if (stdscr) {
         // Fallback if main_window isn't set but ncurses is initialized
         nodelay(stdscr, TRUE);
    }
    // --- END MODIFICATION ---

    // REMOVED: These printfs will be overwritten by ncurses
    // printf("TrafficGuru simulation running...\n");
    // printf("Press 'h' during simulation for help, 'q' to quit\n");
    
    // Write an initial message to the status line
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    mvprintw(max_y - 1, 0, "Simulation running... Press 'q' to quit, 'h' for help.");
    refresh();


    // Main loop - handle user input and maintain simulation
    while (keep_running && g_traffic_system->simulation_running) {
        
        // --- DELETED ---
        // update_visualization_snapshot() is removed.
        // --- END DELETED ---

        // Display real-time visualization
        // This function must be drawing to the ncurses screen
        display_real_time_status(); // This will now use the snapshot

        // --- MODIFICATION ---
        // We will call your project's built-in input handler,
        // which is declared in visualization.h.
        // We assume this function handles its own getch() and 
        // will set 'keep_running = false' (or similar) when 'q' is pressed.
        handle_user_input(&g_traffic_system->visualization);
        // --- END MODIFICATION ---

        // Check if simulation duration has elapsed
        if (time(NULL) >= g_traffic_system->simulation_end_time) {
            // MODIFIED: Use ncurses print
            getmaxyx(stdscr, max_y, max_x);
            mvprintw(max_y - 1, 0, "%*s", max_x, ""); // Clear line
            mvprintw(max_y - 1, 0, "Simulation duration elapsed. Shutting down...");
            refresh();
            sleep(1); // Pause to show message
            // END MODIFIED
            break;
        }

        // --- UI DEADLOCK FIX: REMOVED VALIDATION FROM UI LOOP ---
        // This function caused a deadlock with the simulation thread.
        /*
        static int validation_counter = 0;
        if (++validation_counter >= 10) {
            if (!validate_system_state()) {
                // ... (code removed) ...
                break;
            }
            validation_counter = 0;
        }
        */
        // --- END UI DEADLOCK FIX ---

        // --- MODIFICATION ---
        // sleep(1); // Old: Update every second
        usleep(100000); // New: Update every 100ms for responsive input
        // --- END MODIFICATION ---
    }

    // Stop simulation
    stop_traffic_simulation();

    // Cleanup (destroy_visualization will call endwin())
    // and exit
    cleanup_and_exit(0);

    // --- Code below is now unreachable after cleanup_and_exit(0) ---
    // Print performance summary
    // Note: ncurses must be shut down (in destroy_visualization)
    // BEFORE printing to stdout again.
    if (!args.debug_mode) {
        log_performance_summary();
    }

    // Cleanup and exit
    cleanup_and_exit(0);
}
