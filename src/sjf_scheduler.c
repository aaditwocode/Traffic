#include "../include/scheduler.h"
#include "../include/lane_process.h"
#include "../include/trafficguru.h"
#include <stdlib.h>
#include <limits.h>
#include <float.h>

// Shortest Job First scheduling algorithm
// --- THIS FUNCTION HAS BEEN REWRITTEN TO BE THREAD-SAFE ---
int schedule_next_lane_sjf(Scheduler* scheduler, LaneProcess lanes[4]) {
    if (!scheduler || !lanes) {
        return -1;
    }

    int best_lane = -1;
    int min_estimated_time = INT_MAX;
    time_t earliest_arrival = time(NULL);

    // --- FIX: Read all lane data in a thread-safe way ---
    // We will store the data we need in local variables.
    LaneState state[4];
    int queue_length[4];
    time_t arrival_time[4];

    for (int i = 0; i < 4; i++) {
        // 1. Lock the lane
        pthread_mutex_lock(&lanes[i].queue_lock);
        
        // 2. Read all data
        state[i] = lanes[i].state;
        queue_length[i] = lanes[i].queue_length;
        arrival_time[i] = lanes[i].last_arrival_time;
        
        // 3. Unlock the lane
        pthread_mutex_unlock(&lanes[i].queue_lock);
    }
    // --- END FIX ---


    // Find lane with shortest estimated processing time
    // Now we operate *only* on our safe local copies.
    for (int i = 0; i < 4; i++) {
        // We use the local 'state[i]' variable
        if (state[i] == READY) { 
            // We use the local 'queue_length[i]' variable
            int estimated_time = queue_length[i] * VEHICLE_CROSS_TIME;

            // Select lane with minimum estimated time
            if (estimated_time < min_estimated_time) {
                min_estimated_time = estimated_time;
                best_lane = i;
                // We use the local 'arrival_time[i]' variable
                earliest_arrival = arrival_time[i];
            } else if (estimated_time == min_estimated_time) {
                // Tie breaker: use FIFO order (earliest arrival)
                if (arrival_time[i] < earliest_arrival) {
                    best_lane = i;
                    earliest_arrival = arrival_time[i];
                }
            }
        }
    }

    return best_lane;
}

// SJF variant: Preemptive Shortest Remaining Time First
// --- FIX: This function must also be thread-safe ---
int schedule_next_lane_srtf(Scheduler* scheduler, LaneProcess lanes[4]) {
    if (!scheduler || !lanes) {
        return -1;
    }

    int best_lane = -1;
    int min_remaining_time = INT_MAX;

    // --- FIX: Read data safely ---
    LaneState state[4];
    int queue_length[4];
    for (int i = 0; i < 4; i++) {
        pthread_mutex_lock(&lanes[i].queue_lock);
        state[i] = lanes[i].state;
        queue_length[i] = lanes[i].queue_length;
        pthread_mutex_unlock(&lanes[i].queue_lock);
    }
    // --- END FIX ---

    // Find lane with shortest remaining time
    for (int i = 0; i < 4; i++) {
        if (state[i] == READY) {
            int remaining_time = queue_length[i] * VEHICLE_CROSS_TIME;

            if (remaining_time < min_remaining_time) {
                min_remaining_time = remaining_time;
                best_lane = i;
            }
        }
    }

    return best_lane;
}

// SJF with aging to prevent starvation
// --- FIX: This function must also be thread-safe ---
int schedule_next_lane_sjf_with_aging(Scheduler* scheduler, LaneProcess lanes[4]) {
    if (!scheduler || !lanes) {
        return -1;
    }

    int best_lane = -1;
    float min_priority_score = FLT_MAX;
    
    // --- FIX: Read data safely ---
    LaneState state[4];
    int queue_length[4];
    int waiting_time[4];
    for (int i = 0; i < 4; i++) {
        pthread_mutex_lock(&lanes[i].queue_lock);
        state[i] = lanes[i].state;
        queue_length[i] = lanes[i].queue_length;
        waiting_time[i] = lanes[i].waiting_time; // Read waiting_time safely
        pthread_mutex_unlock(&lanes[i].queue_lock);
    }
    // --- END FIX ---

    for (int i = 0; i < 4; i++) {
        if (state[i] == READY) {
            // Calculate priority score: estimated_time - aging_factor
            float estimated_time = queue_length[i] * VEHICLE_CROSS_TIME;
            float aging_factor = waiting_time[i] * 0.1f; // 10% aging bonus
            float priority_score = estimated_time - aging_factor;

            if (priority_score < min_priority_score) {
                min_priority_score = priority_score;
                best_lane = i;
            }
        }
    }

    return best_lane;
}

// Enhanced SJF with multiple factors
// --- FIX: This function must also be thread-safe ---
int schedule_next_lane_enhanced_sjf(Scheduler* scheduler, LaneProcess lanes[4]) {
    if (!scheduler || !lanes) {
        return -1;
    }

    int best_lane = -1;
    float min_weighted_score = FLT_MAX;
    
    // --- FIX: Read data safely ---
    LaneState state[4];
    int queue_length[4];
    int waiting_time[4];
    float avg_wait[4];
    for (int i = 0; i < 4; i++) {
        pthread_mutex_lock(&lanes[i].queue_lock);
        state[i] = lanes[i].state;
        queue_length[i] = lanes[i].queue_length;
        waiting_time[i] = lanes[i].waiting_time;
        // We assume get_lane_average_wait_time is NOT thread-safe
        // and must be called inside the lock
        avg_wait[i] = get_lane_average_wait_time(&lanes[i]);
        pthread_mutex_unlock(&lanes[i].queue_lock);
    }
    // --- END FIX ---

    for (int i = 0; i < 4; i++) {
        if (state[i] == READY) {
            // Calculate weighted score considering multiple factors
            float processing_time = queue_length[i] * VEHICLE_CROSS_TIME;
            float waiting_bonus = waiting_time[i] * 0.2f; // 20% waiting bonus
            float fairness_penalty = avg_wait[i] * 0.1f;  // 10% fairness penalty

            float weighted_score = processing_time - waiting_bonus + fairness_penalty;

            if (weighted_score < min_weighted_score) {
                min_weighted_score = weighted_score;
                best_lane = i;
            }
        }
    }

    return best_lane;
}

// SJF with burst time prediction
// --- FIX: This function must also be thread-safe ---
int schedule_next_lane_predictive_sjf(Scheduler* scheduler, LaneProcess lanes[4]) {
    if (!scheduler || !lanes) {
        return -1;
    }

    int best_lane = -1;
    float min_predicted_time = FLT_MAX;

    // --- FIX: Read data safely ---
    LaneState state[4];
    int queue_length[4];
    int throughput[4];
    for (int i = 0; i < 4; i++) {
        pthread_mutex_lock(&lanes[i].queue_lock);
        state[i] = lanes[i].state;
        queue_length[i] = lanes[i].queue_length;
        throughput[i] = get_lane_throughput(&lanes[i]);
        pthread_mutex_unlock(&lanes[i].queue_lock);
    }
    // --- END FIX ---

    for (int i = 0; i < 4; i++) {
        if (state[i] == READY) {
            // Predict processing time based on historical throughput
            float avg_service_time = throughput[i] > 0 ? (60.0f / throughput[i]) : VEHICLE_CROSS_TIME;
            float predicted_time = queue_length[i] * avg_service_time;

            if (predicted_time < min_predicted_time) {
                min_predicted_time = predicted_time;
                best_lane = i;
            }
        }
    }

    return best_lane;
}
