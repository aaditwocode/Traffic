#define _XOPEN_SOURCE 600
#include "../include/scheduler.h"
#include "../include/lane_process.h"
#include "../include/trafficguru.h"
// --- ADDED ---
// We need these headers for the fix
#include "../include/performance_metrics.h" 
// --- END ADD ---

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

// Algorithm names for display
static const char* algorithm_names[] = {
    "Shortest Job First",
    "Multilevel Feedback Queue",
    "Priority Round Robin"
};

// Initialize scheduler
void init_scheduler(Scheduler* scheduler, SchedulingAlgorithm algorithm) {
    if (!scheduler) {
        return;
    }

    scheduler->algorithm = algorithm;
    scheduler->ready_queue = create_queue(20); // Support up to 20 lanes in ready state
    scheduler->time_quantum = DEFAULT_TIME_QUANTUM;
    scheduler->context_switch_time = CONTEXT_SWITCH_TIME;
    scheduler->current_lane = -1; // No lane currently selected
    scheduler->execution_history = NULL;
    scheduler->history_size = 1000; // Keep last 1000 execution records
    scheduler->history_index = 0;
    scheduler->total_context_switches = 0;
    scheduler->last_schedule_time = time(NULL);
    scheduler->scheduler_running = false;

    // Allocate execution history buffer
    scheduler->execution_history = (ExecutionRecord*)malloc(
        scheduler->history_size * sizeof(ExecutionRecord));
    if (!scheduler->execution_history) {
        scheduler->history_size = 0;
    }

    // Initialize synchronization primitives
    pthread_mutex_init(&scheduler->scheduler_lock, NULL);
    pthread_cond_init(&scheduler->scheduler_cond, NULL);
}

// Destroy scheduler and cleanup resources
void destroy_scheduler(Scheduler* scheduler) {
    if (!scheduler) {
        return;
    }

    if (scheduler->ready_queue) {
        destroy_queue(scheduler->ready_queue);
        scheduler->ready_queue = NULL;
    }

    if (scheduler->execution_history) {
        free(scheduler->execution_history);
        scheduler->execution_history = NULL;
    }

    pthread_mutex_destroy(&scheduler->scheduler_lock);
    pthread_cond_destroy(&scheduler->scheduler_cond);
}

// Start scheduler
void start_scheduler(Scheduler* scheduler) {
    if (!scheduler) {
        return;
    }

    pthread_mutex_lock(&scheduler->scheduler_lock);
    scheduler->scheduler_running = true;
    scheduler->last_schedule_time = time(NULL);
    pthread_cond_signal(&scheduler->scheduler_cond);
    pthread_mutex_unlock(&scheduler->scheduler_lock);
}

// Stop scheduler
void stop_scheduler(Scheduler* scheduler) {
    if (!scheduler) {
        return;
    }

    pthread_mutex_lock(&scheduler->scheduler_lock);
    scheduler->scheduler_running = false;
    pthread_cond_signal(&scheduler->scheduler_cond);
    pthread_mutex_unlock(&scheduler->scheduler_lock);
}

// Main scheduling function - delegates to specific algorithm
int schedule_next_lane(Scheduler* scheduler, LaneProcess lanes[4]) {
    if (!scheduler || !lanes) {
        return -1;
    }

    // This function is locked by the simulation thread
    pthread_mutex_lock(&scheduler->scheduler_lock);

    int next_lane = -1;

    // --- Select next lane based on algorithm ---
    switch (scheduler->algorithm) {
        case SJF:
            // Assuming schedule_next_lane_sjf is defined in sjf_scheduler.c
            next_lane = schedule_next_lane_sjf(scheduler, lanes);
            break;
        case MULTILEVEL_FEEDBACK:
            // Assuming schedule_next_lane_multilevel is defined in multilevel_scheduler.c
            next_lane = schedule_next_lane_multilevel(scheduler, lanes);
            break;
        case PRIORITY_ROUND_ROBIN:
            // Assuming schedule_next_lane_priority_rr is defined in priority_rr_scheduler.c
            next_lane = schedule_next_lane_priority_rr(scheduler, lanes);
            break;
        default:
            // Fallback
            next_lane = schedule_next_lane_sjf(scheduler, lanes);
            break;
    }

    // --- Perform context switch if needed ---
    if (next_lane != scheduler->current_lane && next_lane != -1) {
        // This function MUST lock the lanes it touches
        context_switch(scheduler,
                      scheduler->current_lane >= 0 ? &lanes[scheduler->current_lane] : NULL,
                      &lanes[next_lane]);
        scheduler->current_lane = next_lane;
        
        // --- METRICS FIX ---
        // We lock global state to update metrics
        // --- DEADLOCK FIX: Use trylock ---
        if (pthread_mutex_trylock(&g_traffic_system->global_state_lock) == 0) {
            update_context_switch_count(&g_traffic_system->metrics);
            pthread_mutex_unlock(&g_traffic_system->global_state_lock);
        }
                
        // --- VALIDATION: Ensure only one lane is running ---
        // This verifies mutual exclusion after context switch
        if (!validate_single_lane_running(lanes)) {
            // Critical error: Multiple lanes running simultaneously!
            // This should never happen and indicates a synchronization bug
            // In production, you might want to log this error or trigger an alert
        }
        // --- END VALIDATION ---
        // --- END DEADLOCK FIX ---
    }

    scheduler->last_schedule_time = time(NULL);
    pthread_mutex_unlock(&scheduler->scheduler_lock);

    return next_lane;
}

// --- MODIFIED FUNCTION ---
// This is the critical fix for "Metrics are 0" and the flickering
void execute_lane_time_slice(Scheduler* scheduler, LaneProcess* lane, int time_quantum) {
    // Note: time_quantum is not used here, as we process 1 vehicle per "tick"
    (void)time_quantum; 
    
    if (!scheduler || !lane || !g_traffic_system) {
        return;
    }

    time_t start_time = time(NULL);
    int vehicles_processed = 0;
    int vehicle_id = -1;
    int wait_time_sec = 0;

    // State is already set to RUNNING by context_switch.
    // We do NOT call update_lane_state(lane, RUNNING) here.

    // --- DEADLOCK FIX: Enforce lock order: global -> lane ---
    // We must lock the global state FIRST, then the lane state.
    pthread_mutex_lock(&g_traffic_system->global_state_lock);
    pthread_mutex_lock(&lane->queue_lock);
    
    // 1. Get vehicle ID from queue (using unlocked version since we hold the lock)
    vehicle_id = remove_vehicle_from_lane_unlocked(lane);
    
    if (vehicle_id != -1) { // Assuming -1 means empty queue
        // We successfully processed one vehicle
        vehicles_processed = 1;
        
        // 2. Calculate wait time
        time_t now = time(NULL);
        wait_time_sec = (int)(now - lane->last_arrival_time); 
        if (wait_time_sec < 0) wait_time_sec = 0; // Sanity check

        // 3. --- FIX: UPDATE RAW METRICS ---
        // This is the fix for "metrics are 0".
        // We are holding the global_state_lock, so this is safe.
        g_traffic_system->metrics.total_vehicles_processed++;
        g_traffic_system->metrics.lane_throughput[lane->lane_id]++;
        g_traffic_system->metrics.lane_wait_times[lane->lane_id] += (float)wait_time_sec; 
        
        // --- NEW: Add processing delay to make vehicle allocation visible ---
        // Unlock before sleeping to allow other threads to run
        pthread_mutex_unlock(&lane->queue_lock);
        pthread_mutex_unlock(&g_traffic_system->global_state_lock);
        
        // Sleep to simulate vehicle crossing the intersection (2-4 seconds)
        usleep(2000000 + (rand() % 2000000));  // 2-4 seconds
        
        // Re-acquire locks for cleanup
        pthread_mutex_lock(&g_traffic_system->global_state_lock);
        pthread_mutex_lock(&lane->queue_lock);
    }
    // --- END FIX ---

    time_t end_time = time(NULL);

    // Record execution (even if 0 vehicles)
    record_execution(scheduler, lane->lane_id, start_time, end_time, vehicles_processed);

    // --- FIX: FLICKER FIX ---
    // We ONLY change state if the queue becomes empty.
    // The context_switch function will handle the RUNNING -> READY transition
    if (lane->queue_length == 0 && lane->state == RUNNING) {
        // Queue is empty, so this lane is done.
        lane->state = WAITING; 
    }
    // If queue is not empty, we leave it as RUNNING.
    
    // --- DEADLOCK FIX: Unlock in reverse order ---
    pthread_mutex_unlock(&lane->queue_lock);
    pthread_mutex_unlock(&g_traffic_system->global_state_lock);
    // --- END DEADLOCK FIX ---
}
// --- END MODIFIED FUNCTION ---

// Perform context switch between lanes
void context_switch(Scheduler* scheduler, LaneProcess* from_lane, LaneProcess* to_lane) {
    if (!scheduler) {
        return;
    }

    // Stop previous lane if exists
    if (from_lane) {
        // Lock before changing state
        pthread_mutex_lock(&from_lane->queue_lock);
        if (from_lane->state == RUNNING) {
            // --- FIX: Change state directly without calling update_lane_state ---
            // (update_lane_state tries to lock again, causing deadlock)
            // If queue is empty, context switch will set to WAITING
            if (from_lane->queue_length > 0) {
                from_lane->state = READY;
            } else {
                from_lane->state = WAITING;
            }
            pthread_cond_signal(&from_lane->queue_cond);
        }
        pthread_mutex_unlock(&from_lane->queue_lock);
    }

    // Start new lane if exists
    if (to_lane) {
        // Lock before changing state
        pthread_mutex_lock(&to_lane->queue_lock);
        if (to_lane->state == READY) {
            // --- FIX: Change state directly without calling update_lane_state ---
            // (update_lane_state tries to lock again, causing deadlock)
            to_lane->state = RUNNING;
            to_lane->waiting_time = 0;  // Reset wait time when entering RUNNING
            pthread_cond_signal(&to_lane->queue_cond);
        }
        pthread_mutex_unlock(&to_lane->queue_lock);
    }
    // scheduler->total_context_switches++; // Moved to schedule_next_lane

    // Simulate context switch overhead (increased delay for visibility)
    usleep(scheduler->context_switch_time * 1000);
    usleep(1000000);  // Additional 1 second delay to show lane transitions clearly
}

// Set scheduling algorithm
void set_scheduling_algorithm(Scheduler* scheduler, SchedulingAlgorithm algorithm) {
    if (!scheduler) {
        return;
    }

    // --- DEADLOCK FIX: Use trylock ---
    // This function is called from the UI thread,
    // so it must not block forever.
    if (pthread_mutex_trylock(&scheduler->scheduler_lock) == 0) {
        scheduler->algorithm = algorithm;
        scheduler->current_lane = -1; // Reset current lane on algorithm change
                
        // --- ENHANCEMENT: Reset all lane states when switching algorithms ---
        // This ensures clean transition and no lane remains running from previous algorithm
        extern TrafficGuruSystem* g_traffic_system;  // Access global system
        if (g_traffic_system && g_traffic_system->simulation_running) {
            for (int i = 0; i < 4; i++) {
                LaneProcess* lane = &g_traffic_system->lanes[i];
                pthread_mutex_lock(&lane->queue_lock);
                
                // Stop any running lane
                if (lane->state == RUNNING) {
                    lane->state = (lane->queue_length > 0) ? READY : WAITING;
                    pthread_cond_signal(&lane->queue_cond);
                }
                
                pthread_mutex_unlock(&lane->queue_lock);
            }
            
            // Clear the ready queue for fresh start
            if (scheduler->ready_queue) {
                while (!is_empty(scheduler->ready_queue)) {
                    dequeue(scheduler->ready_queue);
                }
            }
        }
        // --- END ENHANCEMENT ---
        pthread_mutex_unlock(&scheduler->scheduler_lock);
    }
    // If lock is busy, we just skip the change for this frame.
    // --- END DEADLOCK FIX ---
}

// Get current scheduling algorithm
SchedulingAlgorithm get_scheduling_algorithm(Scheduler* scheduler) {
    // Read is generally safe, but trylock is safer
    SchedulingAlgorithm algo = SJF;
    if (pthread_mutex_trylock(&scheduler->scheduler_lock) == 0) {
        algo = scheduler->algorithm;
        pthread_mutex_unlock(&scheduler->scheduler_lock);
    }
    return algo;
}

// Get algorithm name
const char* get_algorithm_name(SchedulingAlgorithm algorithm) {
    if (algorithm >= 0 && algorithm < 3) {
        return algorithm_names[algorithm];
    }
    return "Unknown";
}

// Record execution in history
void record_execution(Scheduler* scheduler, int lane_id, time_t start_time,
                      time_t end_time, int vehicles_processed) {
    if (!scheduler || !scheduler->execution_history) {
        return;
    }

    // --- DEADLOCK FIX: Use trylock ---
    if (pthread_mutex_trylock(&scheduler->scheduler_lock) == 0) {
        ExecutionRecord* record = &scheduler->execution_history[scheduler->history_index];
        record->lane_id = lane_id;
        record->start_time = start_time;
        record->end_time = end_time;
        record->duration = (int)(end_time - start_time);
        record->vehicles_processed = vehicles_processed;

        scheduler->history_index = (scheduler->history_index + 1) % scheduler->history_size;
        
        pthread_mutex_unlock(&scheduler->scheduler_lock);
    }
}

// Print execution history
void print_execution_history(Scheduler* scheduler) {
    if (!scheduler || !scheduler->execution_history) {
        printf("No execution history available.\n");
        return;
    }

    printf("\n=== EXECUTION HISTORY ===\n");
    printf("Lane | Start Time | Duration | Vehicles\n");
    printf("-----|------------|----------|----------\n");
    
    // --- DEADLOCK FIX: Lock before reading history ---
    pthread_mutex_lock(&scheduler->scheduler_lock);

    int count = (scheduler->history_index < scheduler->history_size) ?
                scheduler->history_index : scheduler->history_size;

    int start_index = (scheduler->history_index < scheduler->history_size) ? 0 : scheduler->history_index;

    for (int i = 0; i < count; i++) {
        int index = (start_index + i) % scheduler->history_size;
        ExecutionRecord* record = &scheduler->execution_history[index];

        printf("%4d | %10ld | %8ds | %8d\n",
               record->lane_id,
               record->start_time,
               record->duration,
               record->vehicles_processed);
    }
    
    pthread_mutex_unlock(&scheduler->scheduler_lock);
    // --- END FIX ---
    printf("\n");
}

// Get execution history
ExecutionRecord* get_execution_history(Scheduler* scheduler, int* count) {
    if (!scheduler || !count) {
        return NULL;
    }
    
    // This function is problematic as it returns a pointer
    // without holding the lock.
    // For now, we assume it's read quickly by the caller.
    // --- DEADLOCK FIX: At least use trylock for count ---
    if (pthread_mutex_trylock(&scheduler->scheduler_lock) == 0) {
        *count = (scheduler->history_index < scheduler->history_size) ?
                 scheduler->history_index : scheduler->history_size;
        pthread_mutex_unlock(&scheduler->scheduler_lock);
    } else {
        *count = 0;
    }
    // --- END FIX ---

    return scheduler->execution_history;
}

// Calculate average waiting time
float calculate_average_wait_time(Scheduler* scheduler, LaneProcess lanes[4]) {
    if (!scheduler || !lanes) {
        return 0.0f;
    }

    float total_wait = 0.0f;
    int active_lanes = 0;

    for (int i = 0; i < 4; i++) {
        // This function should be thread-safe (lock inside)
        float lane_wait = get_lane_average_wait_time(&lanes[i]);
        if (lane_wait > 0) {
            total_wait += lane_wait;
            active_lanes++;
        }
    }

    return active_lanes > 0 ? (total_wait / active_lanes) : 0.0f;
}

// Calculate throughput
float calculate_throughput(Scheduler* scheduler, time_t time_period) {
    if (!scheduler || time_period <= 0) {
        return 0.0f;
    }

    int total_vehicles = 0;
    int count = 0;
    
    // --- DEADLOCK FIX: Lock before getting history ---
    pthread_mutex_lock(&scheduler->scheduler_lock);
    ExecutionRecord* history = scheduler->execution_history;
    count = (scheduler->history_index < scheduler->history_size) ?
                 scheduler->history_index : scheduler->history_size;
                 
    // Copy data to avoid holding lock during iteration
    ExecutionRecord* history_copy = (ExecutionRecord*)malloc(count * sizeof(ExecutionRecord));
    if (history_copy) {
         memcpy(history_copy, history, count * sizeof(ExecutionRecord));
    }
    pthread_mutex_unlock(&scheduler->scheduler_lock);
    // --- END FIX ---

    if (history_copy) {
        for (int i = 0; i < count; i++) {
            total_vehicles += history_copy[i].vehicles_processed;
        }
        free(history_copy);
    }

    double minutes = time_period / 60.0;
    return minutes > 0 ? (total_vehicles / minutes) : 0.0f;
}

// Calculate fairness index (Jain's fairness index)
float calculate_fairness_index(Scheduler* scheduler, LaneProcess lanes[4]) {
    if (!scheduler || !lanes) {
        return 0.0f;
    }

    float wait_times[4];
    float sum_wait = 0.0f, sum_wait_sq = 0.0f;
    int active_lanes = 0;

    for (int i = 0; i < 4; i++) {
        // This function should be thread-safe
        wait_times[i] = get_lane_average_wait_time(&lanes[i]);
        if (wait_times[i] > 0) {
            sum_wait += wait_times[i];
            sum_wait_sq += wait_times[i] * wait_times[i];
            active_lanes++;
        }
    }

    if (sum_wait > 0 && active_lanes > 0) {
        return (sum_wait * sum_wait) / (active_lanes * sum_wait_sq);
    }

    return 1.0f; // Perfect fairness when no waiting
}

// Calculate context switch overhead
int calculate_context_switch_overhead(Scheduler* scheduler) {
    int total = 0;
    // --- DEADLOCK FIX: Use trylock ---
    if (pthread_mutex_trylock(&scheduler->scheduler_lock) == 0) {
        total = scheduler->total_context_switches * scheduler->context_switch_time;
        pthread_mutex_unlock(&scheduler->scheduler_lock);
    }
    return total;
}

// Add lane to ready queue
void add_lane_to_ready_queue(Scheduler* scheduler, LaneProcess* lane) {
    if (!scheduler || !lane || !scheduler->ready_queue) {
        return;
    }
    
    // --- DEADLOCK FIX: Use trylock ---
    if (pthread_mutex_trylock(&scheduler->scheduler_lock) == 0) {
        enqueue(scheduler->ready_queue, lane->lane_id);
        pthread_mutex_unlock(&scheduler->scheduler_lock);
    }
}

// Remove lane from ready queue
void remove_lane_from_ready_queue(Scheduler* scheduler, LaneProcess* lane) {
    if (!scheduler || !lane || !scheduler->ready_queue) {
        return;
    }

    // Note: This is a simplified implementation
    // In practice, we'd need to search for the lane in the queue
    // --- DEADLOCK FIX: Use trylock ---
    if (pthread_mutex_trylock(&scheduler->scheduler_lock) == 0) {
        dequeue(scheduler->ready_queue); // Remove from front (simplified)
        pthread_mutex_unlock(&scheduler->scheduler_lock);
    }
}

// Get ready queue size
int get_ready_queue_size(Scheduler* scheduler) {
    if (!scheduler || !scheduler->ready_queue) {
        return 0;
    }

    int size = 0;
    // --- DEADLOCK FIX: Use trylock ---
    if (pthread_mutex_trylock(&scheduler->scheduler_lock) == 0) {
        size = get_size(scheduler->ready_queue);
        pthread_mutex_unlock(&scheduler->scheduler_lock);
    }
    return size;
}

// Check if ready queue is empty
bool is_ready_queue_empty(Scheduler* scheduler) {
    return get_ready_queue_size(scheduler) == 0;
}

// --- NEW FUNCTION: Validate only one lane is running ---
// This ensures mutual exclusion at intersection level
bool validate_single_lane_running(LaneProcess lanes[4]) {
    if (!lanes) {
        return false;
    }
    
    int running_count = 0;
    int running_lane_id = -1;
    
    for (int i = 0; i < 4; i++) {
        // Try to lock each lane to safely check state
        if (pthread_mutex_trylock(&lanes[i].queue_lock) == 0) {
            if (lanes[i].state == RUNNING) {
                running_count++;
                running_lane_id = i;
            }
            pthread_mutex_unlock(&lanes[i].queue_lock);
        }
    }
    
    // Validation: At most one lane should be RUNNING
    if (running_count > 1) {
        // ERROR: Multiple lanes running simultaneously!
        // This violates intersection mutual exclusion
        return false;
    }
    
    return true;  // Valid: 0 or 1 lane running
}
// --- END NEW FUNCTION ---
