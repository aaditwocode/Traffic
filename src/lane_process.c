#define _XOPEN_SOURCE 600
#include "../include/lane_process.h"
#include "../include/synchronization.h"
#include "../include/trafficguru.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <assert.h>

// Global lane names for display
static const char* lane_names[] = {"North", "South", "East", "West"};

// Initialize lane process
void init_lane_process(LaneProcess* lane, int lane_id, int max_capacity) {
    if (!lane || lane_id < 0 || lane_id >= 4 || max_capacity <= 0) {
        return;
    }

    lane->lane_id = lane_id;
    lane->queue = create_queue(max_capacity);
    lane->queue_length = 0;
    lane->max_queue_length = max_capacity;
    lane->state = WAITING;
    lane->priority = 2; // Normal priority
    lane->waiting_time = 0;
    lane->thread_id = 0;
    lane->last_arrival_time = time(NULL);
    lane->last_service_time = 0;
    lane->total_vehicles_served = 0;
    lane->total_waiting_time = 0;
    lane->requested_quadrants = 0;
    lane->allocated_quadrants = 0;

    // Initialize synchronization primitives
    pthread_mutex_init(&lane->queue_lock, NULL);
    pthread_cond_init(&lane->queue_cond, NULL);
}

// Destroy lane process and cleanup resources
void destroy_lane_process(LaneProcess* lane) {
    if (!lane) {
        return;
    }

    if (lane->queue) {
        destroy_queue(lane->queue);
        lane->queue = NULL;
    }

    pthread_mutex_destroy(&lane->queue_lock);
    pthread_cond_destroy(&lane->queue_cond);
}

// Lane process main thread function
void* lane_process_thread(void* arg) {
    LaneProcess* lane = (LaneProcess*)arg;
    if (!lane) {
        return NULL;
    }

    srand(time(NULL) ^ lane->lane_id); // Seed random generator

    while (true) {
        pthread_mutex_lock(&lane->queue_lock);

        // Generate random vehicle arrivals
        if (rand() % 10 == 0) { // 10% chance each iteration
            int vehicle_id = rand() % 1000;
            add_vehicle_to_lane(lane, vehicle_id);
        }

        // Update lane state based on queue and conditions
        if (lane->queue_length > 0 && lane->state == WAITING) {
            lane->state = READY;
        } else if (lane->queue_length == 0 && lane->state != RUNNING) {
            lane->state = WAITING;
        }

                // If lane is running, process multiple vehicles in batch
        if (lane->state == RUNNING) {
            // Process up to BATCH_EXIT_SIZE vehicles simultaneously
            int batch_count = 0;
            int max_batch = (lane->queue_length < BATCH_EXIT_SIZE) ? lane->queue_length : BATCH_EXIT_SIZE;
            
            for (int i = 0; i < max_batch; i++) {
                int vehicle_id = remove_vehicle_from_lane(lane);
                if (vehicle_id != -1) {
                    lane->total_vehicles_served++;
                    batch_count++;
                }
            }
            
            if (batch_count > 0) {
                lane->last_service_time = time(NULL);
                
                // Simulate vehicle crossing time for the batch
                pthread_mutex_unlock(&lane->queue_lock);
                usleep(VEHICLE_CROSS_TIME * 1000000 / 10); // Simulate batch crossing
                pthread_mutex_lock(&lane->queue_lock);
            }
        }
        // Update waiting time
        if (lane->state == READY || lane->state == WAITING) {
            lane->waiting_time++;
            lane->total_waiting_time++;
        }

        pthread_cond_signal(&lane->queue_cond);
        pthread_mutex_unlock(&lane->queue_lock);

        // Sleep before next iteration
        usleep(100000); // 100ms
    }

    return NULL;
}

// Add vehicle to lane queue
void add_vehicle_to_lane(LaneProcess* lane, int vehicle_id) {
    if (!lane || !lane->queue) {
        return;
    }

    pthread_mutex_lock(&lane->queue_lock);

    if (enqueue(lane->queue, vehicle_id)) {
        lane->queue_length = get_size(lane->queue);
        lane->last_arrival_time = time(NULL);
    }

    pthread_mutex_unlock(&lane->queue_lock);
}

// Remove vehicle from lane queue (with locking)
int remove_vehicle_from_lane(LaneProcess* lane) {
    if (!lane || !lane->queue) {
        return -1;
    }

    pthread_mutex_lock(&lane->queue_lock);

    int vehicle_id = dequeue(lane->queue);
    lane->queue_length = get_size(lane->queue);

    pthread_mutex_unlock(&lane->queue_lock);

    return vehicle_id;
}

// --- NEW FUNCTION: Remove vehicle WITHOUT locking ---
// For use when you already hold the lane->queue_lock
int remove_vehicle_from_lane_unlocked(LaneProcess* lane) {
    if (!lane || !lane->queue) {
        return -1;
    }

    // NOTE: Caller must hold lane->queue_lock
    int vehicle_id = dequeue(lane->queue);
    lane->queue_length = get_size(lane->queue);

    return vehicle_id;
}

// Get current queue length
int get_lane_queue_length(LaneProcess* lane) {
    if (!lane) {
        return 0;
    }

    pthread_mutex_lock(&lane->queue_lock);
    int length = lane->queue_length;
    pthread_mutex_unlock(&lane->queue_lock);

    return length;
}

// Update lane state
void update_lane_state(LaneProcess* lane, LaneState new_state) {
    if (!lane) {
        return;
    }

    pthread_mutex_lock(&lane->queue_lock);
    lane->state = new_state;
    pthread_cond_signal(&lane->queue_cond);
    pthread_mutex_unlock(&lane->queue_lock);
}

// Check if lane is ready for processing
int is_lane_ready(LaneProcess* lane) {
    if (!lane) {
        return 0;
    }

    pthread_mutex_lock(&lane->queue_lock);
    int ready = (lane->state == READY && lane->queue_length > 0);
    pthread_mutex_unlock(&lane->queue_lock);

    return ready;
}

// Check if lane is blocked
int is_lane_blocked(LaneProcess* lane) {
    if (!lane) {
        return 0;
    }

    pthread_mutex_lock(&lane->queue_lock);
    int blocked = (lane->state == BLOCKED);
    pthread_mutex_unlock(&lane->queue_lock);

    return blocked;
}

// Update lane performance metrics
void update_lane_metrics(LaneProcess* lane) {
    if (!lane) {
        return;
    }

    pthread_mutex_lock(&lane->queue_lock);

    // Reset waiting time periodically to prevent overflow
    if (lane->waiting_time > 1000) {
        lane->waiting_time = 0;
    }

    pthread_mutex_unlock(&lane->queue_lock);
}

// Get average waiting time for this lane
float get_lane_average_wait_time(LaneProcess* lane) {
    if (!lane || lane->total_vehicles_served == 0) {
        return 0.0f;
    }

    pthread_mutex_lock(&lane->queue_lock);
    float avg_wait = (float)lane->total_waiting_time / lane->total_vehicles_served;
    pthread_mutex_unlock(&lane->queue_lock);

    return avg_wait;
}

// Get lane throughput (vehicles per minute)
int get_lane_throughput(LaneProcess* lane) {
    if (!lane) {
        return 0;
    }

    pthread_mutex_lock(&lane->queue_lock);
    int throughput = lane->total_vehicles_served;
    pthread_mutex_unlock(&lane->queue_lock);

    return throughput;
}

// Request intersection quadrants for this lane
void request_intersection_quadrants(LaneProcess* lane, int quadrants) {
    if (!lane) {
        return;
    }

    pthread_mutex_lock(&lane->queue_lock);
    lane->requested_quadrants = quadrants;
    pthread_mutex_unlock(&lane->queue_lock);
}

// Release intersection quadrants
void release_intersection_quadrants(LaneProcess* lane) {
    if (!lane) {
        return;
    }

    pthread_mutex_lock(&lane->queue_lock);
    lane->allocated_quadrants = 0;
    lane->requested_quadrants = 0;
    pthread_mutex_unlock(&lane->queue_lock);
}

// Get lane name
const char* get_lane_name(int lane_id) {
    if (lane_id >= 0 && lane_id < 4) {
        return lane_names[lane_id];
    }
    return "Unknown";
}

// Print lane information for debugging
void print_lane_info(LaneProcess* lane) {
    if (!lane) {
        printf("Lane: NULL\n");
        return;
    }

    pthread_mutex_lock(&lane->queue_lock);

    printf("Lane %d (%s):\n", lane->lane_id, get_lane_name(lane->lane_id));
    printf("  State: %d\n", lane->state);
    printf("  Queue Length: %d/%d\n", lane->queue_length, lane->max_queue_length);
    printf("  Priority: %d\n", lane->priority);
    printf("  Waiting Time: %d\n", lane->waiting_time);
    printf("  Total Served: %d\n", lane->total_vehicles_served);
    printf("  Requested Quadrants: %d\n", lane->requested_quadrants);
    printf("  Allocated Quadrants: %d\n", lane->allocated_quadrants);

    pthread_mutex_unlock(&lane->queue_lock);
}
