# SPDX-FileCopyrightText: 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0
import subprocess
import matplotlib.pyplot as plt
from datetime import datetime
import sys
import os

# --- Configuration ---
# The directory to analyze, relative to the repository root.
TARGET_DIR = "./device"
# The branch to analyze (usually 'main' or 'master', but 'HEAD' works for the current branch)
BRANCH = "HEAD"
# Git log filtering: checks commits from the last year and a half.
SINCE_DATE = "18 months ago"
# Output filename for the plot (updated in plotting function)
OUTPUT_FILE = "nloc_history.png"
# Commands to extract metrics from the lizard summary line
# NLOC is the first field ($1), CCN is the third field ($3)
LIZARD_NLOC_COMMAND = f"lizard {TARGET_DIR} | tail -n 1 | awk '{{print $1}}'"
LIZARD_CCN_COMMAND = f"lizard {TARGET_DIR} | tail -n 1 | awk '{{print $3}}'"
# ---------------------

def run_command(command, shell=True, check=True):
    """Utility function to run a subprocess command and handle potential errors."""
    try:
        result = subprocess.run(
            command,
            shell=shell,
            check=check,
            capture_output=True,
            text=True,
            encoding='utf-8'
        )
        return result.stdout.strip()
    except subprocess.CalledProcessError as e:
        print(f"Error executing command: '{command}'")
        print(f"Stderr: {e.stderr.strip()}")
        print("Please ensure 'lizard', 'tail', and 'awk' are installed and on your PATH.")
        # Exit gracefully if a critical command (like git) fails
        if check:
            sys.exit(1)
        return None
    except FileNotFoundError:
        print("Error: Required external commands ('git', 'lizard', 'awk', 'tail') were not found.")
        sys.exit(1)

def get_commit_list():
    """Fetches commit hashes and dates for the last year and a half on the target branch."""
    print(f"Fetching commits for '{BRANCH}' since '{SINCE_DATE}'...")
    # Get hash and date (timestamp) for commits
    # %H: hash, %ct: committer date (unix timestamp)
    log_command = [
        "git", "log", BRANCH,
        f"--since='{SINCE_DATE}'",
        "--date-order",
        "--pretty=format:%H,%ct"
    ]
    # We use run_command with shell=False here since it's a simple git command
    output = run_command(log_command, shell=False)

    commits = []
    for line in output.split('\n'):
        if line:
            hash, timestamp = line.split(',')
            # Convert timestamp to a datetime object for plotting
            date_obj = datetime.fromtimestamp(int(timestamp))
            commits.append({'hash': hash, 'date': date_obj})
    
    # Analyze older commits first for a chronological plot
    commits.reverse()
    return commits

def analyze_history():
    """Iterates through commits, runs lizard, and collects NLOC and CCN data."""
    commits = get_commit_list()
    if not commits:
        print("No commits found in the last year and a half on the current branch. Exiting.")
        return [], [], [] # dates, nloc_values, ccn_values

    print(f"Found {len(commits)} commits to analyze in '{TARGET_DIR}'.")
    
    # Store data
    nloc_values = []
    ccn_values = []
    dates = []
    
    # Store the current branch/commit before starting the loop
    original_state = run_command("git rev-parse --abbrev-ref HEAD", check=False) or "HEAD"

    try:
        for i, commit in enumerate(commits):
            hash = commit['hash']
            date = commit['date']
            print(f"[{i+1}/{len(commits)}] Checking out {hash[:7]} (Date: {date.strftime('%Y-%m-%d %H:%M')})...", end='', flush=True)

            # 3. Checkout the commit quietly. 
            # Added '--recurse-submodules=no' to prevent submodule-related errors during historical checkout.
            run_command(f"git checkout -q {hash} --recurse-submodules=no", check=True)

            # 4. Run the lizard commands to get both metrics
            nloc_str = run_command(LIZARD_NLOC_COMMAND, shell=True, check=False)
            ccn_str = run_command(LIZARD_CCN_COMMAND, shell=True, check=False)
            
            try:
                # 5. Store the data
                nloc = float(nloc_str)
                ccn = float(ccn_str)
                
                nloc_values.append(nloc)
                ccn_values.append(ccn)
                dates.append(date)
                print(f" -> NLOC: {nloc}, CCN: {ccn}")
            except (ValueError, TypeError):
                print(f" -> FAILED. One or more metrics were not a float or empty. Skipping commit.")

    finally:
        # 6. Reset the repository to the original state
        print("\nResetting to original branch/commit...")
        run_command(f"git checkout -q {original_state}", check=False)
        run_command("git reset --hard", check=False) # Ensure working directory is clean
        print(f"Returned to {original_state}.")

    return dates, nloc_values, ccn_values

def plot_metric(dates, values, metric_name, output_file, unit_label):
    """Uses matplotlib to plot a single metric over time and save the plot."""
    if not dates:
        print(f"No {metric_name} data collected to plot.")
        return

    print(f"Generating plot for {metric_name} and saving to {output_file}...")
    
    plt.style.use('seaborn-v0_8-darkgrid')
    
    # Create the plot
    fig, ax = plt.subplots(figsize=(12, 6))

    # Plot the values against the commit dates
    ax.plot(dates, values, marker='o', linestyle='-', color='#1f77b4', linewidth=2, markersize=4, label=f'Total {metric_name}')

    # Add trend line
    import numpy as np
    x_numeric = np.array([date.timestamp() for date in dates])
    y_values = np.array(values)
    # Calculate best fit line (linear regression)
    z = np.polyfit(x_numeric, y_values, 1)
    p = np.poly1d(z)
    ax.plot(dates, p(x_numeric), "r--", label=f'Trend Line (Slope: {z[0]:.4f})')

    # Add labels and title
    ax.set_title(f"Total {metric_name} Trend for '{TARGET_DIR}' over the Last 1.5 Years ({BRANCH})", fontsize=16)
    ax.set_xlabel("Commit Date and Time", fontsize=12)
    ax.set_ylabel(unit_label, fontsize=12)

    # Format the x-axis for dates and times
    # This automatically shows a clearer date and time format including the time component.
    fig.autofmt_xdate(rotation=45, ha='right')
    
    # Add CCN specific line if needed
    if metric_name == "CCN":
        ax.axhline(y=10, color='orange', linestyle=':', label='Common Warning Threshold (CCN=10)')
        

    # Legend
    ax.legend()

    # Save the plot
    plt.savefig(output_file)
    print(f"Successfully saved {metric_name} chart to '{output_file}'.")
    


if __name__ == "__main__":
    # Ensure matplotlib is available
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("Error: 'matplotlib' is required but not installed.")
        print("Please install it using: pip install matplotlib")
        sys.exit(1)

    dates, nloc_values, ccn_values = analyze_history()
    
    # Plot 1: Total NLOC (Non-Commented Lines of Code)
    plot_metric(dates, nloc_values, "NLOC", "nloc_history.png", "Total Non-Commented Lines of Code (NLOC)")
    
    # Plot 2: Average CCN (Average Cyclomatic Complexity Number)
    plot_metric(dates, ccn_values, "CCN", "ccn_history.png", "Average Cyclomatic Complexity Number (CCN)")
