# TrafficGuru: OS-Inspired Traffic Management System

TrafficGuru is a sophisticated traffic intersection management system that applies operating systems concepts including scheduling algorithms, synchronization mechanisms, and deadlock avoidance techniques to optimize traffic flow.

## Features

### Core Traffic Management
- **Process-based Lane Modeling**: Each traffic lane functions as a process maintaining a dynamic queue
- **Central Scheduler**: Coordinates intersection access using OS scheduling algorithms
- **Dynamic Memory Management**: Realistic simulation of fluctuating traffic conditions

### Scheduling Algorithms
1. **Shortest Job First (SJF)**: Prioritizes lanes with shortest estimated processing time
2. **Multilevel Feedback Queue**: Dynamic priority adjustment with aging to prevent starvation
3. **Priority Round Robin**: Combines priority scheduling with time slicing, including emergency vehicle preemption

### Synchronization & Deadlock Prevention
- **Mutual Exclusion**: Strict intersection access control using mutexes and condition variables
- **Banker's Algorithm**: Prevents traffic gridlock using OS deadlock avoidance techniques
- **Resource Allocation**: Safe quadrant allocation for turning movements

### Emergency Vehicle System
- **Automatic Detection**: Random emergency vehicle generation with configurable probability
- **Priority Preemption**: Immediate intersection clearing for emergency vehicles
- **Response Time Tracking**: Performance metrics for emergency response

### Real-time Visualization
- **ncurses Interface**: Terminal-based UI with color support
- **Live Signal Sequences**: Historical display of signal changes
- **Gantt Charts**: Timeline visualization of lane execution
- **Performance Dashboard**: Real-time metrics and system status

## Quick Start

### Prerequisites
- GCC compiler
- ncurses library
- pthread library
- Make utility

### Installation (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install build-essential libncurses5-dev libncursesw5-dev
```

### Building and Running
```bash
# Clone or download the project
cd TrafficGuru

# Build the project
make

# Run with default settings
make run

# Or run directly
./bin/trafficguru
```

### Command Line Options
```bash
# Set simulation duration (seconds)
./bin/trafficguru -d 120

# Choose scheduling algorithm
./bin/trafficguru -g multilevel

# Enable debug mode
./bin/trafficguru --debug

# Run 60-second benchmark
./bin/trafficguru --benchmark

# Show help
./bin/trafficguru --help
```

## Interactive Controls

During simulation:
- **1-3**: Switch scheduling algorithms
- **SPACE**: Pause/Resume simulation
- **e**: Trigger emergency vehicle
- **r**: Reset simulation
- **h**: Show help screen
- **q**: Quit simulation

## System Architecture

```
TrafficGuru/
├── src/                    # Source code
│   ├── main.c             # Main program entry
│   ├── lane_process.c     # Lane process modeling
│   ├── queue.c            # Dynamic queue management
│   ├── scheduler.c        # Central scheduler framework
│   ├── sjf_scheduler.c    # Shortest Job First algorithm
│   ├── multilevel_scheduler.c  # Multilevel Feedback Queue
│   ├── priority_rr_scheduler.c # Priority Round Robin
│   ├── synchronization.c  # Mutex and condition variables
│   ├── bankers_algorithm.c    # Deadlock prevention
│   ├── traffic_mutex.c   # Enhanced traffic control
│   ├── emergency_system.c      # Emergency vehicle handling
│   └── ...
├── include/               # Header files
├── tests/                 # Unit tests
├── docs/                  # Documentation
└── Makefile              # Build system
```

## Scheduling Algorithms

### Shortest Job First (SJF)
- Estimates processing time as `queue_length * average_vehicle_cross_time`
- Non-preemptive: Once lane gets green light, complete full cycle
- Breaks ties using FIFO order

### Multilevel Feedback Queue
- Three priority levels: HIGH, MEDIUM, LOW
- Dynamic priority adjustment based on waiting time and consecutive runs
- Aging mechanism prevents starvation
- Time quantum varies by priority (2s, 4s, 6s)

### Priority Round Robin
- Priority 1: Emergency vehicles (highest)
- Priority 2: Normal lanes with > 3 vehicles
- Priority 3: Normal lanes with ≤ 3 vehicles
- 3-second time quantum for all priorities
- Priority inheritance for emergency vehicles

## Deadlock Prevention

The system uses the Banker's Algorithm to prevent traffic gridlock:

1. **Resource Modeling**: Intersection quadrants as resources, lanes as processes
2. **Safety Checks**: Verifies system remains in safe state before allocation
3. **Deadlock Detection**: Monitors for circular wait conditions
4. **Resolution Strategies**: Multiple approaches including resource preemption

## Performance Metrics

- **Throughput**: Vehicles processed per minute
- **Average Wait Time**: Mean waiting time across lanes
- **Intersection Utilization**: Percentage of active intersection time
- **Fairness Index**: Jain's fairness index for lane equality
- **Emergency Response Time**: Time to grant emergency vehicle access
- **Context Switch Overhead**: Scheduling change costs
- **Deadlocks Prevented**: Count of averted gridlocks

## Development

### Build Targets
```bash
make help              # Show all available targets
make debug            # Build with debug symbols
make test             # Build and run tests
make clean            # Remove build artifacts
make docs             # Generate documentation
make analyze          # Run static analysis
make memcheck         # Memory leak check (valgrind)
make benchmark        # Performance benchmark
```

### Code Quality
- Wall of compiler warnings enabled
- Static analysis with cppcheck
- Memory leak detection with valgrind
- Thread safety verification with helgrind
- Code formatting with clang-format

## Configuration

### Default Settings
- Simulation duration: 300 seconds (5 minutes)
- Vehicle arrival rate: 1-5 seconds
- Time quantum: 3 seconds
- Queue capacity: 20 vehicles per lane
- Emergency probability: 1 in 200 checks

### Customization
System parameters can be adjusted through:
- Command line arguments
- Configuration constants in `trafficguru.h`
- Runtime interactive controls

## Algorithm Comparison

The system provides built-in comparative analysis:
- Runs identical traffic patterns across all algorithms
- Generates performance comparison reports
- Identifies optimal algorithms for different scenarios
- Tracks fairness and efficiency metrics

## Future Enhancements

### AI-Driven Scheduling
- Traffic pattern prediction using historical data
- Reinforcement learning for adaptive scheduling
- Neural network-based optimization

### Smart City Integration
- Real-time traffic data feeds
- Connected vehicle communication
- Multi-intersection coordination
- Cloud-based analytics

### Advanced Features
- Multi-intersection networks
- Vehicle-type awareness
- Environmental considerations
- Mobile interface

## Troubleshooting

### Common Issues

**Build Errors**:
```bash
# Ensure dependencies are installed
make install-dependencies

# Check compiler
gcc --version
```

**Runtime Errors**:
```bash
# Check terminal compatibility
echo $TERM

# Ensure ncurses support
tic -x ncurses 2>/dev/null
```

**Performance Issues**:
```bash
# Run with debug mode
./bin/trafficguru --debug

# Check system resources
top -p $(pgrep trafficguru)
```

## Contributing

1. Fork the repository
2. Create feature branch (`git checkout -b feature/amazing-feature`)
3. Commit changes (`git commit -m 'Add amazing feature'`)
4. Push to branch (`git push origin feature/amazing-feature`)
5. Open Pull Request

### Code Standards
- Follow C99 standard
- Use consistent naming conventions
- Add comprehensive comments
- Include unit tests for new features
- Update documentation

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Acknowledgments

- Operating Systems concepts for scheduling and synchronization
- Banker's Algorithm for deadlock prevention
- ncurses library for terminal interface
- Open source community for inspiration and tools

## Contact

For questions, suggestions, or issues:
- Create an issue on GitHub
- Email: [project-email]
- Documentation: [docs-link]