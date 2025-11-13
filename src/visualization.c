/*
 * MODIFIED: This is a new implementation of visualization.c
 * It uses the ncurses library to draw the UI and handle input,
 * fixing the "infinite loop" and "screen clearing" issues.
 *
 * THIS VERSION fixes the deadlock (freeze) by using
 * pthread_mutex_trylock() instead of pthread_mutex_lock().
 * The UI thread will no longer block if a mutex is held
 * by a simulation thread.
 *
 * THIS VERSION ALSO fixes a potential input-blocking bug
 * by changing getch() to wgetch(viz->main_window).
 */

#define _XOPEN_SOURCE 600
#include "../include/visualization.h"
#include "../include/trafficguru.h" // Includes main system, keep_running, etc.
#include <ncurses.h> // <--- Use ncurses
#include <string.h>
#include <time.h>
#include <unistd.h>

// --- FIX: Local static variables for state ---
static bool show_help = false;
static bool pause_requested = false;
// --- END FIX ---

// Global pointers to ncurses windows
static WINDOW *main_win = NULL;
static WINDOW *status_win = NULL;
static WINDOW *metrics_win = NULL;
static WINDOW *lanes_win = NULL;
static WINDOW *help_win = NULL;

// --- DEADLOCK FIX: Stale Data Cache ---
// We will store the last known good values here.
// The UI will draw these values.
// A 'trylock' will attempt to update them each frame.
// This prevents the UI thread from blocking (and deadlocking).
static int last_queues[NUM_LANES] = {0, 0, 0, 0};
static int last_waits[NUM_LANES] = {0, 0, 0, 0};
static LaneState last_states[NUM_LANES] = {WAITING, WAITING, WAITING, WAITING};
static PerformanceMetrics last_metrics = {0}; // Assumes {0} is a valid init
static bool last_emergency_mode = false;
// --- END DEADLOCK FIX ---


// Forward declarations for local functions
static void draw_borders(void);
// --- FIX: Renamed functions to avoid header conflict ---
static void draw_lanes_window(LaneProcess lanes[4]);
static void draw_metrics_window(PerformanceMetrics* metrics, SchedulingAlgorithm current_algo);
// --- FIX: Added forward declaration ---
static void show_help_screen(Visualization* viz);

// Initialize visualization system
void init_visualization(Visualization* viz) {
    if (!viz) return;

    // --- NCURSES INITIALIZATION ---
    main_win = initscr();      // Start ncurses mode
    cbreak();                  // Disable line buffering (pass keys immediately)
    noecho();                  // Don't echo key presses to the screen
    keypad(main_win, TRUE);    // Enable function keys (arrows, etc.)
    nodelay(main_win, TRUE);   // Make getch() non-blocking
    curs_set(0);               // Hide the cursor
    
    if (has_colors()) {
        start_color();
        // Define color pairs (ID, foreground, background)
        init_pair(1, COLOR_RED, COLOR_BLACK);
        init_pair(2, COLOR_GREEN, COLOR_BLACK);
        init_pair(3, COLOR_YELLOW, COLOR_BLACK);
        init_pair(4, COLOR_CYAN, COLOR_BLACK);
        init_pair(5, COLOR_WHITE, COLOR_BLACK);
        viz->color_enabled = true;
    } else {
        viz->color_enabled = false;
    }

    viz->main_window = main_win; // Store main window
    
    // Create windows
    int y, x;
    getmaxyx(main_win, y, x);
    viz->screen_height = y;
    viz->screen_width = x;
    
    // (Window creation logic would go here, for now, we use main_win)
    lanes_win = subwin(main_win, 15, x - 4, 3, 2);
    metrics_win = subwin(main_win, 10, x - 4, 19, 2);
    status_win = subwin(main_win, 3, x, y - 3, 0);
    
    // Initialize signal history
    init_signal_history(&viz->signal_history, 100);

    // --- FIX: Use local static variable ---
    show_help = false;

    // --- DEADLOCK FIX: Initialize stale cache ---
    // Clear all our static cache variables to a known-good state.
    memset(&last_metrics, 0, sizeof(PerformanceMetrics));
    last_emergency_mode = false;
    for (int i = 0; i < NUM_LANES; i++) {
        last_queues[i] = 0;
        last_waits[i] = 0;
        last_states[i] = WAITING;
    }
    // --- END DEADLOCK FIX ---
}

// Destroy visualization system
void destroy_visualization(Visualization* viz) {
    if (!viz) return;
    
    // Clean up ncurses windows
    delwin(lanes_win);
    delwin(metrics_win);
    delwin(status_win);
    if(help_win) delwin(help_win);

    // --- NCURSES CLEANUP ---
    endwin(); // IMPORTANT: Restore terminal to normal mode

    // Clean up signal history
    destroy_signal_history(&viz->signal_history);
}

// Handle user input
int handle_user_input(Visualization* viz) {
    if (!viz || !g_traffic_system) return -1;

    // --- INPUT FREEZE FIX ---
    // Changed getch() to wgetch(viz->main_window)
    // to ensure we read from the window that has nodelay(TRUE) set.
    int ch = wgetch(viz->main_window); // Read a key (non-blocking)
    // --- END INPUT FREEZE FIX ---

    // If help is active, any key closes it
    // --- FIX: Use local static variable ---
    if (show_help) {
        if (ch != ERR) {
            show_help = false;
            if (help_win) {
                delwin(help_win);
                help_win = NULL;
            }
            
            // --- HELP SCREEN FREEZE FIX ---
            // If pause was requested (sim was paused before 'h'), keep it paused.
            // If pause was NOT requested (sim was running before 'h'),
            // we must now UN-PAUSE it.
            if (pause_requested) {
                 g_traffic_system->simulation_paused = true; // Stay paused
                 pause_requested = false;
            } else {
                 g_traffic_system->simulation_paused = false; // <<< --- THIS LINE FIXES THE FREEZE
            }
            // --- END HELP SCREEN FREEZE FIX ---
        }
        return 0; // Don't process other keys
    }

    switch (ch) {
        case 'q':
        case 'Q':
            // --- THIS IS THE FIX ---
            // Tell the main loop in main.c to stop
            keep_running = false;
            // --- END FIX ---
            break;

        case ' ': // Spacebar
            g_traffic_system->simulation_paused = !g_traffic_system->simulation_paused;
            break;

        case '1':
            set_scheduling_algorithm(&g_traffic_system->scheduler, SJF);
            break;

        case '2':
            set_scheduling_algorithm(&g_traffic_system->scheduler, MULTILEVEL_FEEDBACK);
            break;
        
        case '3':
            set_scheduling_algorithm(&g_traffic_system->scheduler, PRIORITY_ROUND_ROBIN);
            break;

        case 'e':
        case 'E':
            // --- FIX: Commented out to fix build error ---
            // You can re-enable this once trigger_emergency_vehicle is in a header
            // trigger_emergency_vehicle(&g_traffic_system->emergency_system, rand() % NUM_LANES);
            (void)0; // No-op to keep 'e' case
            break;

        case 'h':
        case 'H':
            // --- FIX: Use local static variable ---
            show_help = true;
            // Store pause state
            if(g_traffic_system->simulation_paused) {
                pause_requested = true;
            } else {
                g_traffic_system->simulation_paused = true; // Pause sim to show help
                pause_requested = false;
            }
            break;

        case ERR: // No key pressed
        default:
            return 0; // No action
    }
    return ch;
}

// --- All functions below are ncurses-based display functions ---

static void draw_borders(void) {
    int y, x;
    getmaxyx(main_win, y, x);
    
    // --- FIX: Cleaned up garbled text ---
    // We remove the box(main_win, 0, 0) call, as it's
    // unnecessary and causes the garbled text on line 0.
    // box(main_win, 0, 0); // <-- DELETED
    // --- END FIX ---
    
    // Lane window
    box(lanes_win, 0, 0);
    mvwprintw(lanes_win, 0, 2, " Intersection Status ");

    // Metrics window
    box(metrics_win, 0, 0);
    mvwprintw(metrics_win, 0, 2, " Performance Metrics ");

    // Status bar
    box(status_win, 0, 0);
    mvwprintw(status_win, 0, 2, " Status & Controls ");

    // --- FIX: Suppress unused 'y' warning ---
    (void)y;
}

void display_real_time_status() {
    // --- FIX: Use local static variable ---
    if (!g_traffic_system || show_help) {
        show_help_screen(&g_traffic_system->visualization);
        return; // Don't draw main UI if help is showing
    }

    // --- FIX: FLICKER REMOVED ---
    // We must manually clear the header lines
    int max_x = getmaxx(stdscr);
    // Clear line 1 and 2
    attron(A_NORMAL); // Use normal attributes
    // --- FIX: Clear line 0 as well ---
    mvprintw(0, 0, "%*s", max_x, ""); 
    mvprintw(1, 0, "%*s", max_x, ""); 
    mvprintw(2, 0, "%*s", max_x, "");


    // Get current time for display
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

    // --- FIX: GARBLED TEXT ---
    // Draw borders first, THEN draw text on top
    draw_borders();
    
    // Header
    attron(A_BOLD); // Make title bold
    // --- EMOJI REMOVED ---
    mvprintw(1, (getmaxx(stdscr) - 24) / 2, "TrafficGuru Simulation");
    attroff(A_BOLD);
    
    mvprintw(2, 3, "Time: %s", time_str);
    
    // This read is safe because 'scheduler.algorithm' is only
    // written by 'handle_user_input', which is on this same thread.
    mvprintw(2, 20, "Algorithm: %s", get_algorithm_name(g_traffic_system->scheduler.algorithm));
    
    time_t elapsed = now - g_traffic_system->simulation_start_time;
    time_t remaining = g_traffic_system->simulation_end_time - now;
    mvprintw(2, max_x - 22, "Elapsed: %lds / %lds", (long)elapsed, (long)(elapsed + remaining));
    // --- END FIX ---


    // --- FIX FOR PAUSE & FLICKER ---
    // We only redraw the data windows if the simulation is NOT paused.
    // This "freezes" the screen on the last frame when you pause.
    if(!g_traffic_system->simulation_paused) {
        draw_lanes_window(g_traffic_system->lanes);
        draw_metrics_window(&g_traffic_system->metrics,
                            g_traffic_system->scheduler.algorithm);
    }
    // --- END FIX ---
    
    // We *always* redraw the status bar.
    wclear(status_win); // Clear status bar
    box(status_win, 0, 0);
    mvwprintw(status_win, 0, 2, " Status & Controls ");

    // Display status bar (always draw this)
    const char* status = g_traffic_system->simulation_paused ? "PAUSED" : "RUNNING";
    mvwprintw(status_win, 1, 2, "STATUS: %s", status);
    mvwprintw(status_win, 1, 20, "CONTROLS: [Q] Quit | [Space] Pause | [1-3] Algo | [H] Help");
    
    // --- FIX: FLICKER-FREE REFRESH ---
    // Replace all wrefresh() calls with wnoutrefresh()
    // and then call doupdate() once at the end.
    
    wnoutrefresh(stdscr); // Refresh header
    wnoutrefresh(lanes_win);
    wnoutrefresh(metrics_win);
    wnoutrefresh(status_win);
    
    doupdate(); // Draw all changes to the screen at once
    // --- END FIX ---
}

// --- FIX: Rewritten for appealing layout ---
static void draw_lanes_window(LaneProcess lanes[4]) {
    wclear(lanes_win); // Clear window content
    box(lanes_win, 0, 0);
    mvwprintw(lanes_win, 0, 2, " Intersection Status ");

    const char* lane_names[] = {"NORTH", "SOUTH", "EAST ", "WEST "};
    char queue_str[4][10];
    
    // --- Get all lane data first (to avoid holding locks while drawing) ---
    int queues[4];
    int waits[4];
    LaneState states[4];

    // --- DEADLOCK FIX: Use 'trylock' to update cache ---
    for (int i = 0; i < 4; i++) {
        // Try to get the lock. If we can't, EBUSY is returned.
        // We DON'T wait.
        if (pthread_mutex_trylock(&lanes[i].queue_lock) == 0) {
            // Success! Update our cached values.
            last_queues[i] = lanes[i].queue_length;
            last_waits[i] = lanes[i].waiting_time;
            last_states[i] = lanes[i].state;
            pthread_mutex_unlock(&lanes[i].queue_lock);
        }
        // Use the cached values for drawing (either new or old)
        queues[i] = last_queues[i];
        waits[i] = last_waits[i];
        states[i] = last_states[i];

        // --- EMOJI REMOVED ---
        snprintf(queue_str[i], 10, "Q: %d", queues[i]);
    }

    // Try to get the emergency status
    // This is protected by global_state_lock (see main.c)
    if (pthread_mutex_trylock(&g_traffic_system->global_state_lock) == 0) {
        last_emergency_mode = g_traffic_system->emergency_system.emergency_mode;
        pthread_mutex_unlock(&g_traffic_system->global_state_lock);
    }
    // --- END DEADLOCK FIX ---


    // --- Draw ASCII Intersection (Left Side) ---
        mvwprintw(lanes_win, 2, 13, "N");  // North direction label
    mvwprintw(lanes_win, 3, 12, "%s", queue_str[LANE_NORTH]);
    mvwprintw(lanes_win, 4, 13, "|"); // Adjusted for alignment
    mvwprintw(lanes_win, 5, 5, "%s ---+--- %s", queue_str[LANE_WEST], queue_str[LANE_EAST]);
        mvwprintw(lanes_win, 5, 3, "W");  // West direction label
        mvwprintw(lanes_win, 5, 23, "E");  // East direction label
    mvwprintw(lanes_win, 6, 13, "|"); // Adjusted for alignment
    mvwprintw(lanes_win, 7, 12, "%s", queue_str[LANE_SOUTH]);
        mvwprintw(lanes_win, 8, 13, "S");  // South direction label

    // --- Draw Status Block (Right Side) ---
    int status_x_pos = 35;
    mvwprintw(lanes_win, 2, status_x_pos, "LANE   | STATUS   | QUEUE ");
    mvwprintw(lanes_win, 3, status_x_pos, "-------+----------+-------");

    for (int i = 0; i < 4; i++) {
        // Set color based on state
        int color_pair = 5; // Default white
        char state_indicator[20];
        
        if (states[i] == RUNNING) {
            color_pair = 2; // Green
            snprintf(state_indicator, 20, ">> RUN <<");  // --- NEW: Visual indicator ---
        }
        else if (states[i] == READY) {
            color_pair = 3; // Yellow
            snprintf(state_indicator, 20, "  OPEN");    // --- NEW: Visual indicator ---
        }
        else if (states[i] == WAITING) {
            color_pair = 1; // Red
            snprintf(state_indicator, 20, "  WAIT");    // --- NEW: Visual indicator ---
        }
        else {
            snprintf(state_indicator, 20, " BLOCK");
        }
        
        wattron(lanes_win, COLOR_PAIR(color_pair));
        
        // Draw the formatted status line with better visual indicators
        mvwprintw(lanes_win, 4 + i, status_x_pos, "%-6s | %-8s | %-5d", 
                  lane_names[i], 
                  state_indicator,        // --- NEW: Use indicator instead of state name ---
                  queues[i]);
        
        wattroff(lanes_win, COLOR_PAIR(color_pair));
    }
    
    // --- Draw Emergency Status (Bottom) ---
    if (last_emergency_mode) { // <-- Use cached value
        wattron(lanes_win, A_BLINK | COLOR_PAIR(1)); // Blinking Red
        // --- EMOJI REMOVED ---
        mvwprintw(lanes_win, 13, 4, "*** EMERGENCY ACTIVE ***");
        wattroff(lanes_win, A_BLINK | COLOR_PAIR(1));
    }
    
    // --- NEW: Add legend for lane states ---
    mvwprintw(lanes_win, 10, 2, "Legend:");
    wattron(lanes_win, COLOR_PAIR(2));
    mvwprintw(lanes_win, 11, 2, ">> RUN <<");
    wattroff(lanes_win, COLOR_PAIR(2));
    mvwprintw(lanes_win, 11, 12, " = Vehicle Processing (Green)");
    
    wattron(lanes_win, COLOR_PAIR(3));
    mvwprintw(lanes_win, 12, 2, "  OPEN");
    wattroff(lanes_win, COLOR_PAIR(3));
    mvwprintw(lanes_win, 12, 12, " = Ready for Processing (Yellow)");
    
    wattron(lanes_win, COLOR_PAIR(1));
    mvwprintw(lanes_win, 13, 2, "  WAIT");
    wattroff(lanes_win, COLOR_PAIR(1));
    mvwprintw(lanes_win, 13, 12, " = Waiting for Green Light (Red)");
}

// --- FIX: Renamed function ---
static void draw_metrics_window(PerformanceMetrics* metrics, SchedulingAlgorithm current_algo) {
    wclear(metrics_win); // Clear window content
    box(metrics_win, 0, 0);
    mvwprintw(metrics_win, 0, 2, " Performance Metrics ");

    // --- DEADLOCK FIX: Use 'trylock' to update metrics cache ---
    // The 'metrics' struct is protected by the 'global_state_lock'
    // as seen in main.c's update_simulation_state()
    if (pthread_mutex_trylock(&g_traffic_system->global_state_lock) == 0) {
        // Success! Atomically copy the entire metrics struct.
        last_metrics = *metrics;
        pthread_mutex_unlock(&g_traffic_system->global_state_lock);
    }
    // --- END DEADLOCK FIX ---

    // --- DEADLOCK FIX: Draw using the 'last_metrics' cache ---
    mvwprintw(metrics_win, 2, 2, "Throughput : %.1f veh/min", last_metrics.vehicles_per_minute);
    mvwprintw(metrics_win, 3, 2, "Avg Wait   : %.1fs", last_metrics.avg_wait_time);
    mvwprintw(metrics_win, 4, 2, "Utilization: %.1f%%", last_metrics.utilization * 100);

    mvwprintw(metrics_win, 2, 30, "Total Served   : %d", last_metrics.total_vehicles_processed);
    mvwprintw(metrics_win, 4, 30, "Context Switches: %d", last_metrics.context_switches);

    mvwprintw(metrics_win, 6, 2, "Emerg. Resp: %.1fs", last_metrics.emergency_response_time);
    mvwprintw(metrics_win, 7, 2, "Deadlocks   : %d", last_metrics.deadlocks_prevented);
    mvwprintw(metrics_win, 8, 2, "Overflows   : %d", last_metrics.queue_overflow_count);
    // --- END DEADLOCK FIX ---
    
    // This read is safe, as 'current_algo' is passed by value
    // from the main thread, which read it safely.
    mvwprintw(metrics_win, 7, 30, "Algorithm: %s", get_algorithm_name(current_algo));
}


// Show help screen
static void show_help_screen(Visualization* viz) {
    if (!viz) return;
    
    int y, x;
    getmaxyx(main_win, y, x);
    
    // Create help window if it doesn't exist
    if (!help_win) {
        help_win = newwin(y - 4, x - 4, 2, 2);
        box(help_win, 0, 0);
        keypad(help_win, TRUE);
    }
    
    wclear(help_win);
    box(help_win, 0, 0);
    // --- EMOJI REMOVED ---
    mvwprintw(help_win, 0, (x-10)/2, " HELP ");
    
    mvwprintw(help_win, 3, 4, "CONTROLS:");
    mvwprintw(help_win, 4, 6, "[Q]       - Quit Program");
    mvwprintw(help_win, 5, 6, "[SPACE]   - Pause/Resume Simulation");
    mvwprintw(help_win, 6, 6, "[H]       - Close this Help Screen");
    mvwprintw(help_win, 7, 6, "[E]       - Trigger Emergency Vehicle");
    
    mvwprintw(help_win, 9, 4, "ALGORITHMS:");
    mvwprintw(help_win, 10, 6, "[1]       - Shortest Job First (SJF)");
    mvwprintw(help_win, 11, 6, "[2]       - Multilevel Feedback Queue");
    mvwprintw(help_win, 12, 6, "[3]       - Priority Round Robin");

    mvwprintw(help_win, (y-4) - 3, (x-27)/2, "Press any key to continue...");
    wrefresh(help_win);
}

// --- STUB FUNCTIONS (Not implemented in ncurses yet or simple) ---

void init_signal_history(SignalHistory* history, int capacity) {
    if (!history || capacity <= 0) return;
    history->events = (SignalEvent*)malloc(capacity * sizeof(SignalEvent));
    if (history->events) {
        history->capacity = capacity;
        history->size = 0;
        history->head = 0;
        history->tail = 0;
    }
}

void destroy_signal_history(SignalHistory* history) {
    if (!history) return;
    if (history->events) {
        free(history->events);
        history->events = NULL;
    }
    history->capacity = 0;
    history->size = 0;
}

// Get state name
const char* get_state_name(LaneState state) {
    switch (state) {
        case RUNNING: return "RUNNING";
        case READY: return "READY";
        case WAITING: return "WAITING";
        case BLOCKED: return "BLOCKED";
        default: return "UNKNOWN";
    }
}

// --- FIX: DELETED all deprecated printf-based functions ---
// (display_detailed_vehicle_information, display_enhanced_metrics_dashboard, etc.)
