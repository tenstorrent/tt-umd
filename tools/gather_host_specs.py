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
from datetime import datetime

# --- Helper Functions (Gathering Info) ---

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
    if not speed_str or "GT/s" not in speed_str: return 0
    try:
        # Extract the number (e.g., "16.0" -> 16.0)
        speed_val = float(speed_str.split()[0])
        if speed_val >= 64.0: return 6 # Gen 6
        if speed_val >= 32.0: return 5 # Gen 5
        if speed_val >= 16.0: return 4 # Gen 4
        if speed_val >= 8.0:  return 3 # Gen 3
        if speed_val >= 5.0:  return 2 # Gen 2
        if speed_val >= 2.5:  return 1 # Gen 1
    except: pass
    return 0

def get_tenstorrent_pcie_info():
    """
    Finds ALL Tenstorrent cards and returns a list of config dicts.
    """
    # Includes common Tenstorrent Vendor IDs (0x1e52, 0x1e24)
    tt_vendor_ids = {"0x1e52", "0x1e24"} 
    pci_root = "/sys/bus/pci/devices"
    
    results = []

    if not os.path.exists(pci_root):
        return results

    try:
        for device_id in os.listdir(pci_root):
            dev_path = os.path.join(pci_root, device_id)
            vendor_file = os.path.join(dev_path, "vendor")
            
            if os.path.exists(vendor_file):
                with open(vendor_file, "r") as f:
                    vendor = f.read().strip()
                
                if vendor in tt_vendor_ids:
                    # Found a card. Default config for this card:
                    config = {
                        "pcie_width": "N/A", 
                        "pcie_speed": 0
                    }

                    # Get Width
                    width_path = os.path.join(dev_path, "current_link_width")
                    if os.path.exists(width_path):
                        with open(width_path, "r") as wf:
                            config["pcie_width"] = wf.read().strip() # Returns "16"
                    
                    # Get Speed and convert to Gen
                    speed_path = os.path.join(dev_path, "current_link_speed")
                    if os.path.exists(speed_path):
                        with open(speed_path, "r") as sf:
                            raw_speed = sf.read().strip() # e.g. "16.0 GT/s"
                            config["pcie_speed"] = get_pcie_gen(raw_speed)
                            
                    # Add this card's config to the list
                    results.append(config)
    except: pass
    
    return results

# --- Main Logic ---

def gather_specs():
    """Aggregates all system info into the desired dictionary format."""
    io_settings = get_io_perf_settings()
    mem_gb = round(psutil.virtual_memory().total / (1024**3), 2)
    
    # Get the PCIe config as a list of dictionaries
    pcie_info = get_tenstorrent_pcie_info()

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
        # This is now a top-level key containing a list
        "tt_pcie_lanes_info": pcie_info
    }
    return data

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
        # All keys, including pcie_width/speed, are now in host_info
        flat_data.update(data["host_info"]) 
        
        # Create a summary string for the PCIe info
        pcie_list = data.get("tt_pcie_lane_info", [])
        if pcie_list:
            summary = ", ".join([f"{c.get('slot', 'N/A')}: x{c.get('pcie_width', '?')} Gen{c.get('pcie_speed', '?')}" for c in pcie_list])
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
            # The flat dataframe now automatically includes pcie_width and pcie_speed
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
    
    # 1. Always gather the specs
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
