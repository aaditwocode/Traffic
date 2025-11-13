#ifndef LANE_PROCESS_H
#define LANE_PROCESS_H

#include <pthread.h>
#include <time.h>
#include "queue.h"

// Traffic simulation constants
#define VEHICLE_CROSS_TIME 2     // seconds
#define BATCH_EXIT_SIZE 3        // number of vehicles that can exit simultaneously

typedef enum {
    WAITING = 0,
    READY = 1,
    RUNNING = 2,
    BLOCKED = 3
} LaneState;

typedef struct {
    int lane_id;                    // 0=North, 1=South, 2=East, 3=West
    Queue* queue;                   // Dynamic queue of vehicle positions
    int queue_length;               // Current number of vehicles waiting
    int max_queue_length;           // Maximum queue capacity
    LaneState state;                // Current lane state
    int priority;                   // Lane priority for scheduling
    int waiting_time;               // Time spent waiting for green light
    pthread_t thread_id;            // Thread identifier
    pthread_mutex_t queue_lock;     // Mutex for queue access
    pthread_cond_t queue_cond;      // Condition variable for signaling
    time_t last_arrival_time;       // Last vehicle arrival timestamp
    time_t last_service_time;       // Last service completion timestamp
    int total_vehicles_served;      // Total vehicles processed by this lane
    int total_waiting_time;         // Cumulative waiting time
    int requested_quadrants;        // Intersection quadrants needed
    int allocated_quadrants;        // Currently allocated quadrants
} LaneProcess;

// Lane process lifecycle functions
void init_lane_process(LaneProcess* lane, int lane_id, int max_capacity);
void destroy_lane_process(LaneProcess* lane);
void* lane_process_thread(void* arg);

// Queue management functions
void add_vehicle_to_lane(LaneProcess* lane, int vehicle_id);
int remove_vehicle_from_lane(LaneProcess* lane);
int remove_vehicle_from_lane_unlocked(LaneProcess* lane);  // --- NEW: Without locking ---
int get_lane_queue_length(LaneProcess* lane);

// State management functions
void update_lane_state(LaneProcess* lane, LaneState new_state);
int is_lane_ready(LaneProcess* lane);
int is_lane_blocked(LaneProcess* lane);

// Performance tracking functions
void update_lane_metrics(LaneProcess* lane);
float get_lane_average_wait_time(LaneProcess* lane);
int get_lane_throughput(LaneProcess* lane);

// Intersection resource management
void request_intersection_quadrants(LaneProcess* lane, int quadrants);
void release_intersection_quadrants(LaneProcess* lane);

// Utility functions
const char* get_lane_name(int lane_id);
void print_lane_info(LaneProcess* lane);

#endif // LANE_PROCESS_H
