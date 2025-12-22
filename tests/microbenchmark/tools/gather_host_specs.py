# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""
This script gathers machine specification information.

Information is gathered manually through sysfs, /proc, and psutil methods.
This avoids any dependency on external tools.
"""

import os
import json
import platform
import socket
import psutil
import pandas as pd
import argparse
import sys
from datetime import datetime

# Helper Functions (Manual Gathering)

def get_distro_pretty_name():
    """Returns the pretty name (e.g., 'Ubuntu 22.04.5 LTS')"""
    try:
        if hasattr(platform, 'freedesktop_os_release'):
            return platform.freedesktop_os_release().get('PRETTY_NAME', 'Linux Unknown')
        with open("/etc/os-release") as f:
            for line in f:
                if line.startswith("PRETTY_NAME="):
                    return line.split("=", 1)[1].strip().strip('"')
    except Exception:
        return "Linux Unknown"
    return "Linux Unknown"

def get_tt_driver_version():
    """Checks /sys/module for Tenstorrent driver version"""
    paths = ["/sys/module/tenstorrent/version", "/sys/module/tt_kmd/version"]
    for path in paths:
        if os.path.exists(path):
            try:
                with open(path, "r") as f:
                    return f"TT-KMD {f.read().strip()}"
            except: pass
    return "TT-KMD Unknown (Not Loaded)"

def get_io_perf_settings():
    """Gathers Hugepages and CPU Governor (critical for IO/Perf)"""
    settings = {"hugepages": "N/A", "cpu_governor": "N/A"}
    if platform.system() == "Linux":
        try:
            with open('/proc/sys/vm/nr_hugepages', 'r') as f:
                settings['hugepages'] = f.read().strip()
        except: pass
        try:
            with open('/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor', 'r') as f:
                settings['cpu_governor'] = f.read().strip()
        except: pass
    return settings

def get_pcie_gen(speed_str):
    """Converts Linux speed string (e.g. '16.0 GT/s') to PCIe Gen integer."""
    # Handle integer input
    if isinstance(speed_str, int):
        return speed_str
    # Handle "N/A" output
    if speed_str == "N/A":
        return 0
    # Handle sysfs output
    if not speed_str or "GT/s" not in speed_str: return 0
    try:
        speed_val = float(speed_str.split()[0])
        if speed_val >= 64.0: return 6 # Gen 6
        if speed_val >= 32.0: return 5 # Gen 5
        if speed_val >= 16.0: return 4 # Gen 4
        if speed_val >= 8.0:  return 3 # Gen 3
        if speed_val >= 5.0:  return 2 # Gen 2
        if speed_val >= 2.5:  return 1 # Gen 1
    except: pass
    return 0

def get_tenstorrent_pcie_info_manual():
    """
    MANUAL FALLBACK: Finds ALL *usable* Tenstorrent cards by checking
    /dev/tenstorrent and mapping them to /sys/class/tenstorrent.
    """
    dev_path = "/dev/tenstorrent"
    class_path = "/sys/class/tenstorrent"
    results = []

    # 1. Find all minor numbers of usable devices in /dev/tenstorrent
    if not os.path.exists(dev_path):
        print(f"INFO: {dev_path} not found.", file=sys.stderr)
        return results
    
    usable_minor_nums = set()
    try:
        for dev_name in os.listdir(dev_path):
            try:
                # Get stats on the character device file (e.g., /dev/tenstorrent/3)
                # os.rdev gives the device number (major+minor)
                device_stat = os.stat(os.path.join(dev_path, dev_name))
                minor_num = os.minor(device_stat.st_rdev)
                usable_minor_nums.add(str(minor_num))
            except Exception as e:
                print(f"INFO: Could not stat {os.path.join(dev_path, dev_name)}: {e}", file=sys.stderr)
    
    except Exception as e:
        print(f"INFO: Could not list devices in {dev_path}: {e}", file=sys.stderr)
        return results # Exit if we can't even list the dir

    if not usable_minor_nums:
        print(f"INFO: {dev_path} is empty.", file=sys.stderr)
        return results

    # 2. Scan /sys/class/tenstorrent to find matching devices
    if not os.path.exists(class_path):
        print(f"INFO: {class_path} not found.", file=sys.stderr)
        return results
        
    try:
        # Iterate /sys/class/tenstorrent/tenstorrent!0, tenstorrent!1, etc.
        for device_dir in os.listdir(class_path):
            dev_class_entry = os.path.join(class_path, device_dir)
            
            try:
                # 3. Read the dev file to get MAJOR:MINOR
                dev_file_path = os.path.join(dev_class_entry, "dev")
                if not os.path.exists(dev_file_path):
                    continue
                
                with open(dev_file_path, "r") as f:
                    major_minor = f.read().strip() # e.g., "241:3"
                
                minor_num = major_minor.split(':')[-1]

                # 4. Check if this device is in our usable list
                if minor_num in usable_minor_nums:
                    # 5. This is a usable device. Get its PCIe info.
                    pci_device_path = os.path.realpath(os.path.join(dev_class_entry, "device"))
                    config = {"pcie_width": "N/A", "pcie_speed": 0}

                    width_path = os.path.join(pci_device_path, "current_link_width")
                    if os.path.exists(width_path):
                        with open(width_path, "r") as wf:
                            config["pcie_width"] = wf.read().strip()
                    
                    speed_path = os.path.join(pci_device_path, "current_link_speed")
                    if os.path.exists(speed_path):
                        with open(speed_path, "r") as sf:
                            raw_speed = sf.read().strip()
                            config["pcie_speed"] = get_pcie_gen(raw_speed)
                    
                    if config["pcie_speed"] > 0 and config["pcie_width"] != "N/A":
                        results.append(config)

            except Exception as e:
                print(f"INFO: Error processing {dev_class_entry}: {e}", file=sys.stderr)
                continue
            
    except Exception as e:
        print(f"INFO: Error during manual PCIe scan: {e}", file=sys.stderr)
        pass # Fallback to empty list
        
    return results

# Main Logic

def gather_specs():
    """Aggregates all system info using manual sysfs/psutil methods."""
    io_settings = get_io_perf_settings()
    mem_gb = round(psutil.virtual_memory().total / (1024**3), 2)
    pcie_info = get_tenstorrent_pcie_info_manual()

    data = {
        "time": datetime.now().isoformat(),
        "host_info": {
            "OS": platform.system(),
            "Distro": get_distro_pretty_name(),
            "Kernel": platform.release(),
            "Hostname": socket.gethostname(),
            "Platform": platform.machine(),
            "Python": platform.python_version(),
            "Memory": f"{mem_gb} GB",
            "Driver": get_tt_driver_version(),
            "Hugepages": io_settings['hugepages'],
            "CPU_Governor": io_settings['cpu_governor'],
            "CPU_Cores_Phys_Log": f"{psutil.cpu_count(logical=False)}/{psutil.cpu_count(logical=True)}",
        },
        "tt_pcie_lane_info": pcie_info
    }
    return data

def export_data(data, base_filename, do_json=False, do_csv=False, do_md=False):
    """Exports the data dictionary to specified file formats."""
    files_created = []

    # 1. JSON Export
    if do_json:
        try:
            with open(f"{base_filename}.json", "w") as f:
                json.dump(data, f, indent=4)
            files_created.append(f"{base_filename}.json")
        except Exception as e:
            print(f"Error writing JSON: {e}", file=sys.stderr)

    # If the selected formt is not JSON, flatten data for CSV/MD
    if do_csv or do_md:
        flat_data = {"Timestamp": data["time"]}
        flat_data.update(data["host_info"]) 
        
        # Create a summary string for the PCIe info
        pcie_list = data.get("tt_pcie_lane_info", [])
        if pcie_list:
            # Create summary: "x16 Gen4, x16 Gen4"
            summary = ", ".join([f"x{c.get('pcie_width', '?')} Gen{c.get('pcie_speed', '?')}" for c in pcie_list])
            flat_data["TT_PCIe_Info"] = summary
        else:
            flat_data["TT_PCIe_Info"] = "No Tenstorrent Card Found"
        
        df = pd.DataFrame([flat_data])

    # 2. CSV Export
    if do_csv:
        try:
            df.to_csv(f"{base_filename}.csv", index=False)
            files_created.append(f"{base_filename}.csv")
        except Exception as e:
            print(f"Error writing CSV: {e}", file=sys.stderr)

    # 3. Markdown Export
    if do_md:
        try:
            md_table = df.T.reset_index().to_markdown(
                index=False, 
                headers=["Metric", "Value"], 
                tablefmt="github"
            )
            with open(f"{base_filename}.md", "w") as f:
                f.write("# Machine Specification Report\n\n")
                f.write(md_table)
            files_created.append(f"{base_filename}.md")
        except Exception as e:
            print(f"Error writing MD: {e}", file=sys.stderr)
    
    if files_created:
        print(f"Successfully created: {', '.join(files_created)}")

def format_data_as(data, fmt):
    """Return a string representation of data in the requested format."""
    if fmt == 'json':
        return json.dumps(data, indent=4)
    
    # Flatten for CSV/MD
    flat_data = {"Timestamp": data["time"]}
    flat_data.update(data["host_info"])
    pcie_list = data.get("tt_pcie_lane_info", [])
    if pcie_list:
        summary = ", ".join([f"x{c.get('pcie_width', '?')} Gen{c.get('pcie_speed', '?')}" for c in pcie_list])
        flat_data["TT_PCIe_Info"] = summary
    else:
        flat_data["TT_PCIe_Info"] = "No Tenstorrent Card Found"
    df = pd.DataFrame([flat_data])
    
    if fmt == 'csv':
        return df.to_csv(index=False)
    if fmt == 'md':
        return df.T.reset_index().to_markdown(
            index=False,
            headers=["Metric", "Value"],
            tablefmt="github"
        )
    # Fallback to JSON if unknown
    return json.dumps(data, indent=4)

def main():
    parser = argparse.ArgumentParser(
        description="Gather machine specifications. Default (no args) writes JSON to 'machine_host_spec.json'."
    )
    parser.add_argument(
        '-o', '--output', 
        type=str, 
        default=None, 
        help="Base filename for output files. If not set, defaults to 'machine_host_spec' unless --no-file is used."
    )
    parser.add_argument('--json', action='store_true', help="Output to JSON file.")
    parser.add_argument('--csv', action='store_true', help="Output to CSV file.")
    parser.add_argument('--md', action='store_true', help="Output to Markdown file.")
    parser.add_argument('--all', action='store_true', help="Output to all formats (JSON, CSV, MD).")
    parser.add_argument('--no-file', action='store_true', help="Do not write files; print to stdout. Defaults to JSON unless a format is specified. Incompatible with --output/-o.")
    
    args = parser.parse_args()
    
    # 1. Gather the specs using manual methods
    specs = gather_specs()
    
    # 2. Determine filenames and formats
    base_filename = args.output
    default_filename = 'machine_host_spec'
    
    do_json = args.json or args.all
    do_csv = args.csv or args.all
    do_md = args.md or args.all

    # 3. Validate incompatible options
    if args.no_file and base_filename is not None:
        print("Error: --no-file cannot be used together with --output/-o.", file=sys.stderr)
        sys.exit(2)

    # 4. Determine selected formats
    selected_formats = []
    if do_json: selected_formats.append('json')
    if do_csv: selected_formats.append('csv')
    if do_md: selected_formats.append('md')
    if not selected_formats:
        selected_formats = ['json']

    # 5. Handle stdout-only mode
    if args.no_file:
        if len(selected_formats) > 1:
            print("Error: With --no-file, specify only one of --json, --csv, or --md.", file=sys.stderr)
            sys.exit(2)
        fmt = selected_formats[0]
        print(format_data_as(specs, fmt), end='' if fmt == 'csv' else '\n')
        return

    # 6. File output mode (use default filename if none provided)
    if base_filename is None:
        base_filename = default_filename
    export_data(
        specs,
        base_filename,
        do_json='json' in selected_formats,
        do_csv='csv' in selected_formats,
        do_md='md' in selected_formats
    )

if __name__ == "__main__":
    main()
