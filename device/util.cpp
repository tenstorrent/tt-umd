#include "device/util.hpp"
#include "device/tt_device.h"

namespace {
fs::path generate_cluster_desc_yaml() { 
    fs::path umd_path = fs::path ( __FILE__ ).parent_path() / "../.umd";
    fs::create_directory( umd_path );
    umd_path /= "cluster_desc.yaml";
    if (!fs::exists(umd_path)){
        auto val = system ( ("touch " + umd_path.string()).c_str());
        if(val != 0) throw std::runtime_error("Cluster Generation Failed!");
    }
    // Generates the cluster descriptor in the CWD

    fs::path eth_fpath = fs::path ( __FILE__ ).parent_path();
    eth_fpath /= "bin/silicon/wormhole/create-ethernet-map";
    std::string cmd = eth_fpath.string() + " " + umd_path.string();

    auto num_devices_total = tt_SiliconDevice::detect_number_of_chips(false); // Get all chips without reservations.
    std::vector<chip_id_t> available_device_ids = tt_SiliconDevice::detect_available_device_ids(true, false);

    // If any reservations detected for user, use only those interface ID's for cluster desc generation
    if (num_devices_total != available_device_ids.size()){
        cmd += " --interface";
        for (auto &device_id: available_device_ids){
            cmd += " " + std::to_string(device_id);
        }
    }

    int val = system(cmd.c_str());
    if(val != 0) throw std::runtime_error("Cluster Generation Failed!");

    return fs::absolute(umd_path);
}

}

fs::path GetClusterDescYAML()
{
    static fs::path yaml_path{generate_cluster_desc_yaml()};
    return yaml_path;
}