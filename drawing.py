import matplotlib.pyplot as plt

def read_data(filename):
    """Read data from the file and return lists of jiffies, min_flt, and maj_flt."""
    jiffies, min_flt, maj_flt = [], [], []
    with open(filename, 'r') as file:
        for line in file:
            parts = line.split()
            if len(parts) == 4:
                jiffies.append(int(parts[0]))
                min_flt.append(int(parts[1]))
                maj_flt.append(int(parts[2]))
    return jiffies, min_flt, maj_flt

def plot_graph(jiffies, min_flt, maj_flt):
    """Plot the graph with jiffies on the x-axis and accumulated page fault count on the y-axis."""
    plt.figure(figsize=(10, 6))

    plt.plot(jiffies, min_flt, label='Minor Page Faults')
    plt.plot(jiffies, maj_flt, label='Major Page Faults')

    plt.xlabel('Jiffies')
    plt.ylabel('Accumulated Page Fault Count')
    plt.title('Accumulated Page Fault Count vs Time')
    plt.legend()
    plt.grid(True)

    plt.savefig('case_1_work_1_2.png')
    plt.show()

# Assuming the data file is named 'data.txt'
filename = 'profile1.data'
jiffies, min_flt, maj_flt = read_data(filename)
plot_graph(jiffies, min_flt, maj_flt)


# Case Study 2
# Recalculating CPU Utilizations with the correct normalization
import matplotlib.pyplot as plt

data = [
    {"N": 5, "CPU_time": 710921200, "start_jiffy": 4294755862, "end_jiffy": 4294776013},
    {"N": 11, "CPU_time": 1594917900, "start_jiffy": 4294692469, "end_jiffy": 4294712780},
    {"N": 16, "CPU_time": 3418859400, "start_jiffy": 4294712220, "end_jiffy": 4294733212},
    {"N": 20, "CPU_time": 3797369100, "start_jiffy": 4294679926, "end_jiffy": 4294700843},
    {"N": 22, "CPU_time": 5376555900, "start_jiffy": 4294705162, "end_jiffy": 4294727268}
]

# CPU properties
HZ = 1000
cpu_utilizations = []
finish_times = []

for d in data:
    cpu_time = d["CPU_time"] / 1e9  # Convert to seconds
    wall_time = (d["end_jiffy"] - d["start_jiffy"]) / HZ  # Convert jiffies to seconds
    cpu_utilization = cpu_time / wall_time if wall_time != 0 else 0
    cpu_utilizations.append(cpu_utilization)
    finish_times.append(wall_time)

N_values = [d["N"] for d in data]

# Plotting
fig, ax1 = plt.subplots(figsize=(10, 6))

color = 'tab:red'
ax1.set_xlabel('Number of Instances (N)')
ax1.set_ylabel('CPU Utilization', color=color)
ax1.plot(N_values, cpu_utilizations, marker='o', color=color)
ax1.tick_params(axis='y', labelcolor=color)

ax2 = ax1.twinx()  # instantiate a second axes that shares the same x-axis
color = 'tab:blue'
ax2.set_ylabel('Finish Time (s)', color=color)  # we already handled the x-label with ax1
ax2.plot(N_values, finish_times, marker='x', color=color)
ax2.tick_params(axis='y', labelcolor=color)

plt.title('CPU Utilization and Finish Time vs Degree of Multiprogramming')
fig.tight_layout()  # otherwise the right y-label is slightly clipped
plt.grid(True)
plt.show()
