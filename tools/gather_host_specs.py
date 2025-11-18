# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import os
import json
import platform
import socket
import psutil
import pandas as pd
import argparse
import sys
import subprocess
from datetime import datetime

# --- Helper Functions (Manual Gathering) ---

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
    # Handle tt-smi's int output
    if isinstance(speed_str, int):
        return speed_str
    # Handle tt-smi's "N/A" output
    if speed_str == "N/A":
        return 0
    # Handle manual sysfs output
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

# --- Main Logic ---

def gather_specs_manual():
    """Aggregates all system info using manual sysfs/psutil methods."""
    print("INFO: 'tt-smi' not found or failed. Using manual fallback.", file=sys.stderr)
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

def gather_specs_from_tt_smi(smi_json):
    """Parses the JSON output from 'tt-smi -s' and enriches it."""
    
    # 1. Get host_info from tt-smi
    host_info = smi_json.get("host_info", {})
    
    # 2. Enrich host_info with data tt-smi misses
    io_settings = get_io_perf_settings()
    host_info["Hugepages"] = io_settings.get("hugepages", "N/A")
    host_info["CPU_Governor"] = io_settings.get("cpu_governor", "N/A")
    host_info["CPU_Cores_Phys_Log"] = f"{psutil.cpu_count(logical=False)}/{psutil.cpu_count(logical=True)}"
    
    # 3. Parse device_info to create tt_pcie_lane_info
    tt_pcie_lane_info = []
    device_list = smi_json.get("device_info", [])
    for device in device_list:
        board_info = device.get("board_info", {})
        
        # --- FIX: Filter out devices with no local bus_id ---
        if board_info.get("bus_id", "N/A") == "N/A":
            continue
            
        pcie_speed_raw = board_info.get("pcie_speed", 0)
        
        config = {
            "pcie_width": board_info.get("pcie_width", "N/A"),
            # Pass speed to get_pcie_gen to handle "N/A" or int
            "pcie_speed": get_pcie_gen(pcie_speed_raw) 
        }
        tt_pcie_lane_info.append(config)
        
    # 4. Assemble final data structure
    data = {
        "time": smi_json.get("time", datetime.now().isoformat()),
        "host_info": host_info,
        "tt_pcie_lane_info": tt_pcie_lane_info
    }
    return data

def gather_specs():
    """
    Dispatcher function. Tries tt-smi first, then falls back to manual.
    """
    try:
        # Try to run tt-smi -s
        result = subprocess.run(
            ["tt-smi", "-s"],
            capture_output=True, 
            text=True, 
            check=True,
            encoding='utf-8'
        )
        
        # Find the start of the JSON (tt-smi prints "Gathering Information...")
        json_output = result.stdout
        json_start_index = json_output.find('{')
        if json_start_index == -1:
            # If no JSON found, fall back
            return gather_specs_manual()
            
        json_output = json_output[json_start_index:]
        smi_data = json.loads(json_output)
        
        # If JSON is valid, parse and enrich it
        return gather_specs_from_tt_smi(smi_data)
        
    except (FileNotFoundError, subprocess.CalledProcessError, json.JSONDecodeError):
        # If tt-smi not found, fails, or gives bad JSON, fall back
        return gather_specs_manual()

def export_data(data, base_filename, do_json=False, do_csv=False, do_md=False):
    """Exports the data dictionary to specified file formats."""
    files_created = []

    # --- 1. JSON Export ---
    if do_json:
        try:
            with open(f"{base_filename}.json", "w") as f:
                json.dump(data, f, indent=4)
            files_created.append(f"{base_filename}.json")
        except Exception as e:
            print(f"Error writing JSON: {e}", file=sys.stderr)

    # --- Flatten data for CSV/MD ---
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

    # --- 2. CSV Export ---
    if do_csv:
        try:
            df.to_csv(f"{base_filename}.csv", index=False)
            files_created.append(f"{base_filename}.csv")
        except Exception as e:
            print(f"Error writing CSV: {e}", file=sys.stderr)

    # --- 3. Markdown Export ---
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

def main():
    parser = argparse.ArgumentParser(
        description="Gather machine specifications. Default (no args) prints JSON to stdout."
    )
    parser.add_argument(
        '-o', '--output', 
        type=str, 
        default=None, 
        help="Base filename for output files. If not set, flags use 'machine_host_spec'."
    )
    parser.add_argument('--json', action='store_true', help="Output to JSON file.")
    parser.add_argument('--csv', action='store_true', help="Output to CSV file.")
    parser.add_argument('--md', action='store_true', help="Output to Markdown file.")
    parser.add_argument('--all', action='store_true', help="Output to all formats (JSON, CSV, MD).")
    
    args = parser.parse_args()
    
    # 1. Always gather the specs (dispatcher decides source)
    specs = gather_specs()
    
    # 2. Determine filenames and formats
    base_filename = args.output
    default_filename = 'machine_host_spec'
    
    do_json = args.json or args.all
    do_csv = args.csv or args.all
    do_md = args.md or args.all
    any_format_flag_set = do_json or do_csv or do_md

    # --- 3. Execute logic based on your rules ---
    
    if base_filename is None and not any_format_flag_set:
        # Rule 1: No flags. Print JSON to stdout.
        print(json.dumps(specs, indent=4))
    
    else:
        # Rules 2, 3, & 4: We are writing to file(s).
        
        if base_filename is None:
            # Rule 2: Format flags given, but no name. Use default name.
            base_filename = default_filename
        
        if not any_format_flag_set:
            # Rule 3: Name given, but no format. Default to JSON.
            do_json = True
        
        # Rule 4 (Name + flags) is handled by the initial flag setup.
        
        export_data(specs, base_filename, do_json, do_csv, do_md)

if __name__ == "__main__":
    main()
